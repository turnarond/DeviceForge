/*
* Copyright (c) 2026 ACOAUTO Team.
* All rights reserved.
*
* Detailed license information can be found in the LICENSE file.
*
* File: lwconn_pool.h .
*
* Date: 2026-02-05
*
* Author: Yan.chaodong <yanchaodong@acoinfo.com>
*
*/

#ifndef LWCONN_POOL_H
#define LWCONN_POOL_H

#include "lwconn_base.h"
#include <memory>
#include <map>
#include <vector>
#include <mutex>

// 连接池类
class LWConnPool {
public:
    static LWConnPool& instance();

    // 添加连接到池
    bool addConn(std::shared_ptr<LWConnBase> conn);

    // 从池获取连接
    std::shared_ptr<LWConnBase> getConn(const std::string& name);

    // 移除连接
    bool removeConn(const std::string& name);

    // 获取所有连接
    std::vector<std::shared_ptr<LWConnBase>> getAllConns() const;

    // 启动所有连接
    void startAll();

    // 停止所有连接
    void stopAll();

    // 清理池
    void clear();

private:
    LWConnPool();
    ~LWConnPool();

    std::map<std::string, std::shared_ptr<LWConnBase>> conn_map_;
    mutable std::mutex pool_mutex_;
};

#define CONN_POOL LWConnPool::instance()

#endif // LWCONN_POOL_H