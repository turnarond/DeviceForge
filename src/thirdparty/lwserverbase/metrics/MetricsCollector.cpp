/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: MetricsCollector.cpp
 *
 * Date: 2026-03-03
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#include <metrics/MetricsCollector.h>
#include <limits>
#include <sstream>
#include <memory>
#include <algorithm>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <processthreadsapi.h>
#pragma comment(lib, "psapi.lib")
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

#include <fstream>
#include <ctime>

namespace {

/**
 * @brief 根据 name + labels 生成内部唯一 key
 */
std::string BuildMetricKey(const std::string& name,
                           const std::map<std::string, std::string>& labels) {
    std::string key = name;
    for (const auto& [label, value] : labels) {
        key += "_" + label + "_" + value;
    }
    return key;
}

/**
 * @brief 将 labels 序列化为 Prometheus 标签字符串 "{k1=\"v1\", k2=\"v2\"}"
 * @return 标签字符串（不含花括号），labels 为空时返回空字符串
 */
std::string BuildPrometheusLabelStr(const std::map<std::string, std::string>& labels) {
    if (labels.empty()) return "";
    std::string s;
    bool first = true;
    for (const auto& [k, v] : labels) {
        if (!first) s += ", ";
        s += k + "=\"" + v + "\"";
        first = false;
    }
    return s;
}

} // namespace

namespace lwserverbase {
namespace metrics {

// Counter 类实现
void MetricsCollector::Counter::Increment(int64_t delta) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_value += delta;
}

int64_t MetricsCollector::Counter::Get() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_value;
}

void MetricsCollector::Counter::Set(int64_t value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_value = value;
}

// Gauge 类实现
void MetricsCollector::Gauge::Set(double value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_value = value;
}

void MetricsCollector::Gauge::Increment(double delta) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_value += delta;
}

void MetricsCollector::Gauge::Decrement(double delta) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_value -= delta;
}

double MetricsCollector::Gauge::Get() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_value;
}

// Histogram 类实现
void MetricsCollector::Histogram::SetBuckets(const std::vector<double>& buckets) {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_buckets = buckets;
    if (m_buckets.empty()) {
        m_bucketCounts.clear();
        return;
    }

    std::sort(m_buckets.begin(), m_buckets.end());
    m_buckets.erase(std::unique(m_buckets.begin(), m_buckets.end()), m_buckets.end());

    m_bucketCounts.assign(m_buckets.size() + 1, 0);
}

bool MetricsCollector::Histogram::HasBuckets() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_buckets.empty() && m_bucketCounts.size() == m_buckets.size() + 1;
}

std::vector<double> MetricsCollector::Histogram::GetBucketBounds() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_buckets;
}

std::vector<int64_t> MetricsCollector::Histogram::GetBucketCounts() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_bucketCounts;
}

void MetricsCollector::Histogram::Observe(double value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_count++;
    m_sum += value;

    if (m_buckets.empty() || m_bucketCounts.size() != m_buckets.size() + 1) {
        return;
    }

    // Prometheus histogram buckets 是累计 <= le 的计数。
    // 对于每个样本：value <= bound[i] 时，该样本属于桶 i 及所有更大的桶。
    // 由于桶是累进的，value > bound[i] 时不意味着后续桶也不满足，
    // 必须遍历所有桶以确保正确累加。例如 bounds={0.1,0.5,1.0}, value=0.6：
    //   0.6 <= 0.1 → 否 | 0.6 <= 0.5 → 否 | 0.6 <= 1.0 → 是 → bucket[2]+=1
    for (size_t i = 0; i < m_buckets.size(); ++i) {
        if (value <= m_buckets[i]) {
            m_bucketCounts[i] += 1;
        }
    }

    // +Inf bucket
    m_bucketCounts.back() += 1;
}

int64_t MetricsCollector::Histogram::GetCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_count;
}

double MetricsCollector::Histogram::GetSum() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_sum;
}

// MetricsCollector 类实现
MetricsCollector::MetricsCollector() {
    // 初始化系统指标
    CollectSystemMetrics();
}

MetricsCollector::~MetricsCollector() {
}

MetricsCollector::Counter& MetricsCollector::CreateCounter(const std::string& name, const std::string& help, const std::map<std::string, std::string>& labels) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string key = BuildMetricKey(name, labels);

    if (m_counters.find(key) == m_counters.end()) {
        MetricInfo info{name, help, labels};
        m_counters[key] = std::make_pair(info, std::make_unique<Counter>());
    }

    return *m_counters[key].second;
}

MetricsCollector::Gauge& MetricsCollector::CreateGauge(const std::string& name, const std::string& help, const std::map<std::string, std::string>& labels) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string key = BuildMetricKey(name, labels);

    if (m_gauges.find(key) == m_gauges.end()) {
        MetricInfo info{name, help, labels};
        m_gauges[key] = std::make_pair(info, std::make_unique<Gauge>());
    }

    return *m_gauges[key].second;
}

MetricsCollector::Histogram& MetricsCollector::CreateHistogram(const std::string& name, const std::string& help, const std::vector<double>& buckets, const std::map<std::string, std::string>& labels) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string key = BuildMetricKey(name, labels);

    if (m_histograms.find(key) == m_histograms.end()) {
        MetricInfo info{name, help, labels};
        auto hist = std::make_unique<Histogram>();
        if (!buckets.empty()) {
            hist->SetBuckets(buckets);
        }
        m_histograms[key] = std::make_pair(info, std::move(hist));
    } else {
        // 已存在：如果此前未配置 buckets，而本次给了非空 buckets，则补上以支持 bucket 导出
        if (!buckets.empty() && !m_histograms[key].second->HasBuckets()) {
            m_histograms[key].second->SetBuckets(buckets);
        }
    }
    
    return *m_histograms[key].second;
}

std::string MetricsCollector::ExportPrometheus() const {
    std::stringstream ss;
    std::lock_guard<std::mutex> lock(m_mutex);

    // 导出计数器
    for (const auto& [key, counter] : m_counters) {
        const auto& info = counter.first;
        const auto& value = counter.second->Get();

        ss << "# HELP " << info.name << " " << info.help << std::endl;
        ss << "# TYPE " << info.name << " counter" << std::endl;

        std::string labelStr = BuildPrometheusLabelStr(info.labels);
        if (labelStr.empty()) {
            ss << info.name << " " << value << std::endl;
        } else {
            ss << info.name << "{" << labelStr << "} " << value << std::endl;
        }
    }

    // 导出仪表盘
    for (const auto& [key, gauge] : m_gauges) {
        const auto& info = gauge.first;
        const auto& value = gauge.second->Get();

        ss << "# HELP " << info.name << " " << info.help << std::endl;
        ss << "# TYPE " << info.name << " gauge" << std::endl;

        std::string labelStr = BuildPrometheusLabelStr(info.labels);
        if (labelStr.empty()) {
            ss << info.name << " " << value << std::endl;
        } else {
            ss << info.name << "{" << labelStr << "} " << value << std::endl;
        }
    }

    // 导出直方图
    for (const auto& [key, histogram] : m_histograms) {
        const auto& info = histogram.first;
        const auto& count = histogram.second->GetCount();
        const auto& sum = histogram.second->GetSum();

        ss << "# HELP " << info.name << " " << info.help << std::endl;
        ss << "# TYPE " << info.name << " histogram" << std::endl;

        // bucket 行（当调用方提供 buckets 时导出；否则保持向后兼容，只输出 count/sum）
        if (histogram.second->HasBuckets()) {
            const auto bounds = histogram.second->GetBucketBounds();
            const auto counts = histogram.second->GetBucketCounts();

            std::string labelsStr = BuildPrometheusLabelStr(info.labels);

            for (size_t i = 0; i < bounds.size(); ++i) {
                std::ostringstream leStream;
                leStream << std::setprecision(17) << bounds[i];
                const std::string leValue = leStream.str();

                if (labelsStr.empty()) {
                    ss << info.name << "_bucket{le=\"" << leValue << "\"} " << counts[i] << std::endl;
                } else {
                    ss << info.name << "_bucket{" << labelsStr << ",le=\"" << leValue << "\"} " << counts[i] << std::endl;
                }
            }

            // +Inf bucket
            if (labelsStr.empty()) {
                ss << info.name << "_bucket{le=\"+Inf\"} " << counts.back() << std::endl;
            } else {
                ss << info.name << "_bucket{" << labelsStr << ",le=\"+Inf\"} " << counts.back() << std::endl;
            }
        }

        std::string labelsStr = BuildPrometheusLabelStr(info.labels);
        if (labelsStr.empty()) {
            ss << info.name << "_count " << count << std::endl;
            ss << info.name << "_sum " << sum << std::endl;
        } else {
            ss << info.name << "_count{" << labelsStr << "} " << count << std::endl;
            ss << info.name << "_sum{" << labelsStr << "} " << sum << std::endl;
        }
    }

    return ss.str();
}

void MetricsCollector::CollectSystemMetrics() {
    // 创建系统指标
    CreateGauge("process_cpu_usage", "Total CPU usage in seconds");
    CreateGauge("process_memory_usage", "Memory usage in bytes");
    CreateCounter("process_start_time", "Process start time in seconds since epoch");
    CreateCounter("process_uptime", "Process uptime in seconds");

    // 初始化指标值
    UpdateSystemMetrics();
}

void MetricsCollector::UpdateSystemMetrics() {
#if !defined(SYLIXOS)
#if defined(_WIN32)
    // --- Windows: CPU 使用率 (GetProcessTimes) ---
    {
        HANDLE hProcess = GetCurrentProcess();
        FILETIME ftCreation, ftExit, ftKernel, ftUser;
        if (GetProcessTimes(hProcess, &ftCreation, &ftExit, &ftKernel, &ftUser)) {
            // Convert 100-nanosecond intervals to seconds
            double kernelSec = (static_cast<double>(ftKernel.dwHighDateTime) * 429.4967296 +
                              static_cast<double>(ftKernel.dwLowDateTime) / 10000000.0);
            double userSec = (static_cast<double>(ftUser.dwHighDateTime) * 429.4967296 +
                            static_cast<double>(ftUser.dwLowDateTime) / 10000000.0);
            double totalCpuSec = kernelSec + userSec;
            CreateGauge("process_cpu_usage", "Total CPU usage in seconds").Set(totalCpuSec);
        }
    }

    // --- Windows: 内存使用量 (GetProcessMemoryInfo) ---
    {
        HANDLE hProcess = GetCurrentProcess();
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
            double memMB = static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
            CreateGauge("process_memory_usage", "Memory usage in bytes").Set(
                static_cast<double>(pmc.WorkingSetSize));
        }
    }

    // --- Windows: 启动时间与运行时长 ---
    {
        ULONGLONG tickCount = GetTickCount64();
        double uptimeSec = static_cast<double>(tickCount) / 1000.0;
        if (m_processStartTime == 0) {
            // Approximate start time based on current time minus uptime
            time_t now = time(nullptr);
            m_processStartTime = static_cast<time_t>(now - static_cast<int64_t>(uptimeSec));
        }
        CreateCounter("process_start_time",
                      "Process start time in seconds since epoch").Set(m_processStartTime);
        CreateCounter("process_uptime", "Process uptime in seconds").Set(uptimeSec);
    }
#else
    // --- Linux: CPU 使用率 (getrusage): total user + system CPU seconds ---
    {
        struct rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) == 0) {
            double totalCpuSec = static_cast<double>(usage.ru_utime.tv_sec)
                               + static_cast<double>(usage.ru_stime.tv_sec)
                               + (static_cast<double>(usage.ru_utime.tv_usec)
                               +  static_cast<double>(usage.ru_stime.tv_usec)) / 1.0e6;
            CreateGauge("process_cpu_usage", "Total CPU usage in seconds").Set(totalCpuSec);
        }
    }

    // --- Linux: 内存使用量: VmRSS from /proc/self/status ---
    {
        std::ifstream statusFile("/proc/self/status");
        std::string line;
        while (std::getline(statusFile, line)) {
            if (line.compare(0, 5, "VmRSS") == 0) {
                size_t colonPos = line.find(':');
                if (colonPos != std::string::npos) {
                    std::string valStr = line.substr(colonPos + 1);
                    // Trim whitespace and remove "kB" suffix
                    size_t first = valStr.find_first_not_of(" \t\r\n");
                    if (first != std::string::npos) {
                        valStr = valStr.substr(first);
                        size_t spacePos = valStr.find(' ');
                        if (spacePos != std::string::npos) {
                            valStr = valStr.substr(0, spacePos);
                        }
                        long rssKb = std::stol(valStr);
                        CreateGauge("process_memory_usage", "Memory usage in bytes").Set(
                            static_cast<double>(rssKb) * 1024.0);
                    }
                }
                break;
            }
        }
    }

    // --- Linux: 启动时间与运行时长 ---
    {
        struct timespec ts;
        if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
            time_t nowEpoch = static_cast<time_t>(ts.tv_sec);
            if (m_processStartTime == 0) {
                m_processStartTime = nowEpoch;
            }
            CreateCounter("process_start_time",
                          "Process start time in seconds since epoch").Set(m_processStartTime);
            int64_t uptime = static_cast<int64_t>(nowEpoch - m_processStartTime);
            CreateCounter("process_uptime", "Process uptime in seconds").Set(uptime);
        }
    }
#endif // _WIN32
#else
    // SylixOS: 平台特定实现（待补充）
    (void)0;
#endif // !defined(SYLIXOS)
}

std::vector<MetricsCollector::MetricEntry> MetricsCollector::GetAllMetrics() const
{
    std::vector<MetricEntry> result;
    std::lock_guard<std::mutex> lock(m_mutex);

    for (const auto& [key, pair] : m_counters) {
        MetricEntry e;
        e.name  = pair.first.name;
        e.help  = pair.first.help;
        e.type  = "counter";
        e.value = static_cast<double>(pair.second->Get());
        result.push_back(std::move(e));
    }
    for (const auto& [key, pair] : m_gauges) {
        MetricEntry e;
        e.name  = pair.first.name;
        e.help  = pair.first.help;
        e.type  = "gauge";
        e.value = pair.second->Get();
        result.push_back(std::move(e));
    }
    for (const auto& [key, pair] : m_histograms) {
        MetricEntry e;
        e.name    = pair.first.name;
        e.help    = pair.first.help;
        e.type    = "histogram";
        e.count   = pair.second->GetCount();
        e.sum     = pair.second->GetSum();
        auto bounds = pair.second->GetBucketBounds();
        auto counts = pair.second->GetBucketCounts();
        for (size_t i = 0; i < counts.size(); ++i) {
            double le = (i < bounds.size()) ? bounds[i]
                       : std::numeric_limits<double>::infinity();
            e.buckets.push_back({le, counts[i]});
        }
        result.push_back(std::move(e));
    }
    return result;
}

} // namespace metrics
} // namespace lwserverbase