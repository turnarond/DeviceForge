#pragma once
#include "IProtocolAdapter.h"
#include <QTcpSocket>
#include <libssh2/libssh2.h>
#include <QSet>
#include <QFuture>
#include <atomic>

// SSH 协议适配器 — 基于 libssh2 实现 IProtocolAdapter
// 支持密码认证 + TOFU (Trust On First Use) 主机密钥校验
// 阻塞模式，由调用方放入 QtConcurrent::run 线程中执行
class SshAdapter : public IProtocolAdapter {
public:
    SshAdapter();
    ~SshAdapter() override;

    // --- IProtocolAdapter 实现 ---
    std::string protocolId() const override { return "ssh"; }
    bool connect(const DeviceInfo& device, const AuthInfo& auth) override;
    void disconnect() override;
    bool isConnected() const override;
    std::string lastError() const override;
    std::future<Response> request(const Request& req) override;
    void subscribe(const Request& req, StreamCallback onData) override;
    void unsubscribe() override;
    ProtocolCapability capability() const override;

private:
    QTcpSocket*         m_socket = nullptr;
    LIBSSH2_SESSION*    m_session = nullptr;
    LIBSSH2_CHANNEL*    m_channel = nullptr;  // subscribe 模式用，request 模式每次新建
    std::string         m_lastError;
    std::atomic<bool>   m_cancelled{false};
    QSet<QString>       m_knownHosts;         // TOFU: 已接受的主机指纹集合
    QFuture<void>       m_subscribeFuture;
    std::atomic<bool>   m_subscribeActive{false};

    std::string collectHostFingerprint();     // 获取当前连接主机指纹（SHA256 Hex）
    bool verifyHostKey();                     // TOFU 校验（首次接受，变化告警）
    void closeChannel(LIBSSH2_CHANNEL*& ch);  // 安全关闭 channel
};
