/*
 * Copyright (c) 2025 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: boundary_test.cpp
 *
 * Date: 2026-04-18
 *
 * Author: Test Suite
 *
 */

#include "../lwlog.h"
#include "../src/file_appender.h"
#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include <limits>

// 测试空消息
TEST(BoundaryTest, EmptyMessage) {
    auto logger = LogManager::get_logger("empty_message_test");
    
    // 测试空字符串
    EXPECT_NO_THROW(LWLOG_INFO(logger, ""));
    
    // 测试nullptr
    EXPECT_NO_THROW(LogMessage(LW_LOGLEVEL_INFO, nullptr));
}

// 测试超长消息
TEST(BoundaryTest, LongMessage) {
    auto logger = LogManager::get_logger("long_message_test");
    
    // 创建一个超长消息（10000字符）
    std::string long_message(10000, 'a');
    
    // 测试超长消息
    EXPECT_NO_THROW(LWLOG_INFO(logger, long_message));
}

// 测试无效文件路径
TEST(BoundaryTest, InvalidFilePath) {
    // 测试无效的文件路径（不存在的目录）
    std::string invalid_path = "/nonexistent/directory/test.log";
    
    // 这里应该不会崩溃，而是优雅处理
    EXPECT_NO_THROW({
        auto appender = std::make_shared<FileAppender>(invalid_path);
        
        LogEntry entry;
        entry.level = LogLevel::INFO;
        entry.message = "Test with invalid path";
        entry.file = __FILE__;
        entry.line = __LINE__;
        entry.function = __FUNCTION__;
        entry.timestamp = std::chrono::system_clock::now();
        entry.thread_id = std::this_thread::get_id();
        
        appender->append(entry);
    });
}

// 测试文件大小边界
TEST(BoundaryTest, FileSizeBoundary) {
    std::string test_file = "size_boundary_test.log";
    
    // 清理可能存在的旧文件
    remove(test_file.c_str());
    
    { // 作用域确保FileAppender被正确销毁
        FileAppender appender(test_file);
        
        LogOptions options;
        options.max_file_size = 1; // 极小的文件大小
        options.max_file_count = 2;
        appender.set_options(options);
        
        // 测试文件大小边界
        for (int i = 0; i < 10; ++i) {
            LogEntry entry;
            entry.level = LogLevel::INFO;
            entry.message = "Test message " + std::to_string(i);
            entry.file = __FILE__;
            entry.line = __LINE__;
            entry.function = __FUNCTION__;
            entry.timestamp = std::chrono::system_clock::now();
            entry.thread_id = std::this_thread::get_id();
            
            EXPECT_NO_THROW(appender.append(entry));
        }
    }
    
    // 清理测试文件
    for (int i = 1; i <= 2; ++i) {
        std::string filename = test_file + "." + std::to_string(i);
        remove(filename.c_str());
    }
    remove(test_file.c_str());
}

// 测试多日志器名称冲突
TEST(BoundaryTest, LoggerNameConflict) {
    // 创建同名日志器
    auto logger1 = LogManager::get_logger("conflict_test");
    auto logger2 = LogManager::get_logger("conflict_test");
    
    // 验证是同一个实例
    EXPECT_TRUE(logger1.get() == logger2.get());
    
    // 测试日志输出
    EXPECT_NO_THROW(LWLOG_INFO(logger1, "Test from logger1"));
    EXPECT_NO_THROW(LWLOG_INFO(logger2, "Test from logger2"));
}

// 测试日志级别边界
TEST(BoundaryTest, LogLevelBoundary) {
    auto logger = LogManager::get_logger("level_boundary_test");
    
    // 测试最低级别
    logger->set_level(LogLevel::DEBUG);
    EXPECT_EQ(logger->get_level(), LogLevel::DEBUG);
    
    // 测试最高级别
    logger->set_level(LogLevel::CRITICAL);
    EXPECT_EQ(logger->get_level(), LogLevel::CRITICAL);
}

// 测试并发边界
TEST(BoundaryTest, ConcurrencyBoundary) {
    auto logger = LogManager::get_logger("concurrency_boundary_test");
    
    const int thread_count = 50; // 更多线程
    const int logs_per_thread = 500; // 每个线程更少日志
    
    std::vector<std::thread> threads;
    
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back([logger, i, logs_per_thread]() {
            for (int j = 0; j < logs_per_thread; ++j) {
                std::string message = "Boundary test - Thread " + std::to_string(i) + ", log " + std::to_string(j);
                EXPECT_NO_THROW(LWLOG_INFO(logger, message));
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    // 验证没有崩溃
    SUCCEED();
}

// 测试异常处理
TEST(BoundaryTest, ExceptionHandling) {
    auto logger = LogManager::get_logger("exception_test");
    
    // 测试在日志处理过程中的异常
    EXPECT_NO_THROW({
        try {
            LWLOG_INFO(logger, "Test before exception");
            // 模拟异常
            throw std::runtime_error("Test exception");
        } catch (const std::exception& e) {
            // 异常后应该还能正常日志
            LWLOG_ERROR(logger, std::string("Caught exception: ") + e.what());
        }
    });
}

// 测试日志目录创建
TEST(BoundaryTest, LogDirectoryCreation) {
    std::string test_dir = "./test_log_dir";
    std::string test_file = test_dir + "/test.log";
    
    // 清理可能存在的旧目录
    int ret = system(("rm -rf " + test_dir).c_str());
    std::cout << "rm -rf returned: " << ret << std::endl;
    
    { // 作用域确保FileAppender被正确销毁
        std::cout << "Creating FileAppender with path: " << test_file << std::endl;
        FileAppender appender(test_file);
        
        LogEntry entry;
        entry.level = LogLevel::INFO;
        entry.message = "Test directory creation";
        entry.file = __FILE__;
        entry.line = __LINE__;
        entry.function = __FUNCTION__;
        entry.timestamp = std::chrono::system_clock::now();
        entry.thread_id = std::this_thread::get_id();
        
        // 应该自动创建目录
        std::cout << "Calling append()" << std::endl;
        EXPECT_NO_THROW(appender.append(entry));
    }
    
    // 验证目录是否创建
    std::cout << "Checking if file exists: " << test_file << std::endl;
    std::ifstream file(test_file);
    bool is_open = file.is_open();
    std::cout << "File is open: " << is_open << std::endl;
    EXPECT_TRUE(is_open);
    file.close();
    
    // 清理测试目录
    ret = system(("rm -rf " + test_dir).c_str());
    std::cout << "rm -rf returned: " << ret << std::endl;
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
