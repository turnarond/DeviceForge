/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: Logger.h
 *
 * Date: 2026-03-03
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#pragma once

#ifdef _WIN32
#pragma push_macro("ERROR")
#undef ERROR
#endif

#include <string>
#include <lwlog/lwlog.h>

#ifdef _WIN32
#pragma pop_macro("ERROR")
#endif

namespace lwserverbase {
namespace logging {

/**
 * @class Logger
 * @brief Logger for log recording and management
 * 
 * Logger provides log recording functionality with different log levels,
 * using lwlog library for implementation.
 */
class Logger {
public:
    // Use LogLevel from lwlog library instead of redefining
    using Level = LogLevel;
    
    /**
     * @brief Constructor
     */
    Logger();
    
    /**
     * @brief Destructor
     */
    ~Logger();
    
    /**
     * @brief Initialize log system
     * @param logFileName log file name
     * @return true if success, false if failed
     */
    bool Initialize(const std::string& logFileName);
    
    /**
     * @brief Log message
     * @param level log level
     * @param format log format
     * @param ... variable arguments
     */
    void Log(Level level, const char* format, ...);
    
    /**
     * @brief Log message
     * @param level log level
     * @param message log message
     */
    void Log(Level level, const std::string& message);
    
    /**
     * @brief Log debug message
     * @param format log format
     * @param ... variable arguments
     */
    void Debug(const char* format, ...);
    
    /**
     * @brief Log debug message
     * @param message log message
     */
    void Debug(const std::string& message);
    
    /**
     * @brief Log info message
     * @param format log format
     * @param ... variable arguments
     */
    void Info(const char* format, ...);
    
    /**
     * @brief Log info message
     * @param message log message
     */
    void Info(const std::string& message);
    
    /**
     * @brief Log warning message
     * @param format log format
     * @param ... variable arguments
     */
    void Warn(const char* format, ...);
    
    /**
     * @brief Log warning message
     * @param message log message
     */
    void Warn(const std::string& message);
    
    /**
     * @brief Log error message
     * @param format log format
     * @param ... variable arguments
     */
    void Error(const char* format, ...);
    
    /**
     * @brief Log error message
     * @param message log message
     */
    void Error(const std::string& message);
    
    /**
     * @brief Log critical message
     * @param format log format
     * @param ... variable arguments
     */
    void Critical(const char* format, ...);
    
    /**
     * @brief Log critical message
     * @param message log message
     */
    void Critical(const std::string& message);
    
    /**
     * @brief Set log level
     * @param level log level
     */
    void SetLevel(Level level);
    
    /**
     * @brief Get log level
     * @return log level
     */
    Level GetLevel() const;

private:
    /**
     * @brief Log with level filter using va_list
     * @param level log level
     * @param format format string
     * @param args va_list arguments
     */
    void LogV(Level level, const char* format, va_list args);

    CLWLog m_logger;
    Level m_level;
};

} // namespace logging
} // namespace lwserverbase
