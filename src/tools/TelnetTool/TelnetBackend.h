/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * File: TelnetBackend.h
 *
 * Date: 2026-07-05
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: Telnet 批量命令 Tool 后端 — 继承 ToolBackend，通过 ProtocolRegistry
 *              获取 TelnetAdapter 实例，异步批量执行命令到所有目标设备。
 */

#pragma once
#include "framework/ToolBackend.h"
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <atomic>
#include <QFuture>

class TelnetBackend : public ToolBackend {
public:
    TelnetBackend();
    ~TelnetBackend() override;

    // --- ServiceTask 线程入口（纯虚实现） ---
    int svc() override;

    // --- ToolBackend 纯虚实现 ---
    std::string toolId() const override { return "com.deploymaster.telnet.command"; }
    std::string toolName() const override { return "批量命令"; }
    std::string toolVersion() const override { return "2.0.0"; }
    std::string toolCategory() const override { return "command"; }
    std::string toolIcon() const override { return "telnet_command"; }

    void bindDevices(const std::vector<DeviceInfo>& devices) override;
    void bindCredentials(const AuthInfo& auth) override;
    void applyConfig(const lwserverbase::config::ConfigValue& config) override;

    // --- Telnet 命令执行 ---
    // ips: 目标设备 IP 列表
    // commands: 命令列表（换行分割，逐条执行）
    // timeoutSec: 单条命令超时时间（秒）
    void executeCommand(const std::vector<std::string>& ips,
                        const std::vector<std::string>& commands,
                        int timeoutSec);
    void cancel();

    // 回调设置（由 Widget 调用，跨线程安全）
    // logCb: 每步操作日志
    // resultCb: 单设备完成回调 (ip, success, elapsedMs, outputSummary)
    // finishedCb: 全部完成回调 (totalCount, successCount, failureCount)
    void setLogCallback(std::function<void(const std::string&)> cb);
    void setResultCallback(std::function<void(const std::string& ip, bool success,
                                               int elapsedMs, const std::string& output)> cb);
    void setFinishedCallback(std::function<void(int total, int success, int failed)> cb);

private:
    std::vector<DeviceInfo> m_devices;
    AuthInfo m_auth;
    std::atomic<bool> m_cancelled{false};
    QFuture<void> m_execFuture;  // 追踪异步执行任务，析构前等待完成

    std::function<void(const std::string&)> m_logCb;
    std::function<void(const std::string&, bool, int, const std::string&)> m_resultCb;
    std::function<void(int, int, int)> m_finishedCb;
};
