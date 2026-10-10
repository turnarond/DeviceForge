// v2.12 MODBUS 重构 Task 2：轮询样本内存数据表实现（纯内存，GUI 线程独占）。
#include "ModbusDataStore.h"

namespace modbus {

void ModbusDataStore::updatePoll(const QString& targetId, const QVector<quint16>& values,
                                 qint64 elapsedMs)
{
    Sample& s = m_samples[targetId];
    s.values = values;
    s.timestampMs = elapsedMs;   // 最后成功时刻记自调用方注入的单调时钟
    s.elapsedMs = elapsedMs;
    s.error.clear();
    s.ok = true;
}

void ModbusDataStore::updateError(const QString& targetId, const QString& err, qint64 elapsedMs)
{
    Sample& s = m_samples[targetId];   // 无旧档则默认构造（values 空、ok=false）
    s.error = err;
    s.elapsedMs = elapsedMs;
    // 旧 values / timestampMs / ok 语义一律保留：UI 显示末次已知值 + 异常标注
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
