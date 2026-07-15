/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: UpdateTypes.h
 * Date: 2026-07-15
 * Author: turnarond
 * Description: 在线更新系统基础数据类型（状态枚举、版本号、Release 信息）
 */
#pragma once
#include <cstdint>
#include <cstdlib>
#include <string>

// 更新检查状态机
enum class UpdateState {
    Idle,
    Checking,
    Ready,        // new version available
    Downloading,
    Installed,    // download + verification complete, ready to install
    Error         // silent error (network/parse failure)
};

// GitHub Release 解析结果
struct ReleaseInfo {
    std::string tagName;        // "v2.2.0"
    std::string htmlUrl;        // release page URL
    std::string body;           // release notes (markdown)
    std::string assetName;      // "DeviceForge-v2.2.0-win64.zip"
    std::string downloadUrl;    // browser_download_url
    int64_t     assetSize = 0;  // bytes
    bool        isNewer = false; // tag > current?
};

// 语义化版本号
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;

    std::string toString() const {
        return std::to_string(major) + "." +
               std::to_string(minor) + "." +
               std::to_string(patch);
    }
};

// 解析 "v2.1.0" 或 "2.1.0" → Version
inline Version parseVersion(const std::string& tag) {
    Version v;
    const char* s = tag.c_str();
    if (*s == 'v') ++s;
    v.major = std::atoi(s);
    while (*s && *s != '.') ++s; if (*s == '.') ++s;
    v.minor = std::atoi(s);
    while (*s && *s != '.') ++s; if (*s == '.') ++s;
    v.patch = std::atoi(s);
    return v;
}

// 返回: <0 (a更旧), 0 (相同), >0 (a更新)
inline int compareVersion(const Version& a, const Version& b) {
    if (a.major != b.major) return (a.major > b.major) ? 1 : -1;
    if (a.minor != b.minor) return (a.minor > b.minor) ? 1 : -1;
    if (a.patch != b.patch) return (a.patch > b.patch) ? 1 : -1;
    return 0;
}
