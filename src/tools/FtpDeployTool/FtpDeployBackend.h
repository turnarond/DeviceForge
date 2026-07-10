/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: FtpDeployBackend.h
 *
 * Date: 2026-07-04
 *
 * Author: turnarond
 *
 * Description: FTP 部署 Tool 后端 — 继承 ToolBackend，通过 ProtocolRegistry
 *              获取 FtpAdapter 实例，异步批量上传文件到所有绑定设备。
 */

#pragma once
#include "framework/ToolBackend.h"
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <atomic>
#include <QFuture>

class FtpDeployBackend : public ToolBackend {
public:
    FtpDeployBackend();
    ~FtpDeployBackend() override;

    // --- ServiceTask 线程入口（纯虚实现） ---
    int svc() override;

    // --- ToolBackend 纯虚实现 ---
    std::string toolId() const override { return "com.deploymaster.ftp.deploy"; }
    std::string toolName() const override { return "文件部署"; }
    std::string toolVersion() const override { return "2.0.0"; }
    std::string toolCategory() const override { return "deploy"; }
    std::string toolIcon() const override { return "ftp_deploy"; }

    void bindDevices(const std::vector<DeviceInfo>& devices) override;
    void bindCredentials(const AuthInfo& auth) override;
    void applyConfig(const lwserverbase::config::ConfigValue& config) override;

    // --- FTP 部署特有操作 ---
    void startUpload(const std::vector<std::string>& localFiles,
                     const std::string& remotePath,
                     bool clearBeforeDeploy,
                     bool rebootAfterDeploy,
                     bool useFtps = false);
    void cancelUpload();

    // 进度回调设置（由 Widget 调用，跨线程安全）
    void setProgressCallback(std::function<void(int)> cb);
    void setLogCallback(std::function<void(const std::string&)> cb);
    void setFinishedCallback(std::function<void(bool, const std::vector<std::string>&,
                                                 const std::vector<std::string>&)> cb);

private:
    std::vector<DeviceInfo> m_devices;
    AuthInfo m_auth;
    std::string m_remotePath;
    bool m_clearBeforeDeploy = false;
    bool m_rebootAfterDeploy = false;
    std::atomic<bool> m_cancelled{false};
    QFuture<void> m_uploadFuture;  // 追踪异步上传任务，析构前等待完成

    std::function<void(int)> m_progressCb;
    std::function<void(const std::string&)> m_logCb;
    std::function<void(bool, const std::vector<std::string>&,
                       const std::vector<std::string>&)> m_finishedCb;
};
