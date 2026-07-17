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
#include "adapter/ProtocolRegistry.h"
#include "adapter/FtpAdapter.h"
#include <QtConcurrent/QtConcurrent>
#include <lwlog/lwlog.h>
#include <thread>
#include <chrono>
#include <filesystem>

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

    m_uploadFuture = QtConcurrent::run([this, localFiles, useFtps, port]() {
        namespace fs = std::filesystem;

        std::vector<std::string> successes, failures;

        if (m_devices.empty()) {
            if (m_logCb) m_logCb("错误：没有绑定设备，请先在设备总线中添加目标设备");
            if (m_finishedCb) m_finishedCb(false, successes, failures);
            return;
        }

        for (size_t i = 0; i < m_devices.size(); ++i) {
            if (m_cancelled) break;

            auto device = m_devices[i];  // 拷贝，允许覆盖端口

            // 使用 Tool 级端口覆盖设备默认端口（端口由各 Tool 自行配置）
            if (port > 0) {
                device.port = port;
            }

            std::string deviceKey = device.ip + ":" + std::to_string(device.port);

            // 从 ProtocolRegistry 创建 FTP 适配器
            auto adapter = ProtocolRegistry::instance()->create("ftp");
            if (!adapter) {
                if (m_logCb) m_logCb("FTP 适配器不可用: " + deviceKey);
                failures.push_back(deviceKey);
                continue;
            }

            auto* ftp = dynamic_cast<FtpAdapter*>(adapter.get());
            if (!ftp) {
                if (m_logCb) m_logCb("FTP 适配器类型转换失败: " + deviceKey);
                failures.push_back(deviceKey);
                continue;
            }

            if (useFtps) {
                ftp->setUseFtps(true);
                if (m_logCb) m_logCb("FTPS 模式已启用: " + deviceKey);
            }

            // 连接设备
            if (m_logCb) m_logCb("正在连接: " + deviceKey + " ...");
            if (!ftp->connect(device, m_auth)) {
                if (m_logCb) m_logCb("连接失败: " + deviceKey + " — " + ftp->lastError());
                failures.push_back(deviceKey);
                continue;
            }

            if (m_logCb) m_logCb("已连接: " + deviceKey);

            // 可选：部署前清空远程目录
            if (m_clearBeforeDeploy) {
                if (m_logCb) m_logCb("清空远程目录: " + deviceKey + m_remotePath);
                if (!ftp->clearRemoteDirectory(m_remotePath)) {
                    if (m_logCb) m_logCb("清空目录失败: " + deviceKey + " — " + ftp->lastError());
                    // 清空失败不中止，继续上传
                }
            }

            // 设置进度回调
            ftp->setProgressCallback([this](int pct) {
                if (m_progressCb) m_progressCb(pct);
            });

            // 上传所有文件/文件夹
            bool allOk = true;
            for (const auto& file : localFiles) {
                if (m_cancelled) break;

                std::error_code ec;

                if (fs::is_directory(file, ec)) {
                    // 文件夹：递归上传整个目录
                    std::string folderName = fs::path(file).filename().string();
                    if (m_logCb) m_logCb("上传文件夹: " + folderName + " -> " + deviceKey);

                    if (ftp->uploadFolder(file, m_remotePath)) {
                        if (m_logCb) m_logCb(folderName + " 上传完成 (" + deviceKey + ")");
                    } else {
                        if (m_logCb) m_logCb(folderName + " 上传失败: " + ftp->lastError());
                        allOk = false;
                    }
                } else if (!ec) {
                    // 单文件上传
                    std::string fileName = file;
                    size_t lastSlash = file.find_last_of("/\\");
                    if (lastSlash != std::string::npos) {
                        fileName = file.substr(lastSlash + 1);
                    }

                    std::string remoteFile = m_remotePath;
                    if (!remoteFile.empty() && remoteFile.back() != '/') {
                        remoteFile += '/';
                    }
                    remoteFile += fileName;

                    if (m_logCb) m_logCb("上传: " + fileName + " -> " + deviceKey);

                    if (ftp->uploadFile(file, remoteFile)) {
                        if (m_logCb) m_logCb(fileName + " 上传完成 (" + deviceKey + ")");
                    } else {
                        if (m_logCb) m_logCb(fileName + " 上传失败: " + ftp->lastError());
                        allOk = false;
                    }
                } else {
                    if (m_logCb) m_logCb("无法访问路径: " + file);
                    allOk = false;
                }
            }

            ftp->disconnect();

            if (allOk) {
                successes.push_back(deviceKey);
            } else {
                failures.push_back(deviceKey);
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
