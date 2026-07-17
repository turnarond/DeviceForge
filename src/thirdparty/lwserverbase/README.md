# lwserverbase 微服务框架

lwserverbase 是一个**轻量级 C++ 微服务开发框架**。开发者继承合适的基类即可自动获得服务生命周期管理、配置热更新、日志、监控指标、进程控制、VSOA 通信等能力。

## 架构：三层基类体系

框架采用**渐进式三层继承体系**，按需选择复杂度：

```
VsoaNode (vsoa_node/)
  ↑ 继承 ServiceTask，集成 VSOA 通信 (registerRpc/publish/subscribe)
  │ 自动提供: /health /version /config /metrics /cmd /help /urls 内置端点
  │
ServiceTask (core/)
  ↑ 继承 ServiceBase，增加线程池管理 (activate/svc/wait)
  │ 适用: 需要后台工作线程的服务
  │
ServiceBase (core/)
  生命周期基类 (OnStart/OnStop/OnReloadConfig/OnHealthCheck/OnCommand)
  适用: 最简单的、无线程需求的服务
```

| 基类 | 适用场景 | 入口 | 关键方法 |
|------|---------|------|----------|
| `ServiceBase` | 最简单的服务 | `ServiceManager::Run<T>()` | `OnStart`, `OnStop`, `OnReloadConfig`, `OnCommand` |
| `ServiceTask` | 需要后台线程池 | `ServiceManager::Run<T>()` | `svc()`, `activate(N)`, `isRunning()`, `requestShutdown()` |
| `VsoaNode` | 需要 VSOA 通信 | `VsoaNode::Run<T>()` | `OnInit()`, `registerRpc()`, `publish()`, `subscribe()`, `callRpc()` |

**选择指南：**

```
需要 VSOA 通信（RPC/发布/订阅）？
  └── 是 → 继承 VsoaNode（推荐）
         └── 自动获得: 7 个内置端点
         └── 一行注册: registerRpc("/api/xxx", callback)
  └── 否 → 需要后台线程？
         └── 是 → 继承 ServiceTask
                └── 重写 svc()，activate(N) 启动 N 个线程
         └── 否 → 继承 ServiceBase
                └── 最轻量，手动管理一切
```

---

## 快速开始

### VsoaNode — VSOA 微服务（推荐）

```cpp
#include <vsoa_node/VsoaNode.h>
using namespace lwserverbase::vsoa_node;

class MyNode : public VsoaNode {
public:
    MyNode() : VsoaNode("config.json") {       // 构造时指定配置文件路径
        auto& cfg = getNodeConfig();
        cfg.serviceName = "my-node";             // 服务名是唯一必需的配置
        cfg.version      = "1.0.0";
    }

    int OnInit() override {                     // 注册 RPC 端点
        registerRpc("/api/hello", [](const std::string&, const void*,
            size_t, std::string& resp) { resp = "{\"msg\":\"hello\"}"; });
        return 0;
    }

    int svc() override {                        // 业务主循环（可选）
        while (isRunning()) {
            publish("/data", "{}");
            sleep(1);
        }
        return 0;
    }
};

int main(int argc, char* argv[]) {
    return VsoaNode::Run<MyNode>(argc, argv);   // 一行启动，argc/argv 不再被解析
}
```

### ServiceTask — 带线程池的后台服务

```cpp
class MyTaskService : public lwserverbase::core::ServiceTask {
    int OnStart(int argc, char* argv[]) override {
        activate(4);  // 启动 4 个 svc 工作线程
        return 0;
    }
    int svc() override {
        while (isRunning()) {
            // 业务逻辑
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        return 0;
    }
    std::string GetServiceName() const override { return "my-task-service"; }
};
```

> **注意**：ServiceTask::OnStart 默认调用 `activate(1)` 和 `startRunning()`。
> 若重写 OnStart，需手动调用 `startRunning()`，否则 svc 线程立即退出。
> 停止时调用 `requestShutdown()`（框架在 OnStop 中自动调用）。

### ServiceBase — 最简服务

```cpp
class MyService : public lwserverbase::core::ServiceBase {
    int OnStart(int argc, char* argv[]) override {
        GetServiceManager()->GetLogger()->Info("服务启动");
        return 0;
    }
    void OnStop() override {
        GetServiceManager()->GetLogger()->Info("服务停止");
    }
    std::string GetServiceName() const override { return "my-service"; }
};

int main(int argc, char* argv[]) {
    return lwserverbase::core::ServiceManager::Run<MyService>(argc, argv);
}
```

---

## 核心功能模块

框架通过 `ServiceManager`（DI 容器）管理以下组件，各组件可按需替换：

### 1. 配置管理（ConfigManager）

- 支持 **JSON**（推荐）和 **Key=Value** 两种格式
- **类型感知存储**（底层使用 `variant<int64_t, double, bool, string>`）
- 类型安全读取：`Get<T>(key, default)` 支持 `string`、`int`、`int64_t`、`double`、`bool`
- 配置热更新（定时检查文件 mtime）
- 变更监听器（`RegisterChangeHandler`）
- 配置修改后保存自动还原 JSON 嵌套结构

```cpp
auto* config = manager->GetConfigManager();

// 类型安全读取
std::string host = config->Get("server.host", std::string("localhost"));
int         port = config->Get("server.port", 8080);
bool       debug = config->Get("server.debug", false);

// 变更监听
config->RegisterChangeHandler("server.port",
    [](const std::string& key, const std::string& oldVal, const std::string& newVal) {
        // 处理变更...
    });

// 热更新
config->EnableHotReload("config.json", 2000);
config->SetReloadCallback([this]() { OnReloadConfig(); });
```

**配置生命周期：**

```
构造 VsoaNode(configPath)               ← 配置文件路径在构造时确定
  → ServiceManager::Initialize()
       └─ ConfigManager::Load()          ← 唯一的一次文件加载
  → ServiceManager::Start()
       → VsoaNode::OnStart()
            ├─ applyConfigOverrides()    ← 读 node.* 到 NodeConfig
            ├─ m_config.validate()       ← 配置校验
            └─ SetReloadCallback()      ← 注册热更新回调
```

**VsoaNode 配置文件示例：**

```json
{
    "node": {
        "service_name": "my-node",
        "server_host": "0.0.0.0",
        "server_port": 0,
        "data_mode": 1,
        "vsoa_spin_interval_ms": 50
    }
}
```

日志由 lwlog 的 `logconfig.xml` 控制，锁文件自动使用 `<serviceName>.lock`，均无需在 JSON 中配置。

### 2. 日志系统（Logger）

- 五级日志：`DEBUG`、`INFO`、`WARN`、`ERROR`、`CRITICAL`
- 底层使用 lwlog 库，日志行为由 `config/logconfig.xml` 统一控制
- 提供 printf 风格格式化输出

```cpp
auto* logger = manager->GetLogger();
logger->Info("服务启动于端口 %d", port);
logger->Warn("配置文件未找到，使用默认值");
logger->Error("连接失败: %s", error.c_str());
```

### 3. 监控指标（MetricsCollector）

- **Counter**：单调递增（`Increment` / `Set` / `Get`）
- **Gauge**：可增可减（`Set` / `Increment` / `Decrement` / `Get`）
- **Histogram**：分布统计（`Observe` / `GetCount` / `GetSum`）
- **Prometheus 导出**（`ExportPrometheus()`）
- **结构化导出**（`GetAllMetrics()` → `vector<MetricEntry>`）
- 线程安全

```cpp
auto* metrics = manager->GetMetricsCollector();
auto& counter = metrics->CreateCounter("requests_total", "Total requests");
counter.Increment();
std::string prom = metrics->ExportPrometheus();
```

### 4. 进程控制（ProcessController）

- **单实例锁**：自动使用 `<serviceName>.lock`，可通过 `process.lock_file` 覆盖
- **守护进程模式**：`SetDaemonMode(true)` + `Daemonize()`，或配置 `process.daemon = true`
- **信号处理**：自动处理 SIGINT/SIGTERM，触发优雅退出
- **手动退出**：`TriggerExit()` + `WaitForExitSignal()`

```cpp
auto* process = manager->GetProcessController();
// 框架在 Initialize() 中自动调用 EnsureSingleInstance + Daemonize
```

### 5. VSOA 通信（VsoaNode）

VsoaNode 提供高级 VSOA API，并自动注册 7 个内置端点：

| URL | 功能 |
|-----|------|
| `/health` | 健康检查 |
| `/version` | 版本信息 |
| `/config` | 运行配置 |
| `/metrics` | 监控指标（Prometheus 格式） |
| `/cmd` | 运行时命令（路由到 OnCommand） |
| `/help` | API 帮助信息 |
| `/urls` | 已注册的 RPC URL 列表 |

```cpp
registerRpc("/api/hello", callback);
registerRpc("/api/hello", callback, "打招呼");  // 带描述，显示在 /help 中
publish("/data", json_string);
subscribe("/events", callback);
callRpc("remote-svc", "/api/query", &req, sizeof(req), callback);
auto [status, data] = callRpcSync("remote-svc", "/api/query", &req, sizeof(req));
registerHealthChecker([]() -> HealthState { ... });
```

---

## VsoaNode 生命周期

`OnStart()` 和 `OnStop()` 用 `final` 锁定，用户不可重写。框架提供以下钩子：

| 钩子 | 调用时机 | 用途 |
|------|---------|------|
| `OnInit()` | VSOA 初始化后、spin 启动前 | 注册 RPC 端点、健康检查器、指标 |
| `svc()` | 独立线程中循环执行 | 业务主循环、发布数据 |
| `OnShutdown()` | svc 线程仍在运行时 | 通知 svc 优雅退出 |
| `OnHealthCheck()` | `/health` 端点被访问时 | 自定义健康逻辑 |
| `OnCommand(cmd,args,resp)` | `/cmd` 端点被调用时 | 运行时命令 |
| `OnReloadConfig()` | 配置文件热更新时 | 重新读取配置 |

**线程管理：**

```cpp
bool isRunning() const;       // 检查运行状态（替代旧的 running_）
void requestShutdown();       // 请求关闭（替代 running_ = false）
void startRunning();          // 启动运行状态（框架内部使用）
```

---

## NodeConfig 完整配置项

| 字段 | 配置文件 key | 默认值 | 说明 |
|------|-------------|--------|------|
| `serviceName` | `node.service_name` | `"UnknownNode"` | 服务名称 |
| `version` | `node.version` | `"1.0.0"` | 版本号 |
| `description` | `node.description` | `""` | 服务描述 |
| `serverHost` | `node.server_host` | `"0.0.0.0"` | 监听地址 |
| `serverPort` | `node.server_port` | `0` | 端口（0=自动分配） |
| `serverPassword` | `node.server_password` | `""` | 服务端密码 |
| `configPath` | — | `"config.json"` | 配置文件路径（构造函数指定） |
| `daemonMode` | `node.daemon_mode` | `false` | 守护进程模式 |
| `encoding` | `node.encoding` | `"json"` | 序列化方式 |
| `dataMode` | `node.data_mode` | `1` | 0=PARAM_MODE, 1=DATA_MODE |
| `vsoaSpinIntervalMs` | `node.vsoa_spin_interval_ms` | `50` | 事件循环间隔(ms) |
| `shutdownTimeoutMs` | `node.shutdown_timeout_ms` | `5000` | 关闭超时(ms) |

**配置校验**（`validate()` 在启动时自动调用）：

| 检查项 | 约束 |
|--------|------|
| `dataMode` | 必须为 0 或 1 |
| `shutdownTimeoutMs` | 必须 >= 0 |

---

## 框架级配置 key

| Key | 用途 | 默认值 |
|-----|------|--------|
| `log.path` | 日志目录 | `logs` |
| `log.file` | 日志文件名 | `service.log` |
| `process.lock_file` | 单实例锁文件 | `<serviceName>.lock`（自动使用服务名） |
| `process.daemon` | 守护进程模式 | `false` |

---

## 目录结构

```
lwserverbase/
├── core/
│   ├── ServiceBase.h/.cpp        ← 生命周期基类
│   ├── ServiceManager.h/.cpp     ← DI 容器 + 组件编排
│   └── ServiceTask.h/.cpp        ← 带线程池的 ServiceBase
├── config/
│   └── ConfigManager.h/.cpp      ← JSON/KV 加载、variant 存储、热更新
├── logging/
│   └── Logger.h/.cpp             ← lwlog 封装（五级日志）
├── metrics/
│   └── MetricsCollector.h/.cpp   ← Counter/Gauge/Histogram + Prometheus
├── process/
│   └── ProcessController.h/.cpp  ← 单实例锁、daemon、信号处理
├── vsoa_node/
│   ├── VsoaNode.h/.cpp           ← VSOA 集成基类
│   ├── NodeConfig.h              ← 节点配置 + validate()
│   ├── HealthState.h             ← 健康状态
│   ├── LogFormatter.h/.cpp       ← 日志前缀格式化
│   ├── examples/                 ← VsoaNode 示例
│   └── tests/                    ← 单元测试
├── examples/                     ← 框架级示例
├── tests/                        ← 单元测试
└── include/                      ← 版本头文件
```

## 依赖

- lwcomm / lwevent / lwlog — SylixOS 起源的底层库
- platform_sdk — VSOA 通信 SDK
- nlohmann/json — JSON 解析
- pthread — 线程支持

## 运行时要求

- `VSOA_POS_SERVER` 环境变量
- `vsoa_master` 已启动且可达
- `LD_LIBRARY_PATH` 包含库路径

---

## 设计原则

1. **渐进式复杂度**：从 ServiceBase → ServiceTask → VsoaNode，按需选择
2. **约定优于配置**：构造时指定配置文件路径，框架自动完成初始化流程
3. **内置可观测性**：日志、指标（Prometheus 兼容）、健康检查端点，开箱即用
4. **DI 可替换**：ServiceManager 的组件可通过 `Register*()` 注入自定义实现
5. **生命周期锁定**：`OnStart/OnStop` 用 `final` 锁定，提供 `OnInit/OnShutdown` 替代
6. **类型安全配置**：variant 存储保留原始类型，读取零开销
7. **跨平台**：Linux (GCC/Clang)、SylixOS、QNX
