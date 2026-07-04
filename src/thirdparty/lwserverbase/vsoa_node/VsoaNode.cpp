/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * File: VsoaNode.cpp
 *
 * Date: 2026-04-29
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: VSOA Node core base class implementation
 */

#ifdef _WIN32
#pragma push_macro("ERROR")
#undef ERROR
#endif

#include <vsoa_node/VsoaNode.h>
#include <vsoa_node/LogFormatter.h>

#include <core/ServiceManager.h>
#include <config/ConfigManager.h>
#include <logging/Logger.h>
#include <metrics/MetricsCollector.h>
#include <process/ProcessController.h>

#include <platform_sdk/server.h>
#include <platform_sdk/client.h>
#include <platform_sdk/master.h>

// VSOA SDK static member definitions (not exported by prebuilt libs)
namespace vsoa {
    const Status Status::CODE_0(0, "Success");
    const Status Status::CODE_1(1, "Continue");
    const Status Status::CODE_2(2, "Invalid arguments");
    const Status Status::CODE_3(3, "Invalid url");
    const Status Status::CODE_4(4, "No respond");
    const Status Status::CODE_5(5, "No permissions");
    const Status Status::CODE_6(6, "No memory");
}
#include <platform_sdk/master_cli.h>
#include <platform_sdk/init.h>
#include "nlohmann/json.hpp"

#include <iostream>
#include <cstring>
#include <sstream>

namespace lwserverbase {
namespace vsoa_node {

// ==================== 构造 / 析构 ====================

VsoaNode::VsoaNode(const std::string& configPath)
    : core::ServiceTask()
    , m_startTime(std::chrono::steady_clock::now()) {
    m_config.configPath = configPath;
}

VsoaNode::~VsoaNode() {
    if (m_serverCreated || m_vsoaSdkInitialized) {
        shutdownVsoa();
    }
}

// ==================== 用户回调默认实现 ====================

int VsoaNode::OnInit() {
    return 0;
}

int VsoaNode::svc() {
    // 默认空实现：用户无需强制重写 svc()
    // 仅等待 isRunning() 变为 false
    while (isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
}

void VsoaNode::OnShutdown() {
    // 默认空实现
}

// ==================== 生命周期 ====================

int VsoaNode::OnStart(int argc, char* argv[]) {
    (void)argc;  // 不再使用命令行参数
    (void)argv;

    // ---- 步骤 1: 从已加载的配置中覆盖 NodeConfig 字段 ----
    // 配置文件已在 ServiceManager::Initialize 中通过 SetConfigPath 指定的路径加载。
    // 此处仅读取已加载的配置值，不触发二次 I/O。
    applyConfigOverrides();

    // ---- 步骤 2: 配置验证 ----
    auto validation = m_config.validate();
    if (!validation.ok) {
        auto* logger = GetServiceManager() ? GetServiceManager()->GetLogger() : nullptr;
        if (logger) {
            logger->Log(logging::Logger::Level::ERROR,
                        std::string("NodeConfig validation failed: ") + validation.error);
        }
        return -1;
    }

    // ---- 步骤 3: 注册配置热更新回调 ----
    auto* configMgr = GetServiceManager()->GetConfigManager();
    if (configMgr) {
        configMgr->SetReloadCallback([this]() { OnReloadConfig(); });
    }

    // ---- 步骤 4: 设置日志格式 ----
    auto* logger = GetServiceManager()->GetLogger();
    if (logger) {
        m_logFormatter = std::make_unique<LogFormatter>(logger, getNodeName());
        logger->Log(logging::Logger::Level::INFO, "Node starting: " + getNodeName() +
                    " v" + getNodeVersion());
    }

    // ---- 步骤 5: 初始化 VSOA SDK ----
    if (!initVsoaSdk()) {
        if (logger) {
            logger->Log(logging::Logger::Level::ERROR, "Failed to initialize VSOA SDK");
        }
        return -1;
    }

    // ---- 步骤 6: 创建 ServerHandle ----
    if (!createServerHandle()) {
        if (logger) {
            logger->Log(logging::Logger::Level::ERROR, "Failed to create server handle");
        }
        return -1;
    }

    // ---- 步骤 7: 注册内置 URL 处理器 ----
    registerBuiltinHandlers();

    // ---- 步骤 8: 调用用户回调 OnInit ----
    if (OnInit() != 0) {
        if (logger) {
            logger->Log(logging::Logger::Level::ERROR, "User OnInit() failed");
        }
        return -1;
    }

    // ---- 步骤 9: 启动 VSOA 事件循环（SrvSpinAsync 内部创建后台线程） ----
    m_serverHandle->SrvSpinAsync(m_config.vsoaSpinIntervalMs);

    // ---- 步骤 10: 设置运行标志并启动 svc 线程 ----
    startRunning();
    activate(1);

    // ---- 步骤 11: 记录启动时间 ----
    m_startTime = std::chrono::steady_clock::now();

    if (logger) {
        logger->Log(logging::Logger::Level::INFO, "Node started successfully: " + getNodeName());
    }

    return 0;
}

void VsoaNode::OnStop() {
    auto* logger = GetServiceManager() ? GetServiceManager()->GetLogger() : nullptr;

    if (logger) {
        logger->Log(logging::Logger::Level::INFO, "Node stopping: " + getNodeName());
        logger->Debug("[VSOA] VsoaNode::OnStop: start");
    }

    // ---- 步骤 1: 调用用户 OnShutdown（svc 线程仍在运行） ----
    if (logger) logger->Debug("[VSOA] VsoaNode::OnStop: calling OnShutdown()...");
    OnShutdown();
    if (logger) logger->Debug("[VSOA] VsoaNode::OnStop: OnShutdown() returned");

    // ---- 步骤 2: 停止 svc 线程 ----
    if (logger) logger->Debug("[VSOA] VsoaNode::OnStop: stopping svc thread...");
    ServiceTask::OnStop();
    if (logger) logger->Debug("[VSOA] VsoaNode::OnStop: svc thread stopped");

    // ---- 步骤 3: 关闭 VSOA 资源 ----
    // DestroyServer 内部设置 stop_cmd，SrvSpinAsync 的后台线程检测到后自动退出
    if (logger) logger->Debug("[VSOA] VsoaNode::OnStop: shutdownVsoa()...");
    shutdownVsoa();
    if (logger) logger->Debug("[VSOA] VsoaNode::OnStop: shutdownVsoa() returned");

    if (logger) {
        logger->Log(logging::Logger::Level::INFO, "Node shutdown complete: " + getNodeName());
    }
}

// ==================== 配置覆盖 ====================

void VsoaNode::applyConfigOverrides() {
    auto* configMgr = GetServiceManager()->GetConfigManager();
    if (!configMgr) return;

    m_config.serviceName       = configMgr->Get("node.service_name",       m_config.serviceName);
    m_config.serverHost        = configMgr->Get("node.server_host",        m_config.serverHost);
    int port                   = configMgr->Get("node.server_port",        static_cast<int>(m_config.serverPort));
    m_config.serverPort        = static_cast<uint16_t>(port);
    m_config.shutdownTimeoutMs = configMgr->Get("node.shutdown_timeout_ms", m_config.shutdownTimeoutMs);
    m_config.vsoaSpinIntervalMs= configMgr->Get("node.vsoa_spin_interval_ms", m_config.vsoaSpinIntervalMs);
    m_config.encoding          = configMgr->Get("node.encoding",           m_config.encoding);
    m_config.dataMode          = configMgr->Get("node.data_mode",          m_config.dataMode);
    m_config.daemonMode        = configMgr->Get("node.daemon_mode",         m_config.daemonMode);
    m_config.serverPassword    = configMgr->Get("node.server_password",     m_config.serverPassword);
    m_config.description       = configMgr->Get("node.description",         m_config.description);
}

// ==================== 健康检查 ====================

void VsoaNode::OnHealthCheck() {
    // 默认健康检查 - 由 /health 处理器实现
}

bool VsoaNode::OnCommand(const std::string& cmd, const std::string& /*args*/,
                          std::string& resp) {
    if (cmd == "status") {
        resp = "Node: " + getNodeName() + " v" + getNodeVersion();
        return true;
    }
    return false;
}

// ==================== 服务信息 ====================

std::string VsoaNode::GetServiceName() const {
    return m_config.serviceName;
}

std::string VsoaNode::GetServiceVersion() const {
    return m_config.version;
}

std::string VsoaNode::GetServiceDescription() const {
    return m_config.description;
}

// ==================== 高层 VSOA API ====================

bool VsoaNode::registerRpc(const std::string& url, RpcCallback callback) {
    return registerRpc(url, std::move(callback), "");
}

bool VsoaNode::registerRpc(const std::string& url, RpcCallback callback, const std::string& desc) {
    if (!m_serverHandle) {
        auto* logger = GetServiceManager() ? GetServiceManager()->GetLogger() : nullptr;
        if (logger) {
            logger->Log(logging::Logger::Level::ERROR,
                        "registerRpc failed: server handle not created");
        }
        return false;
    }

    if (!desc.empty()) {
        m_urlDescriptions[url] = desc;
    }

    // 去重记录 RPC URL
    if (std::find(m_rpcUrls.begin(), m_rpcUrls.end(), url) == m_rpcUrls.end()) {
        m_rpcUrls.push_back(url);
    }

    auto dispatch = [this, cb = std::move(callback)]
        (vsoa_sdk::server::CliRpcInfo& cli_info,
         const void* dto, size_t len, void* /*arg*/) {
        std::string response;
        cb(cli_info.url, dto, len, response);
        m_serverHandle->SrvResponse(cli_info, vsoa::Status::CODE_0, response);
    };

    m_serverHandle->AddRpcListener(url, dispatch, nullptr);
    return true;
}

bool VsoaNode::publish(const std::string& url, const std::string& data) {
    if (!m_serverHandle) return false;

    // 去重记录 publisher URL
    if (std::find(m_publishUrls.begin(), m_publishUrls.end(), url) == m_publishUrls.end()) {
        m_publishUrls.push_back(url);
    }

    return m_serverHandle->Publish(url, data) == 0;
}

bool VsoaNode::publish(const std::string& url, const void* data, size_t len) {
    if (!m_serverHandle) return false;

    // 去重记录 publisher URL
    if (std::find(m_publishUrls.begin(), m_publishUrls.end(), url) == m_publishUrls.end()) {
        m_publishUrls.push_back(url);
    }

    std::string urlCopy(url);
    return m_serverHandle->Publish(std::move(urlCopy), data, len) == 0;
}

bool VsoaNode::subscribe(const std::string& url, SubscribeCallback callback) {
    ensureClientHandle();
    if (!m_clientHandle) return false;

    auto cb = [callback = std::move(callback)](const std::string& subUrl, const void* dto,
                                                size_t len, void* arg) mutable {
        (void)arg;
        callback(subUrl, dto, len);
    };

    int ret = m_clientHandle->Subscribe(url, cb, nullptr);
    return ret == 0;
}

bool VsoaNode::callRpc(const std::string& serverName, const std::string& url,
                       const void* data, size_t len,
                       RpcResponseCallback callback, unsigned int timeoutMs) {
    ensureClientHandle();
    if (!m_clientHandle) return false;

    // 查找或创建对应服务的客户端上下文
    auto it = m_clientContexts.find(serverName);
    if (it == m_clientContexts.end()) {
        vsoa_sdk::client::SrvConnInfo connInfo;
        connInfo.server_name = serverName;
        auto ctx = m_clientHandle->CreateClient(connInfo);
        if (!ctx) return false;
        it = m_clientContexts.emplace(serverName, ctx).first;
    }

    // 使用 shared_ptr 管理回调生命周期，避免超时等场景下的泄漏
    auto cbPtr = std::make_shared<RpcResponseCallback>(std::move(callback));
    auto cb = [cbPtr](const vsoa::Status& status, const std::string& rpcUrl,
                      const void* dto, size_t dtoLen, void* /*arg*/) {
        (*cbPtr)(status.code, rpcUrl, dto, dtoLen);
    };

    auto ctx = static_cast<vsoa_sdk::client::CLIENT_CONTEXT>(it->second);
    std::string urlCopy(url);
    return m_clientHandle->Call(ctx, urlCopy, data, len, cb, nullptr, timeoutMs) == 0;
}

std::pair<int, std::vector<char>> VsoaNode::callRpcSync(
    const std::string& serverName, const std::string& url,
    const void* data, size_t len,
    unsigned int timeoutMs) {
    ensureClientHandle();

    std::pair<int, std::vector<char>> result;
    result.first = -1;

    if (!m_clientHandle) return result;

    // 查找或创建对应服务的客户端上下文
    auto it = m_clientContexts.find(serverName);
    if (it == m_clientContexts.end()) {
        vsoa_sdk::client::SrvConnInfo connInfo;
        connInfo.server_name = serverName;
        auto ctx = m_clientHandle->CreateClient(connInfo);
        if (!ctx) return result;
        it = m_clientContexts.emplace(serverName, ctx).first;
    }

    // 使用 CallSyncEx 获取 RAII 管理的响应
    auto ctx = static_cast<vsoa_sdk::client::CLIENT_CONTEXT>(it->second);
    auto syncResult = m_clientHandle->CallSyncEx(ctx, url, data, len,
                                                  timeoutMs);
    result.first = syncResult.first;

    if (syncResult.first == 0 && syncResult.second.has_data()) {
        auto* respData = static_cast<const char*>(syncResult.second.data());
        size_t respLen = syncResult.second.len();
        result.second.assign(respData, respData + respLen);
    }

    return result;
}

// ==================== 客户端连接回调 ====================

void VsoaNode::setOnClientConnect(ClientConnectCallback callback) {
    if (!callback) return;

    m_clientConnectCallback = std::move(callback);

    // 如果 ServerHandle 已创建，立即桥接到底层
    if (m_serverHandle) {
        auto wrappedCb = [this](vsoa_sdk::server::ServerHandle* /*server*/,
                                 uint32_t cid, bool connect, void* /*arg*/) {
            if (m_clientConnectCallback) {
                m_clientConnectCallback(cid, connect);
            }
        };
        m_serverHandle->SetOnConnectCb(wrappedCb, nullptr);
    }
}

// ==================== 健康检查 ====================

void VsoaNode::registerHealthChecker(HealthChecker checker) {
    if (checker) {
        m_healthCheckers.push_back(std::move(checker));
    }
}

// ==================== 节点发现 ====================

std::vector<vsoa_sdk::master::srv_item_t> VsoaNode::listNodes() {
    std::vector<vsoa_sdk::master::srv_item_t> nodes;

    // 使用 MasterCli API 获取服务列表
    try {
        std::list<std::string> srvNames;
        if (MasterCli::GetInstance()->GetSrvList(srvNames) == 0 && !srvNames.empty()) {
            std::list<vsoa_sdk::master::srv_item_t> srvItems;
            if (MasterCli::GetInstance()->GetSrvInfoByName(srvNames, srvItems) == 0) {
                nodes.assign(srvItems.begin(), srvItems.end());
            }
        }
    } catch (const std::exception& e) {
        auto* logger = GetServiceManager() ? GetServiceManager()->GetLogger() : nullptr;
        if (logger) {
            logger->Log(logging::Logger::Level::WARN,
                        std::string("listNodes() failed: ") + e.what());
        }
    }

    return nodes;
}

vsoa_sdk::master::srv_item_t VsoaNode::discoverNode(const std::string& name) {
    vsoa_sdk::master::srv_item_t result;

    // 在节点列表中查找指定名称的节点
    auto nodes = listNodes();
    for (const auto& node : nodes) {
        if (node.server_name == name) {
            result = node;
            break;
        }
    }

    return result;
}

// ==================== 查询方法 ====================

std::string VsoaNode::getNodeName() const {
    return m_config.serviceName;
}

std::string VsoaNode::getNodeVersion() const {
    return m_config.version;
}

NodeConfig& VsoaNode::getNodeConfig() {
    return m_config;
}

core::ServiceManager* VsoaNode::getServiceManager() const {
    return GetServiceManager();
}

// ==================== 内部方法 ====================

bool VsoaNode::initVsoaSdk() {
    if (m_vsoaSdkInitialized) return true;

    vsoa_sdk::init(getNodeName());
    vsoa_sdk::master::init();

    vsoa_sdk::setDataMode(static_cast<DataMode>(m_config.dataMode));

    m_vsoaSdkInitialized = true;
    return true;
}

bool VsoaNode::createServerHandle() {
    if (m_serverCreated) return true;

    try {
        // 创建 ServerHandle，使用服务名作为 server host (SH)
        m_serverHandle = std::make_unique<vsoa_sdk::server::ServerHandle>(
            m_config.serviceName);

        if (m_config.serverPort > 0) {
            m_serverHandle->SetServerPort(m_config.serverPort);
        }

        // 创建服务器
        if (m_serverHandle->CreateServer() != 0) {
            auto* logger = GetServiceManager() ? GetServiceManager()->GetLogger() : nullptr;
            if (logger) {
                logger->Log(logging::Logger::Level::ERROR,
                            "ServerHandle::CreateServer() failed");
            }
            return false;
        }

        m_serverCreated = true;

        // 如果端口是自动分配的，获取实际端口
        if (m_config.serverPort == 0) {
            m_config.serverPort = m_serverHandle->GetServerPort();
        }

        return true;
    } catch (const std::exception& e) {
        auto* logger = GetServiceManager() ? GetServiceManager()->GetLogger() : nullptr;
        if (logger) {
            logger->Log(logging::Logger::Level::ERROR,
                        std::string("ServerHandle creation failed: ") + e.what());
        }
        return false;
    }
}

void VsoaNode::registerBuiltinHandlers() {
    if (!m_serverHandle) return;

    // 预填充内置 URL 列表
    m_rpcUrls = {"/health", "/version", "/config", "/metrics", "/cmd", "/help", "/urls"};

    auto addHandler = [this](const std::string& url,
        void (VsoaNode::*onRequest)(const std::string&, const void*, size_t, std::string&)) {
        m_serverHandle->AddRpcListener(url,
            [this, onRequest](vsoa_sdk::server::CliRpcInfo& cli,
                              const void* dto, size_t len, void* /*arg*/) {
                std::string response;
                (this->*onRequest)(cli.url, dto, len, response);
                m_serverHandle->SrvResponse(cli, vsoa::Status::CODE_0, response);
            }, nullptr);
    };

    addHandler("/health",  &VsoaNode::onHealthRequest);
    addHandler("/version", &VsoaNode::onVersionRequest);
    addHandler("/config",  &VsoaNode::onConfigRequest);
    addHandler("/metrics", &VsoaNode::onMetricsRequest);
    addHandler("/cmd",     &VsoaNode::onCommandRequest);
    addHandler("/help",    &VsoaNode::onHelpRequest);
    addHandler("/urls",    &VsoaNode::onUrlsRequest);
}

void VsoaNode::ensureClientHandle() {
    if (m_clientCreated) return;

    try {
        m_clientHandle = std::make_unique<vsoa_sdk::client::ClientHandle>();
        m_clientCreated = true;
    } catch (const std::exception& e) {
        auto* logger = GetServiceManager() ? GetServiceManager()->GetLogger() : nullptr;
        if (logger) {
            logger->Log(logging::Logger::Level::ERROR,
                        std::string("ClientHandle creation failed: ") + e.what());
        }
    }
}

void VsoaNode::shutdownVsoa() {
    if (m_serverCreated && m_serverHandle) {
        m_serverHandle->DestroyServer();
        m_serverHandle.reset();
        m_serverCreated = false;
    }

    if (m_clientCreated && m_clientHandle) {
        // 销毁所有客户端上下文
        for (auto& [name, ctx] : m_clientContexts) {
            m_clientHandle->DestroyClient(
                static_cast<vsoa_sdk::client::CLIENT_CONTEXT>(ctx));
        }
        m_clientContexts.clear();

        m_clientHandle->DestroyClient();
        m_clientHandle.reset();
        m_clientCreated = false;
    }

    if (m_vsoaSdkInitialized) {
        vsoa_sdk::shutdown();
        m_vsoaSdkInitialized = false;
    }
}

// ==================== 内置 URL 处理 ====================

void VsoaNode::onHealthRequest(const std::string& /*url*/, const void* /*data*/,
                                size_t /*len*/, std::string& response) {
    auto uptimeSec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - m_startTime).count();

    OnHealthCheck();

    HealthState state;
    for (auto& checker : m_healthCheckers) {
        auto checkResult = checker();
        if (!checkResult.isHealthy())
            state.overallStatus = HealthState::Status::UNHEALTHY;
        for (const auto& [key, value] : checkResult.checkResults)
            state.checkResults[key] = value;
    }
    state.checkResults["uptime"] = std::to_string(uptimeSec) + "s";
    state.checkResults["service"] = getNodeName();

    nlohmann::json j;
    j["status"]     = state.statusToString();
    j["uptime_sec"] = uptimeSec;
    j["service"]    = getNodeName();
    j["checks"]     = state.checkResults;
    response = j.dump();
}

void VsoaNode::onVersionRequest(const std::string& /*url*/, const void* /*data*/,
                                 size_t /*len*/, std::string& response) {
    nlohmann::json j;
    j["service"]     = getNodeName();
    j["version"]     = getNodeVersion();
    j["description"] = GetServiceDescription();
    j["build_time"]  = std::string(__DATE__) + " " + __TIME__;
    response = j.dump();
}

void VsoaNode::onConfigRequest(const std::string& /*url*/, const void* /*data*/,
                                size_t /*len*/, std::string& response) {
    auto* configMgr = GetServiceManager() ? GetServiceManager()->GetConfigManager() : nullptr;
    if (!configMgr) {
        response = "{\"error\":\"config not available\"}";
        return;
    }

    nlohmann::json node;
    node["service_name"]         = m_config.serviceName;
    node["version"]              = m_config.version;
    node["server_host"]          = m_config.serverHost;
    node["server_port"]          = m_config.serverPort;
    node["encoding"]             = m_config.encoding;
    node["shutdown_timeout_ms"]  = m_config.shutdownTimeoutMs;
    node["vsoa_spin_interval_ms"]= m_config.vsoaSpinIntervalMs;
    node["data_mode"]            = m_config.dataMode;
    node["daemon_mode"]          = m_config.daemonMode;

    nlohmann::json j;
    j["node"] = node;
    response = j.dump();
}

void VsoaNode::onMetricsRequest(const std::string& /*url*/, const void* /*data*/,
                                 size_t /*len*/, std::string& response) {
    auto* mc = GetServiceManager() ?
        GetServiceManager()->GetMetricsCollector() : nullptr;
    if (!mc) { response = "{\"metrics\":[]}"; return; }

    auto entries = mc->GetAllMetrics();
    nlohmann::json arr = nlohmann::json::array();

    for (auto& e : entries) {
        nlohmann::json item;
        item["name"] = e.name;
        item["help"] = e.help;
        item["type"] = e.type;

        if (e.type == "histogram") {
            item["count"] = e.count;
            item["sum"]   = e.sum;
            nlohmann::json buckets = nlohmann::json::array();
            for (auto& b : e.buckets) {
                nlohmann::json bkt;
                bkt["le"]    = b.first;
                bkt["count"] = b.second;
                buckets.push_back(bkt);
            }
            item["buckets"] = buckets;
        } else {
            item["value"] = e.value;
        }
        arr.push_back(item);
    }

    nlohmann::json j;
    j["metrics"] = arr;
    response = j.dump();
}

void VsoaNode::onCommandRequest(const std::string& url, const void* data,
                                  size_t len, std::string& response) {
    (void)url;
    std::string cmd, args;
    if (data && len > 0) {
        try {
            auto j = nlohmann::json::parse(
                std::string(static_cast<const char*>(data), len));
            if (j.contains("cmd"))  cmd  = j["cmd"].get<std::string>();
            if (j.contains("args")) args = j["args"].get<std::string>();
        } catch (const nlohmann::json::exception&) { /* invalid json */ }
    }
    if (!OnCommand(cmd, args, response)) {
        response = "{\"error\":\"unknown command\"}";
    }
}

void VsoaNode::onHelpRequest(const std::string& /*url*/, const void* /*data*/,
                               size_t /*len*/, std::string& response) {
    nlohmann::json j;
    j["service"]     = getNodeName();
    j["version"]     = getNodeVersion();
    j["description"] = GetServiceDescription();

    nlohmann::json endpoints = nlohmann::json::array();
    for (const auto& url : m_rpcUrls) {
        nlohmann::json ep;
        ep["url"]    = url;
        ep["method"] = "GET/SET";
        auto it = m_urlDescriptions.find(url);
        ep["desc"]   = (it != m_urlDescriptions.end()) ? it->second : "";
        endpoints.push_back(ep);
    }
    j["endpoints"] = endpoints;

    nlohmann::json topics = nlohmann::json::array();
    for (const auto& url : m_publishUrls) {
        nlohmann::json tp;
        tp["url"] = url;
        auto it = m_urlDescriptions.find(url);
        tp["desc"] = (it != m_urlDescriptions.end()) ? it->second : "";
        topics.push_back(tp);
    }
    j["topics"] = topics;

    response = j.dump();
}

void VsoaNode::onUrlsRequest(const std::string& /*url*/, const void* /*data*/,
                               size_t /*len*/, std::string& response) {
    nlohmann::json j;
    j["service"]    = getNodeName();
    j["listeners"]  = m_rpcUrls;
    j["publishers"] = m_publishUrls;
    response = j.dump();
}

} // namespace vsoa_node
} // namespace lwserverbase

#ifdef _WIN32
#pragma pop_macro("ERROR")
#endif
