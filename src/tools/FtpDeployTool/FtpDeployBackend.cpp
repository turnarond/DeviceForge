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
 * Description: FTP 部署 Tool 后端实现 — v2.8 并行批量部署 Task 4 起，
 *              startUpload 经 DeploymentRunner 并发调度各台 DeployJob；
 *              对外三回调（进度/日志/完成）签名不变。
 */

#include "FtpDeployBackend.h"
#include "DeployJob.h"
#include "DeploymentRunner.h"
#include "config/ConfigStore.h"
#include "adapter/FtpAdapter.h"
#include "adapter/IDeployable.h"
#include "adapter/ProtocolRegistry.h"
#include <QtConcurrent/QtConcurrent>
#include <lwlog/lwlog.h>
#include <algorithm>
#include <thread>
#include <chrono>

namespace {
// 读批量部署并发度（ConfigStore 键 deploy.concurrency，设计 §8：int 1-8）。
// 存储形态与主题键同构：type="deploy"、key="concurrency"、值字段
// "concurrency"（SettingsDialog 滑块按同形态读写，v2.8 Task 5 落地）。
// 缺省/非法值一律回退 1 = 与串行行为完全一致（升级零惊扰）。
//
// 线程亲和：必须在调用方线程读取——QSqlDatabase 连接仅允许创建线程使用，
// ConfigStore 单例连接不可移入 QtConcurrent 工作线程触碰。
int loadConcurrency()
{
    const QVariant v = ConfigStore::instance()
        .load(QStringLiteral("deploy"), QStringLiteral("concurrency"))
        .value(QStringLiteral("concurrency"), 1);
    bool ok = false;
    const int n = v.toInt(&ok);
    if (!ok || n < 1) {
        return 1;
    }
    return std::min(n, 8);   // 设计上限 8；超配钳制防误配置压垮嵌入式 ftpd
}
}  // namespace

FtpDeployBackend::FtpDeployBackend()
{
}

FtpDeployBackend::~FtpDeployBackend()
{
    // 兜底（设计 §7 长稳审计点 2）：cancel-all + 等待工作线程结束，
    // 杜绝后台线程仍持有 this / Runner 悬垂（UAF 防线）
    cancelUpload();
    if (m_uploadFuture.isRunning()) {
        m_uploadFuture.waitForFinished();
    }
    // m_activeRunner 已由工作线程在 run() 返回后注销；Runner 实例随其
    // 工作作用域析构（内部 waitForDone 见 DeploymentRunner::~DeploymentRunner）
}

void FtpDeployBackend::OnStop()
{
    // 停止钩子同样 cancel-all + wait：后台工作线程不再持有 this 后才放行
    // 基类回收 svc 线程（与析构兜底同一防线，双入口各自完整）
    cancelUpload();
    if (m_uploadFuture.isRunning()) {
        m_uploadFuture.waitForFinished();
    }
    ServiceTask::OnStop();
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
    // 先等上一轮批量彻底收尾（工作线程返回、Runner 注销），再复位批量取消
    // 标志——顺序不能反：先复位会把在途批量的取消请求静默吞掉
    if (m_uploadFuture.isRunning()) {
        m_uploadFuture.waitForFinished();
    }
    m_batchCancel = false;
    m_remotePath = remotePath;
    m_clearBeforeDeploy = clearBeforeDeploy;
    m_rebootAfterDeploy = rebootAfterDeploy;

    // 并发度在调用方线程读库（Qt SQL 线程亲和，见 loadConcurrency 注释），
    // 值捕获进工作线程
    const int concurrency = loadConcurrency();

    m_uploadFuture = QtConcurrent::run([this, localFiles, protocol, useFtps, port, concurrency]() {
        if (m_devices.empty()) {
            if (m_logCb) m_logCb("错误：没有绑定设备，请先在设备总线中添加目标设备");
            if (m_finishedCb) m_finishedCb(false, {}, {});
            return;
        }

        // 组装全部台次参数（Tool 级端口覆盖在此完成——与 Runner/Job 生成的
        // "ip:port" 行键同源）；globalCancel 统一指向 m_batchCancel，生命周期
        // 由后端成员保证覆盖整个工作线程执行期
        std::vector<DeployJob::Params> allParams;
        allParams.reserve(m_devices.size());
        for (auto device : m_devices) {   // 拷贝，允许覆盖端口
            if (port > 0) {
                device.port = port;
            }

            DeployJob::Params params;
            params.device = device;
            params.auth = m_auth;   // 凭证拷贝传入，Job 自持有
            params.files = localFiles;
            params.remotePath = m_remotePath;
            params.clearBefore = m_clearBeforeDeploy;
            params.useFtps = useFtps;
            params.protocol = protocol;
            params.globalCancel = &m_batchCancel;
            params.logSink = [this](const std::string& msg) {
                if (m_logCb) m_logCb(msg);
            };
            // progressSink 不在此赋值：由 Runner 收口为带键的 deviceProgress
            // 通道（单台即时 + kOverallKey 聚合节流两路）
            allParams.push_back(std::move(params));
        }

        // 进度分诊（v2.8 Task 4）：kOverallKey 哨兵 → 既有聚合总进度通道；
        // 其余 "ip:port" → per-device 通道。两路回调均从 Runner 池线程并发
        // 触发（Runner 线程契约 I2），Widget 侧经 QueuedConnection 编组。
        auto deviceProgress = [this](const std::string& key, int pct) {
            if (key == DeploymentRunner::kOverallKey) {
                if (m_progressCb) m_progressCb(pct);
            } else if (m_deviceProgressCb) {
                m_deviceProgressCb(key, pct);
            }
        };

        // 单飞契约：每轮批量新建 Runner 实例；登记到 m_activeRunner 供
        // cancelUpload 跨线程传播取消，run() 返回后立即注销
        auto runner = std::make_shared<DeploymentRunner>();
        {
            std::lock_guard<std::mutex> lock(m_runnerMutex);
            m_activeRunner = runner;
        }

        const DeployReport report =
            runner->run(allParams, concurrency, m_batchCancel, deviceProgress);

        // 报告缓存（v2.8 Task 5）：供 Widget「导出报告」事后读取；互斥保护
        // 跨线程可见性（此处工作线程写，GUI 线程经 lastReport() 读）
        {
            std::lock_guard<std::mutex> lock(m_reportMutex);
            m_lastReport = report;
        }

        {
            std::lock_guard<std::mutex> lock(m_runnerMutex);
            if (m_activeRunner == runner) {
                m_activeRunner.reset();   // 仅注销自己这轮的登记（防误清新一轮）
            }
        }

        // 结果映射（对外 m_finishedCb 契约不变）：
        //   Ok     → successes
        //   Failed → failures
        //   Cancelled → 不入两列——串行时代 break 跳过的台次本就无结果条目，
        //   保持可观察行为等价（明细仍完整保留在 report.results 中）
        std::vector<std::string> successes, failures;
        for (const auto& r : report.results) {
            if (r.state == DeviceResult::Ok) {
                successes.push_back(r.deviceKey);
            } else if (r.state == DeviceResult::Failed) {
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

DeployReport FtpDeployBackend::lastReport() const
{
    std::lock_guard<std::mutex> lock(m_reportMutex);
    return m_lastReport;
}

void FtpDeployBackend::cancelUpload()
{
    // 先置批量级标志（未启动台次预检跳过、进行中台次文件循环头中止），
    // 再经在途 Runner 把各台独立取消标志全量置位（协议栈检查点立即生效）。
    // m_activeRunner 为空（无批量在途）时仅置标志，无害
    m_batchCancel = true;
    std::shared_ptr<DeploymentRunner> runner;
    {
        std::lock_guard<std::mutex> lock(m_runnerMutex);
        runner = m_activeRunner;
    }
    if (runner) {
        runner->requestCancel();
    }
    // 不调 requestShutdown()：svc 线程由 ServiceTask 生命周期统一停止
    LWLOG_I("FtpDeployBackend: 用户取消上传");
}

void FtpDeployBackend::setProgressCallback(std::function<void(int)> cb)
{
    m_progressCb = std::move(cb);
}

void FtpDeployBackend::setDeviceProgressCallback(
    std::function<void(const std::string& key, int pct)> cb)
{
    m_deviceProgressCb = std::move(cb);
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
