#pragma once

// v2.12 MODBUS 重构 Task 2：轮询样本内存数据表（namespace modbus，纯内存、非线程安全，
// GUI 线程独占）。时间契约（终审 I-3 就实）：updatePoll/updateError 第三参 elapsedMs
// = 本轮耗时（毫秒，非时钟）；Sample.timestampMs = 最后成功落库时刻——落库单写者是
// Widget 侧 handleBackendResult（规格 §3/裁决 1），updatePoll 在该调用点以真实墙钟记录，
// updateError 保留旧 values/timestampMs/ok，只更新 error 与耗时——UI 据此可显示
// 末次已知值并叠加通讯异常标注。

#include <QHash>
#include <QString>
#include <QVector>

#include <optional>

namespace modbus {

// 单目标最近一次观测：寄存器值 + 最后成功落库时刻 + 本轮耗时 + 错误 + 成功标志
struct Sample {
    QVector<quint16> values;
    qint64 timestampMs = 0;   // 最后成功落库时刻（墙钟毫秒，updatePoll 落库点记录）
    qint64 elapsedMs = 0;     // 最近一轮耗时（毫秒）
    QString error;
    bool ok = false;
    // 终审 I-2（规格 §2.3 兑现）：轮询溢出跳过累计（调度器计数，落库时由单写者填入）
    int skippedCount = 0;
};

class ModbusDataStore {
public:
    // 轮询成功：整条覆盖 values，ok 置真、清 error；elapsedMs=本轮耗时，
    // timestampMs 记本次落库时刻（真实墙钟）；skippedCount=调度器溢出计数透传（I-2）
    void updatePoll(const QString& targetId, const QVector<quint16>& values, qint64 elapsedMs,
                    int skippedCount = 0);

    // 轮询/写失败：保留旧 values/timestampMs/ok；无旧档则新建空值档（ok=false）；
    // elapsedMs=本轮耗时
    void updateError(const QString& targetId, const QString& err, qint64 elapsedMs);

    // 读回；未知目标返回 nullopt
    std::optional<Sample> sample(const QString& targetId) const;

    // 删除目标档案（目标移除/迁移时由调用方触发）
    void clear(const QString& targetId);

private:
    QHash<QString, Sample> m_samples;
};

} // namespace modbus
