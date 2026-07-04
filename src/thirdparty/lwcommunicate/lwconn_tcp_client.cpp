/*
* Copyright (c) 2026 ACOAUTO Team.
* All rights reserved.
*
* Detailed license information can be found in the LICENSE file.
*
* File: lwconn_tcp_client.cpp .
*
* Date: 2026-02-05
*
* Author: Yan.chaodong <yanchaodong@acoinfo.com>
*
*/

#include "lwconn_tcp_client.h"
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
#define LWCONN_ERRNO WSAGetLastError()
#define LWCONN_EINPROGRESS WSAEWOULDBLOCK
typedef SSIZE_T ssize_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#define LWCONN_ERRNO errno
#define LWCONN_EINPROGRESS EINPROGRESS
#endif

// TCP客户端连接类实现
LWTcpClient::LWTcpClient(const std::string& name, const std::string& host, int port)
    : LWConnBase(LWConnType::TCP_CLIENT, name),
      host_(host),
      port_(port) {
}

LWTcpClient::~LWTcpClient() {
    stop();
}

LWConnError LWTcpClient::start() {
    return start(0);  // 0 = blocking connect (original behavior)
}

LWConnError LWTcpClient::start(int connect_timeout_ms) {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (socket_.valid()) return LWConnError::SUCCESS;
    this->updateStatus(LWConnStatus::CONNECTING);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        this->log(LWLogLevel::ERROR, "LWTcpClient::start: socket creation failed");
        this->updateStatus(LWConnStatus::ERROR);
        return LWConnError::CONNECTION_FAILED;
    }

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
        this->log(LWLogLevel::ERROR, "LWTcpClient::start: invalid address: %s", host_.c_str());
        LWCONN_CLOSE_SOCKET(sock);
        this->updateStatus(LWConnStatus::ERROR);
        return LWConnError::INVALID_PARAM;
    }

    if (connect_timeout_ms > 0) {
#ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(sock, FIONBIO, &mode);
#else
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
        int ret = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
        if (ret < 0 && LWCONN_ERRNO == LWCONN_EINPROGRESS) {
            fd_set write_set;
            FD_ZERO(&write_set);
            FD_SET(sock, &write_set);
            struct timeval tv;
            tv.tv_sec = connect_timeout_ms / 1000;
            tv.tv_usec = (connect_timeout_ms % 1000) * 1000;
            ret = select(sock + 1, nullptr, &write_set, nullptr, &tv);
            if (ret <= 0) {
                LWCONN_CLOSE_SOCKET(sock);
                this->updateStatus(LWConnStatus::DISCONNECTED);
                return (ret == 0) ? LWConnError::TIMEOUT : LWConnError::CONNECTION_FAILED;
            }
            int so_error = 0;
            #ifdef _WIN32
            int len = sizeof(so_error);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&so_error, &len);
            #else
            socklen_t socklen = sizeof(so_error);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &socklen);
            #endif
            if (so_error != 0) {
                LWCONN_CLOSE_SOCKET(sock);
                this->updateStatus(LWConnStatus::DISCONNECTED);
                return LWConnError::CONNECTION_FAILED;
            }
#ifdef _WIN32
            mode = 0;
            ioctlsocket(sock, FIONBIO, &mode);
#else
            fcntl(sock, F_SETFL, flags);
#endif
        } else if (ret < 0) {
            LWCONN_CLOSE_SOCKET(sock);
            this->updateStatus(LWConnStatus::DISCONNECTED);
            return LWConnError::CONNECTION_FAILED;
        }
    } else {
        // Blocking connect (original behavior)
        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            this->log(LWLogLevel::ERROR, "LWTcpClient::start: connect failed: %s", host_.c_str());
            LWCONN_CLOSE_SOCKET(sock);
            this->updateStatus(LWConnStatus::DISCONNECTED);
            return LWConnError::CONNECTION_FAILED;
        }
    }

    socket_.reset(sock);
    this->updateStatus(LWConnStatus::CONNECTED);
    this->notifyConnChange(true, host_ + ":" + std::to_string(port_));
    this->log(LWLogLevel::DEBUG, "LWTcpClient::start: connected to %s:%d", host_.c_str(), port_);
    return LWConnError::SUCCESS;
}

void LWTcpClient::stop() {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (socket_.valid()) {
        socket_.reset();
        this->updateStatus(LWConnStatus::DISCONNECTED);
        this->notifyConnChange(false, "");
        this->log(LWLogLevel::DEBUG, "LWTcpClient::stop: disconnected");
    }
}

LWConnError LWTcpClient::send(const char* data, size_t length, int timeout_ms) {
    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (!socket_.valid()) {
        return LWConnError::NOT_CONNECTED;
    }

    int sock = socket_.get();
    ssize_t sent = ::send(sock, data, (int)length, 0);

    if (sent < 0) {
        this->log(LWLogLevel::ERROR, "LWTcpClient::send: send failed");
        return LWConnError::SEND_FAILED;
    }

    if ((size_t)sent != length) {
        this->log(LWLogLevel::ERROR, "LWTcpClient::send: partial send");
        return LWConnError::SEND_FAILED;
    }

    return LWConnError::SUCCESS;
}

LWConnError LWTcpClient::receive(char* buffer, size_t buffer_size, size_t& received_size, int timeout_ms) {
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

    ssize_t received = recv(sock, buffer, (int)buffer_size, 0);
    if (received < 0) {
        this->log(LWLogLevel::ERROR, "LWTcpClient::receive: recv failed");
        return LWConnError::RECEIVE_FAILED;
    } else if (received == 0) {
        // 连接关闭 - 直接内联清理，因为已持有socket_mutex_，不能调用stop()
        socket_.reset();
        this->updateStatus(LWConnStatus::DISCONNECTED);
        this->notifyConnChange(false, "");
        this->log(LWLogLevel::DEBUG, "LWTcpClient::receive: peer disconnected");
        return LWConnError::NOT_CONNECTED;
    }

    received_size = (size_t)received;
    return LWConnError::SUCCESS;
}

LWConnError LWTcpClient::disconnect() {
    stop();
    return LWConnError::SUCCESS;
}

bool LWTcpClient::isConnected() const {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    return socket_.valid();
}

void LWTcpClient::clearReceiveBuffer() {
    // No-op - receive buffer was removed (was dead code)
}

int LWTcpClient::nativeHandle() const {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    return socket_.get();
}
