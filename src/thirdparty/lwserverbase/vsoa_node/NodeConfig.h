/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * File: NodeConfig.h
 *
 * Date: 2026-04-29
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: VSOA 节点配置数据结构
 */

#pragma once

#include <string>
#include <cstdint>

namespace lwserverbase {
namespace vsoa_node {

/**
 * @struct NodeConfig
 * @brief VSOA 节点配置结构
 *
 * 包含 VSOA 节点运行所需的所有配置项。
 * 配置可通过 JSON 配置文件覆盖，ConfigManager 读取时使用 node.* 前缀。
 */
struct NodeConfig {
    std::string serviceName = "UnknownNode";       ///< 服务名称
    std::string version = "1.0.0";                 ///< 服务版本号
    std::string description;                       ///< 服务描述
    std::string serverHost = "0.0.0.0";            ///< 服务端监听地址
    uint16_t serverPort = 0;                       ///< 服务端端口（0=自动分配）
    std::string serverPassword;                    ///< 服务端密码
    std::string configPath = "config.json";         ///< 配置文件路径
    bool daemonMode = false;                       ///< 是否启用守护进程模式
    int shutdownTimeoutMs = 5000;                  ///< 优雅退出等待超时（毫秒）
    int vsoaSpinIntervalMs = 50;                   ///< VSOA 事件循环间隔（毫秒）
    std::string encoding = "json";                 ///< 序列化方式
    int dataMode = 1;                              ///< payload 模式: 0=PARAM_MODE, 1=DATA_MODE(默认，与 vsoa_master 一致)

    /**
     * @brief 配置验证结果
     */
    struct ValidationResult {
        bool ok = true;
        std::string error;
    };

    /**
     * @brief 验证配置合法性
     * @return ValidationResult 验证结果
     */
    ValidationResult validate() const {
        ValidationResult r;

        // serverPort 类型为 uint16_t，范围在编译期保证，无需运行时检查

        if (shutdownTimeoutMs < 0) {
            r.ok = false;
            r.error = "shutdownTimeoutMs must be >= 0";
            return r;
        }

        if (dataMode != 0 && dataMode != 1) {
            r.ok = false;
            r.error = "dataMode must be 0 or 1, got " + std::to_string(dataMode);
            return r;
        }

        return r;
    }
};

} // namespace vsoa_node
} // namespace lwserverbase
