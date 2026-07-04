/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * File: example_classic_service.cpp
 *
 * Date: 2026-05-16
 *
 * Description: lwserverbase 经典综合服务示例
 *
 * 场景: DataCollectorService — 一个多传感器数据采集服务。
 *
 * 功能覆盖:
 *   ServiceTask    — 线程池管理、activate/svc/wait 生命周期
 *   ConfigManager  — JSON 配置加载、热更新、变更通知
 *   Logger         — 五级日志 (DEBUG/INFO/WARN/ERROR/CRITICAL)
 *   MetricsCollector — Counter/Gauge/Histogram/Prometheus 导出
 *   ProcessController — 单实例锁、信号处理
 *   OnCommand      — 运行时命令控制 (status/metrics/stop)
 *   OnHealthCheck  — 自定义健康检查
 *
 * 构建: cd src/ext/lwserverbase && mkdir -p build && cd build
 *       cmake .. && cmake --build .
 *       cp ../examples/config.json .
 * 运行: ./example_classic_service
 *       ./example_classic_service --config=config.json
 */

#include <core/ServiceTask.h>
#include <core/ServiceManager.h>
#include <metrics/MetricsCollector.h>
#include <config/ConfigManager.h>
#include <logging/Logger.h>
#include <process/ProcessController.h>

#include <iostream>
#include <thread>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <fstream>

using namespace lwserverbase::core;
using namespace lwserverbase::metrics;
using namespace lwserverbase::logging;
using namespace lwserverbase::config;
using namespace lwserverbase::process;

/* -------------------------------------------------------------------------- */
/*  DataCollectorService                                                      */
/* -------------------------------------------------------------------------- */

class DataCollectorService : public ServiceTask {
public:
    DataCollectorService() = default;

    /* ---- 生命周期 ---- */

    int OnStart(int /*argc*/, char* /*argv*/[]) override
    {
        auto* mgr = GetServiceManager();
        logger_         = mgr->GetLogger();
        config_         = mgr->GetConfigManager();
        metrics_collector_ = mgr->GetMetricsCollector();

        logger_->Info("========================================");
        logger_->Info("  %s v%s starting...",
                      GetServiceName().c_str(), GetServiceVersion().c_str());
        logger_->Info("  %s", GetServiceDescription().c_str());
        logger_->Info("========================================");

        /* 1. 加载配置 */
        load_config();

        /* 2. 注册配置热更新 */
        if (config_) {
            config_->RegisterChangeHandler("collector.interval_ms",
                [this](const std::string& /*key*/,
                       const std::string& oldVal, const std::string& newVal) {
                    collect_interval_ms_ = std::stoi(newVal);
                    logger_->Info("[HotReload] collect_interval_ms: %s -> %s",
                                  oldVal.c_str(), newVal.c_str());
                });
        }

        /* 3. 初始化指标 */
        init_metrics();

        /* 4. 启动线程池 */
        int worker_count = config_ ? config_->Get("collector.workers", 3) : 3;
        if (!activate(worker_count)) {
            logger_->Error("Failed to activate worker threads");
            return -1;
        }
        logger_->Info("Worker threads activated: %d", get_thread_count());

        logger_->Info("%s started successfully", GetServiceName().c_str());
        return 0;
    }

    void OnStop() override
    {
        logger_->Info("Stopping %s...", GetServiceName().c_str());

        /* ServiceTask::OnStop() 会设 running_=false 并 join 所有线程 */
        ServiceTask::OnStop();

        logger_->Info("%s stopped", GetServiceName().c_str());
    }

    void OnReloadConfig() override
    {
        logger_->Info("[ReloadConfig] Reloading configuration from disk");
        if (config_ && !config_path_.empty()) {
            config_->Load(config_path_);
            load_config();
        }
        logger_->Info("[ReloadConfig] Configuration reloaded");
    }

    void OnHealthCheck() override
    {
        bool healthy = is_running() && metrics_collector_ != nullptr;
        if (healthy) {
            logger_->Debug("[HealthCheck] OK — %d workers active", get_thread_count());
        } else {
            logger_->Warn("[HealthCheck] DEGRADED");
        }
    }

    bool OnCommand(const std::string& cmd, const std::string& /*args*/,
                   std::string& resp) override
    {
        if (cmd == "status") {
            std::ostringstream oss;
            oss << GetServiceName() << " v" << GetServiceVersion() << "\n"
                << "  Running:  " << (is_running() ? "yes" : "no") << "\n"
                << "  Workers:  " << get_thread_count() << "\n"
                << "  Messages: " << msg_counter_->Get() << "\n"
                << "  Errors:   " << error_counter_->Get() << "\n";
            resp = oss.str();
            return true;
        }
        if (cmd == "metrics" && metrics_collector_) {
            resp = metrics_collector_->ExportPrometheus();
            return true;
        }
        if (cmd == "reset" && metrics_collector_) {
            msg_counter_->Set(0);
            error_counter_->Set(0);
            resp = "Counters reset";
            return true;
        }
        return false;
    }

    /* ---- 服务信息 ---- */

    std::string GetServiceName() const override        { return "data-collector"; }
    std::string GetServiceVersion() const override     { return "2.0.0"; }
    std::string GetServiceDescription() const override {
        return "Multi-sensor data collection service";
    }

    /* ---- svc() — 每个 worker 线程的业务循环 ---- */

    int svc() override
    {
        logger_->Info("[Worker %d] started", worker_id_++);

        std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<> sensor_dist(10.0, 80.0);
        std::uniform_int_distribution<>    error_dist(1, 20);

        while (isRunning()) {
            /* 模拟采集 */
            double temp       = sensor_dist(rng);
            double humidity   = sensor_dist(rng) * 0.8;
            double latency_ms = 5.0 + (std::sin(temp) + 1.0) * 10.0;

            /* 记录指标 */
            msg_counter_->Increment();
            temp_gauge_->Set(temp);
            humidity_gauge_->Set(humidity);
            latency_histogram_->Observe(latency_ms / 1000.0);

            /* 模拟偶发错误 */
            if (error_dist(rng) == 1) {
                error_counter_->Increment();
                logger_->Warn("[Worker] sensor read timeout, temp=%.1f", temp);
            }

            /* 心跳日志 */
            if (msg_counter_->Get() % 50 == 0) {
                logger_->Info("[Worker] heartbeat: msgs=%lld temp=%.1f hum=%.1f",
                              (long long)msg_counter_->Get(), temp, humidity);
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(collect_interval_ms_));
        }

        logger_->Info("[Worker] exiting, processed %lld messages",
                      (long long)msg_counter_->Get());
        return 0;
    }

private:
    void load_config()
    {
        if (!config_) return;

        collect_interval_ms_  = config_->Get("collector.interval_ms", 200);
        int log_lvl           = config_->Get("collector.log_level", static_cast<int>(Logger::Level::INFO));
        std::string log_file  = config_->Get("collector.log_file", std::string("data_collector.log"));

        if (logger_) {
            logger_->SetLevel(static_cast<Logger::Level>(log_lvl));
        }
        logger_->Info("Config loaded: interval=%dms level=%d log=%s",
                      collect_interval_ms_, log_lvl, log_file.c_str());
    }

    void init_metrics()
    {
        if (!metrics_collector_) return;

        msg_counter_ = &metrics_collector_->CreateCounter(
            "collector_messages_total", "Total sensor messages collected");

        error_counter_ = &metrics_collector_->CreateCounter(
            "collector_errors_total", "Total collection errors");

        temp_gauge_ = &metrics_collector_->CreateGauge(
            "sensor_temperature_celsius", "Latest temperature reading (C)");

        humidity_gauge_ = &metrics_collector_->CreateGauge(
            "sensor_humidity_percent", "Latest humidity reading (%)");

        latency_histogram_ = &metrics_collector_->CreateHistogram(
            "collection_latency_seconds", "Collection latency distribution",
            {0.005, 0.01, 0.025, 0.05, 0.1, 0.25});

        logger_->Info("Metrics initialized: 2 counters, 2 gauges, 1 histogram");
    }

    /* 组件指针 */
    Logger*           logger_           = nullptr;
    ConfigManager*     config_           = nullptr;
    MetricsCollector* metrics_collector_ = nullptr;

    /* 配置 */
    std::string config_path_;
    int collect_interval_ms_ = 200;

    /* 指标 */
    MetricsCollector::Counter*   msg_counter_   = nullptr;
    MetricsCollector::Counter*   error_counter_ = nullptr;
    MetricsCollector::Gauge*     temp_gauge_    = nullptr;
    MetricsCollector::Gauge*     humidity_gauge_ = nullptr;
    MetricsCollector::Histogram* latency_histogram_ = nullptr;

    /* Worker ID (thread-local simulation) */
    static thread_local int worker_id_;
};

thread_local int DataCollectorService::worker_id_ = 0;

/* -------------------------------------------------------------------------- */
/*  main                                                                      */
/* -------------------------------------------------------------------------- */

int main(int argc, char* argv[])
{
    return ServiceManager::Run<DataCollectorService>(argc, argv);
}
