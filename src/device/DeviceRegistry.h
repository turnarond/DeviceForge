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

    // 重名不阻止保存；UI 可读取此提示并以名称加地址消歧。
    const std::vector<std::string>& warnings() const;

private:
    static DeviceProfile normalize(const DeviceProfile& profile);
    static std::string stableId(const DeviceProfile& profile);
    void rebuildWarnings();

    std::vector<DeviceProfile> m_profiles;
    std::vector<std::string> m_warnings;
};
