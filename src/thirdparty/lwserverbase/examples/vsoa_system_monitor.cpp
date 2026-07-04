/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * vsoa_system_monitor.cpp — VSOA 系统监控节点
 *
 * 通过 VsoaNode 继承，一键获取完整 VSOA 微服务：
 *   - RPC 端点:  /system/status  /system/cpu  /system/memory
 *   - 发布数据:  /system/cpu_usage  /system/memory_usage (每秒)
 *   - Prometheus: /metrics
 *   - 健康检查:  /health
 *   - 版本信息:  /version
 *   - 配置查询:  /config
 *
 * 构建:
 *   cd src/ext/lwserverbase && mkdir -p build && cd build
 *   cmake .. -DBUILD_EXAMPLES=ON && make vsoa_system_monitor
 *   cp ../examples/vsoa_monitor_config.json config.json
 *
 * 运行: ./vsoa_system_monitor
 */

#include <vsoa_node/VsoaNode.h>
#include <logging/Logger.h>
#include <metrics/MetricsCollector.h>

using namespace lwserverbase::metrics;

#include "libjson/yyjson.h"
#include <fstream>
#include <cmath>
#include <algorithm>

using namespace lwserverbase::vsoa_node;

/* ========================================================================= */
/*  SystemMonitorNode                                                         */
/* ========================================================================= */

/* 使用 yyjson 构建 JSON 并转换为 std::string */
static std::string toJsonAndFree(yyjson_mut_doc* doc, yyjson_mut_val* root) {
    char* json = yyjson_mut_write(doc, 0, nullptr);
    std::string result(json ? json : "{}");
    free(json);
    yyjson_mut_doc_free(doc);
    return result;
}

class SystemMonitorNode : public VsoaNode {
public:
    SystemMonitorNode()
    {
        auto& cfg = getNodeConfig();
        cfg.serviceName = "system-monitor";
        cfg.version      = "1.0.0";
        cfg.description  = "Publishes CPU and memory usage via VSOA";
    }

    /* ---- 用户回调 ------------------------------------------------------ */

    int OnInit() override
    {
        getNodeConfig().dataMode = 0; // 0=字符串，1=二进制
        auto* logger  = getServiceManager()->GetLogger();
        auto* metrics = getServiceManager()->GetMetricsCollector();

        /* 0. 创建监控指标（自动出现在 /metrics 端点） */
        if (metrics) {
            metric_cpu_     = &metrics->CreateGauge(
                "system_cpu_percent", "Current CPU usage percentage");
            metric_mem_     = &metrics->CreateGauge(
                "system_memory_percent", "Current memory usage percentage");
            metric_mem_mb_  = &metrics->CreateGauge(
                "system_memory_used_mb", "Currently used memory in MB");
            metric_mem_total_ = &metrics->CreateGauge(
                "system_memory_total_mb", "Total memory in MB");
            metric_collect_ = &metrics->CreateCounter(
                "system_collect_total", "Total number of collection cycles");
            logger->Info("Metrics registered: cpu, mem%, mem_mb, mem_total, collect_count");
        }

        /* 1. 注册 RPC 端点 */
        registerRpc("/system/status", [this](const std::string&,
            const void*, size_t, std::string& response) {
            collect();
            yyjson_mut_doc* d = yyjson_mut_doc_new(nullptr);
            yyjson_mut_val* r = yyjson_mut_obj(d);
            yyjson_mut_obj_add_real(d, r, "cpu_percent", cpu_percent_);
            yyjson_mut_obj_add_real(d, r, "memory_percent", mem_percent_);
            yyjson_mut_obj_add_real(d, r, "memory_used_mb", mem_used_mb_);
            yyjson_mut_obj_add_real(d, r, "memory_total_mb", mem_total_mb_);
            yyjson_mut_obj_add_int(d, r, "collection_count", collect_count_);
            response = toJsonAndFree(d, r);
        });

        registerRpc("/system/cpu", [this](const std::string&,
            const void*, size_t, std::string& response) {
            collect_cpu();
            yyjson_mut_doc* d = yyjson_mut_doc_new(nullptr);
            yyjson_mut_val* r = yyjson_mut_obj(d);
            yyjson_mut_obj_add_real(d, r, "cpu_percent", cpu_percent_);
            response = toJsonAndFree(d, r);
        });

        registerRpc("/system/memory", [this](const std::string&,
            const void*, size_t, std::string& response) {
            collect_memory();
            yyjson_mut_doc* d = yyjson_mut_doc_new(nullptr);
            yyjson_mut_val* r = yyjson_mut_obj(d);
            yyjson_mut_obj_add_real(d, r, "mem_percent",  mem_percent_);
            yyjson_mut_obj_add_real(d, r, "mem_used_mb",  mem_used_mb_);
            yyjson_mut_obj_add_real(d, r, "mem_total_mb", mem_total_mb_);
            response = toJsonAndFree(d, r);
        });

        /* 2. 注册健康检查 */
        registerHealthChecker([this]() -> HealthState {
            HealthState s;
            collect();
            if (cpu_percent_ > 95.0) {
                s.overallStatus = HealthState::Status::DEGRADED;
                s.checkResults["cpu"] = "CRITICAL: " + std::to_string(cpu_percent_) + "%";
            } else {
                s.checkResults["cpu"] = "OK: " + std::to_string(cpu_percent_) + "%";
            }
            s.checkResults["memory"] = "OK: " + std::to_string(mem_percent_) + "%";
            return s;
        });

        logger->Info("RPC endpoints registered: /system/status /system/cpu /system/memory");
        logger->Info("Health checker registered (cpu>95%% -> degraded)");
        return 0;
    }

    /* ---- 业务循环（ServiceTask 线程入口，每秒执行一次）----------------- */

    int svc() override
    {
        auto* logger = getServiceManager()->GetLogger();
        logger->Info("Publish loop started: /system/cpu_usage + /system/memory_usage");

        while (isRunning()) {
            collect();

            /* 更新 Prometheus 指标（实时反映在 /metrics 端点） */
            if (metric_cpu_)     metric_cpu_->Set(cpu_percent_);
            if (metric_mem_)     metric_mem_->Set(mem_percent_);
            if (metric_mem_mb_)  metric_mem_mb_->Set(mem_used_mb_);
            if (metric_mem_total_) metric_mem_total_->Set(mem_total_mb_);
            if (metric_collect_) metric_collect_->Increment();

            /* 发布 CPU 数据 */
            {
                yyjson_mut_doc* d = yyjson_mut_doc_new(nullptr);
                yyjson_mut_val* r = yyjson_mut_obj(d);
                yyjson_mut_obj_add_real(d, r, "cpu_percent", cpu_percent_);
                yyjson_mut_obj_add_int(d, r, "timestamp", time(nullptr));
                publish("/system/cpu_usage", toJsonAndFree(d, r));
            }

            /* 发布内存数据 */
            {
                yyjson_mut_doc* d = yyjson_mut_doc_new(nullptr);
                yyjson_mut_val* r = yyjson_mut_obj(d);
                yyjson_mut_obj_add_real(d, r, "mem_percent",  mem_percent_);
                yyjson_mut_obj_add_real(d, r, "mem_used_mb",  mem_used_mb_);
                yyjson_mut_obj_add_real(d, r, "mem_total_mb", mem_total_mb_);
                yyjson_mut_obj_add_int(d, r, "timestamp", time(nullptr));
                publish("/system/memory_usage", toJsonAndFree(d, r));
            }

            collect_count_++;
            if (collect_count_ % 10 == 0) {
                logger->Info("[heartbeat] #%d cpu=%.1f%% mem=%.1f%%",
                             collect_count_, cpu_percent_, mem_percent_);
            }

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        return 0;
    }

    /* ---- 运行时命令 ---------------------------------------------------- */

    bool OnCommand(const std::string& cmd, const std::string& args,
                   std::string& resp) override
    {
        if (cmd == "cpu") {
            collect_cpu();
            resp = std::to_string(cpu_percent_) + "%";
            return true;
        }
        if (cmd == "memory") {
            collect_memory();
            resp = std::to_string(mem_percent_) + "% (" +
                   std::to_string(mem_used_mb_) + "MB / " +
                   std::to_string(mem_total_mb_) + "MB)";
            return true;
        }
        return VsoaNode::OnCommand(cmd, args, resp);
    }

    /* ---- 服务信息 ------------------------------------------------------ */

    std::string GetServiceName() const override        { return "system-monitor"; }
    std::string GetServiceVersion() const override     { return "1.0.0"; }
    std::string GetServiceDescription() const override {
        return "System CPU/memory monitor via VSOA pub/sub";
    }

private:
    /* ---- 系统采集 ------------------------------------------------------ */

    void collect()
    {
        collect_cpu();
        collect_memory();
    }

    void collect_cpu()
    {
        std::ifstream stat("/proc/stat");
        if (!stat) { cpu_percent_ = -1; return; }
        std::string line;
        std::getline(stat, line);
        long long user, nice, sys, idle, iowait, irq, softirq;
        std::istringstream(line) >> line >> user >> nice >> sys
                                 >> idle >> iowait >> irq >> softirq;
        long long total  = user + nice + sys + idle + iowait + irq + softirq;
        long long idle_t = idle + iowait;

        if (prev_total_ > 0) {
            long long total_d = total - prev_total_;
            long long idle_d  = idle_t - prev_idle_;
            cpu_percent_ = (total_d - idle_d) * 100.0 / total_d;
        }
        prev_total_ = total;
        prev_idle_  = idle_t;
    }

    void collect_memory()
    {
        std::ifstream meminfo("/proc/meminfo");
        if (!meminfo) { mem_percent_ = -1; return; }
        long long total_kb = 0, free_kb = 0, buf_kb = 0, cache_kb = 0;
        std::string line;
        while (std::getline(meminfo, line)) {
            std::istringstream iss(line);
            std::string key; long long val; std::string unit;
            iss >> key >> val >> unit;
            if (key == "MemTotal:")      total_kb = val;
            else if (key == "MemFree:")  free_kb  = val;
            else if (key == "Buffers:")  buf_kb   = val;
            else if (key == "Cached:")   cache_kb = val;
        }
        long long used_kb = total_kb - free_kb - buf_kb - cache_kb;
        mem_total_mb_ = total_kb / 1024.0;
        mem_used_mb_  = used_kb / 1024.0;
        mem_percent_  = (used_kb * 100.0) / total_kb;
    }

    double cpu_percent_   = 0;
    double mem_percent_   = 0;
    double mem_used_mb_   = 0;
    double mem_total_mb_  = 0;
    int    collect_count_ = 0;

    long long prev_total_ = 0;
    long long prev_idle_  = 0;

    /* 指标对象指针 */
    MetricsCollector::Gauge*   metric_cpu_      = nullptr;
    MetricsCollector::Gauge*   metric_mem_      = nullptr;
    MetricsCollector::Gauge*   metric_mem_mb_   = nullptr;
    MetricsCollector::Gauge*   metric_mem_total_ = nullptr;
    MetricsCollector::Counter* metric_collect_  = nullptr;
};

/* ========================================================================= */
/*  main                                                                      */
/* ========================================================================= */

int main(int argc, char* argv[])
{
    return VsoaNode::Run<SystemMonitorNode>(argc, argv);
}
