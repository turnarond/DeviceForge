/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: TestFramework.h
 *
 * Date: 2026-03-10
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <sstream>
#include <exception>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace lwserverbase {
namespace tests {

// 测试函数类型
using TestFunction = std::function<void()>;

/**
 * @brief 简单的测试断言宏
 */
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "  ❌ ASSERTION FAILED: " << message << std::endl; \
            throw std::runtime_error(message); \
        } \
    } while(0)

#define TEST_ASSERT_EQ(expected, actual, message) \
    do { \
        if ((expected) != (actual)) { \
            std::cerr << "  ❌ ASSERTION FAILED: " << message \
                      << " (expected: " << (expected) << ", actual: " << (actual) << ")" << std::endl; \
            throw std::runtime_error(message); \
        } \
    } while(0)

#define TEST_ASSERT_NEAR(expected, actual, epsilon, message) \
    do { \
        if (std::abs((expected) - (actual)) > (epsilon)) { \
            std::cerr << "  ❌ ASSERTION FAILED: " << message \
                      << " (expected: " << (expected) << ", actual: " << (actual) << ")" << std::endl; \
            throw std::runtime_error(message); \
        } \
    } while(0)

/**
 * @class TestCase
 * @brief 单个测试用例
 */
class TestCase {
public:
    TestCase(const std::string& name, TestFunction func)
        : m_name(name)
        , m_func(func)
        , m_passed(false)
        , m_duration(0)
    {}
    
    void Run() {
        auto start = std::chrono::high_resolution_clock::now();
        try {
            m_func();
            m_passed = true;
        } catch (const std::exception& e) {
            m_errorMessage = e.what();
            m_passed = false;
        }
        auto end = std::chrono::high_resolution_clock::now();
        m_duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    }
    
    const std::string& GetName() const { return m_name; }
    bool IsPassed() const { return m_passed; }
    const std::string& GetErrorMessage() const { return m_errorMessage; }
    long long GetDuration() const { return m_duration; }
    
private:
    std::string m_name;
    TestFunction m_func;
    bool m_passed;
    std::string m_errorMessage;
    long long m_duration;
};

/**
 * @class TestSuite
 * @brief 测试套件
 */
class TestSuite {
public:
    TestSuite(const std::string& name)
        : m_name(name)
    {}
    
    // 重载版本 1：接受名称和函数
    void AddTest(const std::string& name, TestFunction func) {
        m_tests.emplace_back(name, func);
    }
    
    // 重载版本 2：只接受函数指针，自动推导名称
    void AddTest(void (*func)()) {
        // 这里无法从函数指针获取名称，需要调用者提供
        // 这个版本不会被使用
    }
    
    void Run() {
        std::cout << "\n========================================" << std::endl;
        std::cout << "Running Test Suite: " << m_name << std::endl;
        std::cout << "========================================" << std::endl;
        
        int passed = 0;
        int failed = 0;
        
        for (auto& test : m_tests) {
            std::cout << "  Running: " << test.GetName() << "... ";
            std::cout.flush();
            
            test.Run();
            
            if (test.IsPassed()) {
                std::cout << "✅ PASSED (" << test.GetDuration() << " μs)" << std::endl;
                passed++;
            } else {
                std::cout << "❌ FAILED (" << test.GetDuration() << " μs)" << std::endl;
                std::cerr << "    Error: " << test.GetErrorMessage() << std::endl;
                failed++;
            }
        }
        
        std::cout << "\n----------------------------------------" << std::endl;
        std::cout << "Test Summary: " << passed << " passed, " << failed << " failed" << std::endl;
        std::cout << "----------------------------------------" << std::endl;
        
        m_passedCount = passed;
        m_failedCount = failed;
    }
    
    int GetPassedCount() const { return m_passedCount; }
    int GetFailedCount() const { return m_failedCount; }
    
private:
    std::string m_name;
    std::vector<TestCase> m_tests;
    int m_passedCount = 0;
    int m_failedCount = 0;
};

/**
 * @class TestRunner
 * @brief 测试运行器
 */
class TestRunner {
public:
    static TestRunner& Instance() {
        static TestRunner instance;
        return instance;
    }
    
    TestSuite& CreateSuite(const std::string& name) {
        m_suites.emplace_back(name);
        return m_suites.back();
    }
    
    void RunAll() {
        auto start = std::chrono::high_resolution_clock::now();
        
        std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
        std::cout << "║     lwserverbase Test Framework        ║" << std::endl;
        std::cout << "╚════════════════════════════════════════╝" << std::endl;
        
        int totalPassed = 0;
        int totalFailed = 0;
        
        for (auto& suite : m_suites) {
            suite.Run();
            totalPassed += suite.GetPassedCount();
            totalFailed += suite.GetFailedCount();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        
        std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
        std::cout << "║           FINAL SUMMARY                ║" << std::endl;
        std::cout << "╠════════════════════════════════════════╣" << std::endl;
        std::cout << "║ Total Passed: " << totalPassed << std::endl;
        std::cout << "║ Total Failed: " << totalFailed << std::endl;
        std::cout << "║ Total Time: " << duration << " ms" << std::endl;
        std::cout << "╚════════════════════════════════════════╝" << std::endl;
        
        if (totalFailed > 0) {
            exit(1);
        }
    }
    
private:
    TestRunner() = default;
    std::vector<TestSuite> m_suites;
};

} // namespace tests
} // namespace lwserverbase

// 宏定义：添加测试用例（需要同时传入名称和函数）
#define ADD_TEST(suite, name) \
    suite.AddTest(#name, name)
