/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: ToolBackend.h
 *
 * Date: 2026-07-04
 *
 * Author: turnarond
 *
 * Description: Tool 后端基类 — 所有 Tool 的后端逻辑必须继承此类
 *
 * 继承自 lwserverbase::core::ServiceTask，获得完整的服务生命周期管理。
 * 后端可脱离 Qt 独立编译和测试。
 */

#pragma once
#include "lwserverbase/core/ServiceTask.h"
#include "DeviceInfo.h"
#include "lwserverbase/config/ConfigManager.h"
#include <string>
#include <vector>
#include <memory>

// Tool 后端基类 — 所有 Tool 的后端逻辑必须继承此类
// 继承自 lwserverbase::core::ServiceTask，获得完整的服务生命周期管理
class ToolBackend : public lwserverbase::core::ServiceTask {
public:
    ToolBackend() = default;
    virtual ~ToolBackend() = default;

    // --- 身份 ---
    virtual std::string toolId() const = 0;       // "com.deploymaster.ftp.deploy"
    virtual std::string toolName() const = 0;     // "文件部署"
    virtual std::string toolVersion() const = 0;  // "2.0.0"
    virtual std::string toolCategory() const = 0; // "deploy" | "command" | "test" | "diagnostic"
    virtual std::string toolIcon() const = 0;     // 资源路径或图标名

    // --- 设备绑定 ---
    virtual void bindDevices(const std::vector<DeviceInfo>& devices) = 0;
    virtual void bindCredentials(const AuthInfo& auth) = 0;

    // --- 运行时配置 ---
    // 当 ConfigManager 热加载配置变更时调用
    virtual void applyConfig(const lwserverbase::config::ConfigValue& config) = 0;
};
