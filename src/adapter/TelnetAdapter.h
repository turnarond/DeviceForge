#pragma once
#include "IProtocolAdapter.h"
#include <memory>
#include <string>
#include <future>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

// Telnet 协议适配器 — 基于 lwcommunicate::LWTcpClient 实现 IProtocolAdapter
// 同时支持请求-响应模式（单次命令执行）和流模式（持续接收输出）
// 使用 Pimpl 模式隐藏 lwcommunicate 实现细节
class TelnetAdapter : public IProtocolAdapter {
public:
    TelnetAdapter();
    ~TelnetAdapter() override;

    // --- IProtocolAdapter 实现 ---
    std::string protocolId() const override { return "telnet"; }
    bool connect(const DeviceInfo& device, const AuthInfo& auth) override;
    void disconnect() override;
    bool isConnected() const override;
    std::string lastError() const override;
    std::future<Response> request(const Request& req) override;
    void subscribe(const Request& req, StreamCallback onData) override;
    void unsubscribe() override;
    ProtocolCapability capability() const override;

    // --- Telnet 特有配置 ---
    // 登录提示超时（毫秒）：等待 "login:" 提示；超时视为无认证服务器（如 SylixOS telnetd），跳过认证直接可用
    void setLoginPromptTimeoutMs(int ms);
    // 取消标志注入：request() 等待循环 / connect() 登录流程中定期检查，置位则中断并报"已取消"
    void setCancelFlag(std::atomic<bool>* flag);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
