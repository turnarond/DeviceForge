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
        int totalCount = static_cast<int>(ips.size());
        int successCount = 0;
        int failureCount = 0;

        if (totalCount == 0) {
            if (m_logCb) m_logCb("错误：目标 IP 列表为空");
            if (m_finishedCb) m_finishedCb(0, 0, 0);
            return;
        }

        if (commands.empty()) {
            if (m_logCb) m_logCb("错误：命令列表为空");
            if (m_finishedCb) m_finishedCb(totalCount, 0, totalCount);
            return;
        }

        if (m_logCb) {
            m_logCb("开始批量命令执行，目标设备: " + std::to_string(totalCount)
                    + " 台，命令数: " + std::to_string(commands.size()));
        }

        // 逐台设备执行
        for (const auto& ip : ips) {
            if (m_cancelled) break;

            if (m_logCb) m_logCb("--- 设备: " + ip + " ---");

            // 从 ProtocolRegistry 创建 Telnet 适配器
            auto adapter = ProtocolRegistry::instance()->create("telnet");
            if (!adapter) {
                if (m_logCb) m_logCb("Telnet 适配器不可用: " + ip);
                if (m_resultCb) m_resultCb(ip, false, 0, "适配器不可用");
                failureCount++;
                continue;
            }

            auto* telnet = dynamic_cast<TelnetAdapter*>(adapter.get());
            if (!telnet) {
                if (m_logCb) m_logCb("Telnet 适配器类型转换失败: " + ip);
                if (m_resultCb) m_resultCb(ip, false, 0, "适配器类型错误");
                failureCount++;
                continue;
            }

            // 构建设备信息
            DeviceInfo dev;
            dev.ip = ip;
            dev.port = 23;
            dev.protocol = "telnet";

            // 连接设备
            if (m_logCb) m_logCb("正在连接: " + ip + " ...");
            if (!telnet->connect(dev, m_auth)) {
                std::string err = telnet->lastError().empty()
                    ? "连接超时或被拒绝"
                    : telnet->lastError();
                if (m_logCb) m_logCb("连接失败: " + ip + " — " + err);
                if (m_resultCb) m_resultCb(ip, false, 0, err);
                failureCount++;
                continue;
            }

            if (m_logCb) m_logCb("已连接: " + ip);

            // 测量执行耗时
            auto startTime = std::chrono::steady_clock::now();
            bool allOk = true;
            std::string accumulatedOutput;

            // 逐条命令执行
            int timeoutMs = timeoutSec * 1000;
            for (size_t ci = 0; ci < commands.size(); ++ci) {
                if (m_cancelled) { allOk = false; break; }

                const auto& cmd = commands[ci];

                Request req;
                req.path = cmd;
                req.timeoutMs = timeoutMs;

                if (m_logCb) m_logCb("执行命令[" + std::to_string(ci + 1) + "/"
                                     + std::to_string(commands.size()) + "]: " + cmd);

                auto future = telnet->request(req);
                auto resp = future.get();  // 阻塞等待响应

                if (resp.success) {
                    accumulatedOutput += resp.data;
                    if (m_logCb) {
                        m_logCb(ip + " 命令返回 " + std::to_string(resp.data.size()) + " 字节");
                    }
                } else {
                    if (m_logCb) m_logCb("命令执行失败: " + cmd
                                         + " — " + resp.errorMessage);
                    accumulatedOutput += "[ERROR] " + resp.errorMessage + "\n";
                    allOk = false;
                }

                // 命令间短暂间隔，避免 Telnet 缓冲区混乱
                if (ci + 1 < commands.size()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }

            auto endTime = std::chrono::steady_clock::now();
            int elapsedMs = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    endTime - startTime).count());

            // 断开连接
            telnet->disconnect();

            if (allOk) {
                successCount++;
                if (m_logCb) m_logCb(ip + " 执行完成 (" + std::to_string(elapsedMs) + "ms)");
            } else if (!m_cancelled) {
                failureCount++;
                if (m_logCb) m_logCb(ip + " 执行失败/异常");
            } else {
                failureCount++;
                if (m_logCb) m_logCb(ip + " 已取消");
            }

            // 报告单设备结果
            if (m_resultCb) {
                m_resultCb(ip, allOk && !m_cancelled, elapsedMs, accumulatedOutput);
            }
        }

        // 最终回调
        if (m_finishedCb) {
            m_finishedCb(totalCount, successCount, failureCount);
        }

        if (m_logCb) {
            std::string summary = "批量命令执行完毕 — 总计: " + std::to_string(totalCount)
                + ", 成功: " + std::to_string(successCount)
                + ", 失败: " + std::to_string(failureCount);
            m_logCb(summary);
        }
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
