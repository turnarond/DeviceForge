/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: Logger.cpp
 *
 * Date: 2026-03-03
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#ifdef _WIN32
#pragma push_macro("ERROR")
#undef ERROR
#endif

#include <logging/Logger.h>
#include <stdarg.h>

#ifdef _WIN32
#undef ERROR
#endif

namespace lwserverbase {
namespace logging {

Logger::Logger() : m_level(Level::INFO) {
}

Logger::~Logger() {
}

bool Logger::Initialize(const std::string& logFileName) {
    // CLWLog::SetLogFileName 已移除(lwlog v1.5)。
    // 日志文件路径现由 LWComm::GetProcessName() + LWComm::GetLogPath() 统一管理。
    (void)logFileName;
    return true;
}

void Logger::LogV(Level level, const char* format, va_list args) {
    if (static_cast<int>(level) < static_cast<int>(m_level)) {
        return;
    }
    m_logger._Internal_LogMessageV(static_cast<int>(level), format, args);
}

void Logger::Log(Level level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogV(level, format, args);
    va_end(args);
}

void Logger::Log(Level level, const std::string& message) {
    Log(level, message.c_str());
}

void Logger::Debug(const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogV(Level::DEBUG, format, args);
    va_end(args);
}

void Logger::Debug(const std::string& message) {
    Debug(message.c_str());
}

void Logger::Info(const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogV(Level::INFO, format, args);
    va_end(args);
}

void Logger::Info(const std::string& message) {
    Info(message.c_str());
}

void Logger::Warn(const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogV(Level::WARN, format, args);
    va_end(args);
}

void Logger::Warn(const std::string& message) {
    Warn(message.c_str());
}

void Logger::Error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogV(Level::ERROR, format, args);
    va_end(args);
}

void Logger::Error(const std::string& message) {
    Error(message.c_str());
}

void Logger::Critical(const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogV(Level::CRITICAL, format, args);
    va_end(args);
}

void Logger::Critical(const std::string& message) {
    Critical(message.c_str());
}

void Logger::SetLevel(Level level) {
    m_level = level;
}

Logger::Level Logger::GetLevel() const {
    return m_level;
}

} // namespace logging
} // namespace lwserverbase