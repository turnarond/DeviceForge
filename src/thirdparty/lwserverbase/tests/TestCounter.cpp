/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: TestCounter.cpp
 *
 * Date: 2026-03-10
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: Counter 类型指标的完整测试用例
 * - 基本功能测试：创建、递增、获取值
 * - 边界情况测试：负数、零、大数值
 * - 多线程并发测试
 * - 标签功能测试
 */
#include <metrics/MetricsCollector.h>
#include <thread>
#include <vector>
#include <atomic>
#include "TestFramework.h"
using namespace lwserverbase::metrics;
void TestCounterCreation() {
    MetricsCollector collector;
    
    // 测试创建计数器
    auto& counter = collector.CreateCounter("test_counter", "Test counter description");
    
    // 验证初始值为 0
    TEST_ASSERT_EQ(0, counter.Get(), "Initial counter value should be 0");
}
void TestCounterIncrement() {
    MetricsCollector collector;
    auto& counter = collector.CreateCounter("test_counter", "Test counter");
    
    // 测试 Increment() 默认增加 1
    counter.Increment();
    TEST_ASSERT_EQ(1, counter.Get(), "Counter should be 1 after Increment()");
    
    // 测试 Increment(delta) 增加指定值
    counter.Increment(5);
    TEST_ASSERT_EQ(6, counter.Get(), "Counter should be 6 after Increment(5)");
    
    // 测试多次递增
    counter.Increment(10);
    counter.Increment(20);
    TEST_ASSERT_EQ(36, counter.Get(), "Counter should be 36 after multiple increments");
}
void TestCounterNegativeIncrement() {
    MetricsCollector collector;
    auto& counter = collector.CreateCounter("test_counter", "Test counter");
    
    // 测试负数递增（虽然不推荐，但应该允许）
    counter.Increment(-5);
    TEST_ASSERT_EQ(-5, counter.Get(), "Counter should support negative increment");
    
    // 测试从负数恢复
    counter.Increment(10);
    TEST_ASSERT_EQ(5, counter.Get(), "Counter should recover from negative value");
}
void TestCounterZeroIncrement() {
    MetricsCollector collector;
    auto& counter = collector.CreateCounter("test_counter", "Test counter");
    
    // 测试增加 0
    counter.Increment(0);
    TEST_ASSERT_EQ(0, counter.Get(), "Counter should remain 0 after Increment(0)");
    
    counter.Increment(10);
    counter.Increment(0);
    TEST_ASSERT_EQ(10, counter.Get(), "Counter should remain 10 after Increment(0)");
}
void TestCounterLargeValues() {
    MetricsCollector collector;
    auto& counter = collector.CreateCounter("test_counter", "Test counter");
    
    // 测试大数值
    int64_t largeValue = 1000000000LL; // 10 亿
    counter.Increment(largeValue);
    TEST_ASSERT_EQ(largeValue, counter.Get(), "Counter should handle large values");
    
    // 测试累加大数值
    counter.Increment(largeValue);
    TEST_ASSERT_EQ(2 * largeValue, counter.Get(), "Counter should accumulate large values");
}
void TestCounterWithLabels() {
    MetricsCollector collector;
    
    // 测试带标签的计数器
    std::map<std::string, std::string> labels1 = {{"method", "GET"}, {"status", "200"}};
    auto& counter1 = collector.CreateCounter("http_requests", "HTTP requests", labels1);
    
    std::map<std::string, std::string> labels2 = {{"method", "POST"}, {"status", "200"}};
    auto& counter2 = collector.CreateCounter("http_requests", "HTTP requests", labels2);
    
    counter1.Increment(10);
    counter2.Increment(5);
    
    TEST_ASSERT_EQ(10, counter1.Get(), "Counter with labels should work independently");
    TEST_ASSERT_EQ(5, counter2.Get(), "Counter with different labels should be separate");
}
void TestCounterGetOrCreate() {
    MetricsCollector collector;
    
    // 测试获取已存在的计数器（应该返回同一个实例）
    auto& counter1 = collector.CreateCounter("existing_counter", "First creation");
    counter1.Increment(100);
    
    // 再次创建同名同标签的计数器
    auto& counter2 = collector.CreateCounter("existing_counter", "Second creation");
    
    // 应该是同一个计数器，值应该保持
    TEST_ASSERT_EQ(100, counter2.Get(), "GetOrCreate should return existing counter");
    
    counter2.Increment(50);
    TEST_ASSERT_EQ(150, counter1.Get(), "Both references should point to same counter");
}
void TestCounterConcurrentIncrement() {
    MetricsCollector collector;
    auto& counter = collector.CreateCounter("concurrent_counter", "Concurrent test");
    
    const int numThreads = 10;
    const int incrementsPerThread = 1000;
    
    std::vector<std::thread> threads;
    
    // 创建多个线程同时递增
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&counter, incrementsPerThread]() {
            for (int j = 0; j < incrementsPerThread; ++j) {
                counter.Increment();
            }
        });
    }
    
    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }
    
    // 验证最终值
    int64_t expected = numThreads * incrementsPerThread;
    TEST_ASSERT_EQ(expected, counter.Get(), "Concurrent increments should be thread-safe");
}
void TestCounterMixedConcurrentOperations() {
    MetricsCollector collector;
    auto& counter = collector.CreateCounter("mixed_counter", "Mixed operations test");
    
    const int numThreads = 5;
    std::vector<std::thread> threads;
    std::atomic<int> errors(0);
    
    // 混合操作：一些线程增加正数，一些增加负数
    for (int i = 0; i < numThreads; ++i) {
        if (i % 2 == 0) {
            threads.emplace_back([&counter, &errors]() {
                for (int j = 0; j < 100; ++j) {
                    try {
                        counter.Increment(10);
                    } catch (...) {
                        errors++;
                    }
                }
            });
        } else {
            threads.emplace_back([&counter, &errors]() {
                for (int j = 0; j < 100; ++j) {
                    try {
                        counter.Increment(-5);
                    } catch (...) {
                        errors++;
                    }
                }
            });
        }
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    TEST_ASSERT_EQ(0, errors.load(), "No exceptions should occur during concurrent operations");
    
    // 验证值：5 个线程，每个 100 次操作
    // 偶数线程：+10 * 100 = +1000 (2 个线程) = +2000
    // 奇数线程：-5 * 100 = -500 (3 个线程) = -1500
    // 总计：+500
    int64_t expected = (3 * 100 * 10) + (2 * 100 * -5);
    TEST_ASSERT_EQ(expected, counter.Get(), "Mixed concurrent operations should be correct");
}
void TestCounterExportWithPrometheus() {
    MetricsCollector collector;
    auto& counter = collector.CreateCounter("export_test", "Export test counter");
    counter.Increment(42);
    
    std::string output = collector.ExportPrometheus();
    
    // 验证输出包含必要信息
    TEST_ASSERT(output.find("# HELP export_test Export test counter") != std::string::npos,
                "Export should contain HELP line");
    TEST_ASSERT(output.find("# TYPE export_test counter") != std::string::npos,
                "Export should contain TYPE line");
    TEST_ASSERT(output.find("export_test 42") != std::string::npos,
                "Export should contain counter value");
}
int main() {
    lwserverbase::tests::TestRunner& runner = lwserverbase::tests::TestRunner::Instance();
    auto& suite = runner.CreateSuite("CounterTests");
    
           ADD_TEST(suite, TestCounterCreation);
           ADD_TEST(suite, TestCounterIncrement);
           ADD_TEST(suite, TestCounterNegativeIncrement);
           ADD_TEST(suite, TestCounterZeroIncrement);
           ADD_TEST(suite, TestCounterLargeValues);
           ADD_TEST(suite, TestCounterWithLabels);
           ADD_TEST(suite, TestCounterGetOrCreate);
           ADD_TEST(suite, TestCounterConcurrentIncrement);
           ADD_TEST(suite, TestCounterMixedConcurrentOperations);
           ADD_TEST(suite, TestCounterExportWithPrometheus);
    
    lwserverbase::tests::TestRunner::Instance().RunAll();
    return 0;
}
