/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: TestHistogram.cpp
 *
 * Date: 2026-03-10
 *
 * Author: Yan.chaodong <yanchaodong <yanchaodong@acoinfo.com>
 *
 * Description: Histogram 类型指标的完整测试用例
 * - 基本功能测试：Observe、GetCount、GetSum
 * - 边界情况测试：负数、零、极大值、极小值
 * - 统计功能测试：平均值计算
 * - 多线程并发测试
 * - 标签功能测试
 */

#include <metrics/MetricsCollector.h>
#include <thread>
#include <vector>
#include <random>
#include <cmath>
#include <numeric>
#include "TestFramework.h"

using namespace lwserverbase::metrics;

void TestHistogramCreation() {
    MetricsCollector collector;
    
    // 测试创建直方图
    auto& histogram = collector.CreateHistogram("test_histogram", "Test histogram description");
    
    // 验证初始状态
    TEST_ASSERT_EQ(0, histogram.GetCount(), "Initial histogram count should be 0");
    TEST_ASSERT_NEAR(0.0, histogram.GetSum(), 0.0001, "Initial histogram sum should be 0.0");
}

void TestHistogramObserve() {
    MetricsCollector collector;
    auto& histogram = collector.CreateHistogram("test_histogram", "Test histogram");
    
    // 测试单次观察
    histogram.Observe(10.5);
    TEST_ASSERT_EQ(1, histogram.GetCount(), "Count should be 1 after one observation");
    TEST_ASSERT_NEAR(10.5, histogram.GetSum(), 0.0001, "Sum should be 10.5 after one observation");
    
    // 测试多次观察
    histogram.Observe(20.0);
    histogram.Observe(30.0);
    TEST_ASSERT_EQ(3, histogram.GetCount(), "Count should be 3 after three observations");
    TEST_ASSERT_NEAR(60.5, histogram.GetSum(), 0.0001, "Sum should be 60.5 after three observations");
}

void TestHistogramZeroObservation() {
    MetricsCollector collector;
    auto& histogram = collector.CreateHistogram("test_histogram", "Test histogram");
    
    // 测试观察值为 0
    histogram.Observe(0.0);
    TEST_ASSERT_EQ(1, histogram.GetCount(), "Count should be 1 after observing 0");
    TEST_ASSERT_NEAR(0.0, histogram.GetSum(), 0.0001, "Sum should be 0 after observing 0");
    
    // 再次观察非零值
    histogram.Observe(10.0);
    TEST_ASSERT_EQ(2, histogram.GetCount(), "Count should be 2");
    TEST_ASSERT_NEAR(10.0, histogram.GetSum(), 0.0001, "Sum should be 10");
}

void TestHistogramNegativeValues() {
    MetricsCollector collector;
    auto& histogram = collector.CreateHistogram("test_histogram", "Test histogram");
    
    // 测试负数观察值
    histogram.Observe(-10.5);
    TEST_ASSERT_EQ(1, histogram.GetCount(), "Count should handle negative values");
    TEST_ASSERT_NEAR(-10.5, histogram.GetSum(), 0.0001, "Sum should handle negative values");
    
    // 混合正负值
    histogram.Observe(20.0);
    histogram.Observe(-5.0);
    TEST_ASSERT_EQ(3, histogram.GetCount(), "Count should be 3");
    TEST_ASSERT_NEAR(4.5, histogram.GetSum(), 0.0001, "Sum should be 4.5");
}

void TestHistogramLargeValues() {
    MetricsCollector collector;
    auto& histogram = collector.CreateHistogram("test_histogram", "Test histogram");
    
    // 测试极大值
    double largeValue = 1e10;
    histogram.Observe(largeValue);
    TEST_ASSERT_EQ(1, histogram.GetCount(), "Count should handle large values");
    TEST_ASSERT_NEAR(largeValue, histogram.GetSum(), 1.0, "Sum should handle large values");
    
    // 测试极小值
    double smallValue = 1e-10;
    histogram.Observe(smallValue);
    TEST_ASSERT_EQ(2, histogram.GetCount(), "Count should be 2");
    TEST_ASSERT_NEAR(largeValue + smallValue, histogram.GetSum(), 1.0, "Sum should handle mixed scales");
}

void TestHistogramManyObservations() {
    MetricsCollector collector;
    auto& histogram = collector.CreateHistogram("test_histogram", "Test histogram");
    
    // 测试大量观察
    const int numObservations = 10000;
    double expectedSum = 0.0;
    
    for (int i = 0; i < numObservations; ++i) {
        double value = static_cast<double>(i);
        histogram.Observe(value);
        expectedSum += value;
    }
    
    TEST_ASSERT_EQ(numObservations, histogram.GetCount(), "Count should match number of observations");
    TEST_ASSERT_NEAR(expectedSum, histogram.GetSum(), 1.0, "Sum should be accurate");
    
    // 计算平均值
    double average = histogram.GetSum() / histogram.GetCount();
    double expectedAverage = (numObservations - 1) / 2.0;
    TEST_ASSERT_NEAR(expectedAverage, average, 1.0, "Average should be correct");
}

void TestHistogramWithLabels() {
    MetricsCollector collector;
    
    // 测试带标签的直方图
    std::map<std::string, std::string> labels1 = {{"endpoint", "/api/users"}, {"method", "GET"}};
    auto& histogram1 = collector.CreateHistogram("request_latency", "Request latency", {}, labels1);
    
    std::map<std::string, std::string> labels2 = {{"endpoint", "/api/posts"}, {"method", "POST"}};
    auto& histogram2 = collector.CreateHistogram("request_latency", "Request latency", {}, labels2);
    
    histogram1.Observe(100.0);
    histogram1.Observe(150.0);
    
    histogram2.Observe(200.0);
    
    TEST_ASSERT_EQ(2, histogram1.GetCount(), "Histogram with labels should track count independently");
    TEST_ASSERT_NEAR(250.0, histogram1.GetSum(), 0.0001, "Histogram with labels should track sum independently");
    TEST_ASSERT_EQ(1, histogram2.GetCount(), "Different labels should create separate histogram");
    TEST_ASSERT_NEAR(200.0, histogram2.GetSum(), 0.0001, "Different labels should have separate sums");
}

void TestHistogramGetOrCreate() {
    MetricsCollector collector;
    
    // 测试获取已存在的直方图
    auto& histogram1 = collector.CreateHistogram("existing_histogram", "First creation");
    histogram1.Observe(100.0);
    histogram1.Observe(200.0);
    
    // 再次创建同名同标签的直方图
    auto& histogram2 = collector.CreateHistogram("existing_histogram", "Second creation");
    
    // 应该是同一个直方图
    TEST_ASSERT_EQ(2, histogram2.GetCount(), "GetOrCreate should return existing histogram");
    TEST_ASSERT_NEAR(300.0, histogram2.GetSum(), 0.0001, "GetOrCreate should preserve sum");
    
    histogram2.Observe(300.0);
    TEST_ASSERT_EQ(3, histogram1.GetCount(), "Both references should point to same histogram");
}

void TestHistogramConcurrentObservations() {
    MetricsCollector collector;
    auto& histogram = collector.CreateHistogram("concurrent_histogram", "Concurrent test");
    
    const int numThreads = 10;
    const int observationsPerThread = 1000;
    
    std::vector<std::thread> threads;
    
    // 创建多个线程同时观察
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&histogram, observationsPerThread, i]() {
            for (int j = 0; j < observationsPerThread; ++j) {
                histogram.Observe(static_cast<double>(i * 1000 + j));
            }
        });
    }
    
    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }
    
    // 验证最终计数
    int64_t expectedCount = numThreads * observationsPerThread;
    TEST_ASSERT_EQ(expectedCount, histogram.GetCount(), "Concurrent observations should be thread-safe");
    
    // 验证总和（计算期望值）
    double expectedSum = 0.0;
    for (int i = 0; i < numThreads; ++i) {
        for (int j = 0; j < observationsPerThread; ++j) {
            expectedSum += static_cast<double>(i * 1000 + j);
        }
    }
    TEST_ASSERT_NEAR(expectedSum, histogram.GetSum(), 1.0, "Concurrent observations should have correct sum");
}

void TestHistogramRandomObservations() {
    MetricsCollector collector;
    auto& histogram = collector.CreateHistogram("random_histogram", "Random test");
    
    std::random_device rd;
    std::mt19937 gen(42); // 固定种子以保证可重复性
    std::uniform_real_distribution<> valueDist(0.0, 1000.0);
    
    const int numObservations = 10000;
    std::vector<double> values;
    
    // 生成随机观察值
    for (int i = 0; i < numObservations; ++i) {
        double value = valueDist(gen);
        histogram.Observe(value);
        values.push_back(value);
    }
    
    // 验证计数
    TEST_ASSERT_EQ(numObservations, histogram.GetCount(), "Count should match");
    
    // 计算期望的总和
    double expectedSum = std::accumulate(values.begin(), values.end(), 0.0);
    TEST_ASSERT_NEAR(expectedSum, histogram.GetSum(), 1.0, "Sum should match");
    
    // 计算平均值
    double average = histogram.GetSum() / histogram.GetCount();
    TEST_ASSERT(average > 0.0 && average < 1000.0, "Average should be in expected range");
}

void TestHistogramExportWithPrometheus() {
    MetricsCollector collector;
    auto& histogram = collector.CreateHistogram("export_histogram", "Export test histogram");
    
    histogram.Observe(100.0);
    histogram.Observe(200.0);
    histogram.Observe(300.0);
    
    std::string output = collector.ExportPrometheus();
    
    // 验证输出包含必要信息
    TEST_ASSERT(output.find("# HELP export_histogram Export test histogram") != std::string::npos,
                "Export should contain HELP line");
    TEST_ASSERT(output.find("# TYPE export_histogram histogram") != std::string::npos,
                "Export should contain TYPE line with 'histogram' type");
    TEST_ASSERT(output.find("export_histogram_count 3") != std::string::npos,
                "Export should contain count");
    TEST_ASSERT(output.find("export_histogram_sum 600") != std::string::npos,
                "Export should contain sum");
}

void TestHistogramExportWithLabels() {
    MetricsCollector collector;
    
    std::map<std::string, std::string> labels = {{"service", "api"}, {"endpoint", "/users"}};
    auto& histogram = collector.CreateHistogram("labeled_histogram", "Labeled test", {}, labels);
    
    histogram.Observe(50.0);
    
    std::string output = collector.ExportPrometheus();
    
    // 验证标签在导出中
    TEST_ASSERT(output.find("service=\"api\"") != std::string::npos,
                "Export should contain label 'service'");
    TEST_ASSERT(output.find("endpoint=\"/users\"") != std::string::npos,
                "Export should contain label 'endpoint'");
    TEST_ASSERT(output.find("labeled_histogram_count{") != std::string::npos,
                "Export should contain labeled count");
    TEST_ASSERT(output.find("labeled_histogram_sum{") != std::string::npos,
                "Export should contain labeled sum");
}

void TestHistogramDistributionStats() {
    MetricsCollector collector;
    auto& histogram = collector.CreateHistogram("stats_histogram", "Distribution stats test");
    
    // 生成正态分布的观察值
    std::random_device rd;
    std::mt19937 gen(42);
    std::normal_distribution<> normalDist(100.0, 15.0); // 均值 100，标准差 15
    
    const int numSamples = 10000;
    for (int i = 0; i < numSamples; ++i) {
        histogram.Observe(normalDist(gen));
    }
    
    // 计算统计值
    double average = histogram.GetSum() / histogram.GetCount();
    
    // 验证平均值接近期望值（允许一定偏差）
    TEST_ASSERT_NEAR(100.0, average, 5.0, "Average should be close to expected mean");
    
    // 验证计数正确
    TEST_ASSERT_EQ(numSamples, histogram.GetCount(), "Count should be correct");
}

int main() {
    lwserverbase::tests::TestRunner& runner = lwserverbase::tests::TestRunner::Instance();
    auto& suite = runner.CreateSuite("HistogramTests");
    
        ADD_TEST(suite, TestHistogramCreation);
        ADD_TEST(suite, TestHistogramObserve);
        ADD_TEST(suite, TestHistogramZeroObservation);
        ADD_TEST(suite, TestHistogramNegativeValues);
        ADD_TEST(suite, TestHistogramLargeValues);
        ADD_TEST(suite, TestHistogramManyObservations);
        ADD_TEST(suite, TestHistogramWithLabels);
        ADD_TEST(suite, TestHistogramGetOrCreate);
        ADD_TEST(suite, TestHistogramConcurrentObservations);
        ADD_TEST(suite, TestHistogramRandomObservations);
        ADD_TEST(suite, TestHistogramExportWithPrometheus);
        ADD_TEST(suite, TestHistogramExportWithLabels);
        ADD_TEST(suite, TestHistogramDistributionStats);
    
    lwserverbase::tests::TestRunner::Instance().RunAll();
    return 0;
}
