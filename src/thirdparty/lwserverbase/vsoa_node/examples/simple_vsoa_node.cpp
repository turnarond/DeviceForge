/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * File: simple_vsoa_node.cpp
 *
 * Date: 2026-04-29
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: VSOA Node Framework 简单示例
 *
 * 本示例演示如何使用 VsoaNode 基类创建一个完整的 VSOA 微服务节点。
 * 用户只需继承 VsoaNode，实现 OnInit() 和 svc() 即可。
 *
 * 编译：cmake --build build --target vsoa_node_example
 * 运行：./vsoa_node_example
 */

#include <vsoa_node/VsoaNode.h>
#include <iostream>
#include <thread>
#include <chrono>

using namespace lwserverbase::vsoa_node;

/**
 * @class MySensorNode
 * @brief 传感器服务节点示例
 *
 * 继承 VsoaNode 实现：
 * - OnInit() 中注册 RPC 处理函数和订阅 Topic
 * - svc() 中周期性发布传感器数据
 * - GetServiceName()/GetServiceVersion() 返回服务元信息
 */
class MySensorNode : public VsoaNode {
public:
    int OnInit() override {
        // 注册 RPC URL - 带描述（用于 /help 输出）
        registerRpc("/sensor/get_value",
            [this](const std::string& url, const void* data,
                   size_t len, std::string& resp) {
                resp = R"({"value": 42, "unit": "temperature"})";
            }, "Get current sensor reading value");

        // 注册 RPC URL - 带描述
        registerRpc("/sensor/set_config",
            [this](const std::string& url, const void* data,
                   size_t len, std::string& resp) {
                resp = R"({"result": "ok"})";
            }, "Update sensor configuration parameters");

        // 订阅其他节点发布的数据
        subscribe("/sensor/temperature",
            [](const std::string& url, const void* data, size_t len) {
                // 处理接收到的温度数据
                // 实际应用中可在此处处理订阅数据
            });

        std::cout << "MySensorNode initialized." << std::endl;
        return 0;
    }

    int svc() override {
        std::cout << "MySensorNode svc() started." << std::endl;
        while (isRunning()) {
            // 周期性发布传感器数据到 Topic
            publish("/sensor/data", R"({"temp": 25.5, "humidity": 60.0})");

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        std::cout << "MySensorNode svc() exited." << std::endl;
        return 0;
    }

    void OnShutdown() override {
        std::cout << "MySensorNode shutting down." << std::endl;
    }

    std::string GetServiceName() const override {
        return "SensorNode";
    }

    std::string GetServiceVersion() const override {
        return "1.0.0";
    }

    std::string GetServiceDescription() const override {
        return "VSOA Sensor Node Example";
    }
};

/**
 * @brief 程序入口
 *
 * 使用 VsoaNode::Run<MySensorNode>() 一行启动整个服务。
 * 框架自动处理 CLI 解析、VSOA 初始化、信号处理、优雅退出。
 */
int main(int argc, char* argv[]) {
    return VsoaNode::Run<MySensorNode>(argc, argv);
}
