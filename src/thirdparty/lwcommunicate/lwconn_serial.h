/*
* Copyright (c) 2026 ACOAUTO Team.
* All rights reserved.
*
* Detailed license information can be found in the LICENSE file.
*
* File: lwconn_serial.h .
*
* Date: 2026-02-05
*
* Author: Yan.chaodong <yanchaodong@acoinfo.com>
*
*/

#ifndef LWCONN_SERIAL_H
#define LWCONN_SERIAL_H

#include "lwconn_base.h"
#include "lwconn_internal.h"

// 串口连接类
class LWSerial : public LWConnBase {
public:
    LWSerial(const std::string& name, const std::string& port, int baudrate, 
             char parity = 'N', int databits = 8, int stopbits = 1);
    ~LWSerial() override;

    LWConnError start() override;
    void stop() override;
    LWConnError send(const char* data, size_t length, int timeout_ms = 0) override;
    LWConnError receive(char* buffer, size_t buffer_size, size_t& received_size, int timeout_ms = 0) override;
    LWConnError disconnect() override;
    bool isConnected() const override;
    void clearReceiveBuffer() override;
    int nativeHandle() const override;

private:
    std::string port_;
    int baudrate_;
    char parity_;
    int databits_;
    int stopbits_;
#ifdef _WIN32
    lwcomm::internal::ScopedHandle serial_handle_; // Windows 串口句柄
#else
    lwcomm::internal::ScopedFd serial_handle_; // Linux 串口文件描述符
#endif
    mutable std::mutex serial_mutex_;
};

#endif // LWCONN_SERIAL_H