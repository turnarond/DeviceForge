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
//   · 单飞契约：run() 每实例仅可执行一轮，禁止并发/重入调用；实例不可跨
//     线程共享使用——run()/requestCancel() 的状态均绑定发起线程的调用序列。
class DeploymentRunner {
public:
    // 总进度上报的保留键：deviceProgress 通道中 key == kOverallKey 的回调为
    // 聚合总进度 overall = Σ devicePct / N（≥100ms 时间戳节流 + 完成时强制
    // 末次）。真实设备 key 恒为 "ip:port" 格式，不会与之冲突。
    static constexpr const char* kOverallKey = "__overall__";

    DeploymentRunner() = default;
    ~DeploymentRunner();

    DeploymentRunner(const DeploymentRunner&) = delete;
    DeploymentRunner& operator=(const DeploymentRunner&) = delete;

    // 阻塞执行一轮批量部署：params 中每项一台设备，concurrency 为并发上限
    // （<1 按 1 处理，超出设备数按设备数收敛）。globalCancel 为本轮共享取消
    // 标志（预检置位则零调度直接全 Cancelled）；返回按 startedAt 升序聚合的报告。
    //
    // 单飞契约（I1）：本方法每实例仅可执行一轮，禁止并发/重入调用；实例不可
    // 跨线程共享使用（二次 run() 会重置在途注册表与池并发度，并发调用即数据竞争）。
    //
    // deviceProgress 线程契约（I2）：回调从 ≤concurrency 个池线程上【并发】触发、
    // 必不在 GUI 线程——消费方（如 MultiProgressWidget 等 QWidget）自行串行化/
    // 编组（QueuedConnection 或队列转发），严禁在回调内直连 UI。
    // 回调可为空（为空时 Runner 跳过全部进度上报，其余行为不变）。
    //   · key 为 "ip:port" → per-device 即时进度（0-100）；
    //   · key == kOverallKey → 聚合总进度（节流上报 + 末次强制，见 kOverallKey 注释）。
    DeployReport run(const std::vector<DeployJob::Params>& params,
                     int concurrency,
                     std::atomic<bool>& globalCancel,
                     const std::function<void(const std::string& key, int pct)>& deviceProgress);

    // 可在任意线程调用；仅在 run() 存续期有意义——置位所有在途 job 的独立
    // 取消标志与当前轮 globalCancel（未启动台次于预检点记 Cancelled 跳过，
    // 进行中台次于文件循环头/协议栈检查点中止）。run() 之外调用为无害空操作。
    void requestCancel();

private:
    QThreadPool m_pool;

    // 保护下方三组在途状态；requestCancel 与 run 的注册/清理阶段互斥
    std::mutex m_mutex;
    std::vector<std::shared_ptr<DeployJob>> m_liveJobs;               // 本轮存活 job
    std::vector<std::shared_ptr<std::atomic<bool>>> m_liveFlags;      // 配套取消标志
    std::atomic<bool>* m_globalCancel = nullptr;                      // 仅 run() 期间有效
};
