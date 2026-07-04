/*
 * Copyright (c) 2025 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: integration_test.cpp
 *
 * Date: 2026-04-18
 *
 * Author: Test Suite
 *
 */

#include "../lwlog.h"
#include "../src/file_appender.h"
#include "../src/console_appender.h"
#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

// 测试完整的日志系统集成
TEST(IntegrationTest, FullSystemIntegration) {
    // 获取默认日志器
    auto logger = LogManager::get_logger("integration_test");
    
    // 测试不同级别的日志
    EXPECT_NO_THROW(LWLOG_DEBUG(logger, "Debug message"));
    EXPECT_NO_THROW(LWLOG_INFO(logger, "Info message"));
    EXPECT_NO_THROW(LWLOG_WARN(logger, "Warn message"));
    EXPECT_NO_THROW(LWLOG_ERROR(logger, "Error message"));
    EXPECT_NO_THROW(LWLOG_CRITICAL(logger, "Critical message"));
    
    // 验证系统没有崩溃
    SUCCEED();
}

// 测试多输出器集成
TEST(IntegrationTest, MultipleAppenders) {
    auto logger = LogManager::get_logger("multiple_appenders_test");
    
    // 添加文件输出器
    auto file_appender = std::make_shared<FileAppender>("multi_appender_test.log");
    logger->add_appender(file_appender);
    
    // 添加控制台输出器
    auto console_appender = std::make_shared<ConsoleAppender>();
    logger->add_appender(console_appender);
    
    // 测试日志输出
    EXPECT_NO_THROW(LWLOG_INFO(logger, "Test message with multiple appenders"));
    
    // 验证文件是否创建
    std::ifstream file("multi_appender_test.log");
    EXPECT_TRUE(file.is_open());
    file.close();
    
    // 清理测试文件
    remove("multi_appender_test.log");
}

// 测试日志选项全局设置
TEST(IntegrationTest, GlobalOptions) {
    // 设置全局默认选项
    LogOptions global_options;
    global_options.enable_color = false;
    global_options.time_format = "%H:%M:%S";
    global_options.max_file_size = 5 * 1024 * 1024; // 5MB
    global_options.max_file_count = 5;
    
    LogManager::set_default_options(global_options);
    
    // 获取日志器，应该继承全局选项
    auto logger = LogManager::get_logger("global_options_test");
    
    // 测试日志输出
    EXPECT_NO_THROW(LWLOG_INFO(logger, "Test with global options"));
}

// 测试日志系统关闭
TEST(IntegrationTest, Shutdown) {
    auto logger = LogManager::get_logger("shutdown_test");
    
    // 测试日志输出
    EXPECT_NO_THROW(LWLOG_INFO(logger, "Test before shutdown"));
    
    // 关闭日志系统
    EXPECT_NO_THROW(LogManager::shutdown());
    
    // 验证系统可以重新初始化
    auto new_logger = LogManager::get_logger("new_after_shutdown");
    EXPECT_NO_THROW(LWLOG_INFO(new_logger, "Test after shutdown"));
}

// 测试日志级别过滤
TEST(IntegrationTest, LevelFiltering) {
    auto logger = LogManager::get_logger("level_filter_test");
    
    // 设置为ERROR级别，应该只输出ERROR和CRITICAL
    logger->set_level(LogLevel::ERROR);
    
    // 测试不同级别的日志
    EXPECT_NO_THROW(LWLOG_DEBUG(logger, "Debug message (should not appear)"));
    EXPECT_NO_THROW(LWLOG_INFO(logger, "Info message (should not appear)"));
    EXPECT_NO_THROW(LWLOG_WARN(logger, "Warn message (should not appear)"));
    EXPECT_NO_THROW(LWLOG_ERROR(logger, "Error message (should appear)"));
    EXPECT_NO_THROW(LWLOG_CRITICAL(logger, "Critical message (should appear)"));
    
    // 验证系统没有崩溃
    SUCCEED();
}

// 测试并发性能
TEST(IntegrationTest, ConcurrencyPerformance) {
    auto logger = LogManager::get_logger("concurrency_perf_test");
    
    const int thread_count = 20;
    const int logs_per_thread = 1000;
    
    std::vector<std::thread> threads;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back([logger, i, logs_per_thread]() {
            for (int j = 0; j < logs_per_thread; ++j) {
                std::string message = "Performance test - Thread " + std::to_string(i) + ", log " + std::to_string(j);
                LWLOG_INFO(logger, message);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    
    // 验证性能（20线程 * 1000日志 < 5秒）
    EXPECT_LT(duration, 5000);
    
    std::cout << "Concurrency test completed in " << duration << " ms" << std::endl;
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
