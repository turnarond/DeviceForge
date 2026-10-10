#pragma once

// v2.12 MODBUS 重构 Task 1：目标数据模型（纯头文件，namespace modbus）。
// regType 一律持久化 QModbusDataUnit::RegisterType 的 int 值；
// 地址标签遵循 Modbus 4xxxx 约定（5 位零填充）；编解码对齐 DeviceProfileCodec 惯例。

#include <QModbusDataUnit>
#include <QLatin1Char>
#include <QLatin1String>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <cstdio>
#include <optional>
#include <string>

namespace modbus {

// 轮询规格：单从站的寄存器区间与周期（缺省即安全默认：保持寄存器 40001 起点、1 点、1s）
struct PollSpec {
    QModbusDataUnit::RegisterType regType = QModbusDataUnit::HoldingRegisters;
    int startAddr = 0;
    int count = 1;
    int intervalMs = 1000;
};

// 单从站目标：站号 + 使能开关 + 轮询规格
struct SlaveTarget {
    int slaveId = 1;
    bool enabled = true;
    PollSpec poll;
};

// 单设备的轮询目标集（ConfigStore 记录类型 "modbus.target"，key=deviceId）
struct DeviceTargets {
    std::string deviceId;
    QVector<SlaveTarget> targets;
    int tagSchema = 0;
};

// 寄存器地址标签，5 位零填充（snprintf %05d + 区基址）：
// Holding+0→"40001"、Input+0→"30001"、Discrete+0→"10001"、Coil+5→"00005"
// （0xxxx 区按偏移值直显，其余区基址含 1 起点）
inline QString addressLabel(QModbusDataUnit::RegisterType type, int addr)
{
    int base = 0;
    switch (type) {
    case QModbusDataUnit::DiscreteInputs:   base = 10001; break; // 1xxxx 离散输入
    case QModbusDataUnit::Coils:            base = 0;     break; // 0xxxx 线圈
    case QModbusDataUnit::InputRegisters:   base = 30001; break; // 3xxxx 输入寄存器
    case QModbusDataUnit::HoldingRegisters: base = 40001; break; // 4xxxx 保持寄存器
    default: return QString();                                   // Invalid 等非法类型不给标签
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%05d", base + addr);
    return QString::fromLatin1(buf);
}

// 全局唯一目标 ID："<deviceId>#<slaveId>"
inline QString makeTargetId(const std::string& deviceId, int slaveId)
{
    return QString::fromStdString(deviceId) + QLatin1Char('#') + QString::number(slaveId);
}

// 编码为 ConfigStore 记录（type="modbus.target"）。UTF-8 字符串经 QString 承载，regType 存 int。
inline QVariantMap encodeDeviceTargets(const DeviceTargets& targets)
{
    QVariantMap record;
    record.insert(QStringLiteral("deviceId"), QString::fromStdString(targets.deviceId));
    record.insert(QStringLiteral("tagSchema"), targets.tagSchema);

    QVariantList list;
    list.reserve(targets.targets.size());
    for (const auto& target : targets.targets) {
        QVariantMap item;
        item.insert(QStringLiteral("slaveId"), target.slaveId);
        item.insert(QStringLiteral("enabled"), target.enabled);
        item.insert(QStringLiteral("regType"), int(target.poll.regType));
        item.insert(QStringLiteral("startAddr"), target.poll.startAddr);
        item.insert(QStringLiteral("count"), target.poll.count);
        item.insert(QStringLiteral("intervalMs"), target.poll.intervalMs);
        list.append(item);
    }
    record.insert(QStringLiteral("targets"), list);
    return record;
}

// 解码记录。结构性损坏返回 nullopt（调用方据此回退/跳过）：
// 缺 deviceId、targets 缺失或非列表、条目非映射、regType 越界（含 0=Invalid）。
// 可选数值字段缺失/非法按安全默认补齐：startAddr=0（负值钳 0）、count=1（非正→1）、
// intervalMs=1000（非正→1000）、enabled=true、slaveId=1、regType=HoldingRegisters。
inline std::optional<DeviceTargets> decodeDeviceTargets(const QVariantMap& record)
{
    const auto idIt = record.constFind(QStringLiteral("deviceId"));
    if (idIt == record.constEnd()) return std::nullopt;

    DeviceTargets targets;
    targets.deviceId = idIt->toString().toStdString();
    targets.tagSchema = record.value(QStringLiteral("tagSchema")).toInt();

    const QVariant targetsValue = record.value(QStringLiteral("targets"));
    if (targetsValue.typeId() != QMetaType::QVariantList) return std::nullopt;
    const QVariantList list = targetsValue.toList();
    for (const QVariant& itemValue : list) {
        if (itemValue.typeId() != QMetaType::QVariantMap) return std::nullopt;
        const QVariantMap item = itemValue.toMap();

        SlaveTarget target;
        const QVariant slaveIdValue = item.value(QStringLiteral("slaveId"));
        if (slaveIdValue.isValid() && !slaveIdValue.isNull())
            target.slaveId = slaveIdValue.toInt();
        target.enabled = item.value(QStringLiteral("enabled"), true).toBool();

        const QVariant regTypeValue = item.value(QStringLiteral("regType"));
        if (regTypeValue.isValid() && !regTypeValue.isNull()) {
            bool ok = false;
            const int raw = regTypeValue.toInt(&ok);
            // regType 必须是有效 RegisterType 值（DiscreteInputs..HoldingRegisters）
            if (!ok || raw < int(QModbusDataUnit::DiscreteInputs)
                || raw > int(QModbusDataUnit::HoldingRegisters)) {
                return std::nullopt;
            }
            target.poll.regType = static_cast<QModbusDataUnit::RegisterType>(raw);
        }

        target.poll.startAddr = qMax(0, item.value(QStringLiteral("startAddr"), 0).toInt());
        const int count = item.value(QStringLiteral("count"), 1).toInt();
        target.poll.count = count > 0 ? count : 1;
        const int intervalMs = item.value(QStringLiteral("intervalMs"), 1000).toInt();
        target.poll.intervalMs = intervalMs > 0 ? intervalMs : 1000;

        targets.targets.push_back(target);
    }
    return targets;
}

} // namespace modbus
