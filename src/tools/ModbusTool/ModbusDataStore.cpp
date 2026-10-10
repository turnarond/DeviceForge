// v2.12 MODBUS 重构 Task 2：轮询样本内存数据表实现（纯内存，GUI 线程独占）。
// 时间契约（终审 I-3 就实）：elapsedMs 参数=本轮耗时；timestampMs=最后成功落库时刻，
// 在单写者（Widget::handleBackendResult）的调用点取真实墙钟。
#include "ModbusDataStore.h"

#include <QDateTime>

namespace modbus {

void ModbusDataStore::updatePoll(const QString& targetId, const QVector<quint16>& values,
                                 qint64 elapsedMs, int skippedCount)
{
    Sample& s = m_samples[targetId];
    s.values = values;
    s.timestampMs = QDateTime::currentMSecsSinceEpoch();   // 最后成功落库时刻（墙钟）
    s.elapsedMs = elapsedMs;                              // 本轮耗时
    s.skippedCount = skippedCount;                        // 终审 I-2：调度器溢出计数填入
    s.error.clear();
    s.ok = true;
}

void ModbusDataStore::updateError(const QString& targetId, const QString& err, qint64 elapsedMs)
{
    Sample& s = m_samples[targetId];   // 无旧档则默认构造（values 空、ok=false）
    s.error = err;
    s.elapsedMs = elapsedMs;           // 本轮耗时
    // 旧 values / timestampMs / ok / skippedCount 语义一律保留：
    // UI 显示末次已知值 + 异常标注，溢出跳过计数不因错误轮次丢失
}

std::optional<Sample> ModbusDataStore::sample(const QString& targetId) const
{
    const auto it = m_samples.constFind(targetId);
    if (it == m_samples.constEnd())
        return std::nullopt;
    return *it;
}

void ModbusDataStore::clear(const QString& targetId)
{
    m_samples.remove(targetId);
}

} // namespace modbus
