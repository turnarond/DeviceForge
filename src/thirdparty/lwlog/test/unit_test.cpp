/*
 * Copyright (c) 2025 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: unit_test.cpp
 *
 * Date: 2026-04-18
 *
 * Author: Test Suite
 *
 */

#include "../lwlog.h"
#include "../src/logger_impl.h"
#include "../src/file_appender.h"
#include "../src/console_appender.h"
#include "../src/pattern_formatter.h"
#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

// 测试日志级别控制
TEST(LoggerTest, LogLevelControl) {
    auto logger = LogManager::get_logger("test_logger");
    
    // 设置为INFO级别，应该只输出INFO及以上级别
    logger->set_level(LogLevel::INFO);
    
    // 验证级别设置
    EXPECT_EQ(logger->get_level(), LogLevel::INFO);
    
    // 注意：这里我们无法直接验证日志是否输出，因为日志输出是异步的
    // 但我们可以验证级别设置是否正确
}

// 测试日志格式化
TEST(FormatterTest, PatternFormatting) {
    PatternFormatter formatter("%d [%l] %m");
    
    LogEntry entry;
    entry.level = LogLevel::INFO;
    entry.message = "Test message";
    entry.file = __FILE__;
    entry.line = __LINE__;
    entry.function = __FUNCTION__;
    entry.timestamp = std::chrono::system_clock::now();
    entry.thread_id = std::this_thread::get_id();
    
    std::string formatted = formatter.format(entry);
    EXPECT_FALSE(formatted.empty());
    EXPECT_TRUE(formatted.find("Test message") != std::string::npos);
    EXPECT_TRUE(formatted.find("INFO") != std::string::npos);
}

// 测试文件输出
TEST(FileAppenderTest, FileOutput) {
    std::string test_file = "test_file.log";
    
    // 清理可能存在的旧文件
    remove(test_file.c_str());
    
    { // 作用域确保FileAppender被正确销毁
        FileAppender appender(test_file);
        
        LogEntry entry;
        entry.level = LogLevel::INFO;
        entry.message = "File appender test";
        entry.file = __FILE__;
        entry.line = __LINE__;
        entry.function = __FUNCTION__;
        entry.timestamp = std::chrono::system_clock::now();
        entry.thread_id = std::this_thread::get_id();
        
        appender.append(entry);
    }
    
    // 检查文件是否创建并包含内容
    std::ifstream file(test_file);
    EXPECT_TRUE(file.is_open());
    
    std::string content;
    std::getline(file, content);
    EXPECT_FALSE(content.empty());
    EXPECT_TRUE(content.find("File appender test") != std::string::npos);
    
    file.close();
    
    // 清理测试文件
    remove(test_file.c_str());
}

// 测试控制台输出（这里只是测试功能是否正常，不验证输出内容）
TEST(ConsoleAppenderTest, ConsoleOutput) {
    ConsoleAppender appender;
    
    LogEntry entry;
    entry.level = LogLevel::INFO;
    entry.message = "Console appender test";
    entry.file = __FILE__;
    entry.line = __LINE__;
    entry.function = __FUNCTION__;
    entry.timestamp = std::chrono::system_clock::now();
    entry.thread_id = std::this_thread::get_id();
    
    // 这里只是验证函数调用不会崩溃
    EXPECT_NO_THROW(appender.append(entry));
}

// 测试多线程并发
TEST(LoggerTest, MultiThreadConcurrency) {
    auto logger = LogManager::get_logger("multi_thread_test");
    
    const int thread_count = 10;
    const int logs_per_thread = 100;
    
    std::vector<std::thread> threads;
    
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back([logger, i, logs_per_thread]() {
            for (int j = 0; j < logs_per_thread; ++j) {
                std::string message = "Thread " + std::to_string(i) + ", log " + std::to_string(j);
                LWLOG_INFO(logger, message);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    // 验证没有崩溃
    SUCCEED();
}

// 测试日志选项设置
TEST(LoggerTest, LogOptions) {
    auto logger = LogManager::get_logger("options_test");
    
    LogOptions options;
    options.enable_color = false;
    options.time_format = "%H:%M:%S";
    options.max_file_size = 1024 * 1024; // 1MB
    options.max_file_count = 3;
    
    logger->set_options(options);
    
    // 验证日志可以正常输出
    EXPECT_NO_THROW(LWLOG_INFO(logger, "Test with custom options"));
}

// 测试旧版本API兼容性
TEST(LegacyAPITest, Compatibility) {
    // 测试旧版本C接口
    EXPECT_EQ(LWLogInit(), 0);
    
    // 测试旧版本日志函数
    EXPECT_NO_THROW(LogMessage(LW_LOGLEVEL_INFO, "Legacy API test"));
    
    // 清理
    EXPECT_EQ(LWLogFini(), 0);
}

// 测试日志文件轮转
TEST(FileAppenderTest, FileRotation) {
    std::string test_file = "rotation_test.log";

    // 用今天日期构建归档文件名前缀，清理可能存在的旧文件
    time_t now = time(nullptr);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char today[16];
    snprintf(today, sizeof(today), "%04d%02d%02d",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday);
    std::string archive_prefix = test_file + "." + today + ".";

    for (int i = 1; i <= 5; ++i) {
        remove((archive_prefix + std::to_string(i)).c_str());
    }
    remove(test_file.c_str());

    { // 作用域确保FileAppender被正确销毁
        FileAppender appender(test_file);

        LogOptions options;
        options.max_file_size = 100; // 很小的文件大小，确保触发轮转（字节）
        options.max_file_count = 3;
        options.save_days      = 5;
        appender.set_options(options);

        // 写入足够的日志以触发轮转
        for (int i = 0; i < 100; ++i) {
            LogEntry entry;
            entry.level = LogLevel::INFO;
            entry.message = "Log entry " + std::to_string(i) + " to trigger rotation";
            entry.file = __FILE__;
            entry.line = __LINE__;
            entry.function = __FUNCTION__;
            entry.timestamp = std::chrono::system_clock::now();
            entry.thread_id = std::this_thread::get_id();

            appender.append(entry);
        }
    }

    // 检查是否生成了轮转文件（新命名：rotation_test.log.YYYYMMDD.N）
    int rotation_count = 0;
    for (int i = 1; i <= 4; ++i) {
        std::string filename = archive_prefix + std::to_string(i);
        if (std::ifstream(filename).good()) {
            rotation_count++;
        }
    }

    EXPECT_GE(rotation_count, 1);

    // 清理测试文件
    for (int i = 1; i <= 5; ++i) {
        remove((archive_prefix + std::to_string(i)).c_str());
    }
    remove(test_file.c_str());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
