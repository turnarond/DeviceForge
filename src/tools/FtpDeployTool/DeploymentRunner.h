/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: DeploymentRunner.h
 *
 * Date: 2026-08-22
 *
 * Author: turnarond
 *
 * Description: 批量部署并发调度器（v2.8 并行批量部署 Task 3）— 私有 QThreadPool
 *              并发调度 N 台 DeployJob，聚合 DeployReport；per-job 取消标志经
 *              DeployJob::setCancel 注入，requestCancel 全量传播。
 *              设计依据 docs/03-设计/方案设计/2026-08-22-v2.8-并行批量部署设计.md §4。
 */

#pragma once

#include <QThreadPool>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "tools/FtpDeployTool/DeployJob.h"
#include "tools/FtpDeployTool/DeployReport.h"

// 并发调度器生命周期契约：
//   · run() 为阻塞式——池内全部 job 的 run() 完整返回后才读 result() 聚合，
//     满足红线「适配器在 DeployJob::run() 内创建销毁、结果仅事后读取」；
//   · job 与其取消标志均由 Runner 以 shared_ptr 持有至该轮 run() 结束，
//     保证 IDeployable「flag 存活至传输结束」契约（QRunnable 关闭自动删除）；
//   · 单实例可跨多轮部署复用（Backend 成员持有），析构时兜底 cancel-all + wait。
class DeploymentRunner {
public:
    DeploymentRunner() = default;
    ~DeploymentRunner();

    DeploymentRunner(const DeploymentRunner&) = delete;
    DeploymentRunner& operator=(const DeploymentRunner&) = delete;

    // 阻塞执行一轮批量部署：params 中每项一台设备，concurrency 为并发上限
    // （<1 按 1 处理，超出设备数按设备数收敛）。globalCancel 为本轮共享取消
    // 标志（预检置位则零调度直接全 Cancelled）；deviceProgress(key,pct) 为
    // per-device 进度收口回调（可为空）。返回按 startedAt 升序聚合的报告。
    DeployReport run(const std::vector<DeployJob::Params>& params,
                     int concurrency,
                     std::atomic<bool>& globalCancel,
                     const std::function<void(const std::string& key, int pct)>& deviceProgress);

    // 线程安全：置位所有在途 job 的独立取消标志与当前轮 globalCancel。
    // 未启动台次将在 job 预检点记 Cancelled 跳过，进行中台次于文件循环头中止。
    void requestCancel();

private:
    QThreadPool m_pool;

    // 保护下方三组在途状态；requestCancel 与 run 的注册/清理阶段互斥
    std::mutex m_mutex;
    std::vector<std::shared_ptr<DeployJob>> m_liveJobs;               // 本轮存活 job
    std::vector<std::shared_ptr<std::atomic<bool>>> m_liveFlags;      // 配套取消标志
    std::atomic<bool>* m_globalCancel = nullptr;                      // 仅 run() 期间有效
};
