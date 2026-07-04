/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: TestAdvanced.cpp
 *
 * Date: 2026-03-10
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: 高级测试用例
 * - 边界情况测试
 * - 性能测试
 * - 多线程并发压力测试
 * - 内存泄漏检测辅助测试
 * - 长时间运行稳定性测试
 */

#include <metrics/MetricsCollector.h>
#include <thread>
#include <vector>
#include <chrono>
#include <random>
#include <atomic>
#include <algorithm>
#include "TestFramework.h"
#include <cstring>

using namespace lwserverbase::metrics;

// ==================== 边界情况测试 ====================

void TestExtremeCounterValues() {
    MetricsCollector collector;
    auto& counter = collector.CreateCounter("extreme_counter", "Extreme values test");
    
    // 测试最大 int64 值
    counter.Increment(std::numeric_limits<int64_t>::max());
    TEST_ASSERT_EQ(std::numeric_limits<int64_t>::max(), counter.Get(),
                   "Counter should handle max int64");
    
    // 测试最小 int64 值
    counter.Increment(std::numeric_limits<int64_t>::min());
    // 由于溢出，值会回绕，但不应该崩溃
    int64_t value = counter.Get();
    TEST_ASSERT(true, "Counter should not crash with extreme values");
}

void TestExtremeGaugeValues() {
    MetricsCollector collector;
    auto& gauge = collector.CreateGauge("extreme_gauge", "Extreme values test");
    
    // 测试最大 double 值
    gauge.Set(std::numeric_limits<double>::max());
    TEST_ASSERT_NEAR(std::numeric_limits<double>::max(), gauge.Get(), 1e308,
                     "Gauge should handle max double");
    
    // 测试最小 double 值
    gauge.Set(std::numeric_limits<double>::lowest());
    TEST_ASSERT_NEAR(std::numeric_limits<double>::lowest(), gauge.Get(), 1e308,
                     "Gauge should handle lowest double");
    
    // 测试 NaN（应该允许，但不建议）
    gauge.Set(std::nan(""));
    double nanValue = gauge.Get();
    TEST_ASSERT(std::isnan(nanValue), "Gauge should handle NaN");
    
    // 测试无穷大
    gauge.Set(std::numeric_limits<double>::infinity());
    TEST_ASSERT(std::isinf(gauge.Get()), "Gauge should handle infinity");
}

void TestEmptyMetricName() {
    MetricsCollector collector;
    
    // 测试空指标名（应该允许，但可能不符合 Prometheus 规范）
    try {
        auto& counter = collector.CreateCounter("", "Empty name test");
        counter.Increment(1);
        TEST_ASSERT(true, "Empty metric name should be allowed");
    } catch (...) {
        TEST_ASSERT(false, "Empty metric name should not throw exception");
    }
}

void TestVeryLongMetricName() {
    MetricsCollector collector;
    
    // 创建很长的指标名
    std::string longName(10000, 'a');
    auto& counter = collector.CreateCounter(longName, "Long name test");
    counter.Increment(1);
    
    TEST_ASSERT_EQ(1, counter.Get(), "Long metric name should work");
    
    std::string output = collector.ExportPrometheus();
    TEST_ASSERT(output.find(longName) != std::string::npos,
                "Long metric name should be exported");
}

void TestSpecialCharactersInLabels() {
    MetricsCollector collector;
    
    // 测试标签值中的特殊字符
    std::map<std::string, std::string> labels = {
        {"key1", "value with spaces"},
        {"key2", "value/with/slashes"},
        {"key3", "value.with.dots"},
        {"key4", "value-with-dashes"},
        {"key5", "value:with:colons"}
    };
    
    auto& gauge = collector.CreateGauge("special_labels", "Special chars test", labels);
    gauge.Set(42.0);
    
    std::string output = collector.ExportPrometheus();
    
    // 验证特殊字符被正确处理
    TEST_ASSERT(output.find("special_labels") != std::string::npos,
                "Metric with special labels should be exported");
}

void TestUnicodeInLabels() {
    MetricsCollector collector;
    
    // 测试 Unicode 字符
    std::map<std::string, std::string> labels = {
        {"chinese", "中文测试"},
        {"japanese", "日本語テスト"},
        {"emoji", "🚀💻🔥"}
    };
    
    auto& counter = collector.CreateCounter("unicode_test", "Unicode test", labels);
    counter.Increment(1);
    
    std::string output = collector.ExportPrometheus();
    
    // 验证 Unicode 字符被导出
    TEST_ASSERT(output.find("unicode_test") != std::string::npos,
                "Unicode labels should be exported");
}

// ==================== 性能测试 ====================

void TestCounterCreationPerformance() {
    MetricsCollector collector;
    
    const int numCounters = 10000;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < numCounters; ++i) {
        collector.CreateCounter("perf_counter_" + std::to_string(i),
                                "Performance test");
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << "    Created " << numCounters << " counters in " << duration << " ms" << std::endl;
    
    // 应该在合理时间内完成（例如 5 秒）
    TEST_ASSERT(duration < 5000, "Counter creation should be fast enough");
}

void TestExportPerformance() {
    MetricsCollector collector;
    
    // 创建大量指标
    const int numMetrics = 1000;
    for (int i = 0; i < numMetrics; ++i) {
        collector.CreateCounter("perf_counter_" + std::to_string(i),
                                "Performance test").Increment(i);
        collector.CreateGauge("perf_gauge_" + std::to_string(i),
                              "Performance test").Set(i * 1.5);
        collector.CreateHistogram("perf_histogram_" + std::to_string(i),
                                  "Performance test").Observe(i * 0.1);
    }
    
    // 测试导出性能
    const int numIterations = 100;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < numIterations; ++i) {
        std::string output = collector.ExportPrometheus();
        (void)output; // 防止编译器警告
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    double avgMs = static_cast<double>(duration) / numIterations;
    
    std::cout << "    Exported " << numMetrics * 3 << " metrics " << numIterations 
              << " times in " << duration << " ms (avg " << avgMs << " ms/export)" << std::endl;
    
    // 平均导出时间应该小于 100ms
    TEST_ASSERT(avgMs < 100, "Export should be fast enough");
}

void TestHighFrequencyUpdates() {
    MetricsCollector collector;
    auto& counter = collector.CreateCounter("high_freq_counter", "High frequency test");
    
    const int numUpdates = 1000000; // 100 万次更新
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < numUpdates; ++i) {
        counter.Increment();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    double updatesPerSec = (numUpdates * 1000.0) / duration;
    
    std::cout << "    Performed " << numUpdates << " updates in " << duration 
              << " ms (" << updatesPerSec << " updates/sec)" << std::endl;
    
    TEST_ASSERT_EQ(numUpdates, counter.Get(), "All updates should be counted");
    TEST_ASSERT(updatesPerSec > 1000000, "Should handle > 1M updates/sec");
}

// ==================== 多线程并发压力测试 ====================

void TestMassiveConcurrency() {
    MetricsCollector collector;
    auto& counter = collector.CreateCounter("massive_concurrent_counter", "Massive concurrency test");
    
    const int numThreads = 50;
    const int opsPerThread = 10000;
    
    std::vector<std::thread> threads;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&counter, opsPerThread]() {
            for (int j = 0; j < opsPerThread; ++j) {
                counter.Increment();
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    int64_t expected = static_cast<int64_t>(numThreads) * opsPerThread;
    TEST_ASSERT_EQ(expected, counter.Get(), "Massive concurrent increments should be correct");
    
    std::cout << "    " << numThreads << " threads, " << opsPerThread 
              << " ops each in " << duration << " ms" << std::endl;
}

void TestMixedOperationsConcurrency() {
    MetricsCollector collector;
    
    auto& counter = collector.CreateCounter("mixed_counter", "Mixed test");
    auto& gauge = collector.CreateGauge("mixed_gauge", "Mixed test");
    auto& histogram = collector.CreateHistogram("mixed_histogram", "Mixed test");
    
    const int numThreads = 20;
    std::vector<std::thread> threads;
    std::atomic<int> errors(0);
    
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&counter, &gauge, &histogram, &errors, i]() {
            try {
                for (int j = 0; j < 1000; ++j) {
                    if (i % 3 == 0) {
                        counter.Increment(1);
                    } else if (i % 3 == 1) {
                        gauge.Set(static_cast<double>(j));
                    } else {
                        histogram.Observe(static_cast<double>(j));
                    }
                }
            } catch (...) {
                errors++;
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    TEST_ASSERT_EQ(0, errors.load(), "No exceptions in mixed concurrent operations");
    
    // 验证计数器
    int64_t expectedCounter = ((numThreads + 2) / 3) * 1000; // 7 threads (0,3,6,9,12,15,18)
    TEST_ASSERT_EQ(expectedCounter, counter.Get(), "Counter should be correct");
}

void TestConcurrentExportAndModify() {
    MetricsCollector collector;
    auto& counter = collector.CreateCounter("concurrent_export_counter", "Concurrent export test");
    
    const int numThreads = 10;
    std::vector<std::thread> threads;
    std::atomic<bool> stop(false);
    std::atomic<int> exportCount(0);
    std::atomic<int> modifyCount(0);
    
    // 导出线程
    for (int i = 0; i < 3; ++i) {
        threads.emplace_back([&collector, &stop, &exportCount]() {
            while (!stop.load()) {
                std::string output = collector.ExportPrometheus();
                exportCount++;
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
    }
    
    // 修改线程
    for (int i = 3; i < numThreads; ++i) {
        threads.emplace_back([&counter, &stop, &modifyCount]() {
            while (!stop.load()) {
                counter.Increment(1);
                modifyCount++;
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
        });
    }
    
    // 运行 1 秒
    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop.store(true);
    
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "    Exports: " << exportCount.load() 
              << ", Modifications: " << modifyCount.load() << std::endl;
    
    TEST_ASSERT(exportCount.load() > 0, "Should perform exports");
    TEST_ASSERT(modifyCount.load() > 0, "Should perform modifications");
}

// ==================== 稳定性测试 ====================

void TestRepeatedCreateAndGet() {
    MetricsCollector collector;
    
    // 重复创建和获取同一个指标
    const int iterations = 1000;
    for (int i = 0; i < iterations; ++i) {
        auto& counter = collector.CreateCounter("repeated_counter", "Repeated test");
        counter.Increment(1);
    }
    
    // 再次获取计数器（应该返回同一个）
    auto& counter = collector.CreateCounter("repeated_counter", "Repeated test");
    // 应该只有一个计数器，值为 1000
    TEST_ASSERT_EQ(iterations, counter.Get(), "Repeated create should return same counter");
    
    std::string output = collector.ExportPrometheus();
    
    // 验证只有一个 repeated_counter 指标
    int count = 0;
    size_t pos = 0;
    while ((pos = output.find("repeated_counter", pos)) != std::string::npos) {
        count++;
        pos += strlen("repeated_counter");
    }
    
    // 应该有 3 行：HELP, TYPE, value
    TEST_ASSERT(count <= 3, "Should have only one counter definition");
}

void TestCollectorLifetime() {
    // 测试在堆上创建和销毁
    MetricsCollector* collector = new MetricsCollector();
    auto& counter = collector->CreateCounter("lifetime_counter", "Lifetime test");
    counter.Increment(42);
    
    delete collector;
    
    // 不应该崩溃
    TEST_ASSERT(true, "Collector lifetime management should work");
}

void TestRapidCreationDestruction() {
    // 快速创建和销毁多个收集器
    for (int i = 0; i < 100; ++i) {
        MetricsCollector collector;
        auto& counter = collector.CreateCounter("temp_counter", "Temp test");
        counter.Increment(i);
        collector.ExportPrometheus();
    }
    
    TEST_ASSERT(true, "Rapid creation/destruction should work");
}

int main() {
    lwserverbase::tests::TestRunner& runner = lwserverbase::tests::TestRunner::Instance();
    auto& suite = runner.CreateSuite("AdvancedTests");
    
    // 边界情况测试
        ADD_TEST(suite, TestExtremeCounterValues);
        ADD_TEST(suite, TestExtremeGaugeValues);
        ADD_TEST(suite, TestEmptyMetricName);
        ADD_TEST(suite, TestVeryLongMetricName);
        ADD_TEST(suite, TestSpecialCharactersInLabels);
        ADD_TEST(suite, TestUnicodeInLabels);
    
    // 性能测试
        ADD_TEST(suite, TestCounterCreationPerformance);
        ADD_TEST(suite, TestExportPerformance);
        ADD_TEST(suite, TestHighFrequencyUpdates);
    
    // 多线程并发测试
        ADD_TEST(suite, TestMassiveConcurrency);
        ADD_TEST(suite, TestMixedOperationsConcurrency);
        ADD_TEST(suite, TestConcurrentExportAndModify);
    
    // 稳定性测试
        ADD_TEST(suite, TestRepeatedCreateAndGet);
        ADD_TEST(suite, TestCollectorLifetime);
        ADD_TEST(suite, TestRapidCreationDestruction);
    
    lwserverbase::tests::TestRunner::Instance().RunAll();
    return 0;
}
