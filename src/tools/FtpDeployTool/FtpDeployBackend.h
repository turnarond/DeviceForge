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
#include <mutex>
#include <vector>
#include <string>
#include <functional>
#include <atomic>
#include <QFuture>

class DeploymentRunner;

class FtpDeployBackend : public ToolBackend {
public:
    FtpDeployBackend();
    ~FtpDeployBackend() override;

    // --- ServiceTask 线程入口（纯虚实现） ---
    int svc() override;

    // 停止钩子：cancel-all + 等待在途上传结束后才放行基类线程回收
    // （设计 §7 长稳审计点 2：杜绝后台工作线程持有 this 悬垂）
    void OnStop() override;

    // --- ToolBackend 纯虚实现 ---
    std::string toolId() const override { return "com.deviceforge.ftp.deploy"; }
    std::string toolName() const override { return "文件部署"; }
    std::string toolVersion() const override { return "2.0.0"; }
    std::string toolCategory() const override { return "deploy"; }
    std::string toolIcon() const override { return "ftp_deploy"; }

    void bindDevices(const std::vector<DeviceInfo>& devices) override;
    void bindCredentials(const AuthInfo& auth) override;
    void applyConfig(const lwserverbase::config::ConfigValue& config) override;

    // --- FTP 部署特有操作 ---
    // protocol: "ftp" / "ssh"（按 ProtocolRegistry 注册的协议 id 取部署通道；
    // SFTP 复用 "ssh" 注册键，widget 的 currentProtocol() 对 SFTP 即返回 "ssh"）
    void startUpload(const std::vector<std::string>& localFiles,
                     const std::string& remotePath,
                     bool clearBeforeDeploy,
                     bool rebootAfterDeploy,
                     const std::string& protocol = "ftp",
                     bool useFtps = false,
                     int port = 0);
    void cancelUpload();

    // 进度回调设置（由 Widget 调用，跨线程安全）
    // setProgressCallback：聚合总进度（0-100，来自 Runner kOverallKey 哨兵分流）
    void setProgressCallback(std::function<void(int)> cb);
    // setDeviceProgressCallback（v2.8 Task 4）：单台即时进度，key 恒为 "ip:port"
    // （与 MultiProgressWidget 行键、DeviceResult.deviceKey 同源）。回调从
    // Runner 池线程触发，消费方自行 QueuedConnection 编组
    void setDeviceProgressCallback(
        std::function<void(const std::string& key, int pct)> cb);
    void setLogCallback(std::function<void(const std::string&)> cb);
    void setFinishedCallback(std::function<void(bool, const std::vector<std::string>&,
                                                 const std::vector<std::string>&)> cb);

private:
    std::vector<DeviceInfo> m_devices;
    AuthInfo m_auth;
    std::string m_remotePath;
    bool m_clearBeforeDeploy = false;
    bool m_rebootAfterDeploy = false;
    // 本轮批量的全局取消标志：作为 Params.globalCancel 注入各台 Job，
    // 并以引用传入 DeploymentRunner::run（Runner 预检 + requestCancel 传播目标）。
    // 生命周期由后端成员保证，覆盖整个 QtConcurrent 工作线程执行期
    std::atomic<bool> m_batchCancel{false};
    QFuture<void> m_uploadFuture;  // 追踪异步上传任务，析构前等待完成

    // 在途调度器登记（v2.8 Task 4）：每轮批量在 QtConcurrent 工作线程新建
    // Runner 实例（单飞契约），run() 存续期登记于此；cancelUpload 经它把取消
    // 跨线程传播到各台独立取消标志。互斥锁保护指针换入/换出。
    std::mutex m_runnerMutex;
    std::shared_ptr<DeploymentRunner> m_activeRunner;

    std::function<void(int)> m_progressCb;
    std::function<void(const std::string& key, int pct)> m_deviceProgressCb;
    std::function<void(const std::string&)> m_logCb;
    std::function<void(bool, const std::vector<std::string>&,
                       const std::vector<std::string>&)> m_finishedCb;
};
