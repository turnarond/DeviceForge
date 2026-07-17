/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: MetricsCollector.h
 *
 * Date: 2026-03-03
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#pragma once

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <memory>
#include <ctime>

namespace lwserverbase {
namespace metrics {

/**
 * @class MetricsCollector
 * @brief 指标收集器，负责收集和管理服务的监控指标
 * 
 * MetricsCollector 提供了指标的收集、存储和导出功能，
 * 支持计数器、仪表盘和直方图等多种指标类型。
 */
class MetricsCollector {
public:
    /**
     * @class Counter
     * @brief 计数器类型指标
     */
    class Counter {
    public:
        /**
         * @brief 增加计数
         * @param delta 增加的数值
         */
        void Increment(int64_t delta = 1);

        /**
         * @brief 设置计数
         * @param value 要设置的值
         */
        void Set(int64_t value);
        
        /**
         * @brief 获取当前值
         * @return 当前计数
         */
        int64_t Get() const;

    private:
        int64_t m_value = 0; ///< 计数器值
        mutable std::mutex m_mutex; ///< 互斥锁
    };
    
    /**
     * @class Gauge
     * @brief 仪表盘类型指标
     */
    class Gauge {
    public:
        /**
         * @brief 设置值
         * @param value 新值
         */
        void Set(double value);
        
        /**
         * @brief 增加值
         * @param delta 增加的数值
         */
        void Increment(double delta = 1.0);
        
        /**
         * @brief 减少值
         * @param delta 减少的数值
         */
        void Decrement(double delta = 1.0);
        
        /**
         * @brief 获取当前值
         * @return 当前值
         */
        double Get() const;

    private:
        double m_value = 0.0; ///< 仪表盘值
        mutable std::mutex m_mutex; ///< 互斥锁
    };
    
    /**
     * @class Histogram
     * @brief 直方图类型指标
     */
    class Histogram {
    public:
        Histogram() = default;

        /**
         * @brief 观察值
         * @param value 观察到的值
         */
        void Observe(double value);
        
        /**
         * @brief 设置 Prometheus histogram 的 bucket 边界（le 参数）
         *
         * buckets 会在首次非空设置后生效；内部将按升序排序并去重。
         * 若不调用或传入空数组，则仅导出 _count/_sum。
         */
        void SetBuckets(const std::vector<double>& buckets);

        /**
         * @brief 是否已设置 buckets
         */
        bool HasBuckets() const;

        /**
         * @brief 获取 buckets 边界拷贝
         */
        std::vector<double> GetBucketBounds() const;

        /**
         * @brief 获取 bucket 累计计数拷贝（size = bounds.size() + 1，最后一项为 +Inf）
         */
        std::vector<int64_t> GetBucketCounts() const;

        /**
         * @brief 获取样本数量
         * @return 样本数量
         */
        int64_t GetCount() const;
        
        /**
         * @brief 获取样本总和
         * @return 样本总和
         */
        double GetSum() const;

    private:
        int64_t m_count = 0; ///< 样本数量
        double m_sum = 0.0; ///< 样本总和

        // Prometheus histogram buckets
        std::vector<double> m_buckets;           ///< 上界边界（le）
        std::vector<int64_t> m_bucketCounts;   ///< 累计计数（m_buckets.size() + 1，其中最后为 +Inf）
        mutable std::mutex m_mutex; ///< 互斥锁
    };
    
    /**
     * @brief 构造函数
     */
    MetricsCollector();
    
    /**
     * @brief 析构函数
     */
    ~MetricsCollector();
    
    /**
     * @brief 创建计数器
     * @param name 指标名称
     * @param help 指标帮助信息
     * @param labels 标签
     * @return 计数器引用
     */
    Counter& CreateCounter(const std::string& name, const std::string& help, const std::map<std::string, std::string>& labels = {});
    
    /**
     * @brief 创建仪表盘
     * @param name 指标名称
     * @param help 指标帮助信息
     * @param labels 标签
     * @return 仪表盘引用
     */
    Gauge& CreateGauge(const std::string& name, const std::string& help, const std::map<std::string, std::string>& labels = {});
    
    /**
     * @brief 创建直方图
     * @param name 指标名称
     * @param help 指标帮助信息
     * @param buckets 桶边界
     * @param labels 标签
     * @return 直方图引用
     */
    Histogram& CreateHistogram(const std::string& name, const std::string& help, const std::vector<double>& buckets = {}, const std::map<std::string, std::string>& labels = {});
    
    /**
     * @brief 指标快照（纯数据，无格式依赖）
     *
     * 上层可据此自行构建 JSON / Prometheus / 二进制等任意格式。
     */
    struct MetricEntry {
        std::string name;
        std::string help;
        std::string type;   // "counter" | "gauge" | "histogram"
        double      value   = 0.0;       // counter/gauge 当前值
        int64_t     count   = 0;         // histogram: 样本数
        double      sum     = 0.0;       // histogram: 样本总和
        std::vector<std::pair<double, int64_t>> buckets;  // histogram: le→累计计数
    };
    std::vector<MetricEntry> GetAllMetrics() const;

    /** 导出 Prometheus 格式（便捷函数） */
    std::string ExportPrometheus() const;
    
    /**
     * @brief 收集系统指标
     */
    void CollectSystemMetrics();

    /**
     * @brief 更新系统指标值
     *
     * 使用 getrusage() 和 /proc 文件系统获取当前进程的 CPU 和内存使用情况。
     * 调用此方法会更新 CollectSystemMetrics() 创建的 Gauge/Counter 值。
     */
    void UpdateSystemMetrics();

private:
    /**
     * @struct MetricInfo
     * @brief 指标信息
     */
    struct MetricInfo {
        std::string name; ///< 指标名称
        std::string help; ///< 指标帮助信息
        std::map<std::string, std::string> labels; ///< 标签
    };
    
    std::map<std::string, std::pair<MetricInfo, std::unique_ptr<Counter>>> m_counters; ///< 计数器
    std::map<std::string, std::pair<MetricInfo, std::unique_ptr<Gauge>>> m_gauges; ///< 仪表盘
    std::map<std::string, std::pair<MetricInfo, std::unique_ptr<Histogram>>> m_histograms; ///< 直方图
    mutable std::mutex m_mutex; ///< 互斥锁

    time_t m_processStartTime = 0; ///< 进程启动时间 (epoch seconds), 由 UpdateSystemMetrics 首次调用时记录
};

} // namespace metrics
} // namespace lwserverbase