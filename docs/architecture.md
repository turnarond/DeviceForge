# DeviceForge 系统架构

## 概述

DeviceForge 采用 **Tool + Protocol Adapter** 双层插件化架构。上层是 Qt Widget 组成的工具界面，下层是通过统一协议接口接入的各种网络协议实现。

```
┌─ UI Layer (Qt Widgets) ─────────────────────────────────────┐
│  DeployMaster (QMainWindow)                                  │
│  ├─ DeviceBusWidget    设备总线（顶部胶囊形设备列表）         │
│  ├─ QTabWidget          工具标签页容器                        │
│  │   ├─ FtpDeployWidget   文件部署工具                       │
│  │   ├─ TelnetWidget      批量命令工具                       │
│  │   ├─ ModbusWidget      Modbus 测试工具                    │
│  │   ├─ WebSocketWidget   WebSocket 通信工具                 │
│  │   └─ OpcUaClientTab    OPC UA 客户端（演示）              │
│  └─ RemotePreview        远端文件预览面板                    │
├─ Framework Layer ────────────────────────────────────────────┤
│  ToolHost             工具生命周期管理（桥接层）              │
│  ToolRegistry          工具注册表                             │
│  ToolBackend           工具后端基类 (ServiceTask)             │
│  ToolWidget            工具前端基类 (QWidget)                 │
│  AppState              全局应用状态                           │
│  ManifestParser        插件清单解析 (tinyxml2)                │
├─ Adapter Layer ──────────────────────────────────────────────┤
│  IProtocolAdapter      协议适配器统一接口                     │
│  ├─ FtpAdapter         FTP/FTPS (libcurl)                    │
│  ├─ TelnetAdapter      Telnet (lwcommunicate)                │
│  └─ ProtocolRegistry   适配器工厂注册表                       │
└──────────────────────────────────────────────────────────────┘
```

## Tool 架构

每个功能模块由 **Backend + Widget** 配对组成：

- **ToolBackend**：继承 `lwserverbase::core::ServiceTask`，负责业务逻辑和线程管理。通过 `svc()` 方法运行后台线程，通过 `OnStart()/OnStop()` 管理生命周期
- **ToolWidget**：继承 `QWidget`，负责 UI 展示和用户交互。通过回调与 Backend 异步通信

```
Tool = Backend (ServiceTask) + Widget (QWidget)
         ↕ (回调/信号)
     lwmsgq 消息队列（解耦）
```

## Protocol Adapter 抽象

```
IProtocolAdapter (纯虚接口)
├── protocolId()        协议标识
├── connect()           连接设备
├── disconnect()        断开连接
├── request()           请求-响应模式
├── subscribe()         流模式订阅
└── ProtocolRegistry    工厂注册表（单例）
```

现有实现：
- **FtpAdapter**：封装 libcurl，支持 FTP/FTPS。提供 uploadFile/uploadFolder/downloadFile/listDirectory 等操作
- **TelnetAdapter**：基于 lwcommunicate::LWTcpClient，支持请求-响应和流模式

## 第三方库

| 库 | 用途 | 许可 |
|----|------|------|
| Qt 6.10.1 | UI + 网络 + Modbus + WebSocket | LGPL |
| libcurl 8.20 | FTP/FTPS 传输 | MIT |
| lwserverbase | 服务框架（线程管理/ServiceTask） | ACOINFO 内部 |
| lwcommunicate | 网络通信库（TCP 连接池） | ACOINFO 内部 |
| lwlog | 日志库（文件/控制台 appender） | ACOINFO 内部 |
| lwcomm | 工具库（文件系统/Base64） | ACOINFO 内部 |
| lwmsgq/lwevent | 消息队列/事件 | ACOINFO 内部 |
| tinyxml2 | XML 解析（插件清单） | zlib |
| nanopb | Protobuf 编解码（待集成） | zlib |

## 并发模型

- `QtConcurrent::run`：FTP/Telnet/Modbus 异步操作
- `std::async`：TelnetAdapter 单次请求
- `ServiceTask::svc()`：每个 Backend 独立后台线程
- `QTimer`：Modbus 自动刷新
- `QMutex`：AppState 线程安全
