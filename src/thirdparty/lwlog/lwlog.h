/*
 * Copyright (c) 2025-2026 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: lwlog.h
 *
 * Date: 2025-04-17
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#ifndef _LWLOG_H_
#define _LWLOG_H_

#ifdef _WIN32
#ifdef lwlog_EXPORTS
#define LWLog_API __declspec(dllexport)
#else
#define LWLog_API __declspec(dllimport)
#endif
#else
#define LWLog_API __attribute__((visibility("default")))
#endif //_WIN32

#include <stdarg.h>
#include <memory>
#include <string>
#include <chrono>
#include <thread>
#include <sstream>

// Log level enum
// On Windows, ERROR is defined as a macro in winerror.h, so we undefine it first
#ifdef _WIN32
#pragma push_macro("ERROR")
#undef ERROR
#endif

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3,
    CRITICAL = 4
};

#ifdef _WIN32
#pragma pop_macro("ERROR")
#endif

// Log formatting options
struct LogOptions {
    bool enable_color = true;                // Enable ANSI color in console
    std::string time_format = "%Y-%m-%d %H:%M:%S";
    size_t max_file_size = 10 * 1024 * 1024; // Max log file size in bytes (10 MB)
    size_t max_file_count = 5;               // Max archive files per day
    int    save_days = 5;                    // Days to retain archived logs
    bool   log_on_monitor = true;            // Print to console (stdout)
};

// Legacy bitmask log levels for backward compatibility with C API
#ifndef LW_LOGLEVEL_DEBUG
#define LW_LOGLEVEL_DEBUG 0x01
#define LW_LOGLEVEL_INFO 0x02
#define LW_LOGLEVEL_WARN 0x04
#define LW_LOGLEVEL_ERROR 0x08
#define LW_LOGLEVEL_CRITICAL 0x10
#endif // LW_LOGLEVEL_DEBUG

// Log entry passed through the pipeline
struct LogEntry {
    LogLevel level;
    std::string message;
    std::string file;
    int line;
    std::string function;
    std::chrono::system_clock::time_point timestamp;
    std::thread::id thread_id;
};

// Logger interface
class LWLog_API ILogger {
public:
    virtual ~ILogger() = default;

    virtual void log(LogLevel level, const char* file, int line, const char* func,
                     const std::string& message) = 0;

    virtual void set_level(LogLevel level) = 0;
    virtual LogLevel get_level() const = 0;
    virtual void set_options(const LogOptions& options) = 0;
    virtual void add_appender(std::shared_ptr<class IAppender> appender) = 0;
    virtual void add_filter(std::shared_ptr<class IFilter> filter) = 0;
    virtual void set_formatter(std::shared_ptr<class IFormatter> formatter) = 0;
};

// Appender interface — writes formatted log entries to a sink
class LWLog_API IAppender {
public:
    virtual ~IAppender() = default;
    virtual void append(const LogEntry& entry) = 0;
    virtual void set_formatter(std::shared_ptr<class IFormatter> formatter) = 0;
    virtual void set_options(const LogOptions& options) = 0;
};

// Formatter interface — converts LogEntry to string
class LWLog_API IFormatter {
public:
    virtual ~IFormatter() = default;
    virtual std::string format(const LogEntry& entry) = 0;
    virtual void set_options(const LogOptions& options) = 0;
};

// Filter interface — decides whether a log entry should be emitted
class LWLog_API IFilter {
public:
    virtual ~IFilter() = default;
    virtual bool filter(const LogEntry& entry) = 0;
};

// Log manager — singleton that owns all loggers and provides hot-reload
class LWLog_API LogManager {
public:
    static std::shared_ptr<ILogger> get_logger(const std::string& name = "default");
    static void set_default_options(const LogOptions& options);
    static void reload_config();            // Force reload logconfig.xml immediately
    static void check_config_reload();      // Auto hot-reload with ChkCfgMdfyPeriod throttle
    static void shutdown();
private:
    LogManager() = default;
    ~LogManager() = default;
};

// Convenience macros
#define LWLOG_DEBUG(logger, msg) \
    logger->log(LogLevel::DEBUG, __FILE__, __LINE__, __FUNCTION__, msg)

#define LWLOG_INFO(logger, msg) \
    logger->log(LogLevel::INFO, __FILE__, __LINE__, __FUNCTION__, msg)

#define LWLOG_WARN(logger, msg) \
    logger->log(LogLevel::WARN, __FILE__, __LINE__, __FUNCTION__, msg)

#define LWLOG_ERROR(logger, msg) \
    logger->log(LogLevel::ERROR, __FILE__, __LINE__, __FUNCTION__, msg)

#define LWLOG_CRITICAL(logger, msg) \
    logger->log(LogLevel::CRITICAL, __FILE__, __LINE__, __FUNCTION__, msg)

#define LWLOG_DEFAULT LogManager::get_logger()
#define LWLOG_D(msg) LWLOG_DEBUG(LWLOG_DEFAULT, msg)
#define LWLOG_I(msg) LWLOG_INFO(LWLOG_DEFAULT, msg)
#define LWLOG_W(msg) LWLOG_WARN(LWLOG_DEFAULT, msg)
#define LWLOG_E(msg) LWLOG_ERROR(LWLOG_DEFAULT, msg)
#define LWLOG_C(msg) LWLOG_CRITICAL(LWLOG_DEFAULT, msg)

#ifdef __cplusplus
/* Legacy C++ API — wraps the new pipeline, kept for backward compatibility */
class LWLog_API CLWLog
{
public:
    CLWLog(void);
    ~CLWLog();

    /* Record one message */
#ifdef _WIN32
    void LogMessage(int nLogLevel, const char *szFormat, ...);
#else
    void LogMessage(int nLogLevel, const char *szFormat, ...) __attribute__ ((__format__ (__printf__, 3, 4)));
#endif

    /* Record one error message */
#ifdef _WIN32
    void LogErrMessage(const char *szFormat, ...);
#else
    void LogErrMessage(const char *szFormat, ...) __attribute__ ((__format__ (__printf__, 2, 3)));
#endif

    /* Record one message with hex data */
#ifdef _WIN32
    void LogHexMessage(int nLogLevel, const char *szHexBuf, int nHexBufLen, const char *szFormat, ...);
#else
    void LogHexMessage(int nLogLevel, const char *szHexBuf, int nHexBufLen, const char *szFormat, ...) __attribute__ ((__format__ (__printf__, 5, 6)));
#endif

    /* Internal use only - do not call directly */
    void _Internal_LogMessageV(int nLogLevel, const char* szFormat, va_list ap);
};
#endif /* __cplusplus */

/* C interface for backward compatibility */
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

LWLog_API int LWLogInit(void);

LWLog_API int LWLogFini(void);

#ifdef _WIN32
LWLog_API int LogMessage(int nLogLevel, const char *szFormat, ...);
#else
LWLog_API int LogMessage(int nLogLevel, const char *szFormat, ...) __attribute__ ((__format__ (__printf__, 2, 3)));
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ! defined(_LWLOG_H_) */
