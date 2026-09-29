/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: TelnetBackend.cpp
 *
 * Date: 2026-07-05
 *
 * Author: turnarond
 *
 * Description: Telnet 批量命令 Tool 后端实现 — 通过 ProtocolRegistry 获取 TelnetAdapter，
 *              使用 QtConcurrent::run 异步执行命令到所有目标设备。
 */

#include "TelnetBackend.h"
#include "command/BatchCommandRunner.h"
#include "adapter/ProtocolRegistry.h"
#include "adapter/TelnetAdapter.h"
#include <QtConcurrent/QtConcurrent>
#include <lwlog/lwlog.h>
#include <thread>
#include <chrono>

TelnetBackend::TelnetBackend()
{
}

TelnetBackend::~TelnetBackend()
{
    cancel();
    // 等待异步任务完成，防止 UAF（Use-After-Free）
    if (m_execFuture.isRunning()) {
        m_execFuture.waitForFinished();
    }
}

int TelnetBackend::svc()
{
    LWLOG_I("TelnetBackend 线程启动");
    // ServiceTask 线程主循环 — 等待取消信号
    while (isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    LWLOG_I("TelnetBackend 线程退出");
    return 0;
}

void TelnetBackend::bindDevices(const std::vector<DeviceInfo>& devices)
{
    m_devices = devices;
    LWLOG_I(("TelnetBackend: 绑定 " + std::to_string(devices.size()) + " 台设备").c_str());
}

void TelnetBackend::bindCredentials(const AuthInfo& auth)
{
    m_auth = auth;
}

void TelnetBackend::applyConfig(const lwserverbase::config::ConfigValue& /*config*/)
{
    // 从 ConfigManager 读取运行时配置变更（后续扩展）
}

void TelnetBackend::executeCommand(const std::vector<std::string>& ips,
                                   const std::vector<std::string>& commands,
                                   int timeoutSec)
{
    m_cancelled = false;

    // 如果已有任务在运行，先等待完成
    if (m_execFuture.isRunning()) {
        m_execFuture.waitForFinished();
    }

    m_execFuture = QtConcurrent::run([this, ips, commands, timeoutSec]() {
        CommandRequest request;
        request.protocol = m_selectedProtocol.toStdString();
        request.commands = commands;
        request.timeoutSec = timeoutSec;
        request.devices = m_devices;
        if (request.devices.size() != ips.size()) {
            request.devices.clear();
            for (const auto& ip : ips) {
                DeviceInfo device;
                device.ip = ip;
                request.devices.push_back(std::move(device));
            }
        }
        request.auth = m_auth;

        BatchCommandRunner runner;
        BatchCommandRunner::Callbacks callbacks;
        callbacks.onLog = [this](const std::string& message) {
            if (m_logCb) m_logCb(message);
        };
        callbacks.onDeviceResult = [this](const CommandDeviceResult& device) {
            const auto separator = device.deviceKey.find(':');
            const std::string ip = device.deviceKey.substr(0, separator);
            const bool success = device.state == CommandResultState::Succeeded
                              || device.state == CommandResultState::RebootTriggered;
            if (m_resultCb) m_resultCb(ip, success, device.elapsedMs, device.output);
        };
        callbacks.onFinished = [this](const CommandBatchResult& result) {
            int successes = 0;
            int failures = 0;
            for (const auto& device : result.devices) {
                if (device.state == CommandResultState::Succeeded
                    || device.state == CommandResultState::RebootTriggered) {
                    ++successes;
                } else {
                    ++failures;
                }
            }
            if (m_finishedCb)
                m_finishedCb(static_cast<int>(result.devices.size()), successes, failures);
        };
        runner.run(request, m_cancelled, callbacks);
    });
}

void TelnetBackend::cancel()
{
    m_cancelled = true;
    // 不调 requestShutdown()：svc 线程由 ServiceTask::~ServiceTask() 统一停止
    LWLOG_I("TelnetBackend: 用户取消执行");
}

void TelnetBackend::setLogCallback(std::function<void(const std::string&)> cb)
{
    m_logCb = std::move(cb);
}

void TelnetBackend::setResultCallback(
    std::function<void(const std::string& ip, bool success,
                       int elapsedMs, const std::string& output)> cb)
{
    m_resultCb = std::move(cb);
}

void TelnetBackend::setFinishedCallback(
    std::function<void(int total, int success, int failed)> cb)
{
    m_finishedCb = std::move(cb);
}
