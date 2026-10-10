#pragma once

// v2.12 MODBUS 重构 Task 2：轮询样本内存数据表（namespace modbus，纯内存、非线程安全，
// GUI 线程独占）。时间语义：elapsedMs 参数与调度器 nowMs 同源的单调时钟（毫秒），
// updatePoll 以它记 timestampMs（最后成功时刻）；updateError 保留旧 values/timestampMs/ok，
// 只更新 error 与 elapsedMs——UI 据此可显示末次已知值并叠加通讯异常标注。

#include <QHash>
#include <QString>
#include <QVector>

#include <optional>

namespace modbus {

// 单目标最近一次观测：寄存器值 + 时刻 + 错误 + 成功标志
struct Sample {
    QVector<quint16> values;
    qint64 timestampMs = 0;
    qint64 elapsedMs = 0;
    QString error;
    bool ok = false;
};

class ModbusDataStore {
public:
    // 轮询成功：整条覆盖 values，ok 置真、清 error，timestampMs/elapsedMs 记本次时钟
    void updatePoll(const QString& targetId, const QVector<quint16>& values, qint64 elapsedMs);

    // 轮询/写失败：保留旧 values/timestampMs/ok；无旧档则新建空值档（ok=false）
    void updateError(const QString& targetId, const QString& err, qint64 elapsedMs);

    // 读回；未知目标返回 nullopt
    std::optional<Sample> sample(const QString& targetId) const;

    // 删除目标档案（目标移除/迁移时由调用方触发）
    void clear(const QString& targetId);

private:
    QHash<QString, Sample> m_samples;
};

} // namespace modbus
