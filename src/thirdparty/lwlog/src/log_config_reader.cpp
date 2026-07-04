/*
 * Copyright (c) 2025 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: log_config_reader.cpp 
 *
 * Date: 2025-04-18
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#include "log_config_reader.h"
#include "lwcomm/lwcomm.h"
#include "tinyxml2/tinyxml2.h"

using namespace tinyxml2;

#ifdef _WIN32
#undef ERROR
#endif

LogConfigReader& LogConfigReader::instance() {
    static LogConfigReader instance;
    return instance;
}

LogLevel string_to_log_level(const std::string& level_str) {
    if (level_str == "DEBUG") {
        return LogLevel::DEBUG;
    } else if (level_str == "INFO") {
        return LogLevel::INFO;
    } else if (level_str == "WARN" || level_str == "WARNING") {
        return LogLevel::WARN;
    } else if (level_str == "ERROR") {
        return LogLevel::ERROR;
    } else if (level_str == "CRITICAL") {
        return LogLevel::CRITICAL;
    }
    return LogLevel::INFO;
}

bool LogConfigReader::load_config(const std::string& config_path) {
    tinyxml2::XMLDocument doc;
    tinyxml2::XMLError error = doc.LoadFile(config_path.c_str());
    if (error != tinyxml2::XML_SUCCESS) {
        return false;
    }

    tinyxml2::XMLElement* root = doc.FirstChildElement("LogConf");
    if (!root) {
        return false;
    }

    // Hot-reload: clear old config before loading new
    module_configs_.clear();
    default_config_ = ModuleConfig();
    m_configPath = config_path;

    // Parse root attr ChkCfgMdfyPeriod (config check interval in seconds)
    const char* check_period = root->Attribute("ChkCfgMdfyPeriod");
    if (check_period) {
        m_checkInterval = atoi(check_period);
        if (m_checkInterval <= 0) m_checkInterval = 5;
    } else {
        m_checkInterval = 5;
    }

    // Parse General section as default config
    tinyxml2::XMLElement* general_elem = root->FirstChildElement("General");
    if (general_elem) {
        const char* level_str = general_elem->Attribute("CurrentLogLV");
        if (level_str) {
            default_config_.currentLogLevel = string_to_log_level(level_str);
        }

        const char* save_days_str = general_elem->Attribute("SaveDays");
        if (save_days_str) {
            default_config_.saveDays = atoi(save_days_str);
        }

        const char* file_max_kb_str = general_elem->Attribute("FileMaxKb");
        if (file_max_kb_str) {
            default_config_.fileMaxKb = atoi(file_max_kb_str);
        }

        const char* file_max_no_str = general_elem->Attribute("FileMaxNo");
        if (file_max_no_str) {
            default_config_.fileMaxNo = atoi(file_max_no_str);
        }

        const char* log_on_monitor_str = general_elem->Attribute("LogOnMonitor");
        if (log_on_monitor_str) {
            default_config_.logOnMonitor = atoi(log_on_monitor_str) != 0;
        }
    }

    // Parse per-module config sections
    tinyxml2::XMLElement* module_elem = root->FirstChildElement();
    while (module_elem) {
        const char* module_name = module_elem->Name();
        if (module_name && strcmp(module_name, "General") != 0) {
            ModuleConfig config = default_config_;

            const char* level_str = module_elem->Attribute("CurrentLogLV");
            if (level_str) {
                config.currentLogLevel = string_to_log_level(level_str);
            }

            const char* save_days_str = module_elem->Attribute("SaveDays");
            if (save_days_str) {
                config.saveDays = atoi(save_days_str);
            }

            const char* file_max_kb_str = module_elem->Attribute("FileMaxKb");
            if (file_max_kb_str) {
                config.fileMaxKb = atoi(file_max_kb_str);
            }

            const char* file_max_no_str = module_elem->Attribute("FileMaxNo");
            if (file_max_no_str) {
                config.fileMaxNo = atoi(file_max_no_str);
            }

            const char* log_on_monitor_str = module_elem->Attribute("LogOnMonitor");
            if (log_on_monitor_str) {
                config.logOnMonitor = atoi(log_on_monitor_str) != 0;
            }

            module_configs_[module_name] = config;
        }

        module_elem = module_elem->NextSiblingElement();
    }

    return true;
}

const LogConfigReader::ModuleConfig& LogConfigReader::get_module_config(const std::string& module_name) const {
    auto it = module_configs_.find(module_name);
    if (it != module_configs_.end()) {
        return it->second;
    }
    return default_config_;
}
