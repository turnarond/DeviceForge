/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * File: LogFormatter.h
 *
 * Date: 2026-04-29
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: Log formatter, adds unified prefix format for logs
 */

#pragma once

#ifdef _WIN32
#pragma push_macro("ERROR")
#undef ERROR
#endif

#include <string>
#include <memory>
#include <ctime>
#include <sstream>
#include <logging/Logger.h>

namespace lwserverbase {
namespace logging {
class Logger;
}

namespace vsoa_node {

/**
 * @class LogFormatter
 * @brief 日志格式化器
 *
 * 包装 Logger 实例，为每条日志添加统一的前缀格式：
 * [YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] [ServiceName] [ThreadID] 消息内容
 *
 * 不修改 Logger 现有接口，通过包装方式实现格式化。
 */
class LogFormatter {
public:
    /**
     * @brief 构造函数
     * @param logger Logger 实例指针
     * @param serviceName 服务名称（用于日志前缀）
     */
    LogFormatter(logging::Logger* logger, const std::string& serviceName);

    /**
     * @brief 析构函数
     */
    ~LogFormatter() = default;

    /**
     * @brief 获取当前时间戳字符串
     * @return 格式化的时间戳 [YYYY-MM-DD HH:MM:SS.mmm]
     */
    static std::string getCurrentTimestamp();

    /**
     * @brief 获取当前线程 ID 字符串
     * @return 线程 ID 字符串
     */
    static std::string getThreadId();

    /**
     * @brief 格式化日志前缀
     * @param level 日志级别字符串
     * @return 格式化后的前缀，不包含消息内容
     */
    std::string formatPrefix(const std::string& level) const;

    /**
     * @brief 格式化并记录日志
     * @param level 日志级别
     * @param message 日志消息
     *
     * 组装格式为 [timestamp] [LEVEL] [ServiceName] [ThreadID] message
     * 后调用 Logger::Log()。
     */
    void log(logging::Logger::Level level, const std::string& message);

    /**
     * @brief 格式化并记录日志（printf 风格）
     * @param level 日志级别
     * @param format 格式字符串
     * @param ... 可变参数
     */
    void logFormat(logging::Logger::Level level, const char* format, ...);

    /**
     * @brief 设置服务名称
     * @param serviceName 新的服务名称
     */
    void setServiceName(const std::string& serviceName);

    /**
     * @brief 获取当前服务名称
     * @return 服务名称
     */
    std::string getServiceName() const;

private:
    logging::Logger* m_logger;    ///< Logger instance pointer
    std::string m_serviceName;    ///< Service name
};

} // namespace vsoa_node
} // namespace lwserverbase

#ifdef _WIN32
#pragma pop_macro("ERROR")
#endif
