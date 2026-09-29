#pragma once

#include "framework/DeviceInfo.h"

#include <string>
#include <vector>

inline std::vector<DeviceInfo> selectRebootDevices(
    const std::vector<DeviceInfo>& devices,
    const std::vector<std::string>& successfulKeys)
{
    std::vector<DeviceInfo> selected;
    for (const auto& device : devices) {
        const int port = device.port > 0
            ? device.port
            : (device.protocol == "ssh" ? 22 : 21);
        const std::string key = device.ip + ":" + std::to_string(port);
        for (const auto& successful : successfulKeys) {
            if (successful == key) {
                auto rebootDevice = device;
                rebootDevice.port = 0;
                selected.push_back(std::move(rebootDevice));
                break;
            }
        }
    }
    return selected;
}
