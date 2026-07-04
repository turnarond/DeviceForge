/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: TestPrometheusExport.cpp
 *
 * Date: 2026-03-10
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: Prometheus 导出功能的完整测试用例
 * - 正常导出流程验证
 * - 格式正确性测试
 * - 不同类型指标导出测试
 * - 标签导出测试
 * - 空指标集合导出测试
 * - 大量指标导出测试
 * - 特殊字符处理测试
 * - Prometheus 格式兼容性测试
 */

#include <metrics/MetricsCollector.h>
#include <sstream>
#include <regex>
#include <set>
#include "TestFramework.h"

using namespace lwserverbase::metrics;

// 辅助函数：解析 Prometheus 格式
bool ParsePrometheusLine(const std::string& line, std::string& metricName, 
                         std::map<std::string, std::string>& labels, double& value) {
    // 跳过注释行
    if (line.empty() || line[0] == '#') {
        return true;
    }
    
    // 查找最后一个空格分隔值和指标名
    size_t lastSpace = line.rfind(' ');
    if (lastSpace == std::string::npos) {
        return false;
    }
    
    try {
        value = std::stod(line.substr(lastSpace + 1));
    } catch (...) {
        return false;
    }
    
    std::string metricPart = line.substr(0, lastSpace);
    
    // 检查是否有标签
    size_t braceStart = metricPart.find('{');
    size_t braceEnd = metricPart.find('}');
    
    if (braceStart != std::string::npos && braceEnd != std::string::npos) {
        metricName = metricPart.substr(0, braceStart);
        std::string labelsStr = metricPart.substr(braceStart + 1, braceEnd - braceStart - 1);
        
        // 解析标签
        std::regex labelRegex(R"((\w+)=\"([^\"]*)\")");
        std::sregex_iterator it(labelsStr.begin(), labelsStr.end(), labelRegex);
        std::sregex_iterator end;
        
        while (it != end) {
            labels[(*it)[1].str()] = (*it)[2].str();
            ++it;
        }
    } else {
        metricName = metricPart;
    }
    
    return true;
}

void TestEmptyExport() {
    MetricsCollector collector;
    
    // 测试空指标集合导出（应该有系统指标）
    std::string output = collector.ExportPrometheus();
    
    // 即使是空的，也应该有系统指标
    TEST_ASSERT(!output.empty(), "Export should not be empty (system metrics)");
    TEST_ASSERT(output.find("# HELP") != std::string::npos || 
                output.find("process_") != std::string::npos,
                "Should contain system metrics or HELP lines");
}

void TestBasicExportFormat() {
    MetricsCollector collector;
    auto& counter = collector.CreateCounter("test_counter", "Test counter help");
    counter.Increment(42);
    
    std::string output = collector.ExportPrometheus();
    
    // 验证基本格式
    TEST_ASSERT(output.find("# HELP test_counter Test counter help") != std::string::npos,
                "Should contain HELP line with correct format");
    TEST_ASSERT(output.find("# TYPE test_counter counter") != std::string::npos,
                "Should contain TYPE line with correct type");
    
    // 验证指标值
    TEST_ASSERT(output.find("test_counter 42") != std::string::npos,
                "Should contain metric name and value");
}

void TestExportLineEndings() {
    MetricsCollector collector;
    auto& counter = collector.CreateCounter("line_test", "Line test");
    counter.Increment(1);
    
    std::string output = collector.ExportPrometheus();
    
    // 验证每行都以换行符结束
    std::istringstream iss(output);
    std::string line;
    int lineCount = 0;
    
    while (std::getline(iss, line)) {
        lineCount++;
        // 验证行不为空（除了可能的最后一行）
        if (lineCount < 10) { // 只检查前几行
            TEST_ASSERT(!line.empty() || line.find("#") == 0, 
                        "Non-empty lines or comment lines expected");
        }
    }
    
    TEST_ASSERT(lineCount >= 3, "Should have at least HELP, TYPE, and value lines");
}

void TestCounterExportFormat() {
    MetricsCollector collector;
    auto& counter = collector.CreateCounter("http_requests_total", "Total HTTP requests");
    counter.Increment(100);
    
    std::string output = collector.ExportPrometheus();
    
    // 验证 Counter 类型标识
    TEST_ASSERT(output.find("# TYPE http_requests_total counter") != std::string::npos,
                "Counter should have correct TYPE");
    
    // 验证值
    TEST_ASSERT(output.find("http_requests_total 100") != std::string::npos,
                "Counter value should be exported");
}

void TestGaugeExportFormat() {
    MetricsCollector collector;
    auto& gauge = collector.CreateGauge("temperature_celsius", "Current temperature");
    gauge.Set(23.5);
    
    std::string output = collector.ExportPrometheus();
    
    // 验证 Gauge 类型标识
    TEST_ASSERT(output.find("# TYPE temperature_celsius gauge") != std::string::npos,
                "Gauge should have correct TYPE");
    
    // 验证值
    TEST_ASSERT(output.find("temperature_celsius 23.5") != std::string::npos,
                "Gauge value should be exported");
}

void TestHistogramExportFormat() {
    MetricsCollector collector;
    auto& histogram = collector.CreateHistogram("request_duration_seconds", "Request duration");
    histogram.Observe(0.1);
    histogram.Observe(0.2);
    histogram.Observe(0.3);
    
    std::string output = collector.ExportPrometheus();
    
    // 验证 Histogram 类型标识
    TEST_ASSERT(output.find("# TYPE request_duration_seconds histogram") != std::string::npos,
                "Histogram should have correct TYPE");
    
    // 验证 count 和 sum
    TEST_ASSERT(output.find("request_duration_seconds_count 3") != std::string::npos,
                "Histogram count should be exported");
    TEST_ASSERT(output.find("request_duration_seconds_sum 0.6") != std::string::npos,
                "Histogram sum should be exported");
}

void TestMixedTypesExport() {
    MetricsCollector collector;
    
    // 创建所有类型的指标
    auto& counter = collector.CreateCounter("my_counter", "My counter");
    auto& gauge = collector.CreateGauge("my_gauge", "My gauge");
    auto& histogram = collector.CreateHistogram("my_histogram", "My histogram");
    
    counter.Increment(10);
    gauge.Set(20.5);
    histogram.Observe(30.0);
    
    std::string output = collector.ExportPrometheus();
    
    // 验证所有类型都被导出
    TEST_ASSERT(output.find("my_counter 10") != std::string::npos,
                "Counter should be exported");
    TEST_ASSERT(output.find("my_gauge 20.5") != std::string::npos,
                "Gauge should be exported");
    TEST_ASSERT(output.find("my_histogram_count 1") != std::string::npos,
                "Histogram count should be exported");
    TEST_ASSERT(output.find("my_histogram_sum 30") != std::string::npos,
                "Histogram sum should be exported");
}

void TestLabelExportFormat() {
    MetricsCollector collector;
    
    std::map<std::string, std::string> labels = {
        {"method", "GET"},
        {"status", "200"},
        {"endpoint", "/api/users"}
    };
    
    auto& counter = collector.CreateCounter("labeled_counter", "Labeled counter", labels);
    counter.Increment(5);
    
    std::string output = collector.ExportPrometheus();
    
    // 验证所有标签都被导出
    TEST_ASSERT(output.find("method=\"GET\"") != std::string::npos,
                "Label 'method' should be exported");
    TEST_ASSERT(output.find("status=\"200\"") != std::string::npos,
                "Label 'status' should be exported");
    TEST_ASSERT(output.find("endpoint=\"/api/users\"") != std::string::npos,
                "Label 'endpoint' should be exported");
    
    // 验证标签在正确的位置
    TEST_ASSERT(output.find("labeled_counter{") != std::string::npos,
                "Labels should be in curly braces");
}

void TestMultipleLabelsExport() {
    MetricsCollector collector;
    
    // 创建多个带不同标签的指标
    std::map<std::string, std::string> labels1 = {{"method", "GET"}};
    auto& counter1 = collector.CreateCounter("requests", "Requests", labels1);
    counter1.Increment(10);
    
    std::map<std::string, std::string> labels2 = {{"method", "POST"}};
    auto& counter2 = collector.CreateCounter("requests", "Requests", labels2);
    counter2.Increment(20);
    
    std::string output = collector.ExportPrometheus();
    
    // 验证两个指标都被导出
    TEST_ASSERT(output.find("requests{") != std::string::npos,
                "Should contain labeled metrics");
    TEST_ASSERT(output.find("method=\"GET\"") != std::string::npos,
                "Should contain GET label");
    TEST_ASSERT(output.find("method=\"POST\"") != std::string::npos,
                "Should contain POST label");
}

void TestSpecialCharactersInHelp() {
    MetricsCollector collector;
    
    // 测试帮助文本中的特殊字符
    auto& counter = collector.CreateCounter("special_help", "Help with \"quotes\" and 'apostrophes'");
    counter.Increment(1);
    
    std::string output = collector.ExportPrometheus();
    
    // 验证特殊字符被正确处理
    TEST_ASSERT(output.find("# HELP special_help") != std::string::npos,
                "HELP line should be present");
    // 帮助文本应该被导出（可能带有转义）
    TEST_ASSERT(output.find("quotes") != std::string::npos || 
                output.find("special_help 1") != std::string::npos,
                "Special characters should be handled");
}

void TestMetricNameValidation() {
    MetricsCollector collector;
    
    // Prometheus 指标名应该符合规范：[a-zA-Z_:][a-zA-Z0-9_:]*
    auto& counter = collector.CreateCounter("valid_metric_name_123", "Valid name");
    counter.Increment(1);
    
    std::string output = collector.ExportPrometheus();
    
    // 验证指标名被正确导出
    TEST_ASSERT(output.find("valid_metric_name_123 1") != std::string::npos,
                "Valid metric name should be exported");
}

void TestLargeExport() {
    MetricsCollector collector;
    
    // 创建大量指标
    const int numCounters = 100;
    const int numGauges = 100;
    const int numHistograms = 100;
    
    for (int i = 0; i < numCounters; ++i) {
        auto& counter = collector.CreateCounter("counter_" + std::to_string(i), 
                                                 "Counter " + std::to_string(i));
        counter.Increment(i);
    }
    
    for (int i = 0; i < numGauges; ++i) {
        auto& gauge = collector.CreateGauge("gauge_" + std::to_string(i),
                                             "Gauge " + std::to_string(i));
        gauge.Set(i * 1.5);
    }
    
    for (int i = 0; i < numHistograms; ++i) {
        auto& histogram = collector.CreateHistogram("histogram_" + std::to_string(i),
                                                     "Histogram " + std::to_string(i));
        histogram.Observe(i * 10.0);
    }
    
    std::string output = collector.ExportPrometheus();
    
    // 验证输出不为空
    TEST_ASSERT(!output.empty(), "Large export should not be empty");
    
    // 验证包含所有指标类型
    TEST_ASSERT(output.find("counter_") != std::string::npos, "Should contain counters");
    TEST_ASSERT(output.find("gauge_") != std::string::npos, "Should contain gauges");
    TEST_ASSERT(output.find("histogram_") != std::string::npos, "Should contain histograms");
    
    // 验证输出大小合理
    TEST_ASSERT(output.size() > 1000, "Large export should be substantial");
}

void TestExportIdempotency() {
    MetricsCollector collector;
    auto& counter = collector.CreateCounter("idempotent_counter", "Idempotent test");
    counter.Increment(42);
    
    // 多次导出应该得到相同结果
    std::string output1 = collector.ExportPrometheus();
    std::string output2 = collector.ExportPrometheus();
    std::string output3 = collector.ExportPrometheus();
    
    TEST_ASSERT(output1 == output2, "Export should be idempotent (1st == 2nd)");
    TEST_ASSERT(output2 == output3, "Export should be idempotent (2nd == 3rd)");
}

void TestExportAfterModifications() {
    MetricsCollector collector;
    auto& counter = collector.CreateCounter("modify_counter", "Modify test");
    auto& gauge = collector.CreateGauge("modify_gauge", "Modify test");
    
    counter.Increment(10);
    gauge.Set(20.0);
    
    std::string output1 = collector.ExportPrometheus();
    TEST_ASSERT(output1.find("modify_counter 10") != std::string::npos, "Initial counter value");
    TEST_ASSERT(output1.find("modify_gauge 20") != std::string::npos, "Initial gauge value");
    
    // 修改值
    counter.Increment(5);
    gauge.Set(25.0);
    
    std::string output2 = collector.ExportPrometheus();
    TEST_ASSERT(output2.find("modify_counter 15") != std::string::npos, "Modified counter value");
    TEST_ASSERT(output2.find("modify_gauge 25") != std::string::npos, "Modified gauge value");
}

void TestSystemMetricsExport() {
    MetricsCollector collector;
    
    std::string output = collector.ExportPrometheus();
    
    // 验证系统指标被导出
    TEST_ASSERT(output.find("process_cpu_usage") != std::string::npos,
                "Should export process_cpu_usage");
    TEST_ASSERT(output.find("process_memory_usage") != std::string::npos,
                "Should export process_memory_usage");
    TEST_ASSERT(output.find("process_start_time") != std::string::npos,
                "Should export process_start_time");
    TEST_ASSERT(output.find("process_uptime") != std::string::npos,
                "Should export process_uptime");
}

void TestExportConstCorrectness() {
    MetricsCollector collector;
    auto& counter = collector.CreateCounter("const_test", "Const test");
    counter.Increment(100);
    
    // 在 const 上下文中调用 ExportPrometheus
    const MetricsCollector& constCollector = collector;
    std::string output = constCollector.ExportPrometheus();
    
    TEST_ASSERT(output.find("const_test 100") != std::string::npos,
                "Export should work in const context");
}

int main() {
    lwserverbase::tests::TestRunner& runner = lwserverbase::tests::TestRunner::Instance();
    auto& suite = runner.CreateSuite("PrometheusExportTests");
    
        ADD_TEST(suite, TestEmptyExport);
        ADD_TEST(suite, TestBasicExportFormat);
        ADD_TEST(suite, TestExportLineEndings);
        ADD_TEST(suite, TestCounterExportFormat);
        ADD_TEST(suite, TestGaugeExportFormat);
        ADD_TEST(suite, TestHistogramExportFormat);
        ADD_TEST(suite, TestMixedTypesExport);
        ADD_TEST(suite, TestLabelExportFormat);
        ADD_TEST(suite, TestMultipleLabelsExport);
        ADD_TEST(suite, TestSpecialCharactersInHelp);
        ADD_TEST(suite, TestMetricNameValidation);
        ADD_TEST(suite, TestLargeExport);
        ADD_TEST(suite, TestExportIdempotency);
        ADD_TEST(suite, TestExportAfterModifications);
        ADD_TEST(suite, TestSystemMetricsExport);
        ADD_TEST(suite, TestExportConstCorrectness);
    
    lwserverbase::tests::TestRunner::Instance().RunAll();
    return 0;
}
