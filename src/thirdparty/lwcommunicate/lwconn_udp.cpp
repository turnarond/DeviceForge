/*
* Copyright (c) 2026 ACOAUTO Team.
* All rights reserved.
*
* Detailed license information can be found in the LICENSE file.
*
* File: lwconn_udp.cpp .
*
* Date: 2026-02-05
*
* Author: Yan.chaodong <yanchaodong@acoinfo.com>
*
*/

#include "lwconn_udp.h"
#include "lwconn_base.h"
#include <cstring>
#include <cstdarg>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#undef ERROR
#undef DEBUG
#undef WARNING
#undef INFO
#pragma comment(lib, "ws2_32.lib")
typedef SSIZE_T ssize_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

// UDP连接类实现
LWUdp::LWUdp(const std::string& name, int local_port, const std::string& remote_address, int remote_port)
    : LWConnBase(LWConnType::UDP, name),
      local_port_(local_port),
      remote_address_(remote_address),
      remote_port_(remote_port) {
}

LWUdp::~LWUdp() {
    stop();
}

LWConnError LWUdp::start() {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    
    if (socket_.valid()) {
        return LWConnError::SUCCESS;
    }

    this->updateStatus(LWConnStatus::CONNECTING);

    // 创建socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        this->log(LWLogLevel::ERROR, "LWUdp::start: socket creation failed");
        this->updateStatus(LWConnStatus::ERROR);
        return LWConnError::CONNECTION_FAILED;
    }

    // 绑定本地端口
    if (local_port_ > 0) {
        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(local_port_);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            this->log(LWLogLevel::ERROR, "LWUdp::start: bind failed");
            LWCONN_CLOSE_SOCKET(sock);
            this->updateStatus(LWConnStatus::ERROR);
            return LWConnError::CONNECTION_FAILED;
        }
    }

    socket_.reset(sock);
    this->updateStatus(LWConnStatus::CONNECTED);
    this->notifyConnChange(true, "UDP socket started");
    
    this->log(LWLogLevel::DEBUG, "LWUdp::start: started on port %d", local_port_);
    return LWConnError::SUCCESS;
}

void LWUdp::stop() {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (socket_.valid()) {
        socket_.reset();
        this->updateStatus(LWConnStatus::DISCONNECTED);
        this->notifyConnChange(false, "");
        this->log(LWLogLevel::DEBUG, "LWUdp::stop: stopped");
    }
}

LWConnError LWUdp::send(const char* data, size_t length, int timeout_ms) {
    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (!socket_.valid()) {
        return LWConnError::NOT_CONNECTED;
    }

    if (remote_address_.empty() || remote_port_ == 0) {
        return LWConnError::INVALID_PARAM;
    }

    int sock = socket_.get();
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(remote_port_);
    
    if (inet_pton(AF_INET, remote_address_.c_str(), &addr.sin_addr) <= 0) {
        return LWConnError::INVALID_PARAM;
    }

    ssize_t sent = sendto(sock, data, length, 0, (struct sockaddr*)&addr, sizeof(addr));
    if (sent < 0) {
        return LWConnError::SEND_FAILED;
    }

    return LWConnError::SUCCESS;
}

LWConnError LWUdp::receive(char* buffer, size_t buffer_size, size_t& received_size, int timeout_ms) {
    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (!socket_.valid()) {
        return LWConnError::NOT_CONNECTED;
    }

    int sock = socket_.get();

    if (timeout_ms == 0) {
        // Non-blocking: poll once, return immediately
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(sock, &read_set);
        struct timeval tv = {0, 0};
        int result = select(sock + 1, &read_set, nullptr, nullptr, &tv);
        if (result < 0) return LWConnError::RECEIVE_FAILED;
        else if (result == 0) return LWConnError::TIMEOUT;
    } else if (timeout_ms > 0) {
        // Wait up to timeout_ms
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(sock, &read_set);
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        int result = select(sock + 1, &read_set, nullptr, nullptr, &tv);
        if (result < 0) return LWConnError::RECEIVE_FAILED;
        else if (result == 0) return LWConnError::TIMEOUT;
    }
    // timeout_ms < 0: infinite wait (blocking recv, no select)

    ssize_t received = recv(sock, buffer, buffer_size, 0);
    if (received < 0) {
        return LWConnError::RECEIVE_FAILED;
    }

    received_size = (size_t)received;
    return LWConnError::SUCCESS;
}

LWConnError LWUdp::sendTo(const std::string& address, const char* data, size_t length, int timeout_ms) {
    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (!socket_.valid()) {
        return LWConnError::NOT_CONNECTED;
    }

    // 解析地址
    size_t colon_pos = address.find(':');
    if (colon_pos == std::string::npos) {
        return LWConnError::INVALID_PARAM;
    }

    std::string host = address.substr(0, colon_pos);
    std::string port_str = address.substr(colon_pos + 1);
    int port = std::stoi(port_str);

    int sock = socket_.get();
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        return LWConnError::INVALID_PARAM;
    }

    ssize_t sent = sendto(sock, data, length, 0, (struct sockaddr*)&addr, sizeof(addr));
    if (sent < 0) {
        return LWConnError::SEND_FAILED;
    }

    return LWConnError::SUCCESS;
}

LWConnError LWUdp::receiveFrom(std::string& address, char* buffer, size_t buffer_size, size_t& received_size, int timeout_ms) {
    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (!socket_.valid()) {
        return LWConnError::NOT_CONNECTED;
    }

    int sock = socket_.get();

    if (timeout_ms == 0) {
        // Non-blocking: poll once, return immediately
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(sock, &read_set);
        struct timeval tv = {0, 0};
        int result = select(sock + 1, &read_set, nullptr, nullptr, &tv);
        if (result < 0) return LWConnError::RECEIVE_FAILED;
        else if (result == 0) return LWConnError::TIMEOUT;
    } else if (timeout_ms > 0) {
        // Wait up to timeout_ms
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(sock, &read_set);
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        int result = select(sock + 1, &read_set, nullptr, nullptr, &tv);
        if (result < 0) return LWConnError::RECEIVE_FAILED;
        else if (result == 0) return LWConnError::TIMEOUT;
    }
    // timeout_ms < 0: infinite wait (blocking recv, no select)

    sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    ssize_t received = recvfrom(sock, buffer, buffer_size, 0, (struct sockaddr*)&addr, &addr_len);
    if (received < 0) {
        return LWConnError::RECEIVE_FAILED;
    }

    // 构建地址字符串
    char host[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, host, sizeof(host));
    address = std::string(host) + ":" + std::to_string(ntohs(addr.sin_port));
    received_size = (size_t)received;
    
    return LWConnError::SUCCESS;
}

LWConnError LWUdp::disconnect() {
    stop();
    return LWConnError::SUCCESS;
}

bool LWUdp::isConnected() const {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    return socket_.valid();
}

void LWUdp::clearReceiveBuffer() {
    // UDP不需要清理接收缓冲区
}

int LWUdp::nativeHandle() const {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    return socket_.get();
}