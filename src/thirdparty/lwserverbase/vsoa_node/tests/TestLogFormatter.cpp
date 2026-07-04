/*
 * TestLogFormatter.cpp
 *
 * VSOA Node Framework - LogFormatter 单元测试
 * 验证日志格式化输出格式。
 */

#include <vsoa_node/LogFormatter.h>
#include <logging/Logger.h>
#include <iostream>
#include <string>
#include <cassert>
#include <thread>
#include <chrono>

using namespace lwserverbase::vsoa_node;
using namespace lwserverbase::logging;

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
    std::cout << "=== TestLogFormatter ===" << std::endl;

    // 测试 getCurrentTimestamp 格式
    {
        std::string ts = LogFormatter::getCurrentTimestamp();
        // 格式应为 YYYY-MM-DD HH:MM:SS.mmm（无括号，括号在 formatPrefix 中添加）
        // 检查是否包含日期和时间分隔符
        TEST("Timestamp contains date-time separator", ts.find(' ') != std::string::npos);
        TEST("Timestamp contains dot for ms", ts.find('.') != std::string::npos);
        TEST("Timestamp non-empty", !ts.empty());
        TEST("Timestamp starts with digit", !ts.empty() && isdigit(ts.front()));
    }

    // 测试 getThreadId
    {
        std::string tid = LogFormatter::getThreadId();
        TEST("Thread ID is non-empty", !tid.empty());
        // 多次调用应返回相同值（同一个线程）
        std::string tid2 = LogFormatter::getThreadId();
        TEST("Thread ID is consistent", tid == tid2);
    }

    // 测试 formatPrefix
    {
        Logger logger;
        LogFormatter formatter(&logger, "TestService");
        std::string prefix = formatter.formatPrefix("INFO");

        TEST("Prefix contains [", prefix.find('[') != std::string::npos);
        TEST("Prefix contains INFO", prefix.find("INFO") != std::string::npos);
        TEST("Prefix contains TestService", prefix.find("TestService") != std::string::npos);
        TEST("Prefix contains thread-", prefix.find("thread-") != std::string::npos);
        TEST("Prefix ends with space", !prefix.empty() && prefix.back() == ' ');
    }

    // 测试不同日志级别
    {
        Logger logger;
        LogFormatter formatter(&logger, "TestService");

        std::string debugPrefix = formatter.formatPrefix("DEBUG");
        std::string infoPrefix = formatter.formatPrefix("INFO");
        std::string warnPrefix = formatter.formatPrefix("WARN");
        std::string errorPrefix = formatter.formatPrefix("ERROR");
        std::string criticalPrefix = formatter.formatPrefix("CRITICAL");

        TEST("DEBUG prefix works", debugPrefix.find("DEBUG") != std::string::npos);
        TEST("INFO prefix works", infoPrefix.find("INFO") != std::string::npos);
        TEST("WARN prefix works", warnPrefix.find("WARN") != std::string::npos);
        TEST("ERROR prefix works", errorPrefix.find("ERROR") != std::string::npos);
        TEST("CRITICAL prefix works", criticalPrefix.find("CRITICAL") != std::string::npos);
    }

    // 测试 setServiceName
    {
        Logger logger;
        LogFormatter formatter(&logger, "OldName");

        TEST("Initial name", formatter.getServiceName() == "OldName");

        formatter.setServiceName("NewName");
        TEST("Changed name", formatter.getServiceName() == "NewName");

        std::string prefix = formatter.formatPrefix("INFO");
        TEST("Prefix uses new name", prefix.find("NewName") != std::string::npos);
    }

    std::cout << "\nResults: " << passCount << "/" << testCount << " passed" << std::endl;
    return (passCount == testCount) ? 0 : 1;
}
