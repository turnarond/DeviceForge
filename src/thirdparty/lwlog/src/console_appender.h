/*
 * Copyright (c) 2025 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: console_appender.h 
 *
 * Date: 2025-04-17
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#ifndef _CONSOLE_APPENDER_H_
#define _CONSOLE_APPENDER_H_

#include "../lwlog.h"
#include <memory>
#include <string>

class ConsoleAppender : public IAppender {
public:
    ConsoleAppender();
    ~ConsoleAppender() override;

    void append(const LogEntry& entry) override;
    void set_formatter(std::shared_ptr<IFormatter> formatter) override;
    void set_options(const LogOptions& options) override;

private:
    std::shared_ptr<IFormatter> formatter_;
    LogOptions options_;
    
    // 控制台颜色
    std::string get_color_code(LogLevel level) const;
    std::string get_reset_code() const;
};

#endif // _CONSOLE_APPENDER_H_

/*
 * end
 */
