// v2.12 MODBUS 重构 Task 4：本机环回集成（QModbusTcpServer @ 127.0.0.1 自动空闲端口，
// 后端 ModbusScheduler 驱动真实 QModbusTcpClient 轮询/写，不出本机）。
// 简报口径逐字：holdingReadAndWriteLoopback / serverDropThenRecover；
// 多从站串行语义由 tst_modbus_scheduler 覆盖（Qt TCP 服务器模拟多单元支持有限，规格 §5.3），
// 另补 coilWriteLoopback 锁 0x05 线圈写路径（规格 §5.3 "线圈 0x05"）。
#include <QtTest>

#include <QModbusDataUnit>
#include <QModbusTcpServer>
#include <QTcpServer>

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
        m_backend.setResultCallback([this](const QString& targetId, const QVector<quint16>&,
                                           qint64, const QString&) {
            m_observedIds << targetId;
        });
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
};

QTEST_GUILESS_MAIN(TstModbusLoopback)
#include "tst_modbus_loopback.moc"
