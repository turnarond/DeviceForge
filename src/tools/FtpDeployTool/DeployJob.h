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
#include "transfer/EnsuredDirCache.h"
#include "transfer/TransferExecutor.h"
#include "adapter/IProtocolAdapter.h"
#include <memory>

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
        // v2.15 任务9 每条目远端路径贯通（与 files 同长的平行向量）：
        // 非空且长度==files.size() → 显式条目模式——files 为入口（Widget/编辑器）
        // folderexpand::expand 展开后的普通文件清单，entryRemotePaths[i] 即该条
        // 完整远端目标路径（镜像相对段已拼好）；Job 不再派生 basename、不再做
        // 目录遍历（目录在触碰 Job 前已展开，Executor 钉桩保证目录源零远端接触）。
        // 空（缺省）→ v2.14 扁平口径：basename 派生 + Job 内目录展开，逐字不变。
        // 非空但长度不符 = 组装错误，run() 头部拒绝（失败向，不触远端）。
        std::vector<std::string> entryRemotePaths;
        bool clearBefore = false;           // 部署前清空远程目录
        bool useFtps = false;               // FTPS 开关（仅 protocol=="ftp" 生效）
        std::string protocol;               // 协议注册键（"ftp"/"ssh"，SFTP 复用 "ssh"）
        const std::atomic<bool>* globalCancel = nullptr;  // 共享取消标志（只读引用；
                                                          // 生命周期契约：存活至 run() 结束，
                                                          // 本任务由后端成员 m_batchCancel 保证）
        std::function<void(const std::string&)> logSink;  // 日志出口（Job 内统一注入
                                                          // "[ip:port] " 前缀，业务文案不变）
        std::function<void(int)> progressSink;            // 进度出口（单台整体百分比 0-100）
        using ChannelFactory = std::function<std::unique_ptr<ITransferChannel>(
            std::shared_ptr<IProtocolAdapter>)>;
        ChannelFactory channelFactory; // 内部集成测试注入缝；生产使用 AdapterTransferChannel
        std::function<bool(IProtocolAdapter&, const std::string&)> makeDirectory;
        TransferExecutor::Sleeper retrySleeper;
        bool resume = false;
        std::vector<DeployDeliveredFile> deliveredFiles;
    };

    explicit DeployJob(Params params);
    ~DeployJob() override;

    // 仅在 run() 返回后调用；返回本台设备的事务结果
    DeviceResult result() const { return m_result; }

    // v2.8 Task 3（controller Ruling 1）：调度方注入的本台独立取消标志。
    // 生命周期契约：*flag 必须存活至本 job 的 run() 返回
    // （DeploymentRunner 以 shared_ptr 列表持有保证）。与 Params.globalCancel
    // 任一置位即取消；两者皆空视为不取消（防御式空守卫，Task 2 遗留项闭环）。
    void setCancel(std::atomic<bool>* flag) { m_cancelFlag = flag; }

    void run() override;

private:
    // 统一取消判定入口——所有取消检查点必须经此（含空指针守卫）
    bool isCancelled() const {
        return (m_params.globalCancel && m_params.globalCancel->load())
            || (m_cancelFlag && m_cancelFlag->load());
    }

    Params m_params;
    DeviceResult m_result;
    std::atomic<bool>* m_cancelFlag = nullptr;
    // 两路取消标志皆空时的兜底哑标志：仅满足适配器「非空指针」契约，
    // 恒为 false，不影响 isCancelled() 判定
    std::atomic<bool> m_fallbackCancel{false};
    // v2.15 波3 任务11（波2 终审 I-3）：运行级「已确保远端目录」缓存。挂在成员
    // 而非 Params——Params 本就按设备拷贝、且缓存是 job 执行期内部状态而非输入；
    // 每设备一个 job（DeploymentRunner 并发粒度），run() 内单线程使用 ⇒ 无需加锁。
    // 逐文件新建 TransferExecutor 时传址复用，祖先层确认后不再重复 stat。
    xfer::EnsuredDirCache m_ensuredDirs;
};
