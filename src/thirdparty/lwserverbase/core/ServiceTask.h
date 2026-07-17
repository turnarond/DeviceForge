/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * File: ServiceTask.h
 *
 * Date: 2026-04-08
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: 服务任务基类，自动管理线程生命周期
 * 
 * 功能说明：
 * - 类似 ACE_Task，自动创建和管理线程
 * - 提供 svc() 虚函数作为线程入口
 * - 支持多线程模式
 * - 自动资源管理
 */

#pragma once

#include <core/ServiceBase.h>
#include <thread>
#include <vector>
#include <atomic>

namespace lwserverbase {
namespace core {

/**
 * @class ServiceTask
 * @brief 服务任务基类，自动管理线程
 * 
 * 继承自 ServiceBase，添加自动线程管理功能。
 * 用户只需实现 svc() 方法，框架会自动创建线程并调用。
 * 
 * 使用示例：
 * ```cpp
 * class MyService : public ServiceTask {
 * public:
 *     int svc() override {
 *         while (running_) {
 *             // 业务逻辑
 *             std::this_thread::sleep_for(std::chrono::seconds(1));
 *         }
 *         return 0;
 *     }
 * };
 * ```
 */
class ServiceTask : public ServiceBase {
public:
    /**
     * @brief 构造函数
     */
    ServiceTask();
    
    /**
     * @brief 析构函数
     */
    virtual ~ServiceTask();
    
    /**
     * @brief 服务启动时调用
     * @param argc 命令行参数数量
     * @param argv 命令行参数数组
     * @return 0 表示成功，非 0 表示失败
     * 
     * 此方法会自动创建线程并调用 svc()
     */
    int OnStart(int argc, char* argv[]) override;
    void OnStop() override;
    
    /**
     * @brief 线程运行函数（纯虚函数）
     * @return 线程退出码
     * 
     * 子类必须实现此方法，作为线程的入口函数
     */
    virtual int svc() = 0;
    
    /**
     * @brief 启动线程
     * @param num_threads 线程数量，默认为 1
     * @return true 表示成功，false 表示失败
     * 
     * 可以在 OnStart 中调用此方法来启动多个线程
     */
    bool activate(int num_threads = 1);
    
    /**
     * @brief 等待所有线程结束
     * 
     * 会在 OnStop 中自动调用
     */
    void wait();
    
    /**
     * @brief 获取线程数量
     * @return 线程数量
     */
    int get_thread_count() const;
    
    /**
     * @brief 检查服务是否正在运行
     * @return true 表示正在运行，false 表示已停止
     */
    bool isRunning() const;

    /**
     * @brief 检查服务是否正在运行（兼容旧名）
     * @deprecated 请使用 isRunning()
     */
    bool is_running() const { return isRunning(); }

protected:
    /**
     * @brief 设置运行标志为 true（启动 svc 线程前调用）
     *
     * 供框架内部（VsoaNode::OnStart）使用。
     */
    void startRunning();

    /**
     * @brief 请求关闭服务
     *
     * 设置运行标志为 false，使 svc() 线程退出。
     * 用户可在 OnShutdown() 或自定义逻辑中调用。
     */
    void requestShutdown();

private:
    std::atomic<bool> running_; ///< 运行状态标志
    std::vector<std::thread> threads_; ///< 线程池
    int num_threads_; ///< 线程数量
};

} // namespace core
} // namespace lwserverbase
