/*
* Copyright (c) 2026 ACOAUTO Team.
* All rights reserved.
*
* Detailed license information can be found in the LICENSE file.
*
* File: lwconn_base.cpp .
*
* Date: 2026-02-05
*
* Author: Yan.chaodong <yanchaodong@acoinfo.com>
*
*/

#include "lwconn_base.h"
#include <cstdarg>
#include <cstdio>
#include <thread>
#include <chrono>

// 初始化静态成员
LWLogHandler LWConnBase::log_handler_ = nullptr;

// 基础连接类实现
LWConnBase::LWConnBase(LWConnType type, const std::string& name)
    : conn_type_(type),
      conn_status_(LWConnStatus::DISCONNECTED),
      conn_name_(name),
      event_handler_(nullptr) {
    log(LWLogLevel::DEBUG, "LWConnBase created: %s, type: %d", 
        name.c_str(), static_cast<int>(type));
}

LWConnBase::~LWConnBase() {
    log(LWLogLevel::DEBUG, "LWConnBase destroyed: %s", conn_name_.c_str());
}

// 设置全局日志回调
void LWConnBase::setLogHandler(LWLogHandler handler) {
    log_handler_ = handler;
}

// 记录日志
void LWConnBase::log(LWLogLevel level, const char* format, ...) const {
    if (!log_handler_) {
        return; // 如果没有设置日志回调，直接返回
    }

    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // 调用日志回调
    log_handler_(level, conn_name_.c_str(), buffer);
}

LWConnError LWConnBase::sendTo(const std::string& address, const char* data, size_t length, int timeout_ms) {
    return LWConnError::INTERNAL_ERROR; // 默认实现，子类需要重写
}

LWConnError LWConnBase::receiveFrom(std::string& address, char* buffer, size_t buffer_size, size_t& received_size, int timeout_ms) {
    return LWConnError::INTERNAL_ERROR; // 默认实现，子类需要重写
}

LWConnStatus LWConnBase::getStatus() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return conn_status_;
}

LWConnType LWConnBase::getType() const {
    return conn_type_;
}

const std::string& LWConnBase::getName() const {
    return conn_name_;
}

void LWConnBase::setEventHandler(LWConnEventHandler handler) {
    event_handler_ = handler;
}

void LWConnBase::notifyConnChange(bool connected, const std::string& extra_info) {
    if (event_handler_) {
        event_handler_(connected, extra_info);
    }
}

void LWConnBase::updateStatus(LWConnStatus status) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    conn_status_ = status;
}

void LWConnBase::setReconnectPolicy(const ReconnectPolicy& policy) {
    reconnect_policy_ = policy;
    reconnect_retry_count_ = 0;
}

LWConnError LWConnBase::reconnect(int timeout_ms) {
    int delay = reconnect_policy_.calc_delay(reconnect_retry_count_);
    if (delay < 0) return LWConnError::CONNECTION_FAILED;
    if (delay > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
    stop();
    LWConnError err = start();
    if (err == LWConnError::SUCCESS) {
        reconnect_retry_count_ = 0;
    } else {
        reconnect_retry_count_++;
    }
    return err;
}
