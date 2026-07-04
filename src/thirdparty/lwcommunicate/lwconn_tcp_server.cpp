/*
* Copyright (c) 2026 ACOAUTO Team.
* All rights reserved.
*
* Detailed license information can be found in the LICENSE file.
*
* File: lwconn_tcp_server.cpp .
*
* Date: 2026-02-05
*
* Author: Yan.chaodong <yanchaodong@acoinfo.com>
*
*/

#include "lwconn_tcp_server.h"
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

// TCP服务端连接类实现
LWTcpServer::LWTcpServer(const std::string& name, const std::string& listen_address, int port, int backlog)
    : LWConnBase(LWConnType::TCP_SERVER, name),
      listen_address_(listen_address),
      port_(port),
      backlog_(backlog) {
}

void LWTcpServer::setBacklog(int backlog) {
    backlog_ = backlog;
}

LWTcpServer::~LWTcpServer() {
    stop();
}

LWConnError LWTcpServer::start() {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    if (server_socket_.valid()) {
        return LWConnError::SUCCESS;
    }

    this->updateStatus(LWConnStatus::CONNECTING);
    
    // 创建socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        this->log(LWLogLevel::ERROR, "LWTcpServer::start: socket creation failed");
        this->updateStatus(LWConnStatus::ERROR);
        return LWConnError::CONNECTION_FAILED;
    }

    // 设置地址重用
    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, (int)sizeof(opt)) < 0) {
        this->log(LWLogLevel::ERROR, "LWTcpServer::start: setsockopt failed");
        LWCONN_CLOSE_SOCKET(sock);
        this->updateStatus(LWConnStatus::ERROR);
        return LWConnError::INTERNAL_ERROR;
    }

    // 绑定地址
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    
    if (listen_address_.empty() || listen_address_ == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, listen_address_.c_str(), &addr.sin_addr) <= 0) {
            this->log(LWLogLevel::ERROR, "LWTcpServer::start: invalid address: %s", listen_address_.c_str());
            LWCONN_CLOSE_SOCKET(sock);
            this->updateStatus(LWConnStatus::ERROR);
            return LWConnError::INVALID_PARAM;
        }
    }

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        this->log(LWLogLevel::ERROR, "LWTcpServer::start: bind failed");
        LWCONN_CLOSE_SOCKET(sock);
        this->updateStatus(LWConnStatus::ERROR);
        return LWConnError::CONNECTION_FAILED;
    }

    // 开始监听
    if (listen(sock, backlog_) < 0) {
        this->log(LWLogLevel::ERROR, "LWTcpServer::start: listen failed");
        LWCONN_CLOSE_SOCKET(sock);
        this->updateStatus(LWConnStatus::ERROR);
        return LWConnError::CONNECTION_FAILED;
    }

    server_socket_.reset(sock);
    this->updateStatus(LWConnStatus::CONNECTED);
    this->notifyConnChange(true, listen_address_ + ":" + std::to_string(port_));
    
    this->log(LWLogLevel::DEBUG, "LWTcpServer::start: listening on %s:%d", listen_address_.c_str(), port_);
    return LWConnError::SUCCESS;
}

void LWTcpServer::stop() {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    // client_sockets_.clear() auto-closes all client fds via ScopedFd destructor
    client_sockets_.clear();

    // 关闭服务器socket
    server_socket_.reset();
    this->updateStatus(LWConnStatus::DISCONNECTED);
    this->notifyConnChange(false, "");
    this->log(LWLogLevel::DEBUG, "LWTcpServer::stop: stopped");
}

LWConnError LWTcpServer::send(const char* data, size_t length, int timeout_ms) {
    (void)data; (void)length; (void)timeout_ms;
    return LWConnError::INVALID_PARAM;
}

LWConnError LWTcpServer::broadcast(const char* data, size_t length, int timeout_ms) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    if (client_sockets_.empty()) {
        return LWConnError::NOT_CONNECTED;
    }
    bool success = true;
    for (auto& client : client_sockets_) {
        int sock = client.second.get();
        ssize_t sent = ::send(sock, data, length, 0);
        if (sent < 0 || (size_t)sent != length) {
            success = false;
        }
    }
    return success ? LWConnError::SUCCESS : LWConnError::SEND_FAILED;
}

LWConnError LWTcpServer::receive(char* buffer, size_t buffer_size,
                                  size_t& received_size, int timeout_ms) {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    // Drain pending queue first
    if (!pending_queue_.empty()) {
        auto& pending = pending_queue_.front();
        size_t copy_len = std::min(pending.data.size(), buffer_size);
        memcpy(buffer, pending.data.data(), copy_len);
        received_size = copy_len;
        last_sender_ = pending.sender;
        pending_queue_.pop();
        return LWConnError::SUCCESS;
    }

    if (client_sockets_.empty() && !server_socket_.valid()) {
        return LWConnError::NOT_CONNECTED;
    }

    if (!server_socket_.valid()) {
        return LWConnError::NOT_CONNECTED;
    }

    fd_set read_set;
    FD_ZERO(&read_set);
    int max_fd = server_socket_.get();
    FD_SET(server_socket_.get(), &read_set);

    for (auto& client : client_sockets_) {
        int fd = client.second.get();
        FD_SET(fd, &read_set);
        if (fd > max_fd) max_fd = fd;
    }

    struct timeval tv;
    int result;
    if (timeout_ms == 0) {
        // Non-blocking: poll once, return immediately
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        result = select(max_fd + 1, &read_set, nullptr, nullptr, &tv);
        if (result < 0) return LWConnError::RECEIVE_FAILED;
        else if (result == 0) return LWConnError::TIMEOUT;
    } else if (timeout_ms > 0) {
        // Wait up to timeout_ms
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        result = select(max_fd + 1, &read_set, nullptr, nullptr, &tv);
        if (result < 0) return LWConnError::RECEIVE_FAILED;
        else if (result == 0) return LWConnError::TIMEOUT;
    } else {
        // timeout_ms < 0: infinite wait (blocking)
        result = select(max_fd + 1, &read_set, nullptr, nullptr, nullptr);
        if (result < 0) return LWConnError::RECEIVE_FAILED;
    }

    // Accept new connections
    if (FD_ISSET(server_socket_.get(), &read_set)) {
        sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_sock = accept(server_socket_.get(),
                                  (struct sockaddr*)&client_addr,
                                  &client_addr_len);
        if (client_sock >= 0) {
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
            std::string client_address = std::string(client_ip) + ":" +
                                         std::to_string(ntohs(client_addr.sin_port));
            client_sockets_[client_address] = lwcomm::internal::ScopedFd(client_sock);
            this->log(LWLogLevel::DEBUG, "LWTcpServer::receive: new client connected: %s",
                client_address.c_str());
        }
    }

    // Collect data from ALL readable clients into pending queue
    for (auto it = client_sockets_.begin(); it != client_sockets_.end(); ) {
        int client_sock = it->second.get();
        if (FD_ISSET(client_sock, &read_set)) {
            std::vector<char> tmp_buf(buffer_size);
            ssize_t received = recv(client_sock, tmp_buf.data(), buffer_size, 0);
            if (received < 0) {
                it = client_sockets_.erase(it);  // ScopedFd closes fd
            } else if (received == 0) {
                this->log(LWLogLevel::DEBUG, "LWTcpServer: client disconnected: %s",
                    it->first.c_str());
                it = client_sockets_.erase(it);
            } else {
                PendingData pd;
                pd.sender = it->first;
                pd.data.assign(tmp_buf.data(), tmp_buf.data() + received);
                pending_queue_.push(std::move(pd));
                ++it;
            }
        } else {
            ++it;
        }
    }

    // Return first item from queue
    if (!pending_queue_.empty()) {
        auto& pending = pending_queue_.front();
        size_t copy_len = std::min(pending.data.size(), buffer_size);
        memcpy(buffer, pending.data.data(), copy_len);
        received_size = copy_len;
        last_sender_ = pending.sender;
        pending_queue_.pop();
        return LWConnError::SUCCESS;
    }

    return LWConnError::RECEIVE_FAILED;
}

LWConnError LWTcpServer::disconnect() {
    stop();
    return LWConnError::SUCCESS;
}

bool LWTcpServer::isConnected() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return server_socket_.valid();
}

void LWTcpServer::clearReceiveBuffer() {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    std::queue<PendingData> empty;
    std::swap(pending_queue_, empty);
}

int LWTcpServer::nativeHandle() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return server_socket_.get();
}

std::string LWTcpServer::getLastSender() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return last_sender_;
}

LWConnError LWTcpServer::sendToClient(const std::string& client_address, const char* data, size_t length, int timeout_ms) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    auto it = client_sockets_.find(client_address);
    if (it == client_sockets_.end()) {
        return LWConnError::NOT_CONNECTED;
    }

    int sock = it->second.get();
    ssize_t sent = ::send(sock, data, length, 0);
    if (sent < 0 || (size_t)sent != length) {
        return LWConnError::SEND_FAILED;
    }

    return LWConnError::SUCCESS;
}

std::vector<std::string> LWTcpServer::getConnectedClients() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    std::vector<std::string> clients;
    for (auto& client : client_sockets_) {
        clients.push_back(client.first);
    }
    return clients;
}