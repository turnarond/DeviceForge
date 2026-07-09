/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * File: NetRelayBackend.cpp
 *
 * Date: 2026-07-08
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: 网络中继调试 Tool 后端实现 — TCP/UDP 透明中继代理。
 *              Qt socket 在主线程事件循环中异步驱动；svc() 线程仅保持 ServiceTask 生命周期。
 */

#include "NetRelayBackend.h"
#include <lwlog/lwlog.h>
#include <QDateTime>
#include <thread>
#include <chrono>

NetRelayBackend::NetRelayBackend()
{
}

NetRelayBackend::~NetRelayBackend()
{
    // 先作废在途 DNS 回调并置空回调，防止 stopRelay 期间访问已销毁的 Widget
    ++m_dnsGeneration;
    if (m_dnsPending && m_dnsLookupId != -1) {
        QHostInfo::abortHostLookup(m_dnsLookupId);
        m_dnsPending = false;
    }
    clearCallbacks();
    // 停止回放并释放 player/recorder，避免中继/回放对象在析构期间回调已销毁的 Widget
    if (m_player)   { m_player->stop(); m_player.reset(); }
    if (m_recorder) { m_recorder->close(); m_recorder.reset(); }
    if (m_running || m_tcpServer || m_udpListen) {
        stopRelay();
    }
}

int NetRelayBackend::svc()
{
    LWLOG_I("NetRelayBackend 线程启动");
    while (ServiceTask::isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    LWLOG_I("NetRelayBackend 线程退出");
    return 0;
}

void NetRelayBackend::bindDevices(const std::vector<DeviceInfo>& devices)
{
    m_devices = devices;
}

void NetRelayBackend::bindCredentials(const AuthInfo& auth)
{
    m_auth = auth;
}

void NetRelayBackend::applyConfig(const lwserverbase::config::ConfigValue& /*config*/)
{
}

void NetRelayBackend::log(const std::string& msg)
{
    if (m_logCb) m_logCb(msg);
}

void NetRelayBackend::reportError(const std::string& msg)
{
    if (m_errorCb) m_errorCb(msg);
    if (m_logCb) m_logCb("[错误] " + msg);
}

void NetRelayBackend::clearCallbacks()
{
    m_logCb     = nullptr;
    m_errorCb   = nullptr;
    m_dataCb    = nullptr;
    m_sessionCb = nullptr;
}

// ============ 录制 ============

void NetRelayBackend::enableRecording(const QString& path)
{
    m_recordEnabled = true;
    m_recordPath = path;
}

void NetRelayBackend::disableRecording()
{
    m_recordEnabled = false;
    m_recordPath.clear();
}

void NetRelayBackend::recordData(RelayDirection dir, int sessionId, const QByteArray& data)
{
    if (m_recorder && m_recorder->isOpen())
        m_recorder->append(dir, sessionId, m_recordElapsed.elapsed(), data);
}

void NetRelayBackend::beginRecordingIfEnabled()
{
    m_mode = RelayMode::Relaying;
    if (m_recordEnabled && !m_recordPath.isEmpty()) {
        m_recorder = std::make_unique<RelayRecorder>();
        m_recordElapsed.start();
        qint64 epoch = QDateTime::currentMSecsSinceEpoch();
        if (!m_recorder->open(m_recordPath, m_protocol, epoch)) {
            reportError("录制文件打开失败: " + m_recordPath.toStdString());
            m_recorder.reset();
        } else {
            log("录制已开启: " + m_recordPath.toStdString());
        }
    }
}

// ============ 回放（委托 RelayPlayer）============

void NetRelayBackend::startReplay(const QString& nrecPath, const QString& consumerHost,
                                  quint16 consumerPort, double speedFactor)
{
    if (m_mode == RelayMode::Relaying) { reportError("中继进行中，无法回放"); return; }
    if (m_mode == RelayMode::Replaying) { reportError("回放已在进行中"); return; }

    m_player = std::make_unique<RelayPlayer>();
    m_player->setLogCallback([this](const std::string& s){ log(s); });
    m_player->setErrorCallback([this](const std::string& s){
        reportError(s);
        m_mode = RelayMode::Idle;             // 失败回到 Idle
    });
    m_player->setProgressCallback([this](int p, int t, qint64 ts){
        if (m_replayProgressCb) m_replayProgressCb(p, t, ts);
    });
    m_player->setFinishedCallback([this](){
        if (m_replayFinishedCb) m_replayFinishedCb();
        m_mode = RelayMode::Idle;
    });

    if (m_player->start(nrecPath, consumerHost, consumerPort, speedFactor)) {
        m_mode = RelayMode::Replaying;
    } else {
        m_player.reset();
        m_mode = RelayMode::Idle;
    }
}

void NetRelayBackend::pauseReplay()  { if (m_player) m_player->pause(); }
void NetRelayBackend::resumeReplay() { if (m_player) m_player->resume(); }

void NetRelayBackend::stopReplay()
{
    if (m_player) { m_player->stop(); m_player.reset(); }
    if (m_mode == RelayMode::Replaying) m_mode = RelayMode::Idle;
}


// ============ 中继控制 ============

void NetRelayBackend::startRelay(RelayProtocol proto, const QString& listenAddr, quint16 listenPort,
                                 const QString& upstreamHost, quint16 upstreamPort)
{
    if (m_running) {
        reportError("中继已在运行中，请先停止");
        return;
    }
    if (m_mode == RelayMode::Replaying) {
        reportError("回放进行中，无法启动中继");
        return;
    }
    if (upstreamHost.trimmed().isEmpty() || upstreamPort == 0) {
        reportError("上游地址/端口无效");
        return;
    }

    m_cancelled    = false;
    m_protocol     = proto;
    m_listenAddr   = listenAddr.trimmed().isEmpty() ? QString("127.0.0.1") : listenAddr.trimmed();
    m_listenPort   = listenPort;
    m_upstreamHost = upstreamHost.trimmed();
    m_upstreamPort = upstreamPort;
    m_nextSessionId = 1;

    // --- 绑定地址严格校验（fail-closed，避免非法输入静默绑定 0.0.0.0）---
    // QHostAddress::setAddress 仅解析 IP 字面量；非法/主机名输入会得到 Null，
    // 而 Null 地址在 listen/bind 时等价于绑定所有接口（安全隐患）。
    QHostAddress bindAddr;
    if (m_listenAddr.compare("localhost", Qt::CaseInsensitive) == 0) {
        bindAddr = QHostAddress::LocalHost;              // 127.0.0.1
    } else if (m_listenAddr.compare("any", Qt::CaseInsensitive) == 0 || m_listenAddr == "0.0.0.0") {
        bindAddr = QHostAddress::Any;                    // 显式绑定所有接口
    } else if (!bindAddr.setAddress(m_listenAddr)) {
        reportError("监听地址无效（仅接受 IP 字面量、localhost 或 any）: " + m_listenAddr.toStdString());
        return;                                          // fail-closed，绝不落到 Null
    }
    m_bindIsLoopback = bindAddr.isLoopback();
    if (!m_bindIsLoopback) {
        log("[安全警告] 中继监听于非回环地址 " + bindAddr.toString().toStdString()
            + "，将对网络暴露且无客户端鉴权，请确保网络环境可信");
    }

    if (proto == RelayProtocol::Tcp) {
        // TCP: connectToHost 内部支持异步主机名解析，无需单独 DNS
        m_tcpServer = new QTcpServer();
        if (!m_tcpServer->listen(bindAddr, m_listenPort)) {
            reportError("TCP 监听失败: " + m_tcpServer->errorString().toStdString());
            delete m_tcpServer;
            m_tcpServer = nullptr;
            return;
        }
        QObject::connect(m_tcpServer, &QTcpServer::newConnection,
                         [this]() { onTcpNewConnection(); });
        m_running = true;
        log("[TCP] 中继已启动: 监听 " + m_listenAddr.toStdString() + ":" + std::to_string(m_listenPort)
            + " → 上游 " + m_upstreamHost.toStdString() + ":" + std::to_string(m_upstreamPort));
        beginRecordingIfEnabled();
    } else {
        // UDP: 先尝试直接解析为 IP，失败则异步 DNS（避免阻塞主线程）
        QHostAddress upAddr;
        if (upAddr.setAddress(m_upstreamHost)) {
            // 已是有效 IP，直接启动 UDP 监听
            m_udpUpstreamAddr = upAddr;
            startUdpListen(bindAddr);
        } else if (m_upstreamHost.compare("localhost", Qt::CaseInsensitive) == 0) {
            m_udpUpstreamAddr = QHostAddress::LocalHost;
            startUdpListen(bindAddr);
        } else {
            // 异步 DNS 解析（NetRelayBackend 非 QObject，使用无 context 的重载）。
            // 用代际令牌 m_dnsGeneration 防止 stop/restart 后旧解析回调误触发；
            // m_dnsPending 让析构函数在 DNS 未决时也能安全中止。
            log("[UDP] 正在异步解析上游地址: " + m_upstreamHost.toStdString() + " ...");
            m_pendingBindAddr = bindAddr;
            m_running = true;          // 进入"启动中"状态，isRunning() 反映真实意图
            m_dnsPending = true;
            const int gen = ++m_dnsGeneration;
            m_dnsLookupId = QHostInfo::lookupHost(m_upstreamHost, [this, gen](QHostInfo info) {
                if (gen != m_dnsGeneration) return;   // 已被 stop/restart 作废
                m_dnsPending = false;
                onUdpHostResolved(info);
            });
            return;
        }
    }
}

void NetRelayBackend::startUdpListen(const QHostAddress& bindAddr)
{
    m_udpListen = new QUdpSocket();
    if (!m_udpListen->bind(bindAddr, m_listenPort)) {
        reportError("UDP 绑定失败: " + m_udpListen->errorString().toStdString());
        delete m_udpListen;
        m_udpListen = nullptr;
        return;
    }
    QObject::connect(m_udpListen, &QUdpSocket::readyRead,
                     [this]() { onUdpListenReadyRead(); });
    QObject::connect(m_udpListen,
                     QOverload<QAbstractSocket::SocketError>::of(&QUdpSocket::errorOccurred),
                     [this](QAbstractSocket::SocketError) {
                         log("[UDP] 监听 socket 错误: " + m_udpListen->errorString().toStdString());
                     });

    // 启动 UDP 会话空闲清理定时器
    m_udpElapsed.start();
    m_udpCleanupTimer = new QTimer();  // NetRelayBackend 非 QObject，无 parent
    QObject::connect(m_udpCleanupTimer, &QTimer::timeout,
                     [this]() { onUdpCleanup(); });
    m_udpCleanupTimer->start(kUdpCleanupIntervalMs);

    m_running = true;
    log("[UDP] 中继已启动: 监听 " + m_listenAddr.toStdString() + ":" + std::to_string(m_listenPort)
        + " → 上游 " + m_upstreamHost.toStdString() + ":" + std::to_string(m_upstreamPort));
    beginRecordingIfEnabled();
}

void NetRelayBackend::onUdpHostResolved(QHostInfo info)
{
    if (m_cancelled) return;

    if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
        reportError("DNS 解析失败: " + info.errorString().toStdString() + " (" + m_upstreamHost.toStdString() + ")");
        m_running = false;   // 解析失败，退出"启动中"状态
        return;
    }
    m_udpUpstreamAddr = info.addresses().first();
    startUdpListen(m_pendingBindAddr);
}

void NetRelayBackend::stopRelay()
{
    // DNS 未决时 m_running 已为 true，但无 server/socket；下方守卫需放行该状态
    if (!m_running && !m_tcpServer && !m_udpListen && !m_dnsPending) return;

    m_cancelled = true;
    m_dnsPending = false;
    ++m_dnsGeneration;   // 作废任何在途 DNS 回调

    // --- 停止 UDP 清理定时器 ---
    if (m_udpCleanupTimer) {
        m_udpCleanupTimer->stop();
        QObject::disconnect(m_udpCleanupTimer, nullptr, nullptr, nullptr);
        m_udpCleanupTimer->deleteLater();
        m_udpCleanupTimer = nullptr;
    }

    // --- 清理 TCP ---
    if (m_tcpServer) {
        QObject::disconnect(m_tcpServer, &QTcpServer::newConnection, nullptr, nullptr);
    }
    QList<TcpPair*> pairs = m_pairByClient.values();
    for (TcpPair* pair : pairs) {
        closeTcpPair(pair);
    }
    m_pairByClient.clear();
    m_pairByUpstream.clear();
    if (m_tcpServer) {
        m_tcpServer->close();
        m_tcpServer->deleteLater();
        m_tcpServer = nullptr;
    }

    // --- 清理 UDP ---
    for (UdpSession* s : m_udpSessions.values()) {
        if (s->upstream) {
            QObject::disconnect(s->upstream, nullptr, nullptr, nullptr);
            s->upstream->close();
            s->upstream->deleteLater();
        }
        s->session.active = false;
        if (m_sessionCb) m_sessionCb(s->session);
        delete s;
    }
    m_udpSessions.clear();
    if (m_udpListen) {
        QObject::disconnect(m_udpListen, nullptr, nullptr, nullptr);
        m_udpListen->close();
        m_udpListen->deleteLater();
        m_udpListen = nullptr;
    }

    m_running = false;
    if (m_recorder) { m_recorder->close(); m_recorder.reset(); log("录制已保存"); }
    if (m_mode == RelayMode::Relaying) m_mode = RelayMode::Idle;
    if (m_logCb) m_logCb("中继已停止");
}

// ============ TCP 处理 ============

void NetRelayBackend::onTcpNewConnection()
{
    if (!m_tcpServer || m_cancelled) return;

    while (m_tcpServer->hasPendingConnections()) {
        QTcpSocket* client = m_tcpServer->nextPendingConnection();
        if (!client) break;

        if (m_pairByClient.size() >= m_maxConn) {
            log("[TCP] 达到最大连接数 (" + std::to_string(m_maxConn) + ")，拒绝新连接");
            client->close();
            client->deleteLater();
            continue;
        }

        QTcpSocket* upstream = new QTcpSocket();
        TcpPair* pair = new TcpPair();
        pair->client = client;
        pair->upstream = upstream;
        pair->sessionId = m_nextSessionId++;
        pair->session.id = pair->sessionId;
        pair->session.protocol = RelayProtocol::Tcp;
        pair->session.clientAddr = client->peerAddress().toString() + ":" + QString::number(client->peerPort());
        pair->session.upstreamAddr = m_upstreamHost + ":" + QString::number(m_upstreamPort);
        pair->session.active = false;

        m_pairByClient.insert(client, pair);
        m_pairByUpstream.insert(upstream, pair);

        QObject::connect(client, &QTcpSocket::readyRead,
                         [this, client]() { onTcpClientReadyRead(client); });
        QObject::connect(client, &QTcpSocket::disconnected,
                         [this, client]() { onTcpDisconnected(client); });
        QObject::connect(client,
                         QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
                         [this, client](QAbstractSocket::SocketError) {
                             if (m_pairByClient.contains(client)) {
                                 log("[TCP] 客户端连接错误: " + client->errorString().toStdString());
                             }
                         });

        QObject::connect(upstream, &QTcpSocket::connected,
                         [this, upstream]() { onTcpUpstreamConnected(upstream); });
        QObject::connect(upstream, &QTcpSocket::readyRead,
                         [this, upstream]() { onTcpUpstreamReadyRead(upstream); });
        QObject::connect(upstream, &QTcpSocket::disconnected,
                         [this, upstream]() { onTcpDisconnected(upstream); });
        QObject::connect(upstream,
                         QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
                         [this, upstream](QAbstractSocket::SocketError) {
                             if (m_pairByUpstream.contains(upstream)) {
                                 log("[TCP] 上游连接错误: " + upstream->errorString().toStdString());
                                 onTcpDisconnected(upstream);
                             }
                         });

        upstream->connectToHost(m_upstreamHost, m_upstreamPort);

        if (m_sessionCb) m_sessionCb(pair->session);
        log("[TCP] 新连接 " + pair->session.clientAddr.toStdString()
            + " (会话#" + std::to_string(pair->sessionId) + ")，正在连接上游...");
    }
}

void NetRelayBackend::onTcpClientReadyRead(QTcpSocket* client)
{
    if (m_cancelled) return;
    TcpPair* pair = m_pairByClient.value(client, nullptr);
    if (!pair) return;

    QByteArray data = client->readAll();
    if (data.isEmpty()) return;

    pair->session.bytesUp += data.size();
    if (m_dataCb) m_dataCb(RelayDirection::Upstream, pair->session.clientAddr, pair->sessionId, data);
    recordData(RelayDirection::Upstream, pair->sessionId, data);

    if (pair->upstreamConnected && pair->upstream) {
        pair->upstream->write(data);
    } else if (pair->pending.size() < kMaxPendingBytes) {
        pair->pending.append(data);
    } else {
        // pending 缓冲已达上限：丢弃缓冲中最早的数据，为新数据腾空间
        int overflow = pair->pending.size() + data.size() - static_cast<int>(kMaxPendingBytes);
        if (overflow > 0 && overflow < pair->pending.size()) {
            pair->pending = pair->pending.mid(overflow);
        }
        pair->pending.append(data);
        log("[TCP] 会话#" + std::to_string(pair->sessionId)
            + " pending 缓冲区达上限，部分早期数据被丢弃");
    }
    if (m_sessionCb) m_sessionCb(pair->session);
}

void NetRelayBackend::onTcpUpstreamReadyRead(QTcpSocket* upstream)
{
    if (m_cancelled) return;
    TcpPair* pair = m_pairByUpstream.value(upstream, nullptr);
    if (!pair) return;

    QByteArray data = upstream->readAll();
    if (data.isEmpty()) return;

    pair->session.bytesDown += data.size();
    if (m_dataCb) m_dataCb(RelayDirection::Downstream, pair->session.clientAddr, pair->sessionId, data);
    recordData(RelayDirection::Downstream, pair->sessionId, data);

    if (pair->client) {
        pair->client->write(data);
    }
    if (m_sessionCb) m_sessionCb(pair->session);
}

void NetRelayBackend::onTcpUpstreamConnected(QTcpSocket* upstream)
{
    TcpPair* pair = m_pairByUpstream.value(upstream, nullptr);
    if (!pair) return;

    pair->upstreamConnected = true;
    pair->session.active = true;

    if (!pair->pending.isEmpty()) {
        upstream->write(pair->pending);
        pair->pending.clear();
    }
    if (m_sessionCb) m_sessionCb(pair->session);
    log("[TCP] 会话#" + std::to_string(pair->sessionId) + " 上游已连接: "
        + pair->session.upstreamAddr.toStdString());
}

void NetRelayBackend::onTcpDisconnected(QTcpSocket* which)
{
    TcpPair* pair = nullptr;
    if (m_pairByClient.contains(which)) {
        pair = m_pairByClient.value(which);
    } else if (m_pairByUpstream.contains(which)) {
        pair = m_pairByUpstream.value(which);
    }
    if (!pair) return;
    closeTcpPair(pair);
}

void NetRelayBackend::closeTcpPair(TcpPair* pair)
{
    if (!pair) return;

    pair->session.active = false;
    if (m_sessionCb) m_sessionCb(pair->session);

    if (pair->client) {
        QObject::disconnect(pair->client, nullptr, nullptr, nullptr);
        m_pairByClient.remove(pair->client);
        pair->client->close();
        pair->client->deleteLater();
    }
    if (pair->upstream) {
        QObject::disconnect(pair->upstream, nullptr, nullptr, nullptr);
        m_pairByUpstream.remove(pair->upstream);
        pair->upstream->close();
        pair->upstream->deleteLater();
    }
    log("[TCP] 会话#" + std::to_string(pair->sessionId) + " 已关闭 ("
        + pair->session.clientAddr.toStdString() + ")");
    delete pair;
}

// ============ UDP 处理 ============

void NetRelayBackend::onUdpListenReadyRead()
{
    if (!m_udpListen || m_cancelled) return;

    while (m_udpListen->hasPendingDatagrams()) {
        QByteArray data;
        data.resize(static_cast<int>(m_udpListen->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        m_udpListen->readDatagram(data.data(), data.size(), &sender, &senderPort);

        QString key = sender.toString() + ":" + QString::number(senderPort);
        UdpSession* s = m_udpSessions.value(key, nullptr);

        if (!s) {
            if (m_udpSessions.size() >= m_maxConn) {
                log("[UDP] 达到最大会话数 (" + std::to_string(m_maxConn) + ")，丢弃来自 " + key.toStdString() + " 的数据报");
                continue;
            }
            s = new UdpSession();
            s->sessionId = m_nextSessionId++;
            s->clientAddr = sender;
            s->clientPort = senderPort;
            s->upstream = new QUdpSocket();
            s->lastActive = m_udpElapsed.elapsed();
            s->session.id = s->sessionId;
            s->session.protocol = RelayProtocol::Udp;
            s->session.clientAddr = key;
            s->session.upstreamAddr = m_upstreamHost + ":" + QString::number(m_upstreamPort);
            s->session.active = true;

            QObject::connect(s->upstream, &QUdpSocket::readyRead,
                             [this, s]() { onUdpUpstreamReadyRead(s); });
            QObject::connect(s->upstream,
                             QOverload<QAbstractSocket::SocketError>::of(&QUdpSocket::errorOccurred),
                             [this, s](QAbstractSocket::SocketError) {
                                 log("[UDP] 上游 socket 错误 (会话#" + std::to_string(s->sessionId) + "): "
                                     + s->upstream->errorString().toStdString());
                             });

            m_udpSessions.insert(key, s);
            if (m_sessionCb) m_sessionCb(s->session);
            log("[UDP] 新会话 " + key.toStdString() + " (会话#" + std::to_string(s->sessionId) + ")");
        }

        s->lastActive = m_udpElapsed.elapsed();
        s->session.bytesUp += data.size();
        if (m_dataCb) m_dataCb(RelayDirection::Upstream, s->session.clientAddr, s->sessionId, data);
        recordData(RelayDirection::Upstream, s->sessionId, data);

        s->upstream->writeDatagram(data, m_udpUpstreamAddr, m_upstreamPort);
        if (m_sessionCb) m_sessionCb(s->session);
    }
}

void NetRelayBackend::onUdpUpstreamReadyRead(UdpSession* session)
{
    if (!session || !session->upstream || !m_udpListen || m_cancelled) return;

    while (session->upstream->hasPendingDatagrams()) {
        QByteArray data;
        data.resize(static_cast<int>(session->upstream->pendingDatagramSize()));
        QHostAddress from;
        quint16 fromPort = 0;
        session->upstream->readDatagram(data.data(), data.size(), &from, &fromPort);

        session->lastActive = m_udpElapsed.elapsed();
        session->session.bytesDown += data.size();
        if (m_dataCb) m_dataCb(RelayDirection::Downstream, session->session.clientAddr, session->sessionId, data);
        recordData(RelayDirection::Downstream, session->sessionId, data);

        m_udpListen->writeDatagram(data, session->clientAddr, session->clientPort);
        if (m_sessionCb) m_sessionCb(session->session);
    }
}

void NetRelayBackend::onUdpCleanup()
{
    if (m_cancelled) return;

    QList<QString> toRemove;
    qint64 now = m_udpElapsed.elapsed();

    for (auto it = m_udpSessions.begin(); it != m_udpSessions.end(); ++it) {
        UdpSession* s = it.value();
        if (s && (now - s->lastActive) > kUdpSessionTimeoutMs) {
            toRemove.append(it.key());
        }
    }

    for (const QString& key : toRemove) {
        UdpSession* s = m_udpSessions.take(key);
        if (!s) continue;

        if (s->upstream) {
            QObject::disconnect(s->upstream, nullptr, nullptr, nullptr);
            s->upstream->close();
            s->upstream->deleteLater();
        }
        s->session.active = false;
        if (m_sessionCb) m_sessionCb(s->session);
        log("[UDP] 会话#" + std::to_string(s->sessionId) + " 空闲超时已清理 (" + key.toStdString() + ")");
        delete s;
    }
}
