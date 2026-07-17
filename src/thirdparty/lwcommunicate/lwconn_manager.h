/*
* Copyright (c) 2026 ACOAUTO Team.
* All rights reserved.
*
* Detailed license information can be found in the LICENSE file.
*
* File: lwconn_manager.h .
*
* Date: 2026-02-05
*
* Author: Yan.chaodong <yanchaodong@acoinfo.com>
*
*/

#ifndef LWCONN_MANAGER_H
#define LWCONN_MANAGER_H

#include "lwconn_pool.h"
#include "lwconn_tcp_client.h"
#include "lwconn_tcp_server.h"
#include "lwconn_udp.h"
#include "lwconn_serial.h"
#include <memory>

// 连接管理器
class LWConnManager {
public:
    static LWConnManager& instance();

    // 初始化
    bool init();

    // 反初始化
    void uninit();

    // 创建TCP客户端连接
    std::shared_ptr<LWTcpClient> createTcpClient(const std::string& name,
                                                const std::string& host,
                                                int port);

    // 创建TCP服务端连接
    std::shared_ptr<LWTcpServer> createTcpServer(const std::string& name, 
                                                const std::string& listen_address, 
                                                int port);

    // 创建UDP连接
    std::shared_ptr<LWUdp> createUdp(const std::string& name, 
                                    int local_port, 
                                    const std::string& remote_address = "", 
                                    int remote_port = 0);

    // 创建串口连接
    std::shared_ptr<LWSerial> createSerial(const std::string& name, 
                                          const std::string& port, 
                                          int baudrate, 
                                          char parity = 'N', 
                                          int databits = 8, 
                                          int stopbits = 1);

    // 获取连接池
    LWConnPool& getConnPool();

private:
    LWConnManager();
    ~LWConnManager();

    bool initialized_;
    // LWConnPool& conn_pool_;
};

#define CONN_MANAGER LWConnManager::instance()

#endif // LWCONN_MANAGER_H