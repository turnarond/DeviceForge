/*
 * Copyright (c) 2025 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: console_appender.cpp 
 *
 * Date: 2025-04-17
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#include "console_appender.h"
#include "pattern_formatter.h"
#include <iostream>
#include <mutex>

static std::mutex g_console_mutex;

ConsoleAppender::ConsoleAppender() {
    formatter_ = std::make_shared<PatternFormatter>();
}

ConsoleAppender::~ConsoleAppender() {
}

void ConsoleAppender::append(const LogEntry& entry) {
    if (!options_.log_on_monitor) return;

    std::lock_guard<std::mutex> lock(g_console_mutex);

    std::string formatted_message = formatter_->format(entry);
    
    if (options_.enable_color) {
        std::cout << get_color_code(entry.level) << formatted_message << get_reset_code() << std::endl;
    } else {
        std::cout << formatted_message << std::endl;
    }
}

void ConsoleAppender::set_formatter(std::shared_ptr<IFormatter> formatter) {
    formatter_ = formatter;
}

void ConsoleAppender::set_options(const LogOptions& options) {
    options_ = options;
}

std::string ConsoleAppender::get_color_code(LogLevel level) const {
    switch (level) {
        case LogLevel::DEBUG: return "\033[36m";  // Cyan
        case LogLevel::INFO: return "\033[32m";   // Green
        case LogLevel::WARN: return "\033[33m";   // Yellow
        case LogLevel::ERROR: return "\033[31m";  // Red
        case LogLevel::CRITICAL: return "\033[35m";  // Magenta
        default: return "";
    }
}

std::string ConsoleAppender::get_reset_code() const {
    return "\033[0m";
}

/*
 * end
 */
