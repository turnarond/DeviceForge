/*
* Copyright (c) 2026 ACOAUTO Team.
* All rights reserved.
*
* Detailed license information can be found in the LICENSE file.
*
* File: lwconn_manager.cpp .
*
* Date: 2026-02-05
*
* Author: Yan.chaodong <yanchaodong@acoinfo.com>
*
*/

#include "lwconn_manager.h"

// 连接管理器实现
LWConnManager& LWConnManager::instance() {
    static LWConnManager instance;
    return instance;
}

LWConnManager::LWConnManager()
    : initialized_(false) {
}

LWConnManager::~LWConnManager() {
    uninit();
}

bool LWConnManager::init() {
    if (!initialized_) {
        initialized_ = true;
    }
    return true;
}

void LWConnManager::uninit() {
    if (initialized_) {
        CONN_POOL.clear();
        initialized_ = false;
    }
}

std::shared_ptr<LWTcpClient> LWConnManager::createTcpClient(const std::string& name,
                                                           const std::string& host,
                                                           int port) {
    auto client = std::make_shared<LWTcpClient>(name, host, port);
    CONN_POOL.addConn(client);
    return client;
}

std::shared_ptr<LWTcpServer> LWConnManager::createTcpServer(const std::string& name, 
                                                           const std::string& listen_address, 
                                                           int port) {
    auto server = std::make_shared<LWTcpServer>(name, listen_address, port);
    CONN_POOL.addConn(server);
    return server;
}

std::shared_ptr<LWUdp> LWConnManager::createUdp(const std::string& name, 
                                               int local_port, 
                                               const std::string& remote_address, 
                                               int remote_port) {
    auto udp = std::make_shared<LWUdp>(name, local_port, remote_address, remote_port);
    CONN_POOL.addConn(udp);
    return udp;
}

std::shared_ptr<LWSerial> LWConnManager::createSerial(const std::string& name, 
                                                     const std::string& port, 
                                                     int baudrate, 
                                                     char parity, 
                                                     int databits, 
                                                     int stopbits) {
    auto serial = std::make_shared<LWSerial>(name, port, baudrate, parity, databits, stopbits);
    CONN_POOL.addConn(serial);
    return serial;
}

LWConnPool& LWConnManager::getConnPool() {
    return CONN_POOL;
}