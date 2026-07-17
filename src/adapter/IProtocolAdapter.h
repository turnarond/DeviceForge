#pragma once
#include "ProtocolCapability.h"
#include <string>
#include <future>
#include <functional>
#include <memory>

// 流式数据回调：每次收到新数据块时调用
using StreamCallback = std::function<void(const std::string& data, bool isFinished)>;

// 所有协议适配器的统一基类
class IProtocolAdapter {
public:
    virtual ~IProtocolAdapter() = default;

    // --- 身份 ---
    virtual std::string protocolId() const = 0;   // "ftp", "telnet", "ssh"...

    // --- 连接生命周期 ---
    virtual bool connect(const DeviceInfo& device, const AuthInfo& auth) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual std::string lastError() const = 0;

    // --- 传输模式 ---
    // 请求-响应（FTP、HTTP、Modbus 读）
    virtual std::future<Response> request(const Request& req) = 0;

    // 流模式（Telnet 逐行输出、WebSocket 消息）
    virtual void subscribe(const Request& req, StreamCallback onData) = 0;
    virtual void unsubscribe() = 0;

    // --- 能力声明 ---
    virtual ProtocolCapability capability() const = 0;
};

// 适配器工厂函数类型
using AdapterFactory = std::shared_ptr<IProtocolAdapter>(*)();
