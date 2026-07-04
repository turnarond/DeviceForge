/*
* Copyright (c) 2026 ACOAUTO Team.
* All rights reserved.
*
* Detailed license information can be found in the LICENSE file.
*
* File: lwconn_udp.h .
*
* Date: 2026-02-05
*
* Author: Yan.chaodong <yanchaodong@acoinfo.com>
*
*/

#ifndef LWCONN_UDP_H
#define LWCONN_UDP_H

#include "lwconn_base.h"
#include "lwconn_internal.h"

// UDP连接类
class LWUdp : public LWConnBase {
public:
    LWUdp(const std::string& name, int local_port, const std::string& remote_address = "", int remote_port = 0);
    ~LWUdp() override;

    LWConnError start() override;
    void stop() override;
    LWConnError send(const char* data, size_t length, int timeout_ms = 0) override;
    LWConnError receive(char* buffer, size_t buffer_size, size_t& received_size, int timeout_ms = 0) override;
    LWConnError sendTo(const std::string& address, const char* data, size_t length, int timeout_ms = 0) override;
    LWConnError receiveFrom(std::string& address, char* buffer, size_t buffer_size, size_t& received_size, int timeout_ms = 0) override;
    LWConnError disconnect() override;
    bool isConnected() const override;
    void clearReceiveBuffer() override;
    int nativeHandle() const override;

private:
    int local_port_;
    std::string remote_address_;
    int remote_port_;
    lwcomm::internal::ScopedFd socket_; // 内部socket指针
    mutable std::mutex socket_mutex_;
};

#endif // LWCONN_UDP_H