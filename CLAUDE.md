# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 行为准则

1. **中文优先**：对话和文档尽量使用中文撰写，专业术语（如 MVP、EventBus、FTP、Modbus）和论文引述除外
2. **文档同步**：修改代码时，若涉及需求、接口、设计、架构、手册等内容，必须同步更新 `doc/` 和 `docs/superpowers/` 中的对应文档。文档是交互接口与对齐标准，不是代码的附属品

## 项目概述

DeployMaster 是基于 Qt 6.10.1 + C++17 的 SylixOS PLC 工业级批量部署与运维平台。提供 FTP 批量部署、Telnet 批量命令、Modbus 集群测试、OPC UA 客户端、WebSocket 通信等功能。

与 PLCBasicConfigurator 配套，构成 SylixOS PLC 设备完整工具链（DeployMaster 负责部署/测试/运维，PLCBasicConfigurator 负责设备配置）。

## 构建命令

### CMake（推荐）

```bash
# 配置（Windows MSVC，需提前安装 Qt 6.10.1）
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH="C:\Qt\6.10.1\msvc2022_64"

# 编译 Release
cmake --build . --config Release

# 运行
.\Release\DeployMaster.exe
```

### Visual Studio

项目根目录有 `DeployMaster.vcxproj`，可直接用 VS2022 打开。需安装 Qt 6.10.1、Qt Visual Studio Tools 扩展，并在 VS 中配置 Qt 版本路径。

> **注意**：CLAUDE.md 旧版提到 `DeployMaster.slnx`，该文件实际不存在于仓库中，直接使用 `.vcxproj` 即可。

### Visual Studio 调试

VS 中 F5 调试时，如果 libcurl-x64.dll 未在输出目录（如 `x64/Debug/`），需手动复制 `lib/libcurl-x64.dll` 过去。CMake 构建已通过 `POST_BUILD` 自动处理此步骤。

## CI/CD

GitHub Actions（`.github/workflows/msbuild.yml`）：push/PR 到 `main` 分支时触发，在 `windows-latest` 上通过 `jurplel/install-qt-action@v4` 安装 Qt 6.9.2，使用 MSBuild 编译 Debug 配置。

> CI 使用的 Qt 版本（6.9.2）与本地开发版本（6.10.1）不同，注意避免使用仅新版才有的 API。

## 技术栈

| 组件 | 用途 |
|------|------|
| Qt 6.10.1 (Core/Gui/Widgets/Network/SerialBus/WebSockets) | 跨平台 UI + 网络 + Modbus |
| libcurl (lib/curl-8.20.0) | FTP 文件传输 |
| lwserverbase | 服务框架（ServiceTask 生命周期、ServiceManager、ConfigManager） |
| lwcommunicate | 网络通信库（TCP/UDP/Serial 连接池、自动重连） |
| lwcomm | 跨平台工具库（文件系统、Base64、字符串、时间） |
| lwlog | 日志库（控制台/文件 appender、pattern 格式化） |
| lwmsgq | 消息队列（发布/订阅、跨线程解耦） |
| lwevent | 事件信号库 |
| tinyxml2 | XML 解析（插件清单 manifest.xml） |
| nanopb | Protocol Buffers 编解码（消息队列序列化，待集成） |
| CMake 3.16+ | 构建系统 |
| Visual Studio 2022 | Windows 编译 |

## 代码架构

### 架构状态：DeployMaster 2.0 Phase 0-2 完成

项目已完成从 MVP+EventBus 单体架构到 **lwserverbase 服务核 + Qt Widget 壳** 双层架构的基础设施搭建 + 主要 Tool 迁移 + 安全加固。

**架构模型**：Tool = ToolBackend (ServiceTask) + ToolWidget (QWidget)，通过 lwmsgq 双向解耦。统一 IProtocolAdapter 接口 + ProtocolRegistry 连接池。

> **注意**：ToolHost::createTool() 只支持单活跃 Tool，目前 FtpDeploy/Telnet/WebSocket 三个 Tool 通过 DeployMaster 直接创建 Backend + Widget，绕过 ToolHost。待 ToolHost 支持多 Tool 并发后切换。

**已完成（Phase 0-2）**：
- Phase 0（基础设施对齐）：thirdparty 库编译集成、LogBridge 日志桥接、适配器层（IProtocolAdapter/FtpAdapter/TelnetAdapter/ProtocolRegistry）
- Phase 1（框架搭建）：ToolBackend/ToolWidget 基类、ManifestParser 插件清单解析、ToolRegistry 工具注册表、ToolHost 桥接层、DeviceBusWidget 设备总线、darkstyle.qss 工业仪表盘色板、FtpDeployTool 首个完整 Tool
- Phase 2（工具迁移）：TelnetTool ✅、WebSocketTool ✅、FtpDeployTool ✅（UI 重设计、FTPS 加密）；旧 "批量部署" Tab 已隐藏；远端预览面板重建（协议选择 Ftp+SCP 预留、设备同步）

**安全加固（2026-07-05）**：
- FTPS 加密传输（FtpAdapter::setUseFtps + CURLOPT_USE_SSL + UI 复选框）
- libcurl 协议白名单（所有 curl handle CURLOPT_PROTOCOLS_STR = "ftp,ftps"）
- Telnet 认证失败阻断（凭证发送失败→立即断开）
- WebSocket 默认绑定 127.0.0.1 + 可选 Token 认证（URL 参数 ?token=）
- 内存密码安全擦除（AuthInfo::clear / FtpManager::clearCredentials）
- 日志去敏感化（Telnet/WebSocket 消息内容改为记录字节数）
- 安全审查报告见 `docs/superpowers/specs/2026-07-05-ftp-deploy-improvement-design.md`

**待完成（Phase 3）**：
- LogQueryTool、ModbusTool 迁移
- QPluginLoader DLL 加载、SSH Adapter
- 远端预览 SCP 支持（UI 已预留）
- ToolHost 多 Tool 并发支持

详细设计见 `docs/superpowers/specs/2026-07-04-tool-framework-design.md`。
实施计划见 `docs/superpowers/plans/2026-07-04-tool-framework-plan.md`。

### 双层架构

```
UI Layer (Qt Widget)         Framework Layer            Adapter Layer
──────────────────           ─────────────────          ──────────────
DeployMaster.cpp             ToolHost (桥接层)          IProtocolAdapter
  ├─ DeviceBusWidget          ToolRegistry (注册表)       ├─ FtpAdapter
  ├─ Tool Navigation          ToolBackend (基类)          ├─ TelnetAdapter
  └─ QSS 深色主题             ToolWidget (基类)           └─ ProtocolRegistry
        ↕                         ↕                         ↕
    lwmsgq (消息队列)        ServiceManager             lwcommunicate::LWTcpClient
        ↕                         ↕                         ↕
    AppState                  ServiceTask               libcurl
```

### 核心框架组件（src/framework/）

- **ToolBackend**：继承 `lwserverbase::core::ServiceTask`，Tool 后端基类。获得完整的服务生命周期管理（OnStart/OnStop）。定义 toolId/name/version/category/bindDevices/bindCredentials/applyConfig 纯虚接口
- **ToolWidget**：继承 `QWidget`，Tool 前端基类。定义 toolId/toolName/onToolStart/onToolStop 纯虚接口 + toolStatusChanged 信号
- **ToolHost**（QObject 单例）：桥接层，负责 Tool 的创建、配对、生命周期管理。注册内置 Tool 工厂 + createTool/destroyTool
- **ToolRegistry**（单例）：管理所有可用 Tool 的元数据（内置+插件）。registerBuiltin/scanPluginDirectory/listAllTools/findById
- **ManifestParser**：基于 tinyxml2 解析 XML 格式插件清单（manifest.xml），提取 id/版本/协议依赖/DLL 入口点/默认配置
- **AppState**（单例）：全局应用状态（选中设备列表、任务进度、忙闲状态），线程安全（QMutexLocker）
- **DeviceInfo.h**：统一的设备信息 + 认证信息数据结构

### 适配器层（src/adapter/）

- **IProtocolAdapter**：所有协议适配器的纯虚接口（protocolId/connect/disconnect/isConnected/lastError/request/subscribe）
- **ProtocolCapability**：协议能力声明结构体（requestResponse/streaming/broadcast/publishSubscribe/maxConnections）
- **FtpAdapter**：实现 IProtocolAdapter，内部复用 libcurl。额外暴露 FTP 特有操作（uploadFile/uploadFolder/downloadFile/listDirectory/deleteFile/deleteDirectory/clearRemoteDirectory）
- **TelnetAdapter**：实现 IProtocolAdapter，基于 `lwcommunicate::LWTcpClient`。支持请求-响应 + 流模式
- **ProtocolRegistry**（单例）：协议适配器工厂注册表（registerFactory/create/isRegistered/registeredProtocols）

### 日志桥接（src/logging/）

- **LogBridge**：Qt → lwlog 桥接。`LogBridge::install()` 全局安装 QtMessageHandler，将 qDebug/qWarning/qCritical/qInfo 映射到 lwlog 的 DEBUG/WARN/ERROR/INFO 级别，同时保留 stderr 控制台输出

### UI 组件（src/ui/）

- **DeviceBusWidget**：顶部设备总线组件。水平胶囊形 QListWidget，支持多选、添加、删除设备。在线/离线状态指示灯，琥珀色选中光晕。工业仪表盘风格

### 已完成的 Tool（src/tools/）

- **FtpDeployTool**（src/tools/FtpDeployTool/）：首个完整 Tool。FtpDeployBackend（继承 ToolBackend，通过 ProtocolRegistry 获取 FtpAdapter）+ FtpDeployWidget（标准三段式布局：配置→操作→结果+日志）

### 模块对应关系

| 功能 Tab | UI 类 | 业务逻辑 | 协议 | 架构状态 |
|----------|-------|----------|------|----------|
| 文件部署 | FtpDeployWidget (Tool) | FtpDeployBackend → FtpAdapter | FTP/FTPS (libcurl) | ✅ 已迁移 + FTPS 加密 |
| 批量命令 | TelnetWidget (Tool) | TelnetBackend → TelnetAdapter | Telnet (lwcommunicate) | ✅ 已迁移 + 安全警告 |
| WebSocket | WebSocketWidget (Tool) | WebSocketBackend → QWebSocket | WebSocket (QWebSocket) | ✅ 已迁移 + 绑定/认证 |
| 日志查询 | `LogQueryTab` | `FtpManager`（旧） | FTP (libcurl) | 旧模式，待迁移 |
| MODBUS 测试 | `ModbusCluster` | 直接使用 `QModbusTcpClient` | Modbus TCP | 旧模式，待迁移 |
| OPC UA 客户端 | `OpcUaClientTab` | 演示模式/桩 | OPC UA（待实现） | 未实现 |

### UI 资源

- **QRC**：`DeployMaster.qrc` — 打包 `darkstyle.qss`（`:/styles/darkstyle.qss`）
- **QSS**：`darkstyle.qss` — 工业仪表盘深色主题：深黑底 (#0B0E14)、琥珀强调色 (#F0A030)、青绿状态色 (#40C8A0)
- **UI 文件**：`DeployMaster.ui`（主窗口）+ 7 个 Tab 页面 UI（`BatchDeploy.ui`、`tab_telnet.ui`、`tab_logquery.ui`、`tab_modbus_cluster_test.ui`、`tab_opcua_client.ui`、`tab_websocket.ui`、`tab_diagnostic.ui`）

### 源码文件清单

**根目录（旧架构源码，逐步迁移中）**：
`main.cpp` / `DeployMaster.cpp/.h` / `DropListWidget.cpp/.h` / `TelnetClient.cpp/.h` / `TelnetDeploy.cpp/.h` / `ModbusCluster.cpp/.h` / `OpcUaClient.cpp/.h` / `WebSocketClient.cpp/.h` / `LogQueryTab.cpp/.h` / `FtpRemoteItem.h`（数据结构）

**新架构源码（src/）**：
- `src/framework/`：ToolBackend.h / ToolWidget.h(.cpp) / ToolHost(.cpp/.h) / ToolRegistry(.cpp/.h) / ManifestParser(.cpp/.h) / DeviceInfo.h / AppState(.cpp/.h)
- `src/adapter/`：IProtocolAdapter.h / ProtocolCapability.h / FtpAdapter(.cpp/.h) / TelnetAdapter(.cpp/.h) / ProtocolRegistry(.cpp/.h)
- `src/logging/`：LogBridge(.cpp/.h)
- `src/ui/`：DeviceBusWidget(.cpp/.h)
- `src/tools/FtpDeployTool/`：FtpDeployBackend(.cpp/.h) / FtpDeployWidget(.cpp/.h)
- `src/model/`：FtpManager(.cpp/.h)（旧，待 FtpAdapter 替换后移除）
- `src/presenter/`：FtpPresenter(.cpp/.h) / ModbusPresenter(.cpp/.h)（旧，待 Tool 迁移完成后移除）
- `src/utils/`：DeployEvent.h（旧，待完全移除 EventBus 后删除）
- `src/thirdparty/`：lwcomm / lwlog / lwmsgq / lwevent / lwcommunicate / lwserverbase / tinyxml2 / nanopb / multimedia

### 已删除的死代码（Phase 0-1 清理）

- `src/framework/EventBus.h/.cpp` — 由 lwmsgq 消息队列替代
- `src/framework/ConnectionPool.h/.cpp` — 由 lwcommunicate::LWConnPool 替代
- `TelnetManager.cpp` — 1 字节空文件
- `FileDeploy.h/.cpp` — 空壳类（仅 1 行构造函数）

### 并发模型

- `QtConcurrent::run` 用于异步执行网络 IO 和重计算
- libcurl FTP 操作自带进度回调
- `std::async` 用于 TelnetAdapter 异步请求
- 部分模块使用 `QTimer` 定时刷新（Modbus 自动刷新）
- 原子变量 (`std::atomic`) 控制 Telnet/Curl 运行/停止状态
- lwserverbase::ServiceManager 管理 Tool 服务生命周期

### UI 布局

- 顶部全局配置栏（设备总线 DeviceBusWidget + FTP 凭证）
- QSplitter 分隔的左右区域：左侧 QTabWidget（功能模块/Tool 工作区）、右侧远端预览面板
- 底部日志区（共享 QTextEdit），通过 QSplitter 与工作区调整高度
- 深色主题样式表：`darkstyle.qss`（通过 `main.cpp` 加载），工业仪表盘色板

## 设计文档

重构和 UI 设计的关键决策记录在 `docs/superpowers/` 中，修改架构前应先查阅：

**DeployMaster 2.0 设计文档**：
- `docs/superpowers/specs/2026-07-04-tool-framework-design.md` — DeployMaster 2.0 通用设备运维平台设计（lwserverbase + Tool 框架 + IProtocolAdapter + 插件化架构）
- `docs/superpowers/plans/2026-07-04-tool-framework-plan.md` — Phase 0-1 框架搭建实施计划（15 Tasks，已全部完成）

**历史设计文档（MVP+EventBus 架构，已过时）**：
- `docs/superpowers/specs/2026-06-14-refactor-design.md` — MVP+EventBus 架构重构设计
- `docs/superpowers/specs/2026-06-14-ui-modernization.md` — UI 现代化方案
- `docs/superpowers/specs/2026-06-19-ui-redesign-design.md` — QSplitter 动态可调整布局重设计
- `docs/superpowers/specs/2026-06-19-compact-layout-design.md` — 1366x768 低分辨率紧凑布局方案

**实施计划（plans/）**：
- `docs/superpowers/plans/2026-06-14-refactor.md` — 旧版重构计划
- `docs/superpowers/plans/2026-06-14-ui-modernization.md` — QSS 创建/QRC 更新计划
- `docs/superpowers/plans/2026-06-19-ui-redesign-plan.md` — 布局重构计划

**需求文档（doc/）**：已随 VSOA 依赖移除而删除

## 代码规范

- **语言**：全流程使用中文（注释、文档、会话），专业术语和论文引述除外
- **文档驱动**：以 `docs/` 中的设计文档为唯一标准，修改设计/接口/架构/功能时必须同步更新对应文档
- **命名**：类名 PascalCase、方法 camelCase、成员变量 `m_camelCase`、常量 UPPER_SNAKE_CASE
- **设计模式**：单例（ToolRegistry/ProtocolRegistry/AppState）、工厂（ToolBackend/ToolWidget/Adapter 创建）、观察者（lwmsgq 消息队列解耦）
- **Qt 约定**：UI 类继承 `QWidget`/`QMainWindow`，使用 `Q_OBJECT` 宏，通过 `Ui::` 命名空间引用 `.ui` 文件
- **lwserverbase 约定**：Backend 类继承 `ServiceTask`，重写 `OnStart()`/`OnStop()`，通过 `ServiceManager` 管理生命周期

## 注意事项

- **libcurl 运行时依赖**：编译链接 `lib/libcurl_imp.lib`，运行时需要 `lib/libcurl-x64.dll`（已纳入仓库）。CMake 构建后会自动复制 DLL 到输出目录（`add_custom_command(TARGET POST_BUILD)`），VS 手动调试时需自行复制到生成目录（如 `x64/Debug/`）

- **thirdparty 静态库依赖链**：
  ```
  lwcomm → lwevent → lwmsgq → lwlog → lwcommunicate → lwserverbase
  ```
  所有 thirdparty 库编译为 STATIC 库并链接进 DeployMaster.exe。修改 CMakeLists.txt 时需注意依赖顺序。

- **FtpManager 迁移中**：旧版 `FtpManager.cpp/h` 仍存在于 `src/model/`，但新代码应使用 `FtpAdapter`（实现 IProtocolAdapter）。旧 FtpManager 将在 Phase 2 FTP 模块全量迁移后删除

- **EventBus 已移除**：`DeployEvent` 元类型注册、EventBus 事件订阅等已从 `main.cpp` 和 `DeployMaster.cpp` 中移除。`src/utils/DeployEvent.h` 仍保留，待所有引用清理后删除


- **OPC UA 模块为演示模式**：`OpcUaClientTab` 仅含硬编码演示数据，真实实现需引入 open62541 或 Qt OPC UA 模块

- **密码不持久化**：程序不保存密码，每次启动需手动输入

- **尚无测试**：Phase 0-1 设计规划了单元测试（FtpAdapter/TelnetAdapter/ToolRegistry/DeviceBusWidget），但 `tests/` 目录和 CTest 配置尚未创建。计划在 Phase 2 全量迁移时同步补充

- **深色主题色板**（darkstyle.qss）：
  - 背景底层: #0B0E14 | 面板/容器: #141820 | 边框: #252A33
  - 主要文字: #C8CCD4 | 次要文字: #7B8494
  - 强调色（琥珀）: #F0A030 | 强调色 hover: #D48820
  - 成功色（青绿）: #40C8A0 | 警告色: #F0D050 | 错误色: #E85848

- **main.cpp 初始化顺序**（关键）：
  1. `QApplication` 创建
  2. `LogBridge::install()` — Qt → lwlog 桥接
  3. `ProtocolRegistry` 工厂注册（FtpAdapter + TelnetAdapter）
  4. 加载 darkstyle.qss
  5. `DeployMaster window` — 构造函数：DeviceBusWidget + ToolHost + 非 Tool Tab（文件部署/日志查询/MODBUS/OPC UA）
  6. `ToolRegistry` 注册内置 Tool 元数据
  7. `ToolHost` 工厂注册（预留，当前 Tool 通过 DeployMaster 直接创建）
  8. `window.initToolTabs()` — 延迟创建 Telnet/WebSocket Tool（需要工厂注册完成）
  9. `window.show()`
