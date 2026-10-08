#pragma once

#include "device/DeviceProfile.h"

#include <optional>
#include <string>
#include <vector>

class DeviceRegistry
{
public:
    DeviceProfile upsert(const DeviceProfile& profile);
    std::optional<DeviceProfile> find(const std::string& deviceId) const;
    std::vector<DeviceProfile> list() const;

    // ConfigStore 持久化（type=device.profile，key=deviceId）。
    // load() 同时回退读取旧 device.list 记录：缺失 deviceId 的旧记录按规范化
    // 端点身份生成确定性 ID（只读迁移，不改写旧记录）；损坏记录跳过并告警。
    bool load();
    // save() 先合并进内存注册表（生成/保持 deviceId），再持久化合并结果。
    bool save(const DeviceProfile& profile);
    // remove() 从内存与 ConfigStore 删除指定档案；同时删除与该档案端点身份
    // 匹配的旧 device.list 行（key=ip:port），保证对迁移设备删除持久；
    // 不匹配的旧行保留，仍可被 load() 回退读取。
    bool remove(const std::string& deviceId);

    // 重名不阻止保存；UI 可读取此提示并以名称加地址消歧。
    const std::vector<std::string>& warnings() const;

private:
    static DeviceProfile normalize(const DeviceProfile& profile);
    static std::string stableId(const DeviceProfile& profile);
    void rebuildWarnings();

    std::vector<DeviceProfile> m_profiles;
    std::vector<std::string> m_warnings;
};
