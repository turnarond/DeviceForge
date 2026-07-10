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
│  │   ├─ NetRelayWidget    网络调试中继 + 录制回放            │
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
- **SshAdapter**：基于 libssh2，支持密码认证 + TOFU 主机密钥验证，request-响应模式（SSH exec channel）

> **注意**：部分 Tool（Modbus / WebSocket / NetRelay）直接使用 Qt 原生 socket 类（QModbusTcpClient / QWebSocket / QTcpServer + QUdpSocket），未走 IProtocolAdapter 抽象。适配器层主要服务于 FTP/Telnet 这类请求-响应/流模式协议。

## NetRelayTool — 网络调试中继 + 录制回放

NetRelayTool 是一个透明中继代理：数据生产者（TCP/UDP 客户端）连接工具，工具**原封不动**地双向转发到真实消费者，同时旁路抓取流量用于调试，并可录制流量以便日后回放。新增**组播**支持：作为额外订阅者加入组播组抓收数据（对源与现有消费者零影响），并可录制回灌。

```
数据生产者(TCP/UDP客户端) ──①原始数据──▶ 工具(监听) ──②原封不动──▶ 真实消费者
数据生产者               ◀──④转发─────  工具        ◀──③使用/回控制── 真实消费者
                                         │
                                         ├──▶ 实时 Hex+ASCII 视图
                                         └──▶ 录制(.nrec) ──▶ 回放引擎(按原始时序重放上行到消费者)
```

组成单元（均为**非 QObject 纯 C++ 类**，socket/timer 在主线程事件循环驱动）：

| 单元 | 职责 |
|------|------|
| `NetRelayBackend` | 中继引擎（TCP 配对代理 / UDP 会话代理 / 组播抓收）+ 录制钩子 + 回放委托 + `RelayMode{Idle,Relaying,Replaying}` 互斥状态机 |
| `RelayRecorder` | 把每个数据块（方向 + 相对时间戳 + 会话号 + 原始字节）顺序写入 `.nrec` |
| `RelayRecording` | 读取并校验 `.nrec`（magic / 版本 / 长度上限 / 截断），供回放与测试使用 |
| `RelayPlayer` | 加载 `.nrec`，按相邻记录时间间隔用 `QTimer` 重放**上行**记录到消费者（模拟生产者） |
| `NetRelayWidget` | 配置 + 会话列表 + Hex 视图 + 录制勾选 + 回放面板 |

**`.nrec` 格式（v1，小端二进制）**：32 字节文件头（`"NREC"` magic + 版本 + 协议 + 起始 epoch + 保留）+ 变长记录条目（方向 u8 + 会话号 u32 + 相对时间戳 i64 + 长度 u32 + payload）。回放依据相邻记录 `ts_offset_ms` 差值还原原始节奏。

**互斥**：中继与回放为单活跃模式，`RelayMode` 状态机保证二者不并发（含 UDP 异步 DNS 解析窗口期的守卫）。

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
- `ServiceTask::svc()`：每个 Backend 独立后台线程（NetRelay/WebSocket 的 svc() 仅保活，socket I/O 在主线程事件循环）
- `QTimer`：Modbus 自动刷新；NetRelay UDP 会话空闲清理与回放时序调度
- 事件驱动异步 I/O：NetRelay 的 QTcpServer/QUdpSocket 全部在主线程事件循环中信号驱动，Backend→Widget 回调经 `QMetaObject::invokeMethod(Qt::QueuedConnection)` 跨线程投递
- `QMutex`：AppState 线程安全
- `std::atomic`：NetRelay `m_cancelled` 停止标志

## 测试

- **tst_nrec**（`tests/`，QtTest + CTest，仅 CMake 构建，不影响 VS/vcxproj 工程）：项目首个单元测试目标，覆盖 `.nrec` 录制往返、坏 magic / 坏版本 / 超长 length / 截断文件拒绝、回放上行端到端。运行：`ctest -C Release -R tst_nrec`。其余模块暂无单元测试。
