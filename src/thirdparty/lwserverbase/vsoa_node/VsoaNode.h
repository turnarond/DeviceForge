/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * File: VsoaNode.h
 *
 * Date: 2026-04-29
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: VSOA Node core base class
 *
 * VsoaNode inherits from lwserverbase::core::ServiceTask, providing users with a "node"
 * thinking model for VSOA microservice development. Users only need to inherit VsoaNode
 * and implement OnInit() / svc() callbacks, and the framework automatically manages
 * VSOA server/client creation, URL management, health checks, log formatting, etc.
 *
 * Usage example:
 * ```cpp
 * class MyNode : public VsoaNode {
 * public:
 *     MyNode() : VsoaNode("config.json") {
 *         auto& cfg = getNodeConfig();
 *         cfg.serviceName = "my-node";
 *     }
 *     int OnInit() override {
 *         registerRpc("/api/hello", [](auto& url, auto* data, auto len, auto& resp) {
 *             resp = "{\"msg\": \"hello\"}";
 *         });
 *         return 0;
 *     }
 *     int svc() override {
 *         while (isRunning()) { publish("/data", "{}"); sleep(1); }
 *         return 0;
 *     }
 * };
 * int main(int argc, char* argv[]) {
 *     return VsoaNode::Run<MyNode>(argc, argv);
 * }
 * ```
 */

#pragma once

#ifdef _WIN32
#pragma push_macro("ERROR")
#undef ERROR
#endif

#include <core/ServiceTask.h>
#include <core/ServiceManager.h>

#include <vsoa_node/NodeConfig.h>
#include <vsoa_node/HealthState.h>

// 前置声明 platform_sdk 类型（避免 server.h/client.h 头文件泄漏到用户代码）
// 完整类型在 VsoaNode.cpp 中 include，unique_ptr 析构在 .cpp 中实例化
namespace vsoa_sdk {
namespace server { class ServerHandle; }
namespace client { class ClientHandle; }
}

#include <platform_sdk/master.h>  // srv_item_t 供 listNodes/discoverNode 返回值使用（仅 87 行）

#include <memory>
#include <string>
#include <map>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>

namespace lwserverbase {

namespace logging {
class Logger;
}

namespace vsoa_node {

// 前向声明
class LogFormatter;

/**
 * @brief RPC 回调类型
 * @param url 请求的 URL
 * @param data 请求数据指针
 * @param len 请求数据长度
 * @param response 响应字符串（由回调填充）
 */
using RpcCallback = std::function<void(const std::string& url,
    const void* data, size_t len, std::string& response)>;

/**
 * @brief 订阅回调类型
 * @param url 订阅的 URL
 * @param data 收到的数据指针
 * @param len 数据长度
 */
using SubscribeCallback = std::function<void(const std::string& url,
    const void* data, size_t len)>;

/**
 * @brief RPC 响应回调类型
 * @param status 状态码（0=成功）
 * @param url 请求的 URL
 * @param data 响应数据指针
 * @param len 响应数据长度
 */
using RpcResponseCallback = std::function<void(int status,
    const std::string& url, const void* data, size_t len)>;

/**
 * @brief 客户端连接回调类型
 * @param clientId 客户端 ID
 * @param connected true=连接建立, false=连接断开
 *
 * 在 VSOA 事件循环线程中调用，注意线程安全。
 */
using ClientConnectCallback = std::function<void(uint32_t clientId, bool connected)>;

/**
 * @brief 健康检查器回调类型
 * @return 健康状态
 */
using HealthChecker = std::function<HealthState()>;

/**
 * @class VsoaNode
 * @brief VSOA 节点基类
 *
 * 继承自 ServiceTask，集成 VSOA 通信能力。
 * 用户继承此类并实现 OnInit() / svc() / OnShutdown() 即可获得完整 VSOA 微服务。
 */
class VsoaNode : public core::ServiceTask {
public:
    /**
     * @brief 构造函数
     * @param configPath 配置文件路径（默认 "config.json"）
     *
     * 配置路径在构造时确定，优先于所有后续初始化步骤。
     * 用户也可在构造函数体中通过 getNodeConfig().configPath 覆盖。
     */
    explicit VsoaNode(const std::string& configPath = "config.json");

    /**
     * @brief 析构函数
     */
    virtual ~VsoaNode();

    // ==================== 框架入口 ====================

    /**
     * @brief 创建并运行节点
     * @tparam NodeT 节点类型（继承自 VsoaNode）
     * @param argc 命令行参数数量（保留，不再使用）
     * @param argv 命令行参数数组（保留，不再使用）
     * @return 进程退出码
     *
     * 生命周期：ServiceManager 初始化 -> OnStart -> 等待信号 -> OnStop -> 清理
     *
     * 注意：argc/argv 不再用于 CLI 解析。
     * 所有配置通过构造函数 + 配置文件（config.json）指定。
     */
    template <typename NodeT>
    static int Run(int argc, char** argv);

    // ==================== 用户可重写的回调 ====================

    /**
     * @brief 初始化回调
     * @return 0 表示成功，非 0 表示失败
     *
     * 在 VSOA 初始化完成后、spin 线程启动前调用。
     * 用户在此注册 RPC 处理函数、订阅 Topic 等。
     */
    virtual int OnInit();

    /**
     * @brief 业务主循环
     * @return 线程退出码
     *
     * 作为 ServiceTask 的线程入口在独立线程中运行。
     * 用户可在 while(isRunning()) 循环中执行业务逻辑。
     * VsoaNode 提供默认空实现，用户无需强制重写。
     */
    int svc() override;

    /**
     * @brief 关闭回调
     *
     * 在收到退出信号后、VSOA 资源清理前调用。
     * 用户在此释放业务资源。
     */
    virtual void OnShutdown();

    // ==================== 高层 VSOA API ====================

    /**
     * @brief 注册 RPC URL 处理函数
     * @param url RPC URL
     * @param callback 处理回调
     * @return true 表示注册成功
     */
    bool registerRpc(const std::string& url, RpcCallback callback);

    /**
     * @brief 注册 RPC URL 处理函数（带描述）
     * @param url RPC URL
     * @param callback 处理回调
     * @param desc URL 描述（用于 /help 输出）
     * @return true 表示注册成功
     */
    bool registerRpc(const std::string& url, RpcCallback callback, const std::string& desc);

    /**
     * @brief 发布 Topic 数据（字符串重载）
     * @param url Topic URL
     * @param data 字符串数据
     * @return true 表示发布成功
     */
    bool publish(const std::string& url, const std::string& data);

    /**
     * @brief 发布 Topic 数据（二进制重载）
     * @param url Topic URL
     * @param data 数据指针
     * @param len 数据长度
     * @return true 表示发布成功
     */
    bool publish(const std::string& url, const void* data, size_t len);

    /**
     * @brief 订阅 Topic
     * @param url Topic URL
     * @param callback 数据接收回调
     * @return true 表示订阅成功
     *
     * 首次调用时会延迟创建 ClientHandle。
     */
    bool subscribe(const std::string& url, SubscribeCallback callback);

    /**
     * @brief 异步调用远程 RPC
     * @param serverName 目标服务名
     * @param url RPC URL
     * @param data 请求数据指针
     * @param len 请求数据长度
     * @param callback 响应回调
     * @param timeoutMs 超时时间（毫秒，默认 3000）
     * @return true 表示调用发起成功
     */
    bool callRpc(const std::string& serverName, const std::string& url,
                 const void* data, size_t len,
                 RpcResponseCallback callback, unsigned int timeoutMs = 3000);

    /**
     * @brief 同步调用远程 RPC
     * @param serverName 目标服务名
     * @param url RPC URL
     * @param data 请求数据指针
     * @param len 请求数据长度
     * @param timeoutMs 超时时间（毫秒，默认 3000）
     * @return pair<status, response_data> 状态码和响应数据
     */
    std::pair<int, std::vector<char>> callRpcSync(
        const std::string& serverName, const std::string& url,
        const void* data, size_t len,
        unsigned int timeoutMs = 3000);

    // ==================== 客户端连接回调 ====================

    /**
     * @brief 设置客户端连接/断开回调
     * @param callback 客户端连接回调（cid, connected）
     *
     * 在客户端连接建立或断开时触发。回调在 VSOA 事件循环线程中执行。
     *
     * 调用时机：
     * - 构造函数体内：通过 getNodeConfig().xxx 设置配置后调用
     * - OnInit() 内：ServerHandle 已创建，但 spin 尚未启动，推荐位置
     *
     * 注意：如果回调中需要访问 VsoaNode 成员，注意线程安全。
     */
    void setOnClientConnect(ClientConnectCallback callback);

    // ==================== 健康检查 ====================

    /**
     * @brief 注册自定义健康检查器
     * @param checker 健康检查回调
     */
    void registerHealthChecker(HealthChecker checker);

    // ==================== 节点发现 ====================

    /**
     * @brief 列出所有已注册节点
     * @return 节点信息列表
     */
    std::vector<vsoa_sdk::master::srv_item_t> listNodes();

    /**
     * @brief 发现指定名称的节点
     * @param name 节点服务名
     * @return 节点信息（如果未找到，server_name 为空）
     */
    vsoa_sdk::master::srv_item_t discoverNode(const std::string& name);

    // ==================== 查询方法 ====================

    /**
     * @brief 获取节点名称
     * @return 节点名称
     */
    std::string getNodeName() const;

    /**
     * @brief 获取节点版本号
     * @return 版本号
     */
    std::string getNodeVersion() const;

    /**
     * @brief 获取节点配置引用
     * @return 节点配置
     */
    NodeConfig& getNodeConfig();

    /**
     * @brief 获取服务管理器指针
     * @return ServiceManager 指针
     */
    core::ServiceManager* getServiceManager() const;

protected:
    // ==================== ServiceBase 生命周期重写 ====================

    /**
     * @brief 服务启动（由 ServiceManager 调用）
     * @param argc 命令行参数数量
     * @param argv 命令行参数数组
     * @return 0 表示成功
     *
     * 执行完整的 VSOA 初始化流程：
     * 配置覆盖 -> 日志格式化 -> VSOA SDK 初始化
     * -> ServerHandle 创建 -> 内置 URL 注册 -> 用户 OnInit -> spin 线程 -> svc 线程
     */
    int OnStart(int argc, char* argv[]) final;

    /**
     * @brief 服务停止（由 ServiceManager 调用）
     *
     * 执行优雅关闭流程：
     * 停止 spin 线程 -> OnShutdown -> 销毁 ServerHandle/ClientHandle -> VSOA SDK 关闭
     */
    void OnStop() final;

    /**
     * @brief 健康检查（由 ServiceManager 触发）
     */
    void OnHealthCheck() override;

    /**
     * @brief 处理控制命令
     * @param cmd 命令名称
     * @param args 命令参数
     * @param resp 命令响应
     * @return true 表示已处理
     */
    bool OnCommand(const std::string& cmd, const std::string& args,
                   std::string& resp) override;

    /**
     * @brief 获取服务名称
     * @return 服务名称
     */
    std::string GetServiceName() const override;

    /**
     * @brief 获取服务版本
     * @return 服务版本
     */
    std::string GetServiceVersion() const override;

    /**
     * @brief 获取服务描述
     * @return 服务描述
     */
    std::string GetServiceDescription() const override;

private:
    // ==================== 内部方法 ====================

    /**
     * @brief 将 NodeConfig 字段写入 ConfigManager（如果配置文件中存在对应 key）
     */
    void applyConfigOverrides();

    /**
     * @brief 初始化 VSOA SDK
     * @return true 表示成功
     */
    bool initVsoaSdk();

    /**
     * @brief 创建 ServerHandle
     * @return true 表示成功
     */
    bool createServerHandle();

    /**
     * @brief 注册内置 URL 处理器到 ServerHandle
     */
    void registerBuiltinHandlers();

    /**
     * @brief 确保 ClientHandle 已创建
     *
     * 延迟创建 ClientHandle，仅当有 subscribe/callRpc 调用时创建。
     */
    void ensureClientHandle();

    /**
     * @brief 关闭 VSOA 资源
     */
    void shutdownVsoa();

    // ==================== 内置 URL 处理 ====================

    void onHealthRequest(const std::string& url, const void* data,
                         size_t len, std::string& response);
    void onVersionRequest(const std::string& url, const void* data,
                          size_t len, std::string& response);
    void onConfigRequest(const std::string& url, const void* data,
                         size_t len, std::string& response);
    void onMetricsRequest(const std::string& url, const void* data,
                          size_t len, std::string& response);
    void onCommandRequest(const std::string& url, const void* data,
                          size_t len, std::string& response);
    void onHelpRequest(const std::string& url, const void* data,
                       size_t len, std::string& response);
    void onUrlsRequest(const std::string& url, const void* data,
                       size_t len, std::string& response);

    // ==================== 成员变量 ====================

    NodeConfig m_config;                                                ///< 节点配置
    std::unique_ptr<LogFormatter> m_logFormatter;                       ///< 日志格式化器
    std::unique_ptr<vsoa_sdk::server::ServerHandle> m_serverHandle;     ///< VSOA 服务端句柄
    std::unique_ptr<vsoa_sdk::client::ClientHandle> m_clientHandle;     ///< VSOA 客户端句柄
    std::map<std::string, void*> m_clientContexts; ///< 客户端上下文映射（实际类型 client::CLIENT_CONTEXT，void* 避免头文件依赖）

    ClientConnectCallback m_clientConnectCallback;       ///< 客户端连接/断开回调
    std::vector<HealthChecker> m_healthCheckers;        ///< 健康检查器列表
    std::map<std::string, std::string> m_urlDescriptions; ///< URL 描述映射（用于 /help）
    std::vector<std::string> m_rpcUrls;                  ///< RPC URL 列表（用于 /urls、/help）
    std::vector<std::string> m_publishUrls;              ///< Publisher URL 列表（用于 /urls、/help）

    std::chrono::steady_clock::time_point m_startTime;  ///< 节点启动时间

    bool m_vsoaSdkInitialized = false; ///< VSOA SDK 初始化状态
    bool m_serverCreated = false;      ///< ServerHandle 创建状态
    bool m_clientCreated = false;      ///< ClientHandle 创建状态
};

// ==================== 模板实现 ====================

template <typename NodeT>
int VsoaNode::Run(int argc, char** argv) {
    auto node = std::make_unique<NodeT>();
    auto serviceManager = std::make_unique<core::ServiceManager>();
    node->SetServiceManager(serviceManager.get());
    serviceManager->SetService(node.get());
    serviceManager->SetConfigPath(node->getNodeConfig().configPath);
    return serviceManager->RunService(argc, argv);
}

} // namespace vsoa_node
} // namespace lwserverbase

#ifdef _WIN32
#pragma pop_macro("ERROR")
#endif
