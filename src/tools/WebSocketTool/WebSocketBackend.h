/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * File: WebSocketBackend.h
 *
 * Date: 2026-07-05
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: WebSocket Tool 后端 — 继承 ToolBackend，直接管理 QWebSocketServer/QWebSocket，
 *              支持 Server（监听多客户端 + 主题订阅）和 Client 两种模式。
 */

#pragma once
#include "framework/ToolBackend.h"
#include <QWebSocketServer>
#include <QWebSocket>
#include <QMutex>
#include <QMap>
#include <QList>
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <atomic>

class WebSocketBackend : public ToolBackend {
public:
    WebSocketBackend();
    ~WebSocketBackend() override;

    // --- ServiceTask 线程入口（纯虚实现） ---
    int svc() override;

    // --- ToolBackend 纯虚实现 ---
    std::string toolId() const override { return "com.deploymaster.websocket.comm"; }
    std::string toolName() const override { return "WebSocket 通信"; }
    std::string toolVersion() const override { return "2.0.0"; }
    std::string toolCategory() const override { return "communication"; }
    std::string toolIcon() const override { return "websocket_comm"; }

    void bindDevices(const std::vector<DeviceInfo>& devices) override;
    void bindCredentials(const AuthInfo& auth) override;
    void applyConfig(const lwserverbase::config::ConfigValue& config) override;

    // --- WebSocket 操作 ---

    // Server 模式
    void startServer(int port, bool sslMode = false);
    void stopServer();

    // Client 模式
    void startClient(const std::string& url);
    void stopClient();

    // 发布/订阅（Client 模式发送订阅/发布协议消息）
    void subscribe(const std::string& topic);
    void publish(const std::string& topic, const std::string& message);

    // 状态查询
    bool isRunning() const { return m_isRunning; }
    bool isServerMode() const { return m_isServerMode; }

    // 回调设置（由 Widget 调用，跨线程安全）
    using LogCallback = std::function<void(const std::string&)>;
    using MessageCallback = std::function<void(const std::string&)>;
    using ClientCallback = std::function<void(const std::string& clientInfo)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    void setLogCallback(LogCallback cb)       { m_logCb = std::move(cb); }
    void setMessageCallback(MessageCallback cb) { m_messageCb = std::move(cb); }
    void setClientConnectCallback(ClientCallback cb)  { m_clientConnectCb = std::move(cb); }
    void setClientDisconnectCallback(ClientCallback cb) { m_clientDisconnectCb = std::move(cb); }
    void setErrorCallback(ErrorCallback cb)   { m_errorCb = std::move(cb); }

private:
    // Server 内部槽（直接绑定 QWebSocketServer/QWebSocket 信号）
    void onNewConnection();
    void onServerTextMessage(const QString& message, QWebSocket* client);
    void onServerClientDisconnected(QWebSocket* client);

    // Client 内部槽
    void onClientConnected();
    void onClientTextMessage(const QString& message);
    void onClientDisconnected();
    void onClientError(QAbstractSocket::SocketError error);

    // 内部辅助
    void serverPublishToSubscribers(const QString& topic, const QString& message);

    // 回调
    LogCallback    m_logCb;
    MessageCallback m_messageCb;
    ClientCallback  m_clientConnectCb;
    ClientCallback  m_clientDisconnectCb;
    ErrorCallback   m_errorCb;

    // --- Server 模式 ---
    QWebSocketServer* m_server = nullptr;
    QList<QWebSocket*> m_clients;
    QMutex m_clientsMutex;
    // 主题 -> 订阅客户端列表
    QMap<QString, QList<QWebSocket*>> m_topicSubscribers;
    QMutex m_topicMutex;

    // --- Client 模式 ---
    QWebSocket* m_client = nullptr;
    QStringList m_subscribedTopics;

    // --- 状态 ---
    bool m_isRunning = false;          // 仅主线程访问
    bool m_isServerMode = true;        // 仅主线程访问
    std::atomic<bool> m_cancelled{false}; // svc() 线程读取，主线程写入

    std::vector<DeviceInfo> m_devices;
    AuthInfo m_auth;
};
