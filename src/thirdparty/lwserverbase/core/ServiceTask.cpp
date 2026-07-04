/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * File: ServiceTask.cpp
 *
 * Date: 2026-04-08
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: 服务任务基类实现
 */

#include <core/ServiceTask.h>

namespace lwserverbase {
namespace core {

ServiceTask::ServiceTask()
    : running_(false)
    , num_threads_(0) {
}

ServiceTask::~ServiceTask() {
    // 确保线程已停止
    if (running_) {
        OnStop();
    }
}

int ServiceTask::OnStart(int argc, char* argv[]) {
    running_ = true;
    
    // 默认启动 1 个线程
    if (!activate(1)) {
        return -1;
    }
    
    return 0;
}

void ServiceTask::OnStop() {
    requestShutdown();
    wait();
}

bool ServiceTask::activate(int num_threads) {
    if (num_threads <= 0) {
        return false;
    }
    
    num_threads_ = num_threads;
    threads_.reserve(num_threads);
    
    for (int i = 0; i < num_threads; ++i) {
        try {
            threads_.emplace_back(&ServiceTask::svc, this);
        } catch (const std::exception& e) {
            // 启动失败，停止已启动的线程
            requestShutdown();
            wait();
            return false;
        }
    }
    
    return true;
}

void ServiceTask::wait() {
    for (auto& t : threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    threads_.clear();
    num_threads_ = 0;
}

int ServiceTask::get_thread_count() const {
    return num_threads_;
}

bool ServiceTask::isRunning() const {
    return running_.load(std::memory_order_acquire);
}

void ServiceTask::startRunning() {
    running_.store(true, std::memory_order_release);
}

void ServiceTask::requestShutdown() {
    running_.store(false, std::memory_order_release);
}

} // namespace core
} // namespace lwserverbase
