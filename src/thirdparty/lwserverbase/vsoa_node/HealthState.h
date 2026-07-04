/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * File: HealthState.h
 *
 * Date: 2026-04-29
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: VSOA 节点健康状态数据结构
 */

#pragma once

#include <string>
#include <map>

namespace lwserverbase {
namespace vsoa_node {

/**
 * @struct HealthState
 * @brief 健康状态结构体
 *
 * 包含整体健康状态和各项检查结果，提供序列化为 JSON 的辅助方法。
 */
struct HealthState {
    /**
     * @brief 整体健康状态
     */
    enum class Status {
        HEALTHY,   ///< 健康
        UNHEALTHY, ///< 不健康
        DEGRADED   ///< 降级运行
    };

    Status overallStatus = Status::HEALTHY;           ///< 整体健康状态
    std::map<std::string, std::string> checkResults;  ///< 各检查项结果（检查项名称 -> "ok"/"fail"/描述）

    /**
     * @brief 判断整体是否健康
     * @return true 表示健康
     */
    bool isHealthy() const {
        return overallStatus == Status::HEALTHY;
    }

    /**
     * @brief 将整体状态转换为字符串
     * @return "healthy", "unhealthy", 或 "degraded"
     */
    std::string statusToString() const {
        switch (overallStatus) {
            case Status::HEALTHY:   return "healthy";
            case Status::UNHEALTHY: return "unhealthy";
            case Status::DEGRADED:  return "degraded";
            default:                return "unknown";
        }
    }

    /**
     * @brief 序列化为 JSON 字符串
     * @return JSON 格式的健康状态字符串
     */
    std::string toJson() const {
        std::string json = "{\n";
        json += "    \"status\": \"" + statusToString() + "\",\n";
        json += "    \"checks\": {\n";
        bool first = true;
        for (const auto& [key, value] : checkResults) {
            if (!first) {
                json += ",\n";
            }
            json += "        \"" + key + "\": \"" + value + "\"";
            first = false;
        }
        json += "\n    }\n";
        json += "}";
        return json;
    }
};

} // namespace vsoa_node
} // namespace lwserverbase
