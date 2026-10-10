#include <QtTest/QtTest>

#include "tools/ModbusTool/ModbusDataStore.h"
#include "tools/ModbusTool/ModbusScheduler.h"

// v2.12 MODBUS 重构 Task 2：纯逻辑轮询调度器 + 内存数据表测试（无 IO、假时钟注入）。
// 覆盖：到期排序、单连接在途守卫、写插队首、队列溢出弹最旧轮询并计数、
//       多从站共享连接串行化、连接退避序列与复位、禁用/空目标零流量、数据表读写与错误保旧。

using modbus::Dispatch;
using modbus::Kind;
using modbus::ModbusDataStore;
using modbus::ModbusScheduler;
using modbus::Outcome;
using modbus::PollSpec;
using modbus::PollTarget;

namespace {

// 构造轮询目标：targetId 采用 Task 1 的 "<deviceId>#<slaveId>" 约定
PollTarget makeTarget(const QString& deviceId, int slaveId, const QString& connKey,
                      int intervalMs, bool enabled = true)
{
    PollTarget t;
    t.targetId = modbus::makeTargetId(deviceId.toStdString(), slaveId);
    t.connKey = connKey;
    t.slaveId = slaveId;
    t.enabled = enabled;
    t.spec.intervalMs = intervalMs;
    return t;
}

} // namespace

class TstModbusScheduler : public QObject {
    Q_OBJECT
private slots:
    // 两目标 interval 500/1000（不同连接）：now=0 双双首发后，500 仅 500 间隔者到期
    void dueOrderingSingleTick()
    {
        ModbusScheduler s;
        s.reset({ makeTarget(QStringLiteral("dev"), 1, QStringLiteral("connA"), 500),
                  makeTarget(QStringLiteral("dev"), 2, QStringLiteral("connB"), 1000) }, 0);

        QCOMPARE(s.onTick(0).size(), 2);          // reset 后首轮两条目均到期
        s.onResult(QStringLiteral("connA"), Outcome::Ok, 0);   // 首轮归位
        s.onResult(QStringLiteral("connB"), Outcome::Ok, 0);
        QCOMPARE(s.onTick(250).size(), 0);        // 均未到期
        const auto d500 = s.onTick(500);
        QCOMPARE(d500.size(), 1);                 // 仅 interval=500 者到期
        QCOMPARE(d500.first().targetId, QStringLiteral("dev#1"));
        QCOMPARE(d500.first().kind, Kind::Poll);
        s.onResult(QStringLiteral("connA"), Outcome::Ok, 500); // 第二次归位
        QCOMPARE(s.onTick(750).size(), 0);
        QCOMPARE(s.onTick(1000).size(), 2);       // 500 目标第三次、1000 目标第二次
    }

    // 派发后未 onResult：同连接后续 tick 不再派发（在途守卫，防堆积）
    void inFlightGuardNoPileup()
    {
        ModbusScheduler s;
        s.reset({ makeTarget(QStringLiteral("dev"), 1, QStringLiteral("connA"), 250) }, 0);

        QCOMPARE(s.onTick(0).size(), 1);          // 派发并置在途
        QCOMPARE(s.onTick(250).size(), 0);        // 在途未归 → 不派发
        QCOMPARE(s.onTick(500).size(), 0);        // 依旧在途
        s.onDispatched(QStringLiteral("connA"));  // 幂等：重复标记不死锁
        QCOMPARE(s.onTick(500).size(), 0);
        s.onResult(QStringLiteral("connA"), Outcome::Ok, 500);
        QCOMPARE(s.onTick(500).size(), 1);        // 归位后恢复派发
    }

    // 先 reset 有轮询，enqueueWrite 后下一 onTick 该连接首个 Dispatch.kind==Write
    void writePreparedHead()
    {
        ModbusScheduler s;
        s.reset({ makeTarget(QStringLiteral("dev"), 1, QStringLiteral("connA"), 500) }, 0);
        s.enqueueWrite(QStringLiteral("dev#1"), 7, quint16(0x1234));

        const auto d = s.onTick(0);
        QCOMPARE(d.size(), 1);
        QCOMPARE(d.first().kind, Kind::Write);    // 写排在轮询之前（插队首）
        QCOMPARE(d.first().writeAddr, 7);
        QCOMPARE(d.first().writeValue, quint16(0x1234));
        QCOMPARE(d.first().targetId, QStringLiteral("dev#1"));
        QCOMPARE(d.first().connKey, QStringLiteral("connA"));
    }

    // 连灌轮询填满队列 cap=8：最旧轮询丢弃且 skippedCount>0；写仍在队列、不被丢
    void writeNeverDroppedAndPollEvictsWithCount()
    {
        ModbusScheduler s;
        QVector<PollTarget> targets;
        for (int i = 1; i <= 10; ++i)             // 同一连接 10 个从站目标，interval 500
            targets << makeTarget(QStringLiteral("dev"), i, QStringLiteral("connA"), 500);
        s.reset(targets, 0);

        const auto d0 = s.onTick(0);              // 入队 10 > cap 8：最旧两条轮询被弹
        QCOMPARE(d0.size(), 1);
        QCOMPARE(d0.first().kind, Kind::Poll);
        s.enqueueWrite(QStringLiteral("dev#4"), 99, quint16(1));   // 写插队首

        QCOMPARE(s.onTick(500).size(), 0);        // 在途：只入队溢出，不派发
        QVERIFY(s.skippedCount(QStringLiteral("dev#1")) > 0);      // 最旧轮询确有丢弃
        QVERIFY(s.skippedCount(QStringLiteral("dev#2")) > 0);

        s.onResult(QStringLiteral("connA"), Outcome::Ok, 500);
        const auto d1 = s.onTick(500);            // 恢复派发：队首仍是那条写
        QCOMPARE(d1.size(), 1);
        QCOMPARE(d1.first().kind, Kind::Write);   // 写历经溢出风暴未被丢弃
        QCOMPARE(d1.first().writeAddr, 99);
        QVERIFY(s.skippedCount(QStringLiteral("dev#13")) == 0);    // 未知目标计数为 0
    }

    // 同 connKey 两 slave：串行——一个在途时另一不派发
    void multiSlaveShareConnSerialized()
    {
        ModbusScheduler s;
        s.reset({ makeTarget(QStringLiteral("dev"), 1, QStringLiteral("connA"), 500),
                  makeTarget(QStringLiteral("dev"), 2, QStringLiteral("connA"), 500) }, 0);

        const auto d0 = s.onTick(0);
        QCOMPARE(d0.size(), 1);                   // 一条连接同一时刻至多一个在途
        QCOMPARE(d0.first().slaveId, 1);          // 先头先出（FIFO）
        QCOMPARE(s.onTick(500).size(), 0);        // 在途期间另一 slave 不派发
        s.onResult(QStringLiteral("connA"), Outcome::Ok, 500);
        const auto d1 = s.onTick(500);
        QCOMPARE(d1.size(), 1);                   // 归位后第二个才走出
        QCOMPARE(d1.first().slaveId, 2);
    }

    // 连续 CommError：nextWake 间隔依序 1s/2s/5s/10s/30s/30s（末值封顶）；一次 Ok 复位回 interval
    void backoffSequenceAndReset()
    {
        ModbusScheduler s;
        s.reset({ makeTarget(QStringLiteral("dev"), 1, QStringLiteral("connA"), 1000) }, 0);

        qint64 now = 0;
        QVector<qint64> wakes;
        for (int i = 0; i < 6; ++i) {             // 六轮：到期派发 → CommError → 记录唤醒点
            QCOMPARE(s.onTick(now).size(), 1);
            s.onResult(QStringLiteral("connA"), Outcome::CommError, now);
            now = s.nextWakeMs(now);
            wakes << now;
        }
        const QVector<qint64> expected{ 1000, 3000, 8000, 18000, 48000, 78000 };
        QCOMPARE(wakes, expected);                // 间隔 1s/2s/5s/10s/30s/30s，封顶生效

        QCOMPARE(s.onTick(now).size(), 1);        // 第 7 次派发：封顶退避后仍按唤醒点归位
        s.onResult(QStringLiteral("connA"), Outcome::Ok, now);
        QCOMPARE(s.nextWakeMs(now), now + 1000);  // 退避复位：回到 interval 节奏
        QCOMPARE(s.onTick(now + 1000).size(), 1); // 下一 interval 到期即发，不再受退避拖延
    }

    // reset 空集/目标全禁用：onTick 恒空、skippedCount 恒 0、nextWake 落回节拍
    void disabledAndEmptyZeroFlow()
    {
        ModbusScheduler s;
        s.reset({}, 0);
        QVERIFY(s.onTick(1000).isEmpty());
        QCOMPARE(s.nextWakeMs(0), qint64(ModbusScheduler::kTickMs));   // 无目标 → 默认节拍兜底

        s.reset({ makeTarget(QStringLiteral("dev"), 1, QStringLiteral("connA"), 500, false),
                  makeTarget(QStringLiteral("dev"), 2, QStringLiteral("connB"), 500, false) }, 0);
        QVERIFY(s.onTick(500).isEmpty());
        QVERIFY(s.onTick(10'000).isEmpty());
        QCOMPARE(s.skippedCount(QStringLiteral("dev#1")), 0);
    }

    // 裁决 2（任务 2 遗留缺陷回归）：在途期间同到期目标不得每个 tick 重复入队。
    // 旧实现 nextDueMs 仅在派发弹队首时推进——连接忙碌时同一到期项反复入队堆积，
    // 慢 PLC 恢复后形成"派发风暴"；修复口径为入队即推进 nextDueMs。
    void noDuplicateEnqueueWhileInFlight()
    {
        ModbusScheduler s;
        s.reset({ makeTarget(QStringLiteral("dev"), 1, QStringLiteral("connA"), 500) }, 0);

        QCOMPARE(s.onTick(0).size(), 1);            // 派发并置在途（入队即推进 nextDue→500）
        QCOMPARE(s.onTick(250).size(), 0);
        QVERIFY(s.onTick(500).isEmpty());           // 500 新到期：入队 1 项，但在途不派发
        QCOMPARE(s.onTick(750).size(), 0);          // 750 无到期：不得因旧项未推进而重复入队
        s.onResult(QStringLiteral("connA"), Outcome::Ok, 750);

        QCOMPARE(s.onTick(750).size(), 1);          // 归位：取出 500 到期那一条
        s.onResult(QStringLiteral("connA"), Outcome::Ok, 750);   // 本次派发归位
        QVERIFY(s.onTick(750).isEmpty());           // 队中已无重复积压项（核心回归断言）
        QCOMPARE(s.onTick(1000).size(), 1);         // 恢复 500ms 正常节奏
    }

    // 裁决 2：Outcome::Timeout 与 CommError 共享同一退避通道，交替命中依序推进
    void timeoutAlternatesWithCommErrorBackoff()
    {
        ModbusScheduler s;
        s.reset({ makeTarget(QStringLiteral("dev"), 1, QStringLiteral("connA"), 1000) }, 0);

        QCOMPARE(s.onTick(0).size(), 1);
        s.onResult(QStringLiteral("connA"), Outcome::Timeout, 0);
        QVERIFY(s.connInBackoff(QStringLiteral("connA"), 999));    // 退避门限 1s 未到（裁决 5 依据）
        QVERIFY(!s.connInBackoff(QStringLiteral("connA"), 1000));
        const qint64 w1 = s.nextWakeMs(0);
        QCOMPARE(w1, qint64(1000));

        QCOMPARE(s.onTick(w1).size(), 1);
        s.onResult(QStringLiteral("connA"), Outcome::CommError, w1);  // 交替：通道不重置
        const qint64 w2 = s.nextWakeMs(w1);
        QCOMPARE(w2, w1 + 2000);

        QCOMPARE(s.onTick(w2).size(), 1);
        s.onResult(QStringLiteral("connA"), Outcome::Timeout, w2);
        const qint64 w3 = s.nextWakeMs(w2);
        QCOMPARE(w3, w2 + 5000);

        QCOMPARE(s.onTick(w3).size(), 1);
        s.onResult(QStringLiteral("connA"), Outcome::Ok, w3);         // 成功复位
        QVERIFY(!s.connInBackoff(QStringLiteral("connA"), w3));
        QCOMPARE(s.nextWakeMs(w3), w3 + 1000);
    }

    // 数据表：updatePoll 写入后读回全字段；clear 后即消失
    void storeUpdateAndReadback()
    {
        ModbusDataStore store;
        const QVector<quint16> values{ 11, 22, 33 };
        store.updatePoll(QStringLiteral("dev#1"), values, 1000);

        const auto smp = store.sample(QStringLiteral("dev#1"));
        QVERIFY(smp.has_value());
        QCOMPARE(smp->values, values);
        QVERIFY(smp->ok);
        QVERIFY(smp->error.isEmpty());
        QCOMPARE(smp->timestampMs, qint64(1000));
        QCOMPARE(smp->elapsedMs, qint64(1000));

        store.clear(QStringLiteral("dev#1"));
        QVERIFY(!store.sample(QStringLiteral("dev#1")).has_value());
        QVERIFY(!store.sample(QStringLiteral("不存在")).has_value());
    }

    // 数据表：updateError 保留旧 values 与旧 timestampMs（及旧 ok 语义），只更新 error/elapsedMs
    void storeErrorKeepsOldSampleTimestamp()
    {
        ModbusDataStore store;
        const QVector<quint16> values{ 44, 55 };
        store.updatePoll(QStringLiteral("dev#1"), values, 1000);
        store.updateError(QStringLiteral("dev#1"), QStringLiteral("通讯超时"), 5000);

        const auto smp = store.sample(QStringLiteral("dev#1"));
        QVERIFY(smp.has_value());
        QCOMPARE(smp->values, values);            // 旧数据保留供 UI 显示
        QCOMPARE(smp->timestampMs, qint64(1000)); // 最后成功时刻不被错误覆盖
        QCOMPARE(smp->elapsedMs, qint64(5000));   // 时间字段更新到本次响应
        QCOMPARE(smp->error, QStringLiteral("通讯超时"));

        store.updatePoll(QStringLiteral("dev#1"), values, 6000);   // 成功后 error 清空
        QVERIFY(store.sample(QStringLiteral("dev#1"))->error.isEmpty());

        store.updateError(QStringLiteral("dev#9"), QStringLiteral("从未成功"), 7000);
        const auto ghost = store.sample(QStringLiteral("dev#9"));  // 无旧数据也可建档
        QVERIFY(ghost.has_value());
        QVERIFY(!ghost->ok);
        QVERIFY(ghost->values.isEmpty());
    }
};

QTEST_MAIN(TstModbusScheduler)
#include "tst_modbus_scheduler.moc"
