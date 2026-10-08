#pragma once

#include "framework/DeviceInfo.h"

#include <string>
#include <vector>

// 同一物理设备可拥有多个协议端点；凭证仅保存 ConfigStore 中的引用键。
struct DeviceEndpoint {
    std::string protocol;
    std::string ip;
    int port = 0;
    std::string credentialRef;
};

struct DeviceProfile {
    std::string deviceId;
    std::string name;
    std::string note;
    std::vector<std::string> tags;
    std::vector<DeviceEndpoint> endpoints;
};

std::string endpointKey(const DeviceEndpoint& endpoint);
DeviceInfo toDeviceInfo(const DeviceProfile& profile, const DeviceEndpoint& endpoint);
DeviceProfile fromDeviceInfo(const DeviceInfo& device);
