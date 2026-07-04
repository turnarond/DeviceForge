/*
* Copyright (c) 2026 ACOAUTO Team.
* All rights reserved.
*
* Detailed license information can be found in the LICENSE file.
*
* File: lwconn_tcp_server.h .
*
* Date: 2026-02-05
*
* Author: Yan.chaodong <yanchaodong@acoinfo.com>
*
*/

#ifndef LWCONN_TCP_SERVER_H
#define LWCONN_TCP_SERVER_H

#include "lwconn_base.h"

#include "lwconn_internal.h"

#include <vector>
#include <map>
#include <queue>

// TCP服务端连接类
class LWTcpServer : public LWConnBase {
public:
    LWTcpServer(const std::string& name, const std::string& listen_address, int port,
                int backlog = 16);
    ~LWTcpServer() override;

    LWConnError start() override;
    void stop() override;
    LWConnError send(const char* data, size_t length, int timeout_ms = 0) override;
    LWConnError receive(char* buffer, size_t buffer_size, size_t& received_size, int timeout_ms = 0) override;
    LWConnError disconnect() override;
    bool isConnected() const override;
    void clearReceiveBuffer() override;
    int nativeHandle() const override;

    // 获取最近一次接收数据的发送者地址
    std::string getLastSender() const;

    // 发送数据到指定客户端
    LWConnError sendToClient(const std::string& client_address, const char* data, size_t length, int timeout_ms = 0);

    // Broadcast data to all connected clients
    LWConnError broadcast(const char* data, size_t length, int timeout_ms = 0);

    // 获取连接的客户端列表
    std::vector<std::string> getConnectedClients() const;

    // 设置监听 backlog
    void setBacklog(int backlog);

private:
    std::string listen_address_;
    int port_;
    int backlog_;
    lwcomm::internal::ScopedFd server_socket_;
    std::map<std::string, lwcomm::internal::ScopedFd> client_sockets_;
    mutable std::mutex clients_mutex_;

    struct PendingData {
        std::string sender;
        std::vector<char> data;
    };
    std::queue<PendingData> pending_queue_;
    mutable std::string last_sender_;
};

#endif // LWCONN_TCP_SERVER_H