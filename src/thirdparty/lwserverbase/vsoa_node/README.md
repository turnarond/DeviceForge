# VSOA Node Framework

基于 `lwserverbase` 微服务框架的 VSOA Node 抽象层。让用户以 "ROS 节点" 的思维模型开发 VSOA 微服务。

## 概述

VSOA Node Framework 提供 `VsoaNode` 基类（继承自 `ServiceTask`），用户只需继承并实现几个回调函数即可获得一个完整的 VSOA 微服务节点，框架自动处理：

- VSOA 服务端/客户端创建和管理
- 服务自动注册到 `vsoa_master`
- 7 个内置管理 URL（`/health`、`/version`、`/config`、`/metrics`、`/cmd`、`/help`、`/urls`）
- 统一日志格式（时间戳、级别、服务名、线程ID）
- 健康检查和自定义健康检查器
- 优雅退出（SIGINT/SIGTERM 信号处理）
- 业务主线程管理（`svc()` 模式）

## 快速开始

### 5 步创建微服务

**步骤 1**：包含头文件

```cpp
#include <vsoa_node/VsoaNode.h>
```

**步骤 2**：继承 VsoaNode 并实现回调

```cpp
class MyNode : public lwserverbase::vsoa_node::VsoaNode {
public:
    MyNode() : VsoaNode("config.json") {
        auto& cfg = getNodeConfig();
        cfg.serviceName = "MyNode";
        cfg.version      = "1.0.0";
    }

    int OnInit() override {
        // 注册 RPC 处理函数
        registerRpc("/api/hello",
            [](const std::string& url, const void* data,
               size_t len, std::string& resp) {
                resp = "{\"msg\": \"hello\"}";
            });
        return 0;
    }

    int svc() override {
        while (isRunning()) {
            // 周期性发布数据
            publish("/data/status", "{\"ok\": true}");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        return 0;
    }
};
```

**步骤 3**：在 main 函数中启动

```cpp
int main(int argc, char* argv[]) {
    return VsoaNode::Run<MyNode>(argc, argv);
}
```

**步骤 4**：编译

```bash
cmake -S src -B build -DBUILD_EXAMPLES=OFF
cmake --build build -j
```

**步骤 5**：运行

```bash
# 正常启动（配置文件路径在构造函数中指定，默认为 config.json）
./my_service
```

## API 参考

### VsoaNode 公有方法

| 方法 | 描述 |
|------|------|
| `static int Run<NodeT>(argc, argv)` | 框架入口，创建并运行节点 |
| `virtual int OnInit()` | 初始化回调，注册 RPC/Topic |
| `int svc() override` | 业务主循环（VsoaNode 提供默认空实现，用户可重写） |
| `virtual void OnShutdown()` | 关闭回调，释放资源 |
| `bool registerRpc(url, callback)` | 注册 RPC URL 处理函数 |
| `bool registerRpc(url, callback, desc)` | 注册 RPC 端点（带描述，显示在 /help 中） |
| `bool publish(url, data)` | 发布 Topic 数据（字符串） |
| `bool publish(url, data, len)` | 发布 Topic 数据（二进制） |
| `bool subscribe(url, callback)` | 订阅 Topic |
| `bool callRpc(serverName, url, data, len, callback, timeoutMs)` | 异步调用远程 RPC |
| `pair<int, vector<char>> callRpcSync(serverName, url, data, len, timeoutMs)` | 同步调用远程 RPC |
| `void registerHealthChecker(checker)` | 注册自定义健康检查器 |
| `vector<srv_item_t> listNodes()` | 列出所有已注册节点 |
| `srv_item_t discoverNode(name)` | 发现指定名称的节点 |
| `string getNodeName()` | 获取节点名称 |
| `string getNodeVersion()` | 获取节点版本号 |
| `NodeConfig& getNodeConfig()` | 获取节点配置引用 |
| `ServiceManager* getServiceManager()` | 获取 ServiceManager 指针 |

### VsoaNode 生命周期钩子（用户可重写）

> **注意**：`OnStart()` 和 `OnStop()` 已用 `final` 锁定，用户不可重写。请使用以下钩子。

| 钩子 | 调用时机 | 用途 |
|------|---------|------|
| `OnInit()` | VSOA 初始化后、spin 启动前 | 注册 RPC 端点、健康检查器、指标 |
| `svc()` | 独立线程中循环执行 | 业务主循环、发布数据 |
| `OnShutdown()` | 收到退出信号后、VSOA 资源清理前 | 通知 svc 优雅退出 |
| `OnHealthCheck()` | `/health` 端点被访问时 | 自定义健康逻辑 |
| `OnCommand(cmd, args, resp)` | `/cmd` 端点被调用时 | 运行时命令 |
| `OnReloadConfig()` | 配置文件热更新时 | 重新读取配置 |

### 回调类型

```cpp
// RPC 处理回调
using RpcCallback = std::function<void(
    const std::string& url,
    const void* data, size_t len,
    std::string& response)>;

// 订阅回调
using SubscribeCallback = std::function<void(
    const std::string& url,
    const void* data, size_t len)>;

// RPC 响应回调
using RpcResponseCallback = std::function<void(
    int status,
    const std::string& url,
    const void* data, size_t len)>;

// 健康检查器
using HealthChecker = std::function<HealthState()>;
```

### NodeConfig 配置项

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| serviceName | string | "UnknownNode" | 服务名称 |
| version | string | "1.0.0" | 版本号 |
| description | string | "" | 服务描述 |
| serverHost | string | "0.0.0.0" | 监听地址 |
| serverPort | uint16_t | 0 (自动分配) | 监听端口 |
| serverPassword | string | "" | 服务端密码 |
| configPath | string | "config.json" | 配置文件路径 |
| daemonMode | bool | false | 守护进程模式 |
| shutdownTimeoutMs | int | 5000 | 优雅退出超时(ms) |
| vsoaSpinIntervalMs | int | 50 | VSOA 事件循环间隔(ms) |
| encoding | string | "json" | 序列化方式 |
| dataMode | int | 1 | 0=PARAM_MODE, 1=DATA_MODE(默认) |

配置文件使用 `node.*` 前缀，例如：
```json
{
    "node": {
        "service_name": "my-node",
        "server_host": "0.0.0.0",
        "server_port": 8080,
        "data_mode": 0
    }
}
```

### 内置管理 URL（7 个）

VsoaNode 自动向 ServerHandle 注册以下 RPC 端点：

| URL | 描述 | 响应格式 |
|-----|------|---------|
| `/health` | 健康检查 | JSON: status, uptime_sec, service, checks |
| `/version` | 版本查询 | JSON: service, version, description, build_time |
| `/config` | 运行配置 | JSON: 当前运行时配置 |
| `/metrics` | 监控指标 | Prometheus 文本格式 |
| `/cmd` | 运行时命令 | 路由到 `OnCommand(cmd, args, resp)` → 用户自定义响应 |
| `/help` | API 帮助 | 列出所有已注册的 RPC URL 及其描述 |
| `/urls` | URL 列表 | 所有已注册的 RPC URL（含内置端点） |

`/health` 响应示例：
```json
{
    "status": "healthy",
    "uptime_sec": 12345,
    "service": "MyNode",
    "checks": {
        "memory": "ok",
        "connection": "ok"
    }
}
```

### 配置方式

所有配置通过**构造函数 + 配置文件（JSON）** 指定。不再支持命令行参数。

```cpp
class MyNode : public VsoaNode {
public:
    MyNode() : VsoaNode("/etc/myapp/config.json") {  // 在构造时指定配置路径
        auto& cfg = getNodeConfig();
        cfg.serviceName = "my-node";
    }
};
```

## 线程安全说明

VsoaNode 中存在多个并发执行上下文：

1. **VSOA spin 线程**：处理网络事件，RPC 回调在此线程中执行
2. **svc 线程**：用户业务主循环（由 `ServiceTask::activate()` 创建）
3. **用户 RPC 回调**：在 VSOA 内部线程中执行

**注意**：RPC 回调与 `svc()` 运行在不同线程中。如果共享数据，请使用适当的同步机制（`std::mutex` 等）。

## 构建和运行指南

### 依赖

- lwserverbase (liblwserverbase) - 服务框架基础
- platform_sdk (libplatform_sdk) - VSOA SDK
- lwcomm, lwevent, lwlog - 底层通信库
- pthread - 线程支持

### 运行时要求

- `VSOA_POS_SERVER` 环境变量已配置
- `vsoa_master` 已启动且可达
- `LD_LIBRARY_PATH` 包含 `lib/linux/x86_64` 等库路径

### 编译

```bash
# 标准编译
cmake -S src -B build
cmake --build build -j

# 仅编译 vsoa_node 库
cmake --build build --target vsoa_node

# 编译示例
cmake --build build --target vsoa_node_example

# 启用测试编译
cmake -S src -B build -DBUILD_TESTS=ON
cmake --build build --target test_vsoa_node_config test_vsoa_node_health test_vsoa_node_log
```

## 示例代码

完整示例见 `examples/simple_vsoa_node.cpp` 和上层 `src/ext/lwserverbase/examples/vsoa_system_monitor.cpp`。
