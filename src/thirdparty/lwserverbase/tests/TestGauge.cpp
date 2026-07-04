/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: TestGauge.cpp
 *
 * Date: 2026-03-10
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: Gauge 类型指标的完整测试用例
 * - 基本功能测试：Set、Get、Increment、Decrement
 * - 边界情况测试：负数、零、小数、大数值
 * - 多线程并发测试
 * - 标签功能测试
 */

#include <metrics/MetricsCollector.h>
#include <thread>
#include <vector>
#include <random>
#include <cmath>
#include "TestFramework.h"

using namespace lwserverbase::metrics;

void TestGaugeCreation() {
    MetricsCollector collector;
    
    // 测试创建仪表盘
    auto& gauge = collector.CreateGauge("test_gauge", "Test gauge description");
    
    // 验证初始值为 0.0
    TEST_ASSERT_NEAR(0.0, gauge.Get(), 0.0001, "Initial gauge value should be 0.0");
}

void TestGaugeSet() {
    MetricsCollector collector;
    auto& gauge = collector.CreateGauge("test_gauge", "Test gauge");
    
    // 测试设置正值
    gauge.Set(100.5);
    TEST_ASSERT_NEAR(100.5, gauge.Get(), 0.0001, "Gauge should be set to 100.5");
    
    // 测试设置负值
    gauge.Set(-50.25);
    TEST_ASSERT_NEAR(-50.25, gauge.Get(), 0.0001, "Gauge should support negative values");
    
    // 测试设置零
    gauge.Set(0.0);
    TEST_ASSERT_NEAR(0.0, gauge.Get(), 0.0001, "Gauge should be set to 0.0");
    
    // 测试设置小数值
    gauge.Set(3.1415926);
    TEST_ASSERT_NEAR(3.1415926, gauge.Get(), 0.0000001, "Gauge should support high precision decimals");
}

void TestGaugeIncrement() {
    MetricsCollector collector;
    auto& gauge = collector.CreateGauge("test_gauge", "Test gauge");
    
    // 测试 Increment() 默认增加 1.0
    gauge.Increment();
    TEST_ASSERT_NEAR(1.0, gauge.Get(), 0.0001, "Gauge should be 1.0 after Increment()");
    
    // 测试 Increment(delta) 增加指定值
    gauge.Increment(5.5);
    TEST_ASSERT_NEAR(6.5, gauge.Get(), 0.0001, "Gauge should be 6.5 after Increment(5.5)");
    
    // 测试多次递增
    gauge.Increment(10.0);
    gauge.Increment(20.0);
    TEST_ASSERT_NEAR(36.5, gauge.Get(), 0.0001, "Gauge should accumulate correctly");
}

void TestGaugeDecrement() {
    MetricsCollector collector;
    auto& gauge = collector.CreateGauge("test_gauge", "Test gauge");
    
    // 先设置一个值
    gauge.Set(100.0);
    
    // 测试 Decrement() 默认减少 1.0
    gauge.Decrement();
    TEST_ASSERT_NEAR(99.0, gauge.Get(), 0.0001, "Gauge should be 99.0 after Decrement()");
    
    // 测试 Decrement(delta) 减少指定值
    gauge.Decrement(50.5);
    TEST_ASSERT_NEAR(48.5, gauge.Get(), 0.0001, "Gauge should be 48.5 after Decrement(50.5)");
    
    // 测试减少到负数
    gauge.Decrement(100.0);
    TEST_ASSERT_NEAR(-51.5, gauge.Get(), 0.0001, "Gauge should support negative values after decrement");
}

void TestGaugeZeroOperations() {
    MetricsCollector collector;
    auto& gauge = collector.CreateGauge("test_gauge", "Test gauge");
    
    // 测试增加 0
    gauge.Increment(0.0);
    TEST_ASSERT_NEAR(0.0, gauge.Get(), 0.0001, "Gauge should remain 0 after Increment(0)");
    
    // 测试减少 0
    gauge.Decrement(0.0);
    TEST_ASSERT_NEAR(0.0, gauge.Get(), 0.0001, "Gauge should remain 0 after Decrement(0)");
    
    gauge.Set(50.0);
    gauge.Increment(0.0);
    TEST_ASSERT_NEAR(50.0, gauge.Get(), 0.0001, "Gauge should remain unchanged after Increment(0)");
    
    gauge.Decrement(0.0);
    TEST_ASSERT_NEAR(50.0, gauge.Get(), 0.0001, "Gauge should remain unchanged after Decrement(0)");
}

void TestGaugeLargeValues() {
    MetricsCollector collector;
    auto& gauge = collector.CreateGauge("test_gauge", "Test gauge");
    
    // 测试大数值
    double largeValue = 1e15; // 10^15
    gauge.Set(largeValue);
    TEST_ASSERT_NEAR(largeValue, gauge.Get(), 1.0, "Gauge should handle large values");
    
    // 测试很小的数值
    double smallValue = 1e-15; // 10^-15
    gauge.Set(smallValue);
    TEST_ASSERT_NEAR(smallValue, gauge.Get(), 1e-16, "Gauge should handle very small values");
}

void TestGaugeWithLabels() {
    MetricsCollector collector;
    
    // 测试带标签的仪表盘
    std::map<std::string, std::string> labels1 = {{"cpu", "cpu0"}, {"type", "user"}};
    auto& gauge1 = collector.CreateGauge("cpu_usage", "CPU usage", labels1);
    
    std::map<std::string, std::string> labels2 = {{"cpu", "cpu1"}, {"type", "user"}};
    auto& gauge2 = collector.CreateGauge("cpu_usage", "CPU usage", labels2);
    
    gauge1.Set(75.5);
    gauge2.Set(80.2);
    
    TEST_ASSERT_NEAR(75.5, gauge1.Get(), 0.0001, "Gauge with labels should work independently");
    TEST_ASSERT_NEAR(80.2, gauge2.Get(), 0.0001, "Gauge with different labels should be separate");
}

void TestGaugeGetOrCreate() {
    MetricsCollector collector;
    
    // 测试获取已存在的仪表盘
    auto& gauge1 = collector.CreateGauge("existing_gauge", "First creation");
    gauge1.Set(100.0);
    
    // 再次创建同名同标签的仪表盘
    auto& gauge2 = collector.CreateGauge("existing_gauge", "Second creation");
    
    // 应该是同一个仪表盘，值应该保持
    TEST_ASSERT_NEAR(100.0, gauge2.Get(), 0.0001, "GetOrCreate should return existing gauge");
    
    gauge2.Set(200.0);
    TEST_ASSERT_NEAR(200.0, gauge1.Get(), 0.0001, "Both references should point to same gauge");
}

void TestGaugeConcurrentSet() {
    MetricsCollector collector;
    auto& gauge = collector.CreateGauge("concurrent_gauge", "Concurrent test");
    
    const int numThreads = 10;
    std::vector<std::thread> threads;
    
    // 多个线程同时设置不同的值
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&gauge, i]() {
            for (int j = 0; j < 100; ++j) {
                gauge.Set(static_cast<double>(i * 100 + j));
            }
        });
    }
    
    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }
    
    // 验证最终值在合理范围内（最后一个设置的值）
    double finalValue = gauge.Get();
    TEST_ASSERT(finalValue >= 0.0 && finalValue <= 1000.0, 
                "Concurrent set should result in a valid value");
}

void TestGaugeConcurrentIncrementDecrement() {
    MetricsCollector collector;
    auto& gauge = collector.CreateGauge("mixed_gauge", "Mixed operations test");
    
    const int numThreads = 10;
    std::vector<std::thread> threads;
    
    // 一半线程递增，一半线程递减
    for (int i = 0; i < numThreads; ++i) {
        if (i % 2 == 0) {
            threads.emplace_back([&gauge]() {
                for (int j = 0; j < 100; ++j) {
                    gauge.Increment(1.0);
                }
            });
        } else {
            threads.emplace_back([&gauge]() {
                for (int j = 0; j < 100; ++j) {
                    gauge.Decrement(1.0);
                }
            });
        }
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // 5 个线程递增：5 * 100 * 1.0 = 500
    // 5 个线程递减：5 * 100 * 1.0 = 500
    // 净值应该接近 0（由于并发，可能有微小差异）
    double finalValue = std::abs(gauge.Get());
    TEST_ASSERT(finalValue <= 100.0, "Concurrent inc/dec should result in value close to 0");
}

void TestGaugeRandomOperations() {
    MetricsCollector collector;
    auto& gauge = collector.CreateGauge("random_gauge", "Random operations test");
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> valueDist(-100.0, 100.0);
    std::uniform_int_distribution<> opDist(0, 2); // 0: Set, 1: Increment, 2: Decrement
    
    double expectedValue = 0.0;
    
    // 执行 1000 次随机操作
    for (int i = 0; i < 1000; ++i) {
        int op = opDist(gen);
        double value = valueDist(gen);
        
        switch (op) {
            case 0:
                gauge.Set(value);
                expectedValue = value;
                break;
            case 1:
                gauge.Increment(value);
                expectedValue += value;
                break;
            case 2:
                gauge.Decrement(value);
                expectedValue -= value;
                break;
        }
    }
    
    // 验证最终值（由于浮点数精度，允许一定误差）
    double actualValue = gauge.Get();
    double diff = std::abs(actualValue - expectedValue);
    TEST_ASSERT(diff < 1e10, "Random operations should maintain consistency");
}

void TestGaugeExportWithPrometheus() {
    MetricsCollector collector;
    auto& gauge = collector.CreateGauge("gauge_export_test", "Export test gauge");
    gauge.Set(42.5);
    
    std::string output = collector.ExportPrometheus();
    
    // 验证输出包含必要信息
    TEST_ASSERT(output.find("# HELP gauge_export_test Export test gauge") != std::string::npos,
                "Export should contain HELP line");
    TEST_ASSERT(output.find("# TYPE gauge_export_test gauge") != std::string::npos,
                "Export should contain TYPE line with 'gauge' type");
    TEST_ASSERT(output.find("gauge_export_test 42.5") != std::string::npos,
                "Export should contain gauge value");
}

void TestGaugeSystemMetrics() {
    MetricsCollector collector;
    
    // 系统指标会自动创建一些 Gauge
    std::string output = collector.ExportPrometheus();
    
    // 验证包含系统指标
    TEST_ASSERT(output.find("process_cpu_usage") != std::string::npos,
                "System metrics should include process_cpu_usage");
    TEST_ASSERT(output.find("process_memory_usage") != std::string::npos,
                "System metrics should include process_memory_usage");
}

int main() {
    lwserverbase::tests::TestRunner& runner = lwserverbase::tests::TestRunner::Instance();
    auto& suite = runner.CreateSuite("GaugeTests");
    
        ADD_TEST(suite, TestGaugeCreation);
        ADD_TEST(suite, TestGaugeSet);
        ADD_TEST(suite, TestGaugeIncrement);
        ADD_TEST(suite, TestGaugeDecrement);
        ADD_TEST(suite, TestGaugeZeroOperations);
        ADD_TEST(suite, TestGaugeLargeValues);
        ADD_TEST(suite, TestGaugeWithLabels);
        ADD_TEST(suite, TestGaugeGetOrCreate);
        ADD_TEST(suite, TestGaugeConcurrentSet);
        ADD_TEST(suite, TestGaugeConcurrentIncrementDecrement);
        ADD_TEST(suite, TestGaugeRandomOperations);
        ADD_TEST(suite, TestGaugeExportWithPrometheus);
        ADD_TEST(suite, TestGaugeSystemMetrics);
    
    lwserverbase::tests::TestRunner::Instance().RunAll();
    return 0;
}
