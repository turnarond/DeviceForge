// v2.12 MODBUS 重构 Task 4：本机环回集成（QModbusTcpServer @ 127.0.0.1 自动空闲端口，
// 后端 ModbusScheduler 驱动真实 QModbusTcpClient 轮询/写，不出本机）。
// 简报口径逐字：holdingReadAndWriteLoopback / serverDropThenRecover；
// 多从站串行语义由 tst_modbus_scheduler 覆盖（Qt TCP 服务器模拟多单元支持有限，规格 §5.3），
// 另补 coilWriteLoopback 锁 0x05 线圈写路径（规格 §5.3 "线圈 0x05"）；
// 审查轮2 发现 I-2 补 readOnceResultNotEvictedByScheduledDispatch（可控应答假 PLC 复现顶槽）。
// 轮2 I-1 后端口径：后端只回调不写 store（单写者=消费侧），GUILESS 无 Widget，
// 测试侧 installStoreWriter 充当单写者——落库分支与 Widget::handleBackendResult 逐字一致
// （error 非空 updateError；否则非空 values updatePoll；写成功回执两头不触）。
#include <QtTest>

#include <QElapsedTimer>
#include <QModbusDataUnit>
#include <QModbusTcpServer>
#include <QTcpServer>
#include <QTcpSocket>

#include "tools/ModbusTool/ModbusBackend.h"
#include "tools/ModbusTool/ModbusDataStore.h"
#include "tools/ModbusTool/ModbusScheduler.h"
#include "tools/ModbusTool/ModbusTargetStore.h"
#include "tools/ModbusTool/ModbusTargetTypes.h"

using modbus::ModbusDataStore;
using modbus::ModbusTargetStore;
using modbus::PollTarget;

namespace {

PollTarget makeTarget(const QString& deviceId, int slaveId, const QString& connKey,
                      QModbusDataUnit::RegisterType regType, int start, int count, int intervalMs)
{
    PollTarget t;
    t.targetId = modbus::makeTargetId(deviceId.toStdString(), slaveId);
    t.connKey = connKey;
    t.slaveId = slaveId;
    t.enabled = true;
    t.spec.regType = regType;
    t.spec.startAddr = start;
    t.spec.count = count;
    t.spec.intervalMs = intervalMs;
    return t;
}

} // namespace

class TstModbusLoopback : public QObject {
    Q_OBJECT
private:
    QModbusTcpServer m_server;
    ModbusBackend m_backend;
    ModbusDataStore m_data;
    ModbusTargetStore m_store;
    QString m_connKey;
    quint16 m_listenPort = 0;
    QStringList m_observedIds;   // 裁决 1：结果交付应恒携带精确 targetId

    // ── 轮2 I-2 工装：可控应答的裸 QTcpServer 假 PLC ──
    // QModbusTcpServer 自动应答无法制造"手工读在途 ≥ 下一到期 tick"的重叠窗口；
    // 假 PLC 按 MBAP 分帧收集请求，由测试择时择值应答。
    QTcpServer m_fakePlc;
    QList<QTcpSocket*> m_plcSockets;
    QByteArray m_plcBuf;
    QVector<QByteArray> m_plcRequests;   // 完整请求 ADU，按到达序

    // 轮2 I-1 单写者代位：结果回调落库口径与 Widget::handleBackendResult 一致
    void installStoreWriter()
    {
        m_backend.setResultCallback([this](const QString& targetId, const QVector<quint16>& values,
                                           qint64 elapsedMs, const QString& error) {
            m_observedIds << targetId;
            if (!error.isEmpty()) m_data.updateError(targetId, error, elapsedMs);
            else if (!values.isEmpty()) m_data.updatePoll(targetId, values, elapsedMs);
        });
    }

    void startFakePlc(quint16 port)
    {
        m_plcBuf.clear();
        m_plcRequests.clear();
        QVERIFY(m_fakePlc.listen(QHostAddress::LocalHost, port));
        connect(&m_fakePlc, &QTcpServer::newConnection, this, [this]() {
            QTcpSocket* socket = m_fakePlc.nextPendingConnection();
            m_plcSockets << socket;
            connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                m_plcBuf.append(socket->readAll());
                for (;;) {
                    if (m_plcBuf.size() < 6) break;   // MBAP 头未收齐
                    const int len = (quint8(m_plcBuf.at(4)) << 8) | quint8(m_plcBuf.at(5));
                    const int frame = 6 + len;
                    if (m_plcBuf.size() < frame) break;
                    m_plcRequests.append(m_plcBuf.left(frame));
                    m_plcBuf.remove(0, frame);
                }
            });
        });
    }

    // 读保持寄存器(0x03)应答：MBAP 回声 tid/pid/unit + func + byteCount + 数据
    static QByteArray readResponseFor(const QByteArray& req, quint16 value)
    {
        const int count = (quint8(req.at(10)) << 8) | quint8(req.at(11));
        const int byteCount = count * 2;
        QByteArray r = req.left(4);                        // tid + protocol id
        const int len = 3 + byteCount;                     // unit+func+byteCount+data
        r.append(char((len >> 8) & 0xFF));
        r.append(char(len & 0xFF));
        r.append(req.at(6));                               // unit id 回声
        r.append(char(3));
        r.append(char(byteCount));
        for (int i = 0; i < count; ++i) {
            r.append(char((value >> 8) & 0xFF));
            r.append(char(value & 0xFF));
        }
        return r;
    }

    void respondRequest(int index, quint16 value)
    {
        QVERIFY(index >= 0 && index < m_plcRequests.size());
        QVERIFY(!m_plcSockets.isEmpty());
        m_plcSockets.first()->write(readResponseFor(m_plcRequests.at(index), value));
    }

    // QModbusTcpServer 无 listen()/实际端口 getter：QTcpServer 探测空闲端口后让渡监听
    // （简报 "127.0.0.1:0" 自动端口语义的等价实现，见规格实施勘误）
    void listenServer()
    {
        m_server.setConnectionParameter(QModbusDevice::NetworkAddressParameter,
                                        QStringLiteral("127.0.0.1"));
        m_server.setConnectionParameter(QModbusDevice::NetworkPortParameter,
                                        int(m_listenPort));
        QVERIFY(m_server.connectDevice());
    }

    bool sampleOk(const QString& targetId, int count, quint16 first)
    {
        const auto s = m_data.sample(targetId);
        return s && s->ok && s->error.isEmpty()
            && s->values.size() == count && s->values.at(0) == first;
    }

    bool sampleCoilsOk(const QString& targetId, int minCount)
    {
        const auto s = m_data.sample(targetId);
        return s && s->ok && s->error.isEmpty() && s->values.size() >= minCount;
    }

    bool sampleHasError(const QString& targetId)
    {
        const auto s = m_data.sample(targetId);
        return s && !s->error.isEmpty();
    }

    bool serverHoldingIs(int addr, quint16 expected)
    {
        quint16 v = 0;
        return m_server.data(QModbusDataUnit::HoldingRegisters, quint16(addr), &v)
            && v == expected;
    }

    bool serverCoilOn(int addr)
    {
        quint16 v = 0;
        return m_server.data(QModbusDataUnit::Coils, quint16(addr), &v) && v != 0;
    }

private slots:
    void initTestCase()
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        m_listenPort = probe.serverPort();
        probe.close();
        m_connKey = QStringLiteral("127.0.0.1:%1").arg(m_listenPort);

        QModbusDataUnitMap map;
        map.insert(QModbusDataUnit::HoldingRegisters,
                   QModbusDataUnit(QModbusDataUnit::HoldingRegisters, 0, 16));
        map.insert(QModbusDataUnit::Coils, QModbusDataUnit(QModbusDataUnit::Coils, 0, 16));
        QVERIFY(m_server.setMap(map));
        QVERIFY(m_server.setData(QModbusDataUnit::HoldingRegisters, quint16(0), quint16(1234)));
        m_server.setServerAddress(1);   // 响应匹配的从站单元地址（Qt TCP 服务器默认地址非 1）
        listenServer();

        // ServiceTask 生命周期壳保持语义（IO 全在 GUI 线程，svc() 只挂起等待）
        QCOMPARE(m_backend.OnStart(0, nullptr), 0);
        // 轮2 I-1：后端不再预写 store，测试侧单写者代位挂一次（逐测试复用）
        installStoreWriter();
    }

    void cleanup()
    {
        // 逐测试兜底：断言中途失败也不把轮询/回调带进下一用例（stopPolling 幂等）
        m_backend.stopPolling();
        m_fakePlc.close();
        qDeleteAll(m_plcSockets);
        m_plcSockets.clear();
    }

    void cleanupTestCase()
    {
        m_backend.setResultCallback(nullptr);
        m_backend.setLogCallback(nullptr);
        m_backend.OnStop();
    }

    // ── 简报逐字：轮询读真值 + 单点写 0x06 生效 ────────────────────
    void holdingReadAndWriteLoopback()
    {
        const PollTarget t = makeTarget(QStringLiteral("plc"), 1, m_connKey,
                                        QModbusDataUnit::HoldingRegisters, 0, 2, 500);
        m_observedIds.clear();
        m_backend.startPolling({t}, &m_store, &m_data);
        QCOMPARE(m_backend.dataStore(), &m_data);

        // 首个读取用带诊断的等待（失败时给出 store 内错误文本；其余断言保持 QTRY）
        qint64 waitedMs = 0;
        while (waitedMs < 10000 && !sampleOk(t.targetId, 2, 1234)) {
            QTest::qWait(100);
            waitedMs += 100;
        }
        const auto diag = m_data.sample(t.targetId);
        QVERIFY2(diag.has_value(), "后端从未交付样本（派发/回调链断裂）");
        QVERIFY2(diag->ok && diag->values.size() == 2 && diag->values.at(0) == 1234,
                 qPrintable(QStringLiteral("轮询读未归位: error=%1 values=%2")
                                .arg(diag->error,
                                     diag->values.isEmpty()
                                         ? QStringLiteral("(空)")
                                         : QString::number(diag->values.at(0)))));

        // 裁决 1：后端结果路径改携带 targetId（deviceId#slaveId）精确回调
        QVERIFY(!m_observedIds.isEmpty());
        for (const QString& id : m_observedIds) QCOMPARE(id, t.targetId);

        m_backend.enqueueWriteTarget(t.targetId, 1, 4321);
        QTRY_VERIFY_WITH_TIMEOUT(serverHoldingIs(1, 4321), 10000);
        m_backend.stopPolling();
    }

    // ── 规格 §5.3：线圈写 0x05（非零→闭合）+ 位区轮询读 ────────────
    void coilWriteLoopback()
    {
        const PollTarget t = makeTarget(QStringLiteral("plc"), 1, m_connKey,
                                        QModbusDataUnit::Coils, 0, 4, 500);
        m_backend.startPolling({t}, &m_store, &m_data);
        qint64 waitedMs = 0;
        // 位区读回数量由 Qt 按字节打包（实测 4 线圈回 8 值），只约束"至少覆盖请求位数"
        while (waitedMs < 10000 && !sampleCoilsOk(t.targetId, 4)) {
            QTest::qWait(100);
            waitedMs += 100;
        }
        const auto diag = m_data.sample(t.targetId);
        QVERIFY2(diag.has_value(), "后端从未交付样本");
        QVERIFY2(diag->ok && diag->values.size() >= 4,
                 qPrintable(QStringLiteral("线圈读未归位: error=%1 size=%2")
                                .arg(diag->error).arg(diag->values.size())));

        m_backend.enqueueWriteTarget(t.targetId, 3, 1);   // 线圈闭合
        QTRY_VERIFY_WITH_TIMEOUT(serverCoilOn(3), 10000);
        m_backend.stopPolling();
    }

    // ── 简报逐字：服务器掉线 → error/退避；重听 → 恢复回 ok ─────────
    void serverDropThenRecover()
    {
        const PollTarget t = makeTarget(QStringLiteral("plc"), 1, m_connKey,
                                        QModbusDataUnit::HoldingRegisters, 0, 1, 500);
        m_backend.startPolling({t}, &m_store, &m_data);
        QTRY_VERIFY_WITH_TIMEOUT(sampleOk(t.targetId, 1, 1234), 10000);

        // 掉线：错误文本入 store（末值保留由 tst_modbus_scheduler::storeErrorKeeps… 锁定；
        // 退避门限推进属调度器内部状态，由 timeoutAlternatesWithCommErrorBackoff 直测，
        // 此处不重复断言门限数值以免时序抖动）。简报 "server.close()"：QModbusTcpServer::close
        // 为 protected，公开等价 = disconnectDevice()（同一实现，见规格实施勘误）
        m_server.disconnectDevice();
        QTRY_VERIFY_WITH_TIMEOUT(sampleHasError(t.targetId), 15000);

        // 服务器重听 → 客户端重连 → 轮询恢复 ok、error 清空
        listenServer();
        QTRY_VERIFY_WITH_TIMEOUT(sampleOk(t.targetId, 1, 1234), 20000);
        m_backend.stopPolling();
    }

    // ── 审查发现 I-2（轮2）：轮询中"立即读取"必须进调度器在途记账 ──────
    // 缺陷形态：readOnce 派发不置 scheduler.inFlight → 下一到期 tick 的周期派发
    // 经 sendDispatch 顶掉 m_active 槽位 → 手工读响应被代际守卫静默丢弃
    // （store/回调/退避归位全无）。修法：派发前补 onDispatched，onResult 三路对称清除。
    void readOnceResultNotEvictedByScheduledDispatch()
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        const quint16 port = probe.serverPort();
        probe.close();
        startFakePlc(port);

        // 专用回调：在单写者口径之外记录逐次读到的值序列（判别 222 是否被顶掉）
        QVector<quint16> valuesSeen;
        m_backend.setResultCallback([this, &valuesSeen](const QString& targetId,
                                                        const QVector<quint16>& values,
                                                        qint64 elapsedMs, const QString& error) {
            if (!error.isEmpty()) m_data.updateError(targetId, error, elapsedMs);
            else if (!values.isEmpty()) {
                valuesSeen << values.first();
                m_data.updatePoll(targetId, values, elapsedMs);
            }
        });

        const QString fakeKey = QStringLiteral("127.0.0.1:%1").arg(port);
        const PollTarget t = makeTarget(QStringLiteral("fake"), 1, fakeKey,
                                        QModbusDataUnit::HoldingRegisters, 0, 1, 500);
        m_backend.startPolling({t}, &m_store, &m_data);

        // ① 首个到期 tick 派发周期读 P1 → 应答 111（基线送达）
        QTRY_COMPARE_WITH_TIMEOUT(m_plcRequests.size(), 1, 5000);
        respondRequest(0, 111);
        QTRY_COMPARE_WITH_TIMEOUT(valuesSeen.size(), 1, 5000);

        // ② 立即读取 M 于下一 tick 派发；不回答，把在途窗口拖过一个到期 tick
        //（interval 500、tick 250：M 派发后 ≤250ms 必有周期项到期抢发——缺陷形态）
        m_backend.requestReadOnce(t.targetId);
        QTRY_COMPARE_WITH_TIMEOUT(m_plcRequests.size(), 2, 5000);
        QElapsedTimer hold;
        hold.start();
        while (hold.elapsed() < 400)
            QTest::qWait(20);   // 事件泵式延时（制造重叠窗口，非 sleep 空等）
        QVERIFY2(m_plcRequests.size() == 2,
                 qPrintable(QStringLiteral("手工读在途期间周期派发抢发 %1 个请求"
                                           "——readOnce 绕过调度器在途记账被顶槽")
                                .arg(m_plcRequests.size() - 2)));

        // ③ 回答 M(222) → 归位后周期读 P2 才被派发 → 应答 333
        respondRequest(1, 222);
        QTRY_COMPARE_WITH_TIMEOUT(m_plcRequests.size(), 3, 5000);
        respondRequest(2, 333);

        // 三段结果逐一送达：缺陷形态下 222 被代际守卫丢弃，序列停在 [111, 333]
        QTRY_COMPARE_WITH_TIMEOUT(valuesSeen.size(), 3, 5000);
        QCOMPARE(valuesSeen.at(0), quint16(111));
        QCOMPARE(valuesSeen.at(1), quint16(222));
        QCOMPARE(valuesSeen.at(2), quint16(333));

        m_backend.stopPolling();
        m_backend.setResultCallback(nullptr);
        installStoreWriter();   // 恢复共享单写者回调，防引用局部量的lambda外泄
    }
};

QTEST_GUILESS_MAIN(TstModbusLoopback)
#include "tst_modbus_loopback.moc"
