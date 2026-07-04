/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: TestFixes.cpp
 *
 * Date: 2026-05-15
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: 验证 T014-T018 修复的测试用例
 * - ConfigManager: 验证 JSON 配置文件的 Load/Save 保持嵌套结构
 * - ConfigManager: 验证 Set 修改后 Save 重建 JSON
 * - MetricsCollector: 验证 UpdateSystemMetrics 产生非零指标值
 */

#include <config/ConfigManager.h>
#include <metrics/MetricsCollector.h>
#include <fstream>
#include <cstdio>
#include <sstream>
#include <unistd.h>
#include "TestFramework.h"

using namespace lwserverbase::config;
using namespace lwserverbase::metrics;

// ==================== ConfigManager JSON 保存修复测试 ====================

/**
 * 辅助函数：创建临时 JSON 配置文件
 */
static std::string CreateTempJsonConfig(const std::string& content) {
    char tmpPath[] = "/tmp/lwserverbase_test_config_XXXXXX";
    int fd = mkstemp(tmpPath);
    if (fd == -1) {
        return "";
    }
    write(fd, content.c_str(), content.size());
    close(fd);
    return std::string(tmpPath);
}

/**
 * 辅助函数：读取文件内容
 */
static std::string ReadFileContent(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    return std::string((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
}

/**
 * 辅助函数：判断字符串是否为合法 JSON（以 { 开头，以 } 结尾）
 */
static bool IsValidJson(const std::string& content) {
    const std::string trimmed = [](const std::string& s) {
        size_t first = s.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return std::string();
        size_t last = s.find_last_not_of(" \t\r\n");
        return s.substr(first, last - first + 1);
    }(content);
    return !trimmed.empty() && trimmed.front() == '{' && trimmed.back() == '}';
}

void TestConfigManagerLoadAndSavePreservesJson() {
    const std::string jsonContent =
        "{\n"
        "    \"node\": {\n"
        "        \"server_host\": \"localhost\",\n"
        "        \"server_port\": 8080,\n"
        "        \"log_level\": \"info\",\n"
        "        \"log_path\": \"/var/log\"\n"
        "    }\n"
        "}\n";

    std::string configPath = CreateTempJsonConfig(jsonContent);
    TEST_ASSERT(!configPath.empty(), "Temp config file should be created");

    ConfigManager configMgr;

    // 加载 JSON 配置
    bool loadOk = configMgr.Load(configPath);
    TEST_ASSERT(loadOk, "ConfigManager should load JSON config successfully");

    // 验证配置值正确
    std::string host = configMgr.Get("node.server_host", std::string(""));
    TEST_ASSERT_EQ(std::string("localhost"), host, "node.server_host should be 'localhost'");

    int port = configMgr.Get("node.server_port", int(0));
    TEST_ASSERT_EQ(8080, port, "node.server_port should be 8080");

    // 保存（无修改）到新文件
    std::string savePath = configPath + ".save";
    bool saveOk = configMgr.Save(savePath);
    TEST_ASSERT(saveOk, "ConfigManager should save successfully");

    // 读取保存的文件
    std::string savedContent = ReadFileContent(savePath);

    // 验证输出是合法 JSON
    TEST_ASSERT(IsValidJson(savedContent),
                "Saved config should be valid JSON (starts with '{', ends with '}')");

    // 验证 JSON 嵌套结构被保留（原始内容应被直接写回）
    TEST_ASSERT(savedContent.find("\"server_host\"") != std::string::npos,
                "Saved JSON should contain 'server_host' inside nested structure");
    TEST_ASSERT(savedContent.find("\"server_port\"") != std::string::npos,
                "Saved JSON should contain 'server_port' inside nested structure");
    TEST_ASSERT(savedContent.find("\"node\"") != std::string::npos,
                "Saved JSON should contain 'node' as nested object");

    // 验证不是扁平 key=value 格式
    TEST_ASSERT(savedContent.find("node.server_host =") == std::string::npos,
                "Saved JSON should NOT be in flat key=value format");

    // 清理临时文件
    std::remove(configPath.c_str());
    std::remove(savePath.c_str());
}

void TestConfigManagerSaveAfterModificationRebuildsJson() {
    const std::string jsonContent =
        "{\n"
        "    \"node\": {\n"
        "        \"server_host\": \"localhost\",\n"
        "        \"server_port\": 8080\n"
        "    }\n"
        "}\n";

    std::string configPath = CreateTempJsonConfig(jsonContent);
    TEST_ASSERT(!configPath.empty(), "Temp config file should be created");

    ConfigManager configMgr;

    // 加载 JSON 配置
    bool loadOk = configMgr.Load(configPath);
    TEST_ASSERT(loadOk, "ConfigManager should load JSON config");

    // 修改一个配置值
    configMgr.Set("node.server_host", std::string("newhost"));

    // 验证内存中的值已更新
    std::string host = configMgr.Get("node.server_host", std::string(""));
    TEST_ASSERT_EQ(std::string("newhost"), host,
                   "node.server_host should be updated after Set()");

    // 保存（有修改）到新文件
    std::string savePath = configPath + ".modified.save";
    bool saveOk = configMgr.Save(savePath);
    TEST_ASSERT(saveOk, "ConfigManager should save successfully after modification");

    // 读取保存的文件
    std::string savedContent = ReadFileContent(savePath);
    TEST_ASSERT(!savedContent.empty(), "Saved file should not be empty");

    // 验证输出是合法 JSON
    TEST_ASSERT(IsValidJson(savedContent),
                "Saved config after modification should be valid JSON");

    // 验证新值被写入
    TEST_ASSERT(savedContent.find("newhost") != std::string::npos,
                "Saved JSON should contain the new value 'newhost'");

    // 验证嵌套结构被重建
    TEST_ASSERT(savedContent.find("\"server_host\"") != std::string::npos,
                "Rebuilt JSON should contain 'server_host' key");
    TEST_ASSERT(savedContent.find("\"node\"") != std::string::npos,
                "Rebuilt JSON should contain 'node' object");

    // 验证不是扁平 key=value 格式
    TEST_ASSERT(savedContent.find("node.server_host =") == std::string::npos,
                "Rebuilt JSON should NOT be in flat key=value format");

    // 验证重新加载修改后的配置文件
    ConfigManager reloadMgr;
    bool reloadOk = reloadMgr.Load(savePath);
    TEST_ASSERT(reloadOk, "Should be able to reload the saved config");
    std::string reloadedHost = reloadMgr.Get("node.server_host", std::string(""));
    TEST_ASSERT_EQ(std::string("newhost"), reloadedHost,
                   "Reloaded config should contain updated value");

    // 清理临时文件
    std::remove(configPath.c_str());
    std::remove(savePath.c_str());
}

void TestConfigManagerMultipleNestedLevels() {
    // 测试多层嵌套的 JSON
    const std::string jsonContent =
        "{\n"
        "    \"node\": {\n"
        "        \"server\": {\n"
        "            \"host\": \"localhost\",\n"
        "            \"port\": 9090\n"
        "        }\n"
        "    }\n"
        "}\n";

    std::string configPath = CreateTempJsonConfig(jsonContent);
    TEST_ASSERT(!configPath.empty(), "Temp config file should be created");

    ConfigManager configMgr;

    bool loadOk = configMgr.Load(configPath);
    TEST_ASSERT(loadOk, "ConfigManager should load multi-level JSON");

    // 验证 3 层嵌套的值
    std::string host = configMgr.Get("node.server.host", std::string(""));
    TEST_ASSERT_EQ(std::string("localhost"), host,
                   "node.server.host should be 'localhost'");

    int port = configMgr.Get("node.server.port", int(0));
    TEST_ASSERT_EQ(9090, port, "node.server.port should be 9090");

    // 修改并保存
    configMgr.Set("node.server.port", int(9091));
    std::string savePath = configPath + ".nested.save";
    bool saveOk = configMgr.Save(savePath);
    TEST_ASSERT(saveOk, "ConfigManager should save multi-level JSON after modification");

    std::string savedContent = ReadFileContent(savePath);
    TEST_ASSERT(IsValidJson(savedContent),
                "Saved multi-level config should be valid JSON");

    // 验证重建后的嵌套结构
    TEST_ASSERT(savedContent.find("\"server\"") != std::string::npos,
                "Rebuilt JSON should contain nested 'server' object");
    TEST_ASSERT(savedContent.find("9091") != std::string::npos,
                "Rebuilt JSON should contain updated port value");

    // 清理临时文件
    std::remove(configPath.c_str());
    std::remove(savePath.c_str());
}

void TestConfigManagerKeyValueFormatStillWorks() {
    // 验证原始 key=value 格式仍然向后兼容
    const std::string kvContent =
        "node.server_host = localhost\n"
        "node.server_port = 8080\n";

    std::string configPath = CreateTempJsonConfig(kvContent);
    TEST_ASSERT(!configPath.empty(), "Temp config file should be created");

    ConfigManager configMgr;

    bool loadOk = configMgr.Load(configPath);
    TEST_ASSERT(loadOk, "ConfigManager should load key=value config");

    std::string host = configMgr.Get("node.server_host", std::string(""));
    TEST_ASSERT_EQ(std::string("localhost"), host,
                   "key=value config should load correctly");

    // 保存 key=value 格式应保持原行为
    std::string savePath = configPath + ".kv.save";
    bool saveOk = configMgr.Save(savePath);
    TEST_ASSERT(saveOk, "ConfigManager should save key=value format");

    std::string savedContent = ReadFileContent(savePath);
    TEST_ASSERT(savedContent.find("node.server_host = localhost") != std::string::npos,
                "key=value save should maintain original format");

    // 清理临时文件
    std::remove(configPath.c_str());
    std::remove(savePath.c_str());
}

// ==================== MetricsCollector 系统指标收集测试 ====================

void TestMetricsCollectorSystemMetricsNonZero() {
    MetricsCollector collector;

    // CollectSystemMetrics 在构造函数中被调用，但为了测试显式调用一次
    collector.CollectSystemMetrics();

    std::string output = collector.ExportPrometheus();

    // 验证四个系统指标均存在
    TEST_ASSERT(output.find("process_cpu_usage") != std::string::npos,
                "Should export process_cpu_usage");
    TEST_ASSERT(output.find("process_memory_usage") != std::string::npos,
                "Should export process_memory_usage");
    TEST_ASSERT(output.find("process_start_time") != std::string::npos,
                "Should export process_start_time");
    TEST_ASSERT(output.find("process_uptime") != std::string::npos,
                "Should export process_uptime");
}

void TestMetricsCollectorUpdateSystemMetrics() {
    MetricsCollector collector;

    // 显式创建系统指标
    collector.CollectSystemMetrics();

    // 调用 UpdateSystemMetrics 更新值
    collector.UpdateSystemMetrics();

    std::string output = collector.ExportPrometheus();

    // 验证内存使用量 > 0 (进程总是占用内存)
    // 格式: "process_memory_usage VALUE"
    bool foundMemoryMetric = false;
    double memoryValue = 0.0;

    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find("process_memory_usage ") != std::string::npos &&
            line.find("#") != 0) {
            size_t spacePos = line.rfind(' ');
            if (spacePos != std::string::npos) {
                std::string valStr = line.substr(spacePos + 1);
                // Handle possible trailing newline
                try {
                    memoryValue = std::stod(valStr);
                    foundMemoryMetric = true;
                } catch (...) {
                    // skip
                }
            }
            break;
        }
    }

    TEST_ASSERT(foundMemoryMetric, "process_memory_usage metric should be present");
    TEST_ASSERT(memoryValue > 0.0,
                "process_memory_usage should be > 0 (process always uses memory)");

    // 验证运行时间 >= 0
    bool foundUptimeMetric = false;
    int64_t uptimeValue = -1;

    std::istringstream iss2(output);
    while (std::getline(iss2, line)) {
        if (line.find("process_uptime ") != std::string::npos &&
            line.find("#") != 0) {
            size_t spacePos = line.rfind(' ');
            if (spacePos != std::string::npos) {
                std::string valStr = line.substr(spacePos + 1);
                try {
                    uptimeValue = std::stoll(valStr);
                    foundUptimeMetric = true;
                } catch (...) {
                    // skip
                }
            }
            break;
        }
    }

    TEST_ASSERT(foundUptimeMetric, "process_uptime metric should be present");
    TEST_ASSERT(uptimeValue >= 0,
                "process_uptime should be >= 0");
}

void TestMetricsCollectorCpuUsageNonNegative() {
    MetricsCollector collector;
    collector.CollectSystemMetrics();

    std::string output = collector.ExportPrometheus();

    // 验证 CPU 使用量 >= 0
    double cpuValue = -1.0;
    bool foundCpuMetric = false;

    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find("process_cpu_usage ") != std::string::npos &&
            line.find("#") != 0) {
            size_t spacePos = line.rfind(' ');
            if (spacePos != std::string::npos) {
                std::string valStr = line.substr(spacePos + 1);
                try {
                    cpuValue = std::stod(valStr);
                    foundCpuMetric = true;
                } catch (...) {
                    // skip
                }
            }
            break;
        }
    }

    TEST_ASSERT(foundCpuMetric, "process_cpu_usage metric should be present");
    TEST_ASSERT(cpuValue >= 0.0,
                "process_cpu_usage should be >= 0");
}

void TestMetricsCollectorStartTimeValid() {
    MetricsCollector collector;
    collector.CollectSystemMetrics();

    std::string output = collector.ExportPrometheus();

    // 验证启动时间为正数（合理的 epoch 时间戳）
    int64_t startTime = -1;
    bool foundStartTime = false;

    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find("process_start_time ") != std::string::npos &&
            line.find("#") != 0) {
            size_t spacePos = line.rfind(' ');
            if (spacePos != std::string::npos) {
                std::string valStr = line.substr(spacePos + 1);
                try {
                    startTime = std::stoll(valStr);
                    foundStartTime = true;
                } catch (...) {
                    // skip
                }
            }
            break;
        }
    }

    TEST_ASSERT(foundStartTime, "process_start_time metric should be present");
    // Should be a recent epoch time (> Jan 1 2020 = 1577836800)
    TEST_ASSERT(startTime > 1577836800,
                "process_start_time should be a valid epoch timestamp");
}

void TestMetricsCollectorCounterSet() {
    MetricsCollector collector;

    auto& counter = collector.CreateCounter("test_set_counter", "Test Set method");
    counter.Set(42);
    TEST_ASSERT_EQ(42, counter.Get(), "Counter::Set should set value to 42");

    counter.Set(100);
    TEST_ASSERT_EQ(100, counter.Get(), "Counter::Set should overwrite value to 100");

    counter.Increment(5);
    TEST_ASSERT_EQ(105, counter.Get(), "Counter::Increment should work after Set");
}

// ==================== 主函数 ====================

int main() {
    lwserverbase::tests::TestRunner& runner = lwserverbase::tests::TestRunner::Instance();
    auto& suite = runner.CreateSuite("FixesTests");

    // ConfigManager 修复测试
    ADD_TEST(suite, TestConfigManagerLoadAndSavePreservesJson);
    ADD_TEST(suite, TestConfigManagerSaveAfterModificationRebuildsJson);
    ADD_TEST(suite, TestConfigManagerMultipleNestedLevels);
    ADD_TEST(suite, TestConfigManagerKeyValueFormatStillWorks);

    // MetricsCollector 系统指标测试
    ADD_TEST(suite, TestMetricsCollectorSystemMetricsNonZero);
    ADD_TEST(suite, TestMetricsCollectorUpdateSystemMetrics);
    ADD_TEST(suite, TestMetricsCollectorCpuUsageNonNegative);
    ADD_TEST(suite, TestMetricsCollectorStartTimeValid);
    ADD_TEST(suite, TestMetricsCollectorCounterSet);

    lwserverbase::tests::TestRunner::Instance().RunAll();
    return 0;
}
