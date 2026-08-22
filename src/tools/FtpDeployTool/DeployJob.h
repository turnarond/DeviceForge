/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: DeployJob.h
 *
 * Date: 2026-08-22
 *
 * Author: turnarond
 *
 * Description: 单台设备部署事务（QRunnable）— v2.8 并行批量部署 Task 2 从
 *              FtpDeployBackend::startUpload 设备循环体平移而来，串行行为等价；
 *              Task 3 起由 DeploymentRunner 以线程池并发调度。
 */

#pragma once

#include <QRunnable>

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "framework/DeviceInfo.h"
#include "tools/FtpDeployTool/DeployReport.h"

// 单台设备部署事务：Params 携带全部输入，run() 执行并产出 DeviceResult。
// 取消语义：设备间跳过判断归调度方（后端检查 globalCancel 后再提交下一台），
// Job 内部只负责传输中途的取消响应（逐文件循环头检查同一标志）。
class DeployJob : public QRunnable {
public:
    struct Params {
        DeviceInfo device;                  // 目标设备（Tool 级端口覆盖已由后端完成）
        AuthInfo auth;                      // 凭证（拷贝传入，Job 自持有）
        std::vector<std::string> files;     // 本地文件/文件夹上传列表
        std::string remotePath;             // 远程目标目录
        bool clearBefore = false;           // 部署前清空远程目录
        bool useFtps = false;               // FTPS 开关（仅 protocol=="ftp" 生效）
        std::string protocol;               // 协议注册键（"ftp"/"ssh"，SFTP 复用 "ssh"）
        const std::atomic<bool>* globalCancel = nullptr;  // 共享取消标志（只读引用；
                                                          // 生命周期契约：存活至 run() 结束，
                                                          // 本任务由后端成员 m_cancelled 保证）
        std::function<void(const std::string&)> logSink;  // 日志出口（Job 内统一注入
                                                          // "[ip:port] " 前缀，业务文案不变）
        std::function<void(int)> progressSink;            // 进度出口（单台整体百分比 0-100）
    };

    explicit DeployJob(Params params);
    ~DeployJob() override;

    // 仅在 run() 返回后调用；返回本台设备的事务结果
    DeviceResult result() const { return m_result; }

    void run() override;

private:
    Params m_params;
    DeviceResult m_result;
};
