/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: DeployJob.cpp
 *
 * Date: 2026-08-22
 *
 * Author: turnarond
 *
 * Description: 单台设备部署事务实现 — 逻辑自 FtpDeployBackend::startUpload
 *              设备循环体逐行平移（Task 2 串行等价重构，行为零变化）。
 */

#include "DeployJob.h"

#include "adapter/FtpAdapter.h"
#include "adapter/IProtocolAdapter.h"
#include "adapter/IDeployable.h"
#include "adapter/ProtocolRegistry.h"

#include <chrono>
#include <ctime>
#include <filesystem>

DeployJob::DeployJob(Params params)
    : m_params(std::move(params))
{
    // deviceKey 与原后端一致：端口覆盖已完成后再拼接 ip:port
    m_result.deviceKey = m_params.device.ip + ":" + std::to_string(m_params.device.port);
}

DeployJob::~DeployJob() = default;

void DeployJob::run()
{
    namespace fs = std::filesystem;

    const auto startClock = std::chrono::steady_clock::now();
    m_result.startedAt = std::time(nullptr);

    // 日志出口统一注入 "[ip:port] " 前缀，业务调用点保留原始文案
    auto log = [this](const std::string& msg) {
        if (m_params.logSink) {
            m_params.logSink("[" + m_result.deviceKey + "] " + msg);
        }
    };
    auto reportProgress = [this](int pct) {
        if (m_params.progressSink) {
            m_params.progressSink(pct);
        }
    };

    // 从 ProtocolRegistry 按协议创建适配器（"ftp"/"ssh"，SFTP 复用 "ssh" 键）
    auto adapter = ProtocolRegistry::instance()->create(m_params.protocol);
    if (!adapter) {
        log("适配器不可用 (" + m_params.protocol + ")");
        m_result.state = DeviceResult::Failed;
        return;
    }

    auto* deployable = dynamic_cast<IDeployable*>(adapter.get());
    if (!deployable) {
        log("适配器不支持部署能力 (" + m_params.protocol + ")");
        m_result.state = DeviceResult::Failed;
        return;
    }

    if (m_params.useFtps && m_params.protocol == "ftp") {
        auto* ftp = dynamic_cast<FtpAdapter*>(adapter.get());
        if (ftp) {
            ftp->setUseFtps(true);
            log("FTPS 模式已启用");
        }
    }

    // 连接设备（connect/lastError/disconnect 属 IProtocolAdapter，
    // 部署能力（上传/清空/进度/取消）属 IDeployable）
    log("正在连接 ...");
    if (!adapter->connect(m_params.device, m_params.auth)) {
        log("连接失败 — " + adapter->lastError());
        m_result.lastError = adapter->lastError();
        m_result.state = DeviceResult::Failed;
        return;
    }

    log("已连接");

    // 可选：部署前清空远程目录
    if (m_params.clearBefore) {
        log("清空远程目录: " + m_params.remotePath);
        if (!deployable->clearRemoteDirectory(m_params.remotePath)) {
            log("清空目录失败 — " + adapter->lastError());
            // 清空失败不中止，继续上传
        }
    }

    // 设置进度回调 + 取消标志。
    // 进度换算：适配器按单文件报 0-100，此处折算为跨文件总进度
    // devicePct = (doneFiles*100 + curPct) / totalFiles；
    // 取消标志：Params 持 const 指针而 IDeployable 契约要求可写 atomic，
    // 底层对象实为后端非 const 成员，此处去 const 安全。
    const size_t totalFiles = m_params.files.empty() ? 1 : m_params.files.size();
    size_t doneFiles = 0;
    deployable->setProgressCallback([this, &doneFiles, totalFiles, &reportProgress](int pct) {
        reportProgress(static_cast<int>((doneFiles * 100 + pct) / totalFiles));
    });
    deployable->setCancelFlag(const_cast<std::atomic<bool>*>(m_params.globalCancel));

    // 上传所有文件/文件夹
    bool allOk = true;
    for (const auto& file : m_params.files) {
        if (m_params.globalCancel->load()) break;

        std::error_code ec;

        if (fs::is_directory(file, ec)) {
            // 文件夹：递归上传整个目录
            std::string folderName = fs::path(file).filename().string();
            log("上传文件夹: " + folderName);

            if (deployable->uploadFolder(file, m_params.remotePath)) {
                log(folderName + " 上传完成");
            } else {
                log(folderName + " 上传失败: " + adapter->lastError());
                m_result.failedFiles.push_back(folderName);
                m_result.lastError = adapter->lastError();
                allOk = false;
            }
        } else if (!ec) {
            // 单文件上传
            std::string fileName = file;
            size_t lastSlash = file.find_last_of("/\\");
            if (lastSlash != std::string::npos) {
                fileName = file.substr(lastSlash + 1);
            }

            std::string remoteFile = m_params.remotePath;
            if (!remoteFile.empty() && remoteFile.back() != '/') {
                remoteFile += '/';
            }
            remoteFile += fileName;

            log("上传: " + fileName);

            if (deployable->uploadFile(file, remoteFile)) {
                log(fileName + " 上传完成");
            } else {
                log(fileName + " 上传失败: " + adapter->lastError());
                m_result.failedFiles.push_back(fileName);
                m_result.lastError = adapter->lastError();
                allOk = false;
            }
        } else {
            log("无法访问路径: " + file);
            m_result.failedFiles.push_back(file);
            allOk = false;
        }

        ++doneFiles;  // 本文件已出结果（成功或失败），推进整体进度窗口
    }

    adapter->disconnect();

    m_result.state = allOk ? DeviceResult::Ok : DeviceResult::Failed;
    m_result.durationMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startClock)
            .count();
}
