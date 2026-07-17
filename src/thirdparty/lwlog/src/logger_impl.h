/*
 * Copyright (c) 2025-2026 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: logger_impl.h
 *
 * Date: 2025-04-17
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#ifndef _LOGGER_IMPL_H_
#define _LOGGER_IMPL_H_

#include "../lwlog.h"
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>

class LoggerImpl : public ILogger {
public:
    explicit LoggerImpl(const std::string& name);
    ~LoggerImpl() override;

    void log(LogLevel level, const char* file, int line, const char* func,
             const std::string& message) override;

    void set_level(LogLevel level) override;
    LogLevel get_level() const override;
    void set_options(const LogOptions& options) override;
    void add_appender(std::shared_ptr<IAppender> appender) override;
    void add_filter(std::shared_ptr<IFilter> filter) override;
    void set_formatter(std::shared_ptr<IFormatter> formatter) override;

private:
    std::string name_;
    std::atomic<LogLevel> level_;          // Atomic — read in log() hot path without mutex
    LogOptions options_;
    std::vector<std::shared_ptr<IAppender>> appenders_;
    std::vector<std::shared_ptr<IFilter>> filters_;
    std::shared_ptr<IFormatter> formatter_;
    std::mutex mutex_;                     // Protects appenders_, filters_, formatter_, options_
};

#endif // _LOGGER_IMPL_H_

/*
 * end
 */
