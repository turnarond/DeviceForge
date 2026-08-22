/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: FtpDeployBackend.cpp
 *
 * Date: 2026-07-04
 *
 * Author: turnarond
 *
 * Description: FTP 部署 Tool 后端实现 — 通过 ProtocolRegistry 获取 FtpAdapter，
 *              使用 QtConcurrent::run 异步上传到所有绑定设备。
 */

#include "FtpDeployBackend.h"
#include "DeployJob.h"
#include "adapter/FtpAdapter.h"
#include "adapter/IDeployable.h"
#include "adapter/ProtocolRegistry.h"
#include <QtConcurrent/QtConcurrent>
#include <lwlog/lwlog.h>
#include <thread>
#include <chrono>

FtpDeployBackend::FtpDeployBackend()
{
}

FtpDeployBackend::~FtpDeployBackend()
{
    cancelUpload();
    // 等待异步上传任务完成，防止 UAF（Use-After-Free）
    if (m_uploadFuture.isRunning()) {
        m_uploadFuture.waitForFinished();
    }
}

int FtpDeployBackend::svc()
{
    LWLOG_I("FtpDeployBackend 线程启动");
    // ServiceTask 线程主循环 — 等待取消信号
    while (isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    LWLOG_I("FtpDeployBackend 线程退出");
    return 0;
}

void FtpDeployBackend::bindDevices(const std::vector<DeviceInfo>& devices)
{
    m_devices = devices;
    LWLOG_I(("FtpDeployBackend: 绑定 " + std::to_string(devices.size()) + " 台设备").c_str());
}

void FtpDeployBackend::bindCredentials(const AuthInfo& auth)
{
    m_auth = auth;
}

void FtpDeployBackend::applyConfig(const lwserverbase::config::ConfigValue& /*config*/)
{
    // 从 ConfigManager 读取运行时配置变更（后续扩展）
}

void FtpDeployBackend::startUpload(const std::vector<std::string>& localFiles,
                                    const std::string& remotePath,
                                    bool clearBeforeDeploy,
                                    bool rebootAfterDeploy,
                                    const std::string& protocol,
                                    bool useFtps,
                                    int port)
{
    m_cancelled = false;
    m_remotePath = remotePath;
    m_clearBeforeDeploy = clearBeforeDeploy;
    m_rebootAfterDeploy = rebootAfterDeploy;

    // 如果已有上传任务在运行，先等待完成
    if (m_uploadFuture.isRunning()) {
        m_uploadFuture.waitForFinished();
    }

    m_uploadFuture = QtConcurrent::run([this, localFiles, protocol, useFtps, port]() {
        std::vector<std::string> successes, failures;

        if (m_devices.empty()) {
            if (m_logCb) m_logCb("错误：没有绑定设备，请先在设备总线中添加目标设备");
            if (m_finishedCb) m_finishedCb(false, successes, failures);
            return;
        }

        // v2.8 Task 2：逐台顺序执行 DeployJob（concurrency=1 串行等价，
        // Task 3 将替换为 DeploymentRunner 并发调度）。
        for (size_t i = 0; i < m_devices.size(); ++i) {
            if (m_cancelled) break;

            auto device = m_devices[i];  // 拷贝，允许覆盖端口

            // 使用 Tool 级端口覆盖设备默认端口（端口由各 Tool 自行配置）
            if (port > 0) {
                device.port = port;
            }

            DeployJob::Params params;
            params.device = device;
            params.auth = m_auth;
            params.files = localFiles;
            params.remotePath = m_remotePath;
            params.clearBefore = m_clearBeforeDeploy;
            params.useFtps = useFtps;
            params.protocol = protocol;
            params.globalCancel = &m_cancelled;  // 后端成员生命周期覆盖全部 Job
            params.logSink = [this](const std::string& msg) {
                if (m_logCb) m_logCb(msg);
            };
            params.progressSink = [this](int pct) {
                if (m_progressCb) m_progressCb(pct);
            };

            DeployJob job(std::move(params));
            job.run();  // 同步执行，保持逐台串行语义

            const DeviceResult& r = job.result();
            if (r.state == DeviceResult::Ok) {
                successes.push_back(r.deviceKey);
            } else {
                failures.push_back(r.deviceKey);
            }
        }

        // 部署后重启
        if (m_rebootAfterDeploy && !successes.empty()) {
            if (m_logCb) m_logCb("待 TelnetPresenter 迁移后实现重启功能");
            if (m_logCb) m_logCb("成功设备列表: ");
            for (const auto& ip : successes) {
                if (m_logCb) m_logCb("  - " + ip);
            }
        }

        if (m_finishedCb) {
            m_finishedCb(!successes.empty(), successes, failures);
        }
    });
}

void FtpDeployBackend::cancelUpload()
{
    m_cancelled = true;
    // 不调 requestShutdown()：svc 线程由 ServiceTask::~ServiceTask() 统一停止
    LWLOG_I("FtpDeployBackend: 用户取消上传");
}

void FtpDeployBackend::setProgressCallback(std::function<void(int)> cb)
{
    m_progressCb = std::move(cb);
}

void FtpDeployBackend::setLogCallback(std::function<void(const std::string&)> cb)
{
    m_logCb = std::move(cb);
}

void FtpDeployBackend::setFinishedCallback(
    std::function<void(bool, const std::vector<std::string>&,
                       const std::vector<std::string>&)> cb)
{
    m_finishedCb = std::move(cb);
}
