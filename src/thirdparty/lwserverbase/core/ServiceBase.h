/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: ServiceBase.h
 *
 * Date: 2026-03-03
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#pragma once

#include <string>
#include <vector>

namespace lwserverbase {
namespace core {

class ServiceManager;

/**
 * @class ServiceBase
 * @brief 服务基类，用户继承此类实现具体服务
 * 
 * ServiceBase 是框架的核心类，提供服务的基本生命周期管理和回调接口。
 * 用户需要继承此类并实现 OnStart 和 OnStop 方法来定义服务的启动和停止逻辑。
 */
class ServiceBase {
public:
    /**
     * @brief 构造函数
     */
    ServiceBase();
    
    /**
     * @brief 析构函数
     */
    virtual ~ServiceBase();
    
    /**
     * @brief 服务启动时调用
     * @param argc 命令行参数数量
     * @param argv 命令行参数数组
     * @return 0 表示成功，非 0 表示失败
     */
    virtual int OnStart(int argc, char* argv[]) = 0;
    
    /**
     * @brief 服务停止时调用
     */
    virtual void OnStop() = 0;
    
    /**
     * @brief 配置重载时调用
     * @note 子类可重写此方法以处理配置变更
     */
    virtual void OnReloadConfig();
    
    /**
     * @brief 健康检查时调用
     * @note 子类可重写此方法以实现健康检查逻辑
     */
    virtual void OnHealthCheck();
    
    /**
     * @brief 处理控制命令
     * @param cmd 命令名称
     * @param args 命令参数
     * @param resp 命令响应
     * @return true 表示命令处理成功，false 表示失败
     * @note 子类可重写此方法以处理自定义命令
     */
    virtual bool OnCommand(const std::string& cmd, const std::string& args, std::string& resp);
    
    /**
     * @brief 获取服务管理器
     * @return 服务管理器指针
     */
    ServiceManager* GetServiceManager() const;
    
    /**
     * @brief 设置服务管理器
     * @param manager 服务管理器指针
     * @note 此方法由框架内部调用，用户不应直接调用
     */
    void SetServiceManager(ServiceManager* manager);
    
    /**
     * @brief 获取服务名称
     * @return 服务名称
     */
    virtual std::string GetServiceName() const;
    
    /**
     * @brief 获取服务版本
     * @return 服务版本
     */
    virtual std::string GetServiceVersion() const;
    
    /**
     * @brief 获取服务描述
     * @return 服务描述
     */
    virtual std::string GetServiceDescription() const;

private:
    ServiceManager* m_serviceManager; ///< 服务管理器指针
};

} // namespace core
} // namespace lwserverbase