/*
* Copyright (c) 2026 ACOAUTO Team.
* All rights reserved.
*
* Detailed license information can be found in the LICENSE file.
*
* File: lwconn_pool.cpp .
*
* Date: 2026-02-05
*
* Author: Yan.chaodong <yanchaodong@acoinfo.com>
*
*/

#include "lwconn_pool.h"
#include <iostream>

// 连接池类实现
LWConnPool& LWConnPool::instance() {
    static LWConnPool instance;
    return instance;
}

LWConnPool::LWConnPool() {
    std::cout << "LWConnPool created" << std::endl;
}

LWConnPool::~LWConnPool() {
    clear();
    std::cout << "LWConnPool destroyed" << std::endl;
}

bool LWConnPool::addConn(std::shared_ptr<LWConnBase> conn) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    const std::string& name = conn->getName();
    if (conn_map_.find(name) != conn_map_.end()) {
        std::cerr << "LWConnPool::addConn: connection already exists: " << name << std::endl;
        return false;
    }
    
    conn_map_[name] = conn;
    std::cout << "LWConnPool::addConn: added connection: " << name << std::endl;
    return true;
}

std::shared_ptr<LWConnBase> LWConnPool::getConn(const std::string& name) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    auto it = conn_map_.find(name);
    if (it == conn_map_.end()) {
        std::cerr << "LWConnPool::getConn: connection not found: " << name << std::endl;
        return nullptr;
    }
    
    return it->second;
}

bool LWConnPool::removeConn(const std::string& name) {
    std::lock_guard<std::mutex> lock(pool_mutex_);

    auto it = conn_map_.find(name);
    if (it == conn_map_.end()) {
        std::cerr << "LWConnPool::removeConn: connection not found: " << name << std::endl;
        return false;
    }
    it->second->stop();  // Stop before erasing
    conn_map_.erase(it);
    std::cout << "LWConnPool::removeConn: removed connection: " << name << std::endl;
    return true;
}

std::vector<std::shared_ptr<LWConnBase>> LWConnPool::getAllConns() const {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    std::vector<std::shared_ptr<LWConnBase>> conns;
    for (auto& conn : conn_map_) {
        conns.push_back(conn.second);
    }
    return conns;
}

void LWConnPool::startAll() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    for (auto& conn : conn_map_) {
        conn.second->start();
    }
}

void LWConnPool::stopAll() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    for (auto& conn : conn_map_) {
        conn.second->stop();
    }
}

void LWConnPool::clear() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    for (auto& conn : conn_map_) {
        conn.second->stop();
    }
    conn_map_.clear();
    std::cout << "LWConnPool::clear: cleared" << std::endl;
}