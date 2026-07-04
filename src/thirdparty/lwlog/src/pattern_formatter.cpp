/*
 * Copyright (c) 2025-2026 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: pattern_formatter.cpp
 *
 * Date: 2025-04-17
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#ifdef _WIN32
#include <windows.h>
#define localtime_r(timep, result) localtime_s(result, timep)
#ifdef ERROR
#undef ERROR
#endif
#endif

#include "pattern_formatter.h"
#include <ctime>
#include <sstream>
#include <cstring>

PatternFormatter::PatternFormatter(const std::string& pattern)
    : pattern_(pattern.empty() ? "%Y-%m-%d %H:%M:%S.%3f [%l][%t] %v" : pattern) {
}

PatternFormatter::~PatternFormatter() {
}

void PatternFormatter::set_options(const LogOptions& options) {
    options_ = options;
}

void PatternFormatter::set_pattern(const std::string& pattern) {
    if (!pattern.empty()) {
        pattern_ = pattern;
    }
}

// ---- Token-based format ----------------------------------------------------

std::string PatternFormatter::format(const LogEntry& entry) {
    std::string result;
    result.reserve(256);

    for (size_t i = 0; i < pattern_.size(); ++i) {
        if (pattern_[i] == '%' && i + 1 < pattern_.size()) {
            ++i;
            char token = pattern_[i];

            switch (token) {
                // strftime-compatible date/time tokens
                case 'Y': case 'm': case 'd':
                case 'H': case 'M': case 'S': {
                    std::string fmt = "%";
                    fmt += token;
                    result += format_time_token(entry.timestamp, fmt);
                    break;
                }
                // Milliseconds with optional digit count (e.g., %3f = 3 digits)
                case '0': case '1': case '2': case '3':
                case '4': case '5': case '6': case '7':
                case '8': case '9': {
                    if (i + 1 < pattern_.size() && pattern_[i + 1] == 'f') {
                        int digits = token - '0';
                        ++i;  // consume 'f'
                        result += format_millis(entry.timestamp, digits);
                    } else {
                        result += '%';
                        result += token;
                    }
                    break;
                }
                case 'f':  // bare %f = 3-digit milliseconds (shorthand)
                    result += format_millis(entry.timestamp, 3);
                    break;
                case 'l':  // log level
                    result += format_level(entry.level);
                    break;
                case 't':  // thread id
                    result += format_thread_id(entry.thread_id);
                    break;
                case 'F':  // source filename
                    result += entry.file;
                    break;
                case 'L':  // source line number
                    result += std::to_string(entry.line);
                    break;
                case 'U':  // source function name
                    result += entry.function;
                    break;
                case 'v':  // log message (value)
                    result += entry.message;
                    break;
                case '%':  // literal percent
                    result += '%';
                    break;
                default:   // unknown token — output literally
                    result += '%';
                    result += token;
                    break;
            }
        } else {
            result += pattern_[i];
        }
    }

    return result;
}

// ---- Token helpers ---------------------------------------------------------

std::string PatternFormatter::format_time_token(
    const std::chrono::system_clock::time_point& time,
    const std::string& token) {
    std::time_t time_t_val = std::chrono::system_clock::to_time_t(time);
    std::tm tm_val;
    localtime_r(&time_t_val, &tm_val);
    char buffer[32];
    strftime(buffer, sizeof(buffer), token.c_str(), &tm_val);
    return std::string(buffer);
}

std::string PatternFormatter::format_millis(
    const std::chrono::system_clock::time_point& time, int digits) {
    auto duration = time.time_since_epoch();
    // Use explicit `long` to avoid int64_t / %ld mismatch on 32-bit ARM
    long ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() % 1000;
    char buffer[8];
    snprintf(buffer, sizeof(buffer), "%0*ld", digits, ms);
    return std::string(buffer);
}

std::string PatternFormatter::format_level(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:    return "DEBUG";
        case LogLevel::INFO:     return "INFO";
        case LogLevel::WARN:     return "WARN";
        case LogLevel::ERROR:    return "ERROR";
        case LogLevel::CRITICAL: return "CRITICAL";
        default:                 return "UNKNOWN";
    }
}

std::string PatternFormatter::format_thread_id(std::thread::id thread_id) {
    std::ostringstream oss;
    oss << thread_id;
    return oss.str();
}

/*
 * end
 */
