/*
* Copyright (c) 2026 ACOAUTO Team.
* All rights reserved.
*
* Detailed license information can be found in the LICENSE file.
*
* File: lwconn_tcp_client.h .
*
* Date: 2026-02-05
*
* Author: Yan.chaodong <yanchaodong@acoinfo.com>
*
*/

#ifndef LWCONN_TCP_CLIENT_H
#define LWCONN_TCP_CLIENT_H

#include "lwconn_base.h"
#include "lwconn_internal.h"

// TCP客户端连接类 (纯传输层，不管理重连策略)
class LWTcpClient : public LWConnBase {
public:
    LWTcpClient(const std::string& name, const std::string& host, int port);
    ~LWTcpClient() override;

    LWConnError start() override;
    LWConnError start(int connect_timeout_ms);  // with connect timeout
    void stop() override;
    LWConnError send(const char* data, size_t length, int timeout_ms = 0) override;
    LWConnError receive(char* buffer, size_t buffer_size,
                        size_t& received_size, int timeout_ms = 0) override;
    LWConnError disconnect() override;
    bool isConnected() const override;
    void clearReceiveBuffer() override;
    int nativeHandle() const override;

private:
    std::string host_;
    int port_;
    lwcomm::internal::ScopedFd socket_;
    mutable std::mutex socket_mutex_;
};

#endif // LWCONN_TCP_CLIENT_H
