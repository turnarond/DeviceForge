// v2.12 MODBUS 重构 Task 2：纯逻辑轮询调度器实现（无 IO，可脱机测）。
#include "ModbusScheduler.h"

namespace modbus {

const QVector<int>& ModbusScheduler::backoffSequenceMs()
{
    static const QVector<int> seq{ 1000, 2000, 5000, 10000, 30000 };
    return seq;
}

int ModbusScheduler::targetIndex(const QString& targetId) const
{
    for (int i = 0; i < m_targets.size(); ++i) {
        if (m_targets[i].target.targetId == targetId)
            return i;
    }
    return -1;
}

void ModbusScheduler::reset(const QVector<PollTarget>& targets, qint64 nowMs)
{
    m_targets.clear();
    m_conns.clear();
    m_targets.reserve(targets.size());
    for (const PollTarget& t : targets) {
        TargetState st;
        st.target = t;
        st.nextDueMs = nowMs;   // reset 后即刻到期，首轮由驱动首个 tick 统一派发
        m_targets.append(st);
        m_conns.insert(t.connKey, Conn{});   // 预建连接态：在途/队列/退避全清零
    }
}

QVector<Dispatch> ModbusScheduler::onTick(qint64 nowMs)
{
    // ① 到期目标入所属连接队尾；队列超容量弹最旧轮询项（写项永不弹）并计数
    for (TargetState& st : m_targets) {
        if (!st.target.enabled)
            continue;
        if (nowMs < st.nextDueMs)
            continue;
        Conn& conn = m_conns[st.target.connKey];
        if (nowMs < conn.connNextReadyMs)
            continue;   // 连接退避门限未到，本轮不入队（nextDueMs 不推进，门限后自然补发）

        Dispatch d;
        d.targetId = st.target.targetId;
        d.connKey = st.target.connKey;
        d.slaveId = st.target.slaveId;
        d.kind = Kind::Poll;
        d.poll = st.target.spec;
        conn.queue.append(d);
        // 裁决 2（任务 2 遗留缺陷修复）：入队即推进 nextDueMs。旧版仅在派发弹队首时
        // 推进，连接在途/退避时同一到期目标每个 tick 重复入队，慢 PLC 恢复后派发风暴。
        st.nextDueMs = nowMs + st.target.spec.intervalMs;

        while (conn.queue.size() > kQueueCap) {
            // 自队首起找最旧的轮询项弹出；写项（插队首而来）扫描时自然跳过
            int idx = -1;
            for (int i = 0; i < conn.queue.size(); ++i) {
                if (conn.queue[i].kind == Kind::Poll) {
                    idx = i;
                    break;
                }
            }
            if (idx < 0)
                break;   // 全是写项：宁超容量也不弹写（防御分支，正常流程不可达）
            const QString evicted = conn.queue.takeAt(idx).targetId;
            const int ti = targetIndex(evicted);
            if (ti >= 0)
                ++m_targets[ti].skipped;
        }
    }

    // ② 每连接单在途：空闲、队列非空、已过退避门限 → 弹队首派发并置在途
    //（nextDueMs 已在入队时推进，派发侧不再记账——裁决 2 修复的成对改动）
    QVector<Dispatch> out;
    for (auto it = m_conns.begin(); it != m_conns.end(); ++it) {
        Conn& conn = it.value();
        if (conn.inFlight || conn.queue.isEmpty() || nowMs < conn.connNextReadyMs)
            continue;
        const Dispatch d = conn.queue.takeFirst();
        conn.inFlight = true;   // onDispatched 语义；驱动侧再调一次幂等
        out.append(d);
    }
    return out;
}

bool ModbusScheduler::enqueueWrite(const QString& targetId, int addr, quint16 value)
{
    const int ti = targetIndex(targetId);
    if (ti < 0)
        return false;   // 未知目标无连接可归属：拒绝交付（调用方保证先 reset 后写）
    const PollTarget& t = m_targets[ti].target;
    Conn& conn = m_conns[t.connKey];

    // 终审 I-4 背压：写项插队首永不丢弃 ≠ 无限收——队列已满（尺寸达 kQueueCap）时
    // 本次写不入队、返回 false，规格 §3"满则直接报错提示"（写项自身致超即拒）
    if (conn.queue.size() >= kQueueCap)
        return false;

    Dispatch d;
    d.targetId = t.targetId;
    d.connKey = t.connKey;
    d.slaveId = t.slaveId;
    d.kind = Kind::Write;
    d.poll = t.spec;
    d.writeAddr = addr;
    d.writeValue = value;
    conn.queue.push_front(d);   // 插队首，永不丢弃、不计 skipped
    return true;
}

void ModbusScheduler::onDispatched(const QString& connKey)
{
    const auto it = m_conns.find(connKey);
    if (it == m_conns.end())
        return;
    it->inFlight = true;
}

void ModbusScheduler::onResult(const QString& connKey, Outcome outcome, qint64 nowMs)
{
    const auto it = m_conns.find(connKey);
    if (it == m_conns.end())
        return;
    Conn& conn = it.value();
    conn.inFlight = false;
    if (outcome == Outcome::Ok) {
        conn.backoffIdx = 0;
        conn.connNextReadyMs = 0;   // 成功即解除退避门限
        return;
    }
    // CommError/Timeout 共享退避通道：本次用 backoffIdx 档，游标前移且末值封顶
    const QVector<int>& seq = backoffSequenceMs();
    const int idx = qBound(0, conn.backoffIdx, seq.size() - 1);
    conn.connNextReadyMs = nowMs + seq[idx];
    if (conn.backoffIdx < seq.size() - 1)
        ++conn.backoffIdx;
}

int ModbusScheduler::skippedCount(const QString& targetId) const
{
    const int ti = targetIndex(targetId);
    return ti >= 0 ? m_targets[ti].skipped : 0;
}

bool ModbusScheduler::connInBackoff(const QString& connKey, qint64 nowMs) const
{
    const auto it = m_conns.constFind(connKey);
    if (it == m_conns.constEnd())
        return false;   // 未知连接无退避可言
    return it->connNextReadyMs > nowMs;
}

qint64 ModbusScheduler::nextWakeMs(qint64 nowMs) const
{
    qint64 best = 0;
    bool found = false;
    const auto consider = [this, &best, &found](qint64 candidate) {
        if (!found || candidate < best) {
            best = candidate;
            found = true;
        }
    };
    for (const TargetState& st : m_targets) {
        if (!st.target.enabled)
            continue;
        // 目标可动作的最早时刻 = max(自身到期, 所属连接退避门限)
        const qint64 gate = m_conns.value(st.target.connKey).connNextReadyMs;
        consider(qMax(st.nextDueMs, gate));
    }
    for (auto it = m_conns.constBegin(); it != m_conns.constEnd(); ++it) {
        // 空闲连接持有待发项（如插队首的写）：门限一到即可动作
        if (!it->inFlight && !it->queue.isEmpty())
            consider(it->connNextReadyMs);
    }
    if (!found)
        return nowMs + kTickMs;   // 无任何待办：回落默认节拍兜底
    return qMax(best, nowMs);     // 已过时刻钳回 now，驱动立即唤醒
}

} // namespace modbus
