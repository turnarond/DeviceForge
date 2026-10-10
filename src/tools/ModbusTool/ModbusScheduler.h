#pragma once

// v2.12 MODBUS 重构 Task 2：纯逻辑轮询调度器（namespace modbus，零 IO、零定时器）。
// 时钟一律经参数注入（假时钟可脱机测），派发动作经 onTick 返回值交付；
// 每个连接（connKey）单在途 + FIFO 待发队列：写插队首永不丢弃，
// 轮询超出容量（kQueueCap）弹最旧轮询项并计 skippedCount，连接错误按序列退避。

#include <QMap>
#include <QString>
#include <QVector>

#include "ModbusTargetTypes.h"

namespace modbus {

// 轮询目标：一条从站轮询线。enabled 由调用方自 SlaveTarget 带入，调度器据此过滤；
// targetId 用 makeTargetId(deviceId, slaveId) 生成，connKey 为协议层连接池键。
struct PollTarget {
    QString targetId;
    QString connKey;
    int slaveId = 1;
    bool enabled = true;
    PollSpec spec;
};

// 派发动作类别：周期轮询读 / 用户写（写仅携带 addr+value，poll 字段仅作上下文冗余）
enum class Kind { Poll, Write };

// 一次派发：后端据此向 connKey 对应连接发起 slaveId 的读（poll）或写（writeAddr/Value）
struct Dispatch {
    QString targetId;
    QString connKey;
    int slaveId = 1;
    Kind kind = Kind::Poll;
    PollSpec poll;
    int writeAddr = 0;
    quint16 writeValue = 0;
};

// 派发结果：Ok 复位退避；CommError/Timeout 推进退避（二者共享同一退避通道）
enum class Outcome { Ok, CommError, Timeout };

class ModbusScheduler {
public:
    static constexpr int kTickMs = 250;   // 驱动 timer 的基准节拍（无待办时的兜底唤醒间隔）
    static constexpr int kQueueCap = 8;   // 每连接待发队列容量（超出只弹最旧轮询项）

    // 连接级退避序列 {1000,2000,5000,10000,30000}，末值封顶
    static const QVector<int>& backoffSequenceMs();

    // 全量重建目标集与连接态（在途/队列/退避/计数清零）；所有目标即刻到期
    void reset(const QVector<PollTarget>& targets, qint64 nowMs);

    // 一个节拍：①到期且 enabled 且已过连接退避门限的目标入队尾（溢出弹最旧轮询）；
    // ②每个空闲、非空、过门限的连接弹队首进入返回值并置在途
    QVector<Dispatch> onTick(qint64 nowMs);

    // 用户写插该连接队首：永不丢弃、不计入 skippedCount；targetId 未知则忽略
    void enqueueWrite(const QString& targetId, int addr, quint16 value);

    // 标记该连接在途（onTick 弹出时已自动置位；驱动侧重复调用幂等）
    void onDispatched(const QString& connKey);

    // 响应归位：清在途；Ok→退避复位，CommError/Timeout→推进退避并设门限 nowMs+退避值。
    // Ruling：简报签名缺 nowMs，退避门限计算必需当前时钟，按任务约定做最小扩展加此参数。
    void onResult(const QString& connKey, Outcome outcome, qint64 nowMs);

    // 某目标的轮询因队列溢出被丢弃的累计次数（未知目标返回 0）
    int skippedCount(const QString& targetId) const;

    // 下次可能产生动作的最早时刻（供驱动 timer 唤醒）：
    // min(enabled 目标的 max(nextDue, 所属连接退避门限)), 另计入空闲连接待发队列的退避门限；
    // 无任何待办时回落 nowMs+kTickMs
    qint64 nextWakeMs(qint64 nowMs) const;

private:
    // 单连接状态机：在途守卫 + FIFO 队列 + 退避游标/门限
    struct Conn {
        bool inFlight = false;
        QVector<Dispatch> queue;   // 队首先出；enqueueWrite 插队首
        int backoffIdx = 0;
        qint64 connNextReadyMs = 0;
    };
    // 目标运行态：nextDueMs 仅在轮询派发（弹队首）后推进；skipped 记溢出丢弃数
    struct TargetState {
        PollTarget target;
        qint64 nextDueMs = 0;
        int skipped = 0;
    };

    // 按 targetId 查目标下标，未找到返回 -1
    int targetIndex(const QString& targetId) const;

    QVector<TargetState> m_targets;
    QMap<QString, Conn> m_conns;   // connKey → 连接态（reset 时按目标预建）
};

} // namespace modbus
