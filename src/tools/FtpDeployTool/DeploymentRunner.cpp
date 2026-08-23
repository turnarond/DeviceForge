/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: DeploymentRunner.cpp
 *
 * Date: 2026-08-22
 *
 * Author: turnarond
 *
 * Description: 批量部署并发调度器实现 — 私有 QThreadPool + per-job 取消标志，
 *              全部 job 完整返回后聚合 DeployReport（v2.8 Task 3）。
 */

#include "DeploymentRunner.h"

#include <algorithm>
#include <chrono>
#include <ctime>

DeploymentRunner::~DeploymentRunner()
{
    // 兜底（设计 §7 长稳审计点 2）：析构前 cancel-all + 等待池清空，
    // 杜绝后台线程仍持有 job/回调捕获的悬垂
    requestCancel();
    m_pool.waitForDone();
}

void DeploymentRunner::requestCancel()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& flag : m_liveFlags) {
        if (flag) {
            flag->store(true);
        }
    }
    // 当前轮全局标志一并置位：进行中台次的适配器级取消检查点立即生效
    if (m_globalCancel) {
        m_globalCancel->store(true);
    }
}

DeployReport DeploymentRunner::run(
    const std::vector<DeployJob::Params>& params,
    int concurrency,
    std::atomic<bool>& globalCancel,
    const std::function<void(const std::string& key, int pct)>& deviceProgress)
{
    DeployReport report;
    report.protocol = params.empty() ? std::string() : params.front().protocol;

    // 并发度钳制：<1 按 1；超出设备数按设备数收敛（多开线程无益）
    concurrency = std::max(1, std::min(concurrency, static_cast<int>(params.size())));
    report.concurrency = concurrency;
    m_pool.setMaxThreadCount(concurrency);

    // 预检取消：零调度、零适配器活动，直接全 Cancelled（不占任何池线程）
    if (globalCancel.load()) {
        const std::time_t now = std::time(nullptr);
        for (const auto& p : params) {
            DeviceResult r;
            r.deviceKey = p.device.ip + ":" + std::to_string(p.device.port);
            r.state = DeviceResult::Cancelled;  // 显式赋值，杜绝默认 Ok 外漏
            r.startedAt = now;
            r.durationMs = 0;
            report.results.push_back(std::move(r));
        }
        return report;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_globalCancel = &globalCancel;  // requestCancel 的传播目标（run() 存续期）
        m_liveJobs.clear();
        m_liveFlags.clear();
        m_liveJobs.reserve(params.size());
        m_liveFlags.reserve(params.size());
    }

    // 聚合总进度（设计 §4.3）：每台一个原子槽位，任一进度更新即重算
    // overall = Σ devicePct / N，经同一 deviceProgress 通道以保留键 kOverallKey
    // 上报；上报受 ≥100ms 时间戳节流约束（防 UI 刷屏），全部完成后强制补发
    // 末次，杜绝 100% 被节流窗口吞掉。
    const size_t n = params.size();
    std::vector<std::unique_ptr<std::atomic<int>>> pcts(n);
    for (auto& slot : pcts) {
        slot = std::make_unique<std::atomic<int>>(0);
    }

    std::mutex emitMutex;
    // 初值取「当前时刻 − 100ms」：首个更新即可通过窗口判定立即发射。
    // 不可用 time_point::min()——与 now() 相减会发生有符号 int64 纳秒溢出
    // （UB，实测回绕为巨大负值），门闸将永不开启、节流沦为死代码。
    auto lastEmit = std::chrono::steady_clock::now() - std::chrono::milliseconds(100);
    auto overallOf = [&pcts, n]() -> int {
        long long sum = 0;
        for (const auto& slot : pcts) {
            sum += slot->load(std::memory_order_relaxed);
        }
        return static_cast<int>(sum / static_cast<long long>(n));
    };
    auto tryEmitOverall = [&](bool force) {
        if (!deviceProgress || n == 0) {
            return;
        }
        bool fire = force;
        if (!fire) {
            // 仅锁节流窗口判定，回调在锁外触发（用户代码不得持锁调用）
            std::lock_guard<std::mutex> lock(emitMutex);
            const auto now = std::chrono::steady_clock::now();
            if (now - lastEmit >= std::chrono::milliseconds(100)) {
                lastEmit = now;
                fire = true;
            }
        }
        if (fire) {
            deviceProgress(std::string(kOverallKey), overallOf());
        }
    };

    for (size_t i = 0; i < params.size(); ++i) {
        const DeployJob::Params& p = params[i];
        DeployJob::Params wired = p;
        wired.globalCancel = &globalCancel;  // 引用参数恒有效——入池前统一接好，
                                             // 消除 Params 默认空指针入池的可能
        // 进度出口统一收口为 deviceProgress(key,pct)，key 由 Runner 补齐：
        // per-device 即时转发不变；总进度走聚合节流通道
        const std::string key =
            wired.device.ip + ":" + std::to_string(wired.device.port);
        wired.progressSink = [deviceProgress, key, slot = pcts[i].get(),
                              tryEmitOverall](int pct) {
            slot->store(pct, std::memory_order_relaxed);
            if (deviceProgress) {
                deviceProgress(key, pct);
            }
            tryEmitOverall(false);
        };

        auto job = std::make_shared<DeployJob>(std::move(wired));
        auto flag = std::make_shared<std::atomic<bool>>(false);
        job->setCancel(flag.get());  // 先注入后调度，杜绝「已开跑标志未接好」窗口

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_liveJobs.push_back(job);
            m_liveFlags.push_back(flag);
        }

        // Runner 以 shared_ptr 托管生命周期，必须关闭 QRunnable 自动删除
        job->setAutoDelete(false);
        m_pool.start(job.get());
    }

    // 阻塞等待全部完成——此后读 result()/state 即安全：
    // 每个 job 恰好执行一次且 run() 已完整返回（红线约束）
    m_pool.waitForDone();

    // 强制末次总进度：不受节流窗口约束，保证完成态（全部 Ok 时即 100%）必达
    tryEmitOverall(true);

    for (const auto& job : m_liveJobs) {
        report.results.push_back(job->result());
    }
    // 聚合按 startedAt 升序稳定排序：并发下完成顺序不定，启动时序 +
    // 同秒保持提交序（stable）保证报告输出确定可归档
    std::stable_sort(report.results.begin(), report.results.end(),
                     [](const DeviceResult& a, const DeviceResult& b) {
                         return a.startedAt < b.startedAt;
                     });

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_liveJobs.clear();
        m_liveFlags.clear();
        m_globalCancel = nullptr;
    }
    return report;
}
