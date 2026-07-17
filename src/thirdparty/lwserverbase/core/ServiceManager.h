/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: ServiceManager.h
 *
 * Date: 2026-03-03
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#pragma once

#include <memory>
#include <string>
#include <map>

namespace lwserverbase {
namespace vsoa_node {
class VsoaNode;
}
}

namespace lwserverbase {

// 前向声明外部命名空间的类
namespace config {
class ConfigManager;
}
namespace logging {
class Logger;
}
namespace metrics {
class MetricsCollector;
}
namespace process {
class ProcessController;
}

namespace core {

class ServiceBase;

// 使用外部命名空间的类
using config::ConfigManager;
using logging::Logger;
using metrics::MetricsCollector;
using process::ProcessController;

/**
 * @class ServiceManager
 * @brief 服务管理器，负责服务的生命周期管理和组件协调
 * 
 * ServiceManager 是框架的核心管理类，负责服务的启动、停止、配置管理等。
 * 它提供了注册各种组件的接口，并协调它们的工作。
 */
class ServiceManager {
public:
    /**
     * @brief 构造函数
     */
    ServiceManager();
    
    /**
     * @brief 析构函数
     */
    ~ServiceManager();
    
    /**
     * @brief 运行服务
     * @tparam ServiceT 服务类型
     * @param argc 命令行参数数量
     * @param argv 命令行参数数组
     * @return 服务退出码
     */
    template <typename ServiceT>
    static int Run(int argc, char** argv);

    // Entry point: Windows dispatches to SCM (StartServiceCtrlDispatcherA); service
    // mode blocks in ServiceMain (process force-terminated there), console/dev mode
    // and Linux run the lifecycle directly. VsoaNode::Run and Run<ServiceT> delegate
    // here instead of calling Initialize/Start/... by hand.
    int RunService(int argc, char** argv);

    // SCM service name (must equal the `sc create` name). Apps whose SCM name differs
    // from the ServiceBase name set this from main() before RunService. Empty -> fall
    // back to m_service->GetServiceName().
    static void SetScmServiceName(const std::string& name);
    static std::string GetScmServiceName();

    /**
     * @brief 启动服务
     * @return 0 表示成功，非 0 表示失败
     */
    int Start();
    
    /**
     * @brief 停止服务
     */
    void Stop();
    
    /**
     * @brief 重载配置
     */
    void ReloadConfig();
    
    /**
     * @brief 执行命令
     * @param cmd 命令名称
     * @param args 命令参数
     * @param resp 命令响应
     * @return true 表示命令执行成功，false 表示失败
     */
    bool ExecuteCommand(const std::string& cmd, const std::string& args, std::string& resp);
    
    /**
     * @brief 注册配置管理器
     * @param configManager 配置管理器指针
     */
    void RegisterConfigManager(std::unique_ptr<config::ConfigManager> configManager);
    
    /**
     * @brief 注册日志器
     * @param logger 日志器指针
     */
    void RegisterLogger(std::unique_ptr<logging::Logger> logger);
    
    /**
     * @brief 注册指标收集器
     * @param metricsCollector 指标收集器指针
     */
    void RegisterMetricsCollector(std::unique_ptr<metrics::MetricsCollector> metricsCollector);
    
    /**
     * @brief 注册进程控制器
     * @param processController 进程控制器指针
     */
    void RegisterProcessController(std::unique_ptr<process::ProcessController> processController);
    
    /**
     * @brief 获取配置管理器
     * @return 配置管理器指针
     */
    config::ConfigManager* GetConfigManager() const;
    
    /**
     * @brief 获取日志器
     * @return 日志器指针
     */
    logging::Logger* GetLogger() const;
    
    /**
     * @brief 获取指标收集器
     * @return 指标收集器指针
     */
    metrics::MetricsCollector* GetMetricsCollector() const;
    
    /**
     * @brief 获取进程控制器
     * @return 进程控制器指针
     */
    process::ProcessController* GetProcessController() const;
    
    /**
     * @brief 获取服务实例
     * @return 服务实例指针
     */
    ServiceBase* GetService() const;

    /**
     * @brief 设置服务实例
     * @param service 服务实例指针
     * @note 此方法由 VsoaNode::Run<T>() 等框架入口调用
     */
    void SetService(ServiceBase* service);

    /**
     * @brief 设置配置文件路径（必须在 Initialize 之前调用）
     */
    void SetConfigPath(const std::string& path) { m_configPath = path; }

    /**
     * @brief 初始化服务
     * @param argc 命令行参数数量
     * @param argv 命令行参数数组
     * @return 0 表示成功，非 0 表示失败
     */
    int Initialize(int argc, char* argv[]);

    /**
     * @brief 处理信号
     */
    void HandleSignals();

    /**
     * @brief 清理资源
     */
    void Cleanup();

    // Initialize -> Start -> [report RUNNING if service mode] -> HandleSignals -> Stop -> Cleanup.
    int RunLifecycle(int argc, char** argv, bool report_running);

private:
    ServiceBase* m_service; ///< 服务实例指针
    std::unique_ptr<config::ConfigManager> m_configManager; ///< 配置管理器
    std::unique_ptr<logging::Logger> m_logger; ///< 日志器
    std::unique_ptr<metrics::MetricsCollector> m_metricsCollector; ///< 指标收集器
    std::unique_ptr<process::ProcessController> m_processController; ///< 进程控制器
    std::string m_configPath = "config.json"; ///< 配置文件路径
    int m_argc; ///< 命令行参数数量
    char** m_argv; ///< 命令行参数数组
    bool m_running; ///< 服务运行状态
};

// 模板方法实现
template <typename ServiceT>
int ServiceManager::Run(int argc, char** argv) {
    ServiceManager manager;
    auto service = std::make_unique<ServiceT>();
    service->SetServiceManager(&manager);
    manager.m_service = service.get();
    return manager.RunService(argc, argv);
}

} // namespace core
} // namespace lwserverbase