/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: WebSocketBackend.cpp
 *
 * Date: 2026-07-05
 *
 * Author: turnarond
 *
 * Description: WebSocket Tool 后端实现 — 直接管理 QWebSocketServer/QWebSocket，
 *              支持 Server 模式（监听多客户端 + 主题订阅/发布）和 Client 模式。
 */

#include "WebSocketBackend.h"
#include <lwlog/lwlog.h>
#include <thread>
#include <chrono>
#include <QDateTime>
#include <QFile>
#include <QHostAddress>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslCertificate>
#include <QUrl>
#include <QUrlQuery>

WebSocketBackend::WebSocketBackend()
{
}

namespace {

// 组装事件流条目：byteCount 统一 UTF-8 口径，preview 边界安全截断
wsproto::WsEvent makeWsEvent(const QString& direction, const QString& peer,
                             const QString& action, const QString& topic,
                             const QString& body)
{
    wsproto::WsEvent e;
    e.timestampMs = QDateTime::currentMSecsSinceEpoch();
    e.direction = direction;
    e.peer = peer;
    e.action = action;
    e.topic = topic;
    e.byteCount = wsproto::utf8Bytes(body);
    e.preview = wsproto::truncatePreview(body);
    e.body = body;
    return e;
}

} // namespace

WebSocketBackend::~WebSocketBackend()
{
    if (m_isRunning) {
        if (m_isServerMode) {
            stopServer();
        } else {
            stopClient();
        }
    }
}

int WebSocketBackend::svc()
{
    LWLOG_I("WebSocketBackend 线程启动");
    // ServiceTask 线程主循环 — 等待取消信号
    // WebSocket 操作在调用线程（主线程）上通过 Qt 事件循环驱动
    // svc() 线程仅用于保持 ServiceTask 生命周期
    while (ServiceTask::isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    LWLOG_I("WebSocketBackend 线程退出");
    return 0;
}

void WebSocketBackend::bindDevices(const std::vector<DeviceInfo>& devices)
{
    m_devices = devices;
}

void WebSocketBackend::bindCredentials(const AuthInfo& auth)
{
    m_auth = auth;
}

void WebSocketBackend::applyConfig(const lwserverbase::config::ConfigValue& /*config*/)
{
    // 从 ConfigManager 读取运行时配置变更（后续扩展）
}

// ============ Server 模式 ============

void WebSocketBackend::startServer(int port, bool sslMode)
{
    m_isServerMode = true;

    QWebSocketServer::SslMode qSslMode = sslMode
        ? QWebSocketServer::SecureMode
        : QWebSocketServer::NonSecureMode;

    m_server = new QWebSocketServer("DeviceForge WebSocket Server", qSslMode);

    if (sslMode) {
        // 加载随程序打包的测试自签名证书（Qt 6.11 无运行时生成 API；
        // 证书由 src/app/certs/ 预生成并编入 QRC，测试用途，客户端会提示证书不受信任）
        QFile certFile(QStringLiteral(":/certs/deviceforge-wss.pem"));
        QFile keyFile(QStringLiteral(":/certs/deviceforge-wss-key.pem"));
        QByteArray certData, keyData;
        if (certFile.open(QIODevice::ReadOnly) && keyFile.open(QIODevice::ReadOnly)) {
            certData = certFile.readAll();
            keyData = keyFile.readAll();
            certFile.close();
            keyFile.close();
        }
        QSslKey sslKey(keyData, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey);
        QSslCertificate sslCert(certData, QSsl::Pem);
        if (!sslKey.isNull() && !sslCert.isNull()) {
            QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
            sslConfig.setLocalCertificate(sslCert);
            sslConfig.setPrivateKey(sslKey);
            m_server->setSslConfiguration(sslConfig);
            if (m_logCb) m_logCb("[Server] WSS 模式：已加载自签名证书（测试用，客户端需信任该证书）");
        } else {
            if (m_errorCb) m_errorCb("自签名证书加载失败，WSS 客户端连接将无法建立");
            if (m_logCb) m_logCb("[Server] WSS 模式：自签名证书加载失败");
        }
    }

    QHostAddress bindAddr(m_bindAddress);
    if (!m_server->listen(bindAddr, static_cast<quint16>(port))) {
        std::string err = m_server->errorString().toStdString();
        if (m_errorCb) m_errorCb("启动失败: " + err);
        if (m_logCb) m_logCb("[Server] 启动失败: " + err);
        delete m_server;
        m_server = nullptr;
        m_isRunning = false;
        return;
    }

    QObject::connect(m_server, &QWebSocketServer::newConnection,
                     [this]() { onNewConnection(); });

    m_isRunning = true;
    std::string mode = sslMode ? "WSS" : "WS";
    if (m_logCb) m_logCb("[Server] 启动成功，" + mode + " 模式，监听端口: " + std::to_string(port));
}

void WebSocketBackend::stopServer()
{
    if (!m_server) return;

    // 断开 newConnection 信号，防止 stop 期间新客户端连入
    QObject::disconnect(m_server, &QWebSocketServer::newConnection, nullptr, nullptr);

    // 收集客户端列表（持锁），然后解锁后再逐个关闭/清理
    // 必须解锁后操作，因为 client->close() 会同步触发 disconnected 信号，
    // onServerClientDisconnected 也会获取 m_clientsMutex，导致死锁
    QList<QWebSocket*> clientsToClean;
    {
        QMutexLocker locker(&m_clientsMutex);
        clientsToClean = m_clients;
        m_clients.clear();
    }

    for (QWebSocket* client : clientsToClean) {
        // 断开该客户端所有信号，避免 stopServer 期间触发回调
        QObject::disconnect(client, nullptr, nullptr, nullptr);
        client->close();
        client->deleteLater();
    }

    // 清空订阅映射
    {
        QMutexLocker locker(&m_topicMutex);
        m_topicSubscribers.clear();
    }

    m_server->close();
    delete m_server;
    m_server = nullptr;
    m_isRunning = false;
    m_isServerMode = false;
    if (m_logCb) m_logCb("[Server] 已停止");
}

void WebSocketBackend::onNewConnection()
{
    QWebSocket* client = m_server->nextPendingConnection();
    if (!client) return;

    if (!m_authToken.empty()) {
        QUrlQuery query(client->requestUrl());
        if (query.queryItemValue("token").toStdString() != m_authToken) {
            std::string addr = client->peerAddress().toString().toStdString();
            if (m_logCb) m_logCb("[Server] 认证失败，拒绝: " + addr);
            client->close(QWebSocketProtocol::CloseCodePolicyViolated, "认证失败");
            client->deleteLater();
            return;
        }
    }

    QObject::connect(client, &QWebSocket::textMessageReceived,
                     [this, client](const QString& msg) { onServerTextMessage(msg, client); });
    QObject::connect(client, &QWebSocket::disconnected,
                     [this, client]() { onServerClientDisconnected(client); });

    {
        QMutexLocker locker(&m_clientsMutex);
        m_clients.append(client);
    }

    std::string addr = client->peerAddress().toString().toStdString();
    if (m_clientConnectCb) m_clientConnectCb(addr);
    if (m_logCb) m_logCb("[Server] 新客户端连接: " + addr);
}

void WebSocketBackend::onServerTextMessage(const QString& message, QWebSocket* client)
{
    if (!client) return;

    const QString addr = client->peerAddress().toString();
    const wsproto::ParsedFrame f = wsproto::parseFrame(message);
    using K = wsproto::FrameKind;

    switch (f.kind) {
    case K::Subscribe: {
        {
            QMutexLocker locker(&m_topicMutex);
            QList<QWebSocket*>& list = m_topicSubscribers[f.topic];
            if (!list.contains(client))
                list.append(client);
        }
        if (m_logCb) m_logCb("[Server] 客户端 " + addr.toStdString() + " 订阅主题 " + f.topic.toStdString());
        if (m_messageCb) m_messageCb(makeWsEvent(QStringLiteral("收"), addr, QStringLiteral("订阅"), f.topic, QString()));
        break;
    }
    case K::Unsubscribe: {
        {
            QMutexLocker locker(&m_topicMutex);
            if (m_topicSubscribers.contains(f.topic)) {
                m_topicSubscribers[f.topic].removeAll(client);
                if (m_topicSubscribers[f.topic].isEmpty())
                    m_topicSubscribers.remove(f.topic);
            }
        }
        if (m_logCb) m_logCb("[Server] 客户端 " + addr.toStdString() + " 退订主题 " + f.topic.toStdString());
        if (m_messageCb) m_messageCb(makeWsEvent(QStringLiteral("收"), addr, QStringLiteral("退订"), f.topic, QString()));
        break;
    }
    case K::Publish: {
        serverPublishToSubscribers(f.topic, f.body);
        if (m_logCb) m_logCb("[Server] 客户端 " + addr.toStdString() + " 发布到主题 "
                             + f.topic.toStdString() + " ("
                             + std::to_string(wsproto::utf8Bytes(f.body)) + " 字节)");
        if (m_messageCb) m_messageCb(makeWsEvent(QStringLiteral("收"), addr, QStringLiteral("发布"), f.topic, f.body));
        break;
    }
    default: {
        // TopicPush/Plain 在 Server 侧收到的都是普通文本帧：按文本入事件流；
        // 正文只进 UI 预览与计数，全局日志仅记方向+字节数（仓规脱敏约束）
        if (m_messageCb) m_messageCb(makeWsEvent(QStringLiteral("收"), addr, QStringLiteral("文本"), QString(), message));
        if (m_logCb) m_logCb("[Server] 收到 " + addr.toStdString() + " 的消息 ("
                             + std::to_string(wsproto::utf8Bytes(message)) + " 字节)");
        break;
    }
    }
}

void WebSocketBackend::onServerClientDisconnected(QWebSocket* client)
{
    if (!client) return;

    std::string addr = client->peerAddress().toString().toStdString();

    {
        QMutexLocker locker(&m_clientsMutex);
        m_clients.removeAll(client);
    }

    // 从所有主题中移除
    {
        QMutexLocker locker(&m_topicMutex);
        for (auto it = m_topicSubscribers.begin(); it != m_topicSubscribers.end(); ++it) {
            it.value().removeAll(client);
        }
    }

    if (m_clientDisconnectCb) m_clientDisconnectCb(addr);
    if (m_logCb) m_logCb("[Server] 客户端断开: " + addr);
    client->deleteLater();
}

void WebSocketBackend::serverPublishToSubscribers(const QString& topic, const QString& message)
{
    QMutexLocker locker(&m_topicMutex);
    if (!m_topicSubscribers.contains(topic)) return;

    QList<QWebSocket*> subscribers = m_topicSubscribers[topic];
    QString formattedMsg = "TOPIC:" + topic + ":" + message;
    for (QWebSocket* client : subscribers) {
        client->sendTextMessage(formattedMsg);
    }
}

// ============ Client 模式 ============

void WebSocketBackend::startClient(const std::string& url)
{
    m_isServerMode = false;

    QString qUrl = QString::fromStdString(url);
    if (qUrl.isEmpty()) {
        if (m_errorCb) m_errorCb("URL 为空");
        return;
    }

    m_client = new QWebSocket();

    QObject::connect(m_client, &QWebSocket::connected,
                     [this]() { onClientConnected(); });
    QObject::connect(m_client, &QWebSocket::disconnected,
                     [this]() { onClientDisconnected(); });
    QObject::connect(m_client, &QWebSocket::textMessageReceived,
                     [this](const QString& msg) { onClientTextMessage(msg); });
    QObject::connect(m_client,
                     QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::errorOccurred),
                     [this](QAbstractSocket::SocketError err) { onClientError(err); });

    if (qUrl.startsWith("wss://")) {
        QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
        m_client->setSslConfiguration(sslConfig);
        if (m_ignoreSslErrors) m_client->ignoreSslErrors();  // 信任自签名证书（测试用）
        if (m_logCb) m_logCb("[Client] 正在以 WSS 模式连接到: " + url);
    } else {
        if (m_logCb) m_logCb("[Client] 正在以 WS 模式连接到: " + url);
    }

    m_client->open(QUrl(qUrl));
}

void WebSocketBackend::stopClient()
{
    if (!m_client) return;

    // 先断开所有信号再关闭，避免 close() 触发的 disconnected 回调在对象删除后执行
    QObject::disconnect(m_client, nullptr, nullptr, nullptr);
    m_client->close();
    m_client->deleteLater();
    m_client = nullptr;
    m_subscribedTopics.clear();
    m_isRunning = false;
    if (m_logCb) m_logCb("[Client] 已断开连接");
}

void WebSocketBackend::onClientConnected()
{
    m_isRunning = true;
    if (m_logCb) m_logCb("[Client] 连接成功");
    if (m_clientConnectCb) m_clientConnectCb("server");
}

void WebSocketBackend::onClientTextMessage(const QString& message)
{
    const wsproto::ParsedFrame f = wsproto::parseFrame(message);
    if (f.kind == wsproto::FrameKind::TopicPush) {
        if (m_messageCb) m_messageCb(makeWsEvent(QStringLiteral("收"), QStringLiteral("server"),
                                                 QStringLiteral("推送"), f.topic, f.body));
        if (m_logCb) m_logCb("[Client] 收到主题 " + f.topic.toStdString() + " 消息 ("
                             + std::to_string(wsproto::utf8Bytes(f.body)) + " 字节)");
    } else {
        if (m_messageCb) m_messageCb(makeWsEvent(QStringLiteral("收"), QStringLiteral("server"),
                                                 QStringLiteral("文本"), QString(), message));
        if (m_logCb) m_logCb("[Client] 收到消息 (" + std::to_string(wsproto::utf8Bytes(message)) + " 字节)");
    }
}

void WebSocketBackend::onClientDisconnected()
{
    if (m_logCb) m_logCb("[Client] 连接断开");
    if (m_clientDisconnectCb) m_clientDisconnectCb("server");
    m_isRunning = false;
}

void WebSocketBackend::onClientError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    if (m_client) {
        std::string err = m_client->errorString().toStdString();
        if (m_errorCb) m_errorCb(err);
        if (m_logCb) m_logCb("[Client] 错误: " + err);
    }
    m_isRunning = false;
}

// ============ 发布/订阅 ============

void WebSocketBackend::subscribe(const std::string& topic)
{
    QString qTopic = QString::fromStdString(topic);
    if (!m_isRunning) {
        if (m_errorCb) m_errorCb("请先启动 WebSocket 服务");
        return;
    }
    if (m_isServerMode) {
        if (m_errorCb) m_errorCb("Server 模式不支持主动订阅操作");
        return;
    }
    if (m_client && m_client->state() == QAbstractSocket::ConnectedState) {
        QString subscribeMsg = wsproto::encodeSubscribe(qTopic);
        m_client->sendTextMessage(subscribeMsg);
        m_subscribedTopics.append(qTopic);
        if (m_messageCb) m_messageCb(makeWsEvent(QStringLiteral("发"), QStringLiteral("server"),
                                                 QStringLiteral("订阅"), qTopic, QString()));
        if (m_logCb) m_logCb("[Client] 订阅主题: " + topic);
    } else {
        if (m_errorCb) m_errorCb("客户端未连接");
    }
}

void WebSocketBackend::unsubscribe(const std::string& topic)
{
    QString qTopic = QString::fromStdString(topic);
    if (!m_isRunning) {
        if (m_errorCb) m_errorCb("请先启动 WebSocket 服务");
        return;
    }
    if (m_isServerMode) {
        if (m_errorCb) m_errorCb("Server 模式不支持主动退订操作");
        return;
    }
    if (m_client && m_client->state() == QAbstractSocket::ConnectedState) {
        m_client->sendTextMessage(wsproto::encodeUnsubscribe(qTopic));
        m_subscribedTopics.removeAll(qTopic);
        if (m_messageCb) m_messageCb(makeWsEvent(QStringLiteral("发"), QStringLiteral("server"),
                                                 QStringLiteral("退订"), qTopic, QString()));
        if (m_logCb) m_logCb("[Client] 退订主题: " + topic);
    } else {
        if (m_errorCb) m_errorCb("客户端未连接");
    }
}

void WebSocketBackend::publish(const std::string& topic, const std::string& message)
{
    QString qTopic = QString::fromStdString(topic);
    QString qMsg = QString::fromStdString(message);

    if (!m_isRunning) {
        if (m_errorCb) m_errorCb("请先启动 WebSocket 服务");
        return;
    }

    if (m_isServerMode) {
        // Server 模式：直接向订阅者发送
        serverPublishToSubscribers(qTopic, qMsg);
        if (m_messageCb) m_messageCb(makeWsEvent(QStringLiteral("发"), QStringLiteral("订阅者"),
                                                 QStringLiteral("发布"), qTopic, qMsg));
        if (m_logCb) {
            m_logCb("[Server] 发布消息到主题 " + topic + " ("
                    + std::to_string(wsproto::utf8Bytes(qMsg)) + " 字节)");
        }
    } else {
        // Client 模式：向服务器发送 PUBLISH 协议消息
        if (m_client && m_client->state() == QAbstractSocket::ConnectedState) {
            m_client->sendTextMessage(wsproto::encodePublish(qTopic, qMsg));
            if (m_messageCb) m_messageCb(makeWsEvent(QStringLiteral("发"), QStringLiteral("server"),
                                                     QStringLiteral("发布"), qTopic, qMsg));
            if (m_logCb) {
                m_logCb("[Client] 发布消息到主题 " + topic + " ("
                        + std::to_string(wsproto::utf8Bytes(qMsg)) + " 字节)");
            }
        } else {
            if (m_errorCb) m_errorCb("客户端未连接");
        }
    }
}

// ============ UI 状态卡数据源 ============

QStringList WebSocketBackend::serverClientPeers() const
{
    QStringList peers;
    QMutexLocker locker(&m_clientsMutex);
    for (QWebSocket* client : m_clients) {
        if (client)
            peers << client->peerAddress().toString()
                  + QStringLiteral(":%1").arg(client->peerPort());
    }
    return peers;
}

QStringList WebSocketBackend::clientSubscriptions() const
{
    return m_subscribedTopics;
}
