/*
* Copyright (c) 2026 ACOAUTO Team.
* All rights reserved.
*
* Detailed license information can be found in the LICENSE file.
*
* File: lwconn_base.h .
*
* Date: 2026-02-05
*
* Author: Yan.chaodong <yanchaodong@acoinfo.com>
*
*/

#ifndef LWCONN_BASE_H
#define LWCONN_BASE_H

#include <string>
#include <functional>
#include <mutex>

// 连接类型
enum class LWConnType {
    TCP_CLIENT,    // TCP客户端
    TCP_SERVER,    // TCP服务端
    UDP,           // UDP
    SERIAL,        // 串口
    UNKNOWN        // 未知类型
};

// 连接状态
enum class LWConnStatus {
    DISCONNECTED,  // 断开连接
    CONNECTING,    // 连接中
    CONNECTED,     // 已连接
    ERROR          // 错误
};

// 错误码
enum class LWConnError {
    SUCCESS = 0,           // 成功
    INVALID_PARAM = -1,    // 参数无效
    CONNECTION_FAILED = -2, // 连接失败
    SEND_FAILED = -3,       // 发送失败
    RECEIVE_FAILED = -4,    // 接收失败
    TIMEOUT = -5,           // 超时
    NOT_CONNECTED = -6,     // 未连接
    INTERNAL_ERROR = -7     // 内部错误
};

// 日志级别
enum class LWLogLevel {
    DEBUG,    // 调试
    INFO,     // 信息
    WARNING,  // 警告
    ERROR,    // 错误
    FATAL     // 致命错误
};

// 连接事件回调类型
typedef std::function<void(bool connected, const std::string& extra_info)> LWConnEventHandler;

// 数据接收回调类型（仅用于异步操作）
typedef std::function<void(const char* data, size_t length, const std::string& extra_info)> LWConnDataHandler;

// 日志回调类型
typedef std::function<void(LWLogLevel level, const char* module, const char* message)> LWLogHandler;

// Reconnect policy configuration
struct ReconnectPolicy {
    int max_retries = -1;      // -1 = unlimited, 0 = no retry, >0 = limit
    int base_delay_ms = 1000;  // initial backoff
    int max_delay_ms = 60000;  // backoff cap
    float multiplier = 2.0f;

    int calc_delay(int retry_count) const {
        if (max_retries >= 0 && retry_count >= max_retries) return -1;
        int ms = base_delay_ms;
        for (int i = 0; i < retry_count && ms < max_delay_ms; i++) {
            ms = static_cast<int>(ms * multiplier);
            if (ms > max_delay_ms) ms = max_delay_ms;
        }
        return ms;
    }
};

// 基础连接类
class LWConnBase {
public:
    LWConnBase(LWConnType type, const std::string& name);
    virtual ~LWConnBase();

    // 启动连接
    virtual LWConnError start() = 0;

    // 停止连接
    virtual void stop() = 0;

    // 发送数据（同步）
    virtual LWConnError send(const char* data, size_t length, int timeout_ms = 0) = 0;

    // 接收数据（同步）
    virtual LWConnError receive(char* buffer, size_t buffer_size, size_t& received_size, int timeout_ms = 0) = 0;

    // 发送数据到指定地址（仅用于UDP）
    virtual LWConnError sendTo(const std::string& address, const char* data, size_t length, int timeout_ms = 0);

    // 从指定地址接收数据（仅用于UDP）
    virtual LWConnError receiveFrom(std::string& address, char* buffer, size_t buffer_size, size_t& received_size, int timeout_ms = 0);

    // 断开连接
    virtual LWConnError disconnect() = 0;

    // 重新连接
    virtual LWConnError reconnect(int timeout_ms = 0);

    // 设置重连策略
    void setReconnectPolicy(const ReconnectPolicy& policy);

    // 检查连接状态
    virtual bool isConnected() const = 0;

    // 获取连接状态
    LWConnStatus getStatus() const;

    // 获取连接类型
    LWConnType getType() const;

    // 获取连接名称
    const std::string& getName() const;

    // 设置事件回调
    void setEventHandler(LWConnEventHandler handler);

    // 清理接收缓冲区
    virtual void clearReceiveBuffer() = 0;

    // Get the native file descriptor for external event loop integration.
    // Returns -1 if not connected/started. Use ONLY for epoll_ctl/select registration.
    virtual int nativeHandle() const = 0;

public:
    // 设置全局日志回调
    static void setLogHandler(LWLogHandler handler);

protected:
    // 记录日志
    void log(LWLogLevel level, const char* format, ...) const;

    // 通知连接状态变化
    void notifyConnChange(bool connected, const std::string& extra_info = "");

    // 更新连接状态
    void updateStatus(LWConnStatus status);

protected:
    LWConnType conn_type_;
    LWConnStatus conn_status_;
    std::string conn_name_;
    LWConnEventHandler event_handler_;
    mutable std::mutex status_mutex_;
    static LWLogHandler log_handler_;
    ReconnectPolicy reconnect_policy_;
    int reconnect_retry_count_ = 0;
};

#endif // LWCONN_BASE_H
