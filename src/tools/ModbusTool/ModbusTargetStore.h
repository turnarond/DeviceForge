#pragma once

// v2.12 MODBUS 重构 Task 1：轮询目标持久化（ConfigStore 记录类型 "modbus.target"，key=deviceId）。
// 迁移语义：旧 modbus.slave（key=slave:<sid>，单全局 PollSpec、无 deviceId）一次性回填为
// 每个含 modbus 端点档案的 modbus.target 记录；旧记录只读不删。

#include "tools/ModbusTool/ModbusTargetTypes.h"

#include <string>
#include <vector>

class DeviceRegistry; // 全局命名空间前向声明（勿在 namespace modbus 内做参数类名首现）

namespace modbus {

class ModbusTargetStore {
public:
    // 读取设备目标集。新记录缺失（或损坏）时回退读旧 modbus.slave 合成临时
    // targets（只回读、不写库、不触发迁移）；返回是否命中数据。
    bool load(const std::string& deviceId, DeviceTargets* out) const;

    // 覆盖保存设备目标集（key=deviceId）。
    bool save(const DeviceTargets& targets);

    // 删除设备的 modbus.target 记录（不触碰旧 modbus.slave 行）。
    bool remove(const std::string& deviceId);

    // 已持有 modbus.target 记录的设备 ID 清单。
    std::vector<std::string> allDeviceIds() const;

    // 旧 modbus.slave → modbus.target 一次性迁移：对注册表中每个含
    // protocol=="modbus" 端点的档案，按旧行逐从站生成 SlaveTarget 并落库；
    // 返回迁移的 profile 数。registry 空指针/无 modbus 设备/写库失败 →
    // qWarning 返回 0，绝不抛异常；旧记录不删除。
    int migrateLegacySlaves(class DeviceRegistry* registry);
};

} // namespace modbus
