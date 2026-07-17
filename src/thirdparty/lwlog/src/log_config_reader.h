/*
 * Copyright (c) 2025 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: log_config_reader.h 
 *
 * Date: 2025-04-18
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#ifndef _LOG_CONFIG_READER_H_
#define _LOG_CONFIG_READER_H_

#include "lwlog.h"
#include <string>
#include <map>

class LogConfigReader {
public:
    struct ModuleConfig {
        LogLevel currentLogLevel = LogLevel::INFO;
        int saveDays = 5;
        int fileMaxKb = 1000;
        int fileMaxNo = 3;
        bool logOnMonitor = true;
    };

    static LogConfigReader& instance();

    bool load_config(const std::string& config_path);
    const ModuleConfig& get_module_config(const std::string& module_name) const;
    const std::string& get_config_path() const { return m_configPath; }
    int  get_check_interval() const { return m_checkInterval; }  // seconds, default 5

private:
    LogConfigReader() = default;
    ~LogConfigReader() = default;

    std::map<std::string, ModuleConfig> module_configs_;
    ModuleConfig default_config_;
    std::string m_configPath;
    int m_checkInterval = 5;  // ChkCfgMdfyPeriod from logconfig.xml, default 5s
};

#endif // _LOG_CONFIG_READER_H_
