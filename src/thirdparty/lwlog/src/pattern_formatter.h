/*
 * Copyright (c) 2025-2026 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: pattern_formatter.h
 *
 * Date: 2025-04-17
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#ifndef _PATTERN_FORMATTER_H_
#define _PATTERN_FORMATTER_H_

#include "../lwlog.h"
#include <string>

class PatternFormatter : public IFormatter {
public:
    explicit PatternFormatter(const std::string& pattern = "%Y-%m-%d %H:%M:%S.%3f [%l][%t] %v");
    ~PatternFormatter() override;

    std::string format(const LogEntry& entry) override;
    void set_options(const LogOptions& options) override;
    void set_pattern(const std::string& pattern);

private:
    std::string pattern_;
    LogOptions options_;

    // Token handlers used by format()
    static std::string format_time_token(const std::chrono::system_clock::time_point& time,
                                         const std::string& token);
    static std::string format_level(LogLevel level);
    static std::string format_thread_id(std::thread::id thread_id);
    static std::string format_millis(const std::chrono::system_clock::time_point& time,
                                     int digits);
};

#endif // _PATTERN_FORMATTER_H_

/*
 * end
 */
