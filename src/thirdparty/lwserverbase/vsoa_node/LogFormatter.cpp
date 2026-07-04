/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * File: LogFormatter.cpp
 *
 * Date: 2026-04-29
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: Log formatter implementation
 */

#ifdef _WIN32
#pragma push_macro("ERROR")
#undef ERROR
#endif

#include <vsoa_node/LogFormatter.h>
#include <logging/Logger.h>
#include <thread>
#include <iomanip>
#include <cstring>
#include <stdarg.h>

#ifndef _WIN32
#include <sys/syscall.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

namespace lwserverbase {
namespace vsoa_node {

LogFormatter::LogFormatter(logging::Logger* logger, const std::string& serviceName)
    : m_logger(logger)
    , m_serviceName(serviceName) {
}

std::string LogFormatter::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    auto timer = std::chrono::system_clock::to_time_t(now);
    std::tm bt;
#ifdef _WIN32
    localtime_s(&bt, &timer);
#else
    localtime_r(&timer, &bt);
#endif

    std::ostringstream oss;
    oss << std::put_time(&bt, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string LogFormatter::getThreadId() {
    // Cross-platform thread ID retrieval
#ifdef _WIN32
    DWORD tid = GetCurrentThreadId();
    return std::to_string(tid);
#else
    pid_t tid = static_cast<pid_t>(::syscall(SYS_gettid));
    return std::to_string(tid);
#endif
}

std::string LogFormatter::formatPrefix(const std::string& level) const {
    return "[" + getCurrentTimestamp() + "] "
           "[" + level + "] "
           "[" + m_serviceName + "] "
           "[thread-" + getThreadId() + "] ";
}

void LogFormatter::log(logging::Logger::Level level, const std::string& message) {
    if (!m_logger) return;

    // 将 Level 转换为字符串
    std::string levelStr;
    switch (level) {
        case logging::Logger::Level::DEBUG:    levelStr = "DEBUG"; break;
        case logging::Logger::Level::INFO:     levelStr = "INFO"; break;
        case logging::Logger::Level::WARN:     levelStr = "WARN"; break;
        case logging::Logger::Level::ERROR:    levelStr = "ERROR"; break;
        case logging::Logger::Level::CRITICAL: levelStr = "CRITICAL"; break;
        default:                               levelStr = "UNKNOWN"; break;
    }

    std::string formatted = formatPrefix(levelStr) + message;
    m_logger->Log(level, formatted);
}

void LogFormatter::logFormat(logging::Logger::Level level, const char* format, ...) {
    if (!m_logger || !format) return;

    va_list args;
    va_start(args, format);
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    log(level, std::string(buffer));
}

void LogFormatter::setServiceName(const std::string& serviceName) {
    m_serviceName = serviceName;
}

std::string LogFormatter::getServiceName() const {
    return m_serviceName;
}

} // namespace vsoa_node
} // namespace lwserverbase

#ifdef _WIN32
#pragma pop_macro("ERROR")
#endif
