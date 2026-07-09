/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * File: NetRelayBackend.h
 *
 * Date: 2026-07-08
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: 网络中继调试 Tool 后端 — 继承 ToolBackend，实现 TCP/UDP 透明中继代理。
 *              监听本地端口，接收客户端连接后建立到真实上游服务器的连接，
 *              双向原样转发数据（不影响生产系统运行），同时旁路抓取上下行流量用于调试。
 */

#pragma once
#include "framework/ToolBackend.h"
#include "NetRelayTypes.h"
#include "RelayRecorder.h"
#include "RelayPlayer.h"
#include <memory>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QHostAddress>
#include <QHostInfo>
#include <QTimer>
#include <QElapsedTimer>
#include <QByteArray>
#include <QMap>
#include <QString>
#include <functional>
#include <atomic>
#include <string>
#include <vector>

// 中继会话元信息（用于 UI 会话列表展示）
struct RelaySession {
    int          id = 0;
    RelayProtocol protocol = RelayProtocol::Tcp;
    QString      clientAddr;     // 客户端地址 ip:port
    QString      upstreamAddr;   // 上游地址 host:port
    qint64       bytesUp = 0;     // 上行累计字节
    qint64       bytesDown = 0;   // 下行累计字节
    bool         active = false;  // 是否活跃（TCP 已连接上游 / UDP 会话存在）
};

class NetRelayBackend : public ToolBackend {
public:
    NetRelayBackend();
    ~NetRelayBackend() override;

    // --- ServiceTask 线程入口 ---
    int svc() override;

    // --- ToolBackend 身份 ---
    std::string toolId() const override { return "com.deviceforge.net.relay"; }
    std::string toolName() const override { return "网络中继"; }
    std::string toolVersion() const override { return "2.1.0"; }
    std::string toolCategory() const override { return "diagnostic"; }
    std::string toolIcon() const override { return "net_relay"; }

    void bindDevices(const std::vector<DeviceInfo>& devices) override;
    void bindCredentials(const AuthInfo& auth) override;
    void applyConfig(const lwserverbase::config::ConfigValue& config) override;

    // --- 中继控制 ---
    // 启动中继：监听 listenAddr:listenPort，透明转发到 upstreamHost:upstreamPort
    void startRelay(RelayProtocol proto, const QString& listenAddr, quint16 listenPort,
                    const QString& upstreamHost, quint16 upstreamPort);
    void stopRelay();
    bool isRunning() const { return m_running; }
    void setMaxConnections(int n) { if (n > 0) m_maxConn = n; }

    // --- 录制 ---
    void enableRecording(const QString& path);   // startRelay 前调用；启动时打开录制
    void disableRecording();

    // --- 回放（与中继互斥）---
    void startReplay(const QString& nrecPath, const QString& consumerHost,
                     quint16 consumerPort, double speedFactor);
    void pauseReplay();
    void resumeReplay();
    void stopReplay();
    bool isReplaying() const { return m_mode == RelayMode::Replaying; }

    using ReplayProgressCallback = std::function<void(int played, int total, qint64 tsOffsetMs)>;
    using ReplayFinishedCallback = std::function<void()>;
    void setReplayProgressCallback(ReplayProgressCallback cb) { m_replayProgressCb = std::move(cb); }
    void setReplayFinishedCallback(ReplayFinishedCallback cb) { m_replayFinishedCb = std::move(cb); }

    // --- 回调（由 Widget 设置，跨线程通过 QueuedConnection 保护） ---
    using LogCallback     = std::function<void(const std::string&)>;
    using ErrorCallback   = std::function<void(const std::string&)>;
    using DataCallback    = std::function<void(RelayDirection dir, const QString& peer, int sessionId, const QByteArray& data)>;
    using SessionCallback = std::function<void(const RelaySession& session)>;

    void setLogCallback(LogCallback cb)         { m_logCb = std::move(cb); }
    void setErrorCallback(ErrorCallback cb)     { m_errorCb = std::move(cb); }
    void setDataCallback(DataCallback cb)       { m_dataCb = std::move(cb); }
    void setSessionCallback(SessionCallback cb) { m_sessionCb = std::move(cb); }

private:
    // TCP 连接对：客户端 socket ↔ 上游 socket
    struct TcpPair {
        QTcpSocket*  client = nullptr;
        QTcpSocket*  upstream = nullptr;
        int          sessionId = 0;
        QByteArray   pending;              // 上游未连接前缓冲的客户端数据
        bool         upstreamConnected = false;
        RelaySession session;
    };

    // UDP 会话：按客户端源地址区分
    struct UdpSession {
        int          sessionId = 0;
        QHostAddress clientAddr;
        quint16      clientPort = 0;
        QUdpSocket*  upstream = nullptr;   // 面向上游的 socket（回程从此读取）
        qint64       lastActive = 0;        // 最后活跃时间戳（ms，QElapsedTimer 基准），用于空闲清理
        RelaySession session;
    };

    // --- TCP 处理 ---
    void onTcpNewConnection();
    void onTcpClientReadyRead(QTcpSocket* client);
    void onTcpUpstreamReadyRead(QTcpSocket* upstream);
    void onTcpUpstreamConnected(QTcpSocket* upstream);
    void onTcpDisconnected(QTcpSocket* which);
    void closeTcpPair(TcpPair* pair);

    // --- UDP 处理 ---
    void onUdpListenReadyRead();
    void onUdpUpstreamReadyRead(UdpSession* session);
    void onUdpCleanup();                     // 定时清理空闲 UDP 会话
    void onUdpHostResolved(QHostInfo info);  // 异步 DNS 解析完成
    void startUdpListen(const QHostAddress& bindAddr);  // 启动 UDP 监听（DNS 完成后调用）

    void log(const std::string& msg);
    void reportError(const std::string& msg);
    void clearCallbacks();                   // 析构安全：置空所有回调

    // 录制辅助
    void recordData(RelayDirection dir, int sessionId, const QByteArray& data);
    void beginRecordingIfEnabled();          // 中继成功启动后按需打开录制（TCP/UDP 两分支复用）

    // 回调
    LogCallback     m_logCb;
    ErrorCallback   m_errorCb;
    DataCallback    m_dataCb;
    SessionCallback m_sessionCb;

    // 配置
    RelayProtocol m_protocol = RelayProtocol::Tcp;
    QString       m_listenAddr = "127.0.0.1";
    quint16       m_listenPort = 0;
    QString       m_upstreamHost;
    quint16       m_upstreamPort = 0;
    int           m_maxConn = 50;
    int           m_nextSessionId = 1;
    bool          m_bindIsLoopback = true;   // 监听地址是否为回环（非回环时警告）

    // 异步 DNS 状态（UDP 上游主机名解析）
    bool          m_dnsPending = false;      // 是否有在途 DNS 查询
    int           m_dnsLookupId = -1;        // QHostInfo::lookupHost 返回的 id（用于 abort）
    int           m_dnsGeneration = 0;       // 代际令牌，stop/restart 时递增以作废旧回调

    // TCP 状态
    QTcpServer*                 m_tcpServer = nullptr;
    QMap<QTcpSocket*, TcpPair*> m_pairByClient;
    QMap<QTcpSocket*, TcpPair*> m_pairByUpstream;

    // UDP 状态
    QUdpSocket*                 m_udpListen = nullptr;
    QHostAddress                m_udpUpstreamAddr;
    QHostAddress                m_pendingBindAddr;  // 异步 DNS 期间暂存绑定地址
    QMap<QString, UdpSession*>  m_udpSessions;
    QTimer*                     m_udpCleanupTimer = nullptr;
    QElapsedTimer               m_udpElapsed;     // UDP 会话空闲计时基准

    // 运行状态
    bool                        m_running = false;
    std::atomic<bool>           m_cancelled{false};

    // 模式状态机（中继/回放互斥，spec §6）
    enum class RelayMode { Idle, Relaying, Replaying };
    RelayMode m_mode = RelayMode::Idle;

    // 录制
    bool                           m_recordEnabled = false;
    QString                        m_recordPath;
    std::unique_ptr<RelayRecorder> m_recorder;
    QElapsedTimer                  m_recordElapsed;

    // 回放
    std::unique_ptr<RelayPlayer>   m_player;
    ReplayProgressCallback         m_replayProgressCb;
    ReplayFinishedCallback         m_replayFinishedCb;

    // 安全限制常量
    static constexpr qint64 kMaxPendingBytes = 1 * 1024 * 1024;      // TCP pending 缓冲上限 1MB
    static constexpr qint64 kUdpSessionTimeoutMs = 300 * 1000;       // UDP 空闲超时 5 分钟
    static constexpr int     kUdpCleanupIntervalMs = 30 * 1000;      // UDP 清理周期 30 秒

    std::vector<DeviceInfo>     m_devices;
    AuthInfo                    m_auth;
};
