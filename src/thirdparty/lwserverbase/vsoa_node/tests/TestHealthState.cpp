/*
 * TestHealthState.cpp
 *
 * VSOA Node Framework - HealthState 单元测试
 * 验证健康状态 JSON 序列化。
 */

#include <vsoa_node/HealthState.h>
#include <iostream>
#include <string>
#include <cassert>

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
    std::cout << "=== TestHealthState ===" << std::endl;

    // 测试默认健康状态
    {
        HealthState state;
        TEST("Default is healthy", state.isHealthy());
        TEST("Default status string", state.statusToString() == "healthy");
        TEST("Default checks empty", state.checkResults.empty());
    }

    // 测试 unhealthy 状态
    {
        HealthState state;
        state.overallStatus = HealthState::Status::UNHEALTHY;
        TEST("Unhealthy state", !state.isHealthy());
        TEST("Unhealthy status string", state.statusToString() == "unhealthy");
    }

    // 测试 degraded 状态
    {
        HealthState state;
        state.overallStatus = HealthState::Status::DEGRADED;
        TEST("Degraded state", !state.isHealthy());
        TEST("Degraded status string", state.statusToString() == "degraded");
    }

    // 测试 JSON 序列化（空 checks）
    {
        HealthState state;
        std::string json = state.toJson();

        TEST("JSON contains status", json.find("\"status\"") != std::string::npos);
        TEST("JSON contains healthy", json.find("\"healthy\"") != std::string::npos);
        TEST("JSON contains checks", json.find("\"checks\"") != std::string::npos);
    }

    // 测试 JSON 序列化（带 checks）
    {
        HealthState state;
        state.checkResults["memory"] = "ok";
        state.checkResults["connection"] = "ok";

        std::string json = state.toJson();

        TEST("JSON with checks contains memory", json.find("\"memory\"") != std::string::npos);
        TEST("JSON with checks contains connection", json.find("\"connection\"") != std::string::npos);
        TEST("JSON with checks contains ok", json.find("\"ok\"") != std::string::npos);
    }

    // 测试状态枚举值
    {
        TEST("HEALTHY value", static_cast<int>(HealthState::Status::HEALTHY) == 0);
        TEST("UNHEALTHY value", static_cast<int>(HealthState::Status::UNHEALTHY) == 1);
        TEST("DEGRADED value", static_cast<int>(HealthState::Status::DEGRADED) == 2);
    }

    // 测试辅助方法一致性
    {
        HealthState healthy;
        TEST("isHealthy true for HEALTHY", healthy.isHealthy());

        HealthState unhealthy;
        unhealthy.overallStatus = HealthState::Status::UNHEALTHY;
        TEST("isHealthy false for UNHEALTHY", !unhealthy.isHealthy());

        HealthState degraded;
        degraded.overallStatus = HealthState::Status::DEGRADED;
        TEST("isHealthy false for DEGRADED", !degraded.isHealthy());
    }

    std::cout << "\nResults: " << passCount << "/" << testCount << " passed" << std::endl;
    return (passCount == testCount) ? 0 : 1;
}
