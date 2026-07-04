/*
 * Copyright (c) 2025-2026 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: logger_impl.cpp
 *
 * Date: 2025-04-17
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#include "logger_impl.h"
#include "pattern_formatter.h"

LoggerImpl::LoggerImpl(const std::string& name)
    : name_(name), level_(LogLevel::INFO) {
    // Initialize default formatter
    formatter_ = std::make_shared<PatternFormatter>();
}

LoggerImpl::~LoggerImpl() {
}

void LoggerImpl::log(LogLevel level, const char* file, int line, const char* func,
                     const std::string& message) {
    // Auto hot-reload: check config file on each write (throttled).
    // MUST be called before acquiring mutex_ to avoid deadlock:
    //   reload_config() holds g_logger_mutex → acquires mutex_ via set_options()
    //   log() would hold mutex_ → check_config_reload() → acquire g_logger_mutex = deadlock
    LogManager::check_config_reload();

    // Atomic read — safe without mutex, no data race
    if (level < level_.load(std::memory_order_relaxed)) {
        return; // Skip if below configured level
    }

    LogEntry entry;
    entry.level = level;
    entry.message = message;
    entry.file = std::string(file);
    entry.line = line;
    entry.function = std::string(func);
    entry.timestamp = std::chrono::system_clock::now();
    entry.thread_id = std::this_thread::get_id();

    // Lock protects filters_, appenders_, formatter_, options_
    std::lock_guard<std::mutex> lock(mutex_);

    // Apply filters
    for (const auto& filter : filters_) {
        if (!filter->filter(entry)) {
            return; // Entry filtered out
        }
    }

    // Apply appenders
    for (const auto& appender : appenders_) {
        appender->append(entry);
    }
}

void LoggerImpl::set_level(LogLevel level) {
    // Atomic store — no mutex needed; independent from other state
    level_.store(level, std::memory_order_relaxed);
}

LogLevel LoggerImpl::get_level() const {
    return level_.load(std::memory_order_relaxed);
}

void LoggerImpl::set_options(const LogOptions& options) {
    std::lock_guard<std::mutex> lock(mutex_);
    options_ = options;
    for (const auto& appender : appenders_) {
        appender->set_options(options);
    }
    formatter_->set_options(options);
}

void LoggerImpl::add_appender(std::shared_ptr<IAppender> appender) {
    std::lock_guard<std::mutex> lock(mutex_);
    appender->set_formatter(formatter_);
    appender->set_options(options_);
    appenders_.push_back(std::move(appender));
}

void LoggerImpl::add_filter(std::shared_ptr<IFilter> filter) {
    std::lock_guard<std::mutex> lock(mutex_);
    filters_.push_back(std::move(filter));
}

void LoggerImpl::set_formatter(std::shared_ptr<IFormatter> formatter) {
    std::lock_guard<std::mutex> lock(mutex_);
    formatter_ = std::move(formatter);
    formatter_->set_options(options_);
    for (const auto& appender : appenders_) {
        appender->set_formatter(formatter_);
    }
}

/*
 * end
 */
