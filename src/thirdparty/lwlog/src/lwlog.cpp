/*
 * Copyright (c) 2025-2026 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: lwlog.cpp .
 *
 * Date: 2025-04-17
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#include "../lwlog.h"
#include "logger_impl.h"
#include "console_appender.h"
#include "file_appender.h"
#include "pattern_formatter.h"
#include "log_config_reader.h"
#include <stdarg.h>
#include <cstring>
#include <map>
#include <mutex>
#include "lwcomm/lwcomm.h"
#ifdef _WIN32
#include <sys/stat.h>
#define stat _stat64
#ifdef ERROR
#undef ERROR
#endif
#else
#include <sys/stat.h>
#endif

#define MAX_LOG_SIZE 4096

// Map legacy bitmask log levels to LogLevel enum
static LogLevel ConvertOldLogLevel(int nOldLevel) {
    if (nOldLevel & LW_LOGLEVEL_DEBUG) {
        return LogLevel::DEBUG;
    } else if (nOldLevel & LW_LOGLEVEL_INFO) {
        return LogLevel::INFO;
    } else if (nOldLevel & LW_LOGLEVEL_WARN) {
        return LogLevel::WARN;
    } else if (nOldLevel & LW_LOGLEVEL_ERROR) {
        return LogLevel::ERROR;
    } else if (nOldLevel & LW_LOGLEVEL_CRITICAL) {
        return LogLevel::CRITICAL;
    }
    return LogLevel::INFO;
}

CLWLog::CLWLog ()
{
}

CLWLog::~CLWLog ()
{
}

/**
 * @brief Write log message with specified level (internal function)
 * @param nLogLevel Log level (LW_LOGLEVEL_INFO, LW_LOGLEVEL_ERROR, etc.)
 * @param szFormat format string, printf style
 * @param ap vargs
 *
 * Note: __FILE__/__LINE__/__FUNCTION__ report lwlog.cpp, not the caller.
 * This is inherent to the legacy API design; prefer the LWLOG_* macros
 * for accurate source location tracking.
 */
void CLWLog::_Internal_LogMessageV(int nLogLevel, const char* szFormat, va_list ap)
{
    auto logger = LogManager::get_logger(LWComm::GetProcessName());

    char szbuf[MAX_LOG_SIZE] = {0};
    int len = vsnprintf(szbuf, sizeof(szbuf), szFormat, ap);

    if (len < 0) {
        snprintf(szbuf, sizeof(szbuf), "[Log formatting failed]");
    } else if (len >= (int)sizeof(szbuf) - 1) {
        szbuf[sizeof(szbuf) - 4] = '.';
        szbuf[sizeof(szbuf) - 3] = '.';
        szbuf[sizeof(szbuf) - 2] = '.';
        szbuf[sizeof(szbuf) - 1] = '\0';
    }

    LogLevel level = ConvertOldLogLevel(nLogLevel);
    logger->log(level, __FILE__, __LINE__, __FUNCTION__, szbuf);
}

/**
 * @brief Write log message with specified level
 * @param nLogLevel Log level (LW_LOGLEVEL_INFO, LW_LOGLEVEL_ERROR, etc.)
 * @param szFormat format string, printf style
 * @param ... vargs
 */
void CLWLog::LogMessage (int nLogLevel, const char *szFormat, ...)
{
    va_list ap;
    va_start(ap, szFormat);
    _Internal_LogMessageV(nLogLevel, szFormat, ap);
    va_end(ap);
}

/**
 * @brief Write log message with LW_LOGLEVEL_ERROR level
 * @param szFormat format string, printf style
 * @param ... vargs
 */
void CLWLog::LogErrMessage(const char* szFormat, ...)
{
    va_list ap;
    va_start(ap, szFormat);
    _Internal_LogMessageV(LW_LOGLEVEL_ERROR, szFormat, ap);
    va_end(ap);
}

/**
 *  the same as BBSERROR uses.
 *
 *  @param  -[in,out]  int  nLogLevel: log level
 *  @param  -[in,out]  const char*  szFormat: log context
 *  @param  -[in,out]  ...: vargs
 *  @return void.
 *
 *  @version
 */
void CLWLog::LogHexMessage (int nLogLevel, const char *szHexBuf, int nHexBufLen, const char *szFormat, ...)
{
    if (nHexBufLen <= 0) return;

    auto logger = LogManager::get_logger(LWComm::GetProcessName());

    if (!(LW_LOGLEVEL_DEBUG & nLogLevel) &&
        !(LW_LOGLEVEL_INFO & nLogLevel) &&
        !(LW_LOGLEVEL_WARN & nLogLevel) &&
        !(LW_LOGLEVEL_ERROR & nLogLevel) &&
        !(LW_LOGLEVEL_CRITICAL & nLogLevel)) {
        return;
    }

    char szbuf[MAX_LOG_SIZE] = { 0 };

    va_list ap;
    va_start(ap, szFormat);
    int len = vsnprintf(szbuf, sizeof(szbuf), szFormat, ap);
    va_end(ap);

    if (len < 0) {
        snprintf(szbuf, sizeof(szbuf), "[Hex log format failed]");
    } else if (len >= (int)sizeof(szbuf)-1) {
        // Truncation handling
        memcpy(szbuf + sizeof(szbuf) - 4, "...", 3);
        szbuf[sizeof(szbuf)-1] = '\0';
    }

    // Convert binary data to hex string
    char szHexString[MAX_LOG_SIZE] = {0};
    int hex_len = 0;
    for (int i = 0; i < nHexBufLen && hex_len < (int)sizeof(szHexString) - 3; i++) {
        hex_len += snprintf(szHexString + hex_len, sizeof(szHexString) - hex_len, "%02X ", (unsigned char)szHexBuf[i]);
        if ((i + 1) % 16 == 0) {
            hex_len += snprintf(szHexString + hex_len, sizeof(szHexString) - hex_len, "\n");
        }
    }

    char szHexLen[32] = {0};
    snprintf(szHexLen, sizeof(szHexLen), "[%04d] ", nHexBufLen);

    std::string strLog = szbuf;
    strLog += szHexLen;
    strLog += szHexString;

    LogLevel level = ConvertOldLogLevel(nLogLevel);
    logger->log(level, __FILE__, __LINE__, __FUNCTION__, strLog);
}

// LogManager implementation
namespace {
    std::map<std::string, std::shared_ptr<ILogger>> g_loggers;
    std::mutex g_logger_mutex;
    LogOptions g_default_options;
    std::once_flag g_config_once;
}

static std::shared_ptr<ILogger> create_logger(const std::string& logger_name,
                                               const LogConfigReader::ModuleConfig& config,
                                               const LogOptions& default_opts) {
    auto logger = std::make_shared<LoggerImpl>(logger_name);

    logger->set_level(config.currentLogLevel);

    // Add default appenders
    auto console_appender = std::make_shared<ConsoleAppender>();
    logger->add_appender(console_appender);

    // Determine log directory
    std::string log_dir = "./logs";
    const char* log_path = LWComm::GetLogPath();
    if (log_path && strlen(log_path) > 0) {
        log_dir = log_path;
    }

    // Ensure log directory exists
    LWFileSystem::CreateDirectory(log_dir, true);

    // Create file appender: {log_dir}/{logger_name}.log
    std::string log_file_path = log_dir + "/" + logger_name + ".log";
    auto file_appender = std::make_shared<FileAppender>(log_file_path);
    logger->add_appender(file_appender);

    // Set options from config — cast to size_t before multiplication to avoid overflow
    LogOptions options = default_opts;  // Snapshot passed from caller under lock
    options.max_file_size  = static_cast<size_t>(config.fileMaxKb) * 1024;
    options.max_file_count = static_cast<size_t>(config.fileMaxNo);
    options.save_days      = config.saveDays;
    options.log_on_monitor = config.logOnMonitor;
    logger->set_options(options);

    // Set default formatter with millisecond precision
    auto formatter = std::make_shared<PatternFormatter>(
        "%Y-%m-%d %H:%M:%S.%3f [%l][%t] %v");
    logger->set_formatter(formatter);

    return logger;
}

std::shared_ptr<ILogger> LogManager::get_logger(const std::string& name) {
    // Resolve logger name
    std::string logger_name = name;
    if (logger_name.empty()) {
        const char* process_name = LWComm::GetProcessName();
        if (process_name && strlen(process_name) > 0) {
            logger_name = process_name;
        } else {
            logger_name = "default";
        }
    }

    // Fast path: check cache under lock
    {
        std::lock_guard<std::mutex> lock(g_logger_mutex);
        auto it = g_loggers.find(logger_name);
        if (it != g_loggers.end()) {
            return it->second;
        }
    }

    // Load XML configuration on first use (once, thread-safe)
    std::call_once(g_config_once, []() {
        const char* config_path = LWComm::GetConfigPath();
        if (config_path && strlen(config_path) > 0) {
            std::string config_file = std::string(config_path) + "/logconfig.xml";
            LogConfigReader::instance().load_config(config_file);
        }
    });

    // Snapshot default options under lock to avoid data race with set_default_options
    LogConfigReader::ModuleConfig config;
    LogOptions default_opts;
    {
        std::lock_guard<std::mutex> lock(g_logger_mutex);
        config = LogConfigReader::instance().get_module_config(logger_name);
        default_opts = g_default_options;
    }

    // Slow path: create the logger (filesystem I/O) WITHOUT holding the global lock
    auto logger = create_logger(logger_name, config, default_opts);

    // Double-checked insert: re-acquire lock, check if another thread raced us
    {
        std::lock_guard<std::mutex> lock(g_logger_mutex);
        auto it = g_loggers.find(logger_name);
        if (it != g_loggers.end()) {
            return it->second;  // Another thread created it first
        }
        g_loggers[logger_name] = logger;
    }

    return logger;
}

void LogManager::set_default_options(const LogOptions& options) {
    std::lock_guard<std::mutex> lock(g_logger_mutex);

    g_default_options = options;

    // Propagate options to all existing loggers
    for (auto& pair : g_loggers) {
        pair.second->set_options(options);
    }
}

void LogManager::check_config_reload() {
    // Thread-safe: protect throttle + stat() + mtime with a dedicated mutex.
    // reload_config() acquires g_logger_mutex; we release check_mutex before that
    // to avoid holding two locks and to keep the small lock fast.
    static std::mutex check_mutex;
    static auto lastCheckTime = std::chrono::steady_clock::now();
    static time_t lastMtime = 0;

    std::string config_path;
    bool need_reload = false;

    {
        std::lock_guard<std::mutex> lock(check_mutex);

        int interval = LogConfigReader::instance().get_check_interval();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastCheckTime).count() < interval) {
            return;
        }
        lastCheckTime = now;

        config_path = LogConfigReader::instance().get_config_path();
        if (config_path.empty()) {
            return;
        }

#ifdef _WIN32
        struct _stat64 fileStat;
#else
        struct stat fileStat;
#endif
        if (stat(config_path.c_str(), &fileStat) != 0) {
            return;
        }

        if (fileStat.st_mtime == lastMtime) {
            return;
        }
        lastMtime = fileStat.st_mtime;
        need_reload = true;
    } // check_mutex released here

    if (need_reload) {
        reload_config();
    }
}

void LogManager::reload_config() {
    std::lock_guard<std::mutex> lock(g_logger_mutex);

    const std::string& config_path = LogConfigReader::instance().get_config_path();
    if (config_path.empty()) {
        return;
    }

    // Reload config file (load_config clears old data before parsing)
    if (!LogConfigReader::instance().load_config(config_path)) {
        return;
    }

    // Apply updated config to all existing loggers
    for (auto& pair : g_loggers) {
        const auto& config = LogConfigReader::instance().get_module_config(pair.first);
        pair.second->set_level(config.currentLogLevel);

        LogOptions opts = g_default_options;
        // Cast to size_t before multiplication to avoid int overflow
        opts.max_file_size  = static_cast<size_t>(config.fileMaxKb) * 1024;
        opts.max_file_count = static_cast<size_t>(config.fileMaxNo);
        opts.save_days      = config.saveDays;
        opts.log_on_monitor = config.logOnMonitor;
        pair.second->set_options(opts);
    }
}

void LogManager::shutdown() {
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    g_loggers.clear();
}

/*
 * end
 */
