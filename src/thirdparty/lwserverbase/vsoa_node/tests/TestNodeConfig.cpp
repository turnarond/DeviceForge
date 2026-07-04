/*
 * TestNodeConfig.cpp
 *
 * VSOA Node Framework - NodeConfig 单元测试
 * 验证 NodeConfig 的默认值和字段访问。
 */

#include <vsoa_node/NodeConfig.h>
#include <iostream>
#include <cassert>
#include <string>

using namespace lwserverbase::vsoa_node;

static int testCount = 0;
static int passCount = 0;

#define TEST(name, expr) do { \
    testCount++; \
    if (!(expr)) { \
        std::cerr << "FAIL: " << name << " (" << #expr << ")" << std::endl; \
    } else { \
        passCount++; \
        std::cout << "PASS: " << name << std::endl; \
    } \
} while(0)

int main() {
    std::cout << "=== TestNodeConfig ===" << std::endl;

    // 测试默认值
    NodeConfig config;

    TEST("Default serviceName", config.serviceName == "UnknownNode");
    TEST("Default version", config.version == "1.0.0");
    TEST("Default description", config.description.empty());
    TEST("Default serverHost", config.serverHost == "0.0.0.0");
    TEST("Default serverPort", config.serverPort == 0);
    TEST("Default serverPassword", config.serverPassword.empty());
    TEST("Default configPath", config.configPath == "config.json");
    TEST("Default daemonMode", config.daemonMode == false);
    TEST("Default shutdownTimeoutMs", config.shutdownTimeoutMs == 5000);
    TEST("Default vsoaSpinIntervalMs", config.vsoaSpinIntervalMs == 50);
    TEST("Default encoding", config.encoding == "json");

    // 测试字段修改
    config.serviceName = "TestService";
    config.version = "2.0.0";
    config.serverPort = 8080;
    config.daemonMode = true;

    TEST("Modified serviceName", config.serviceName == "TestService");
    TEST("Modified version", config.version == "2.0.0");
    TEST("Modified serverPort", config.serverPort == 8080);
    TEST("Modified daemonMode", config.daemonMode == true);

    std::cout << "\nResults: " << passCount << "/" << testCount << " passed" << std::endl;
    return (passCount == testCount) ? 0 : 1;
}
