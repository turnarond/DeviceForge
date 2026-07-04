# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

DeployMaster 是基于 Qt 6.10.1 + C++17 的 SylixOS PLC 工业级批量部署与运维平台。提供 FTP 批量部署、Telnet 批量命令、Modbus 集群测试、OPC UA 客户端、WebSocket 通信、VSOA 诊断分析等功能。

## 构建命令

### CMake（推荐）

```bash
# 配置（Windows MSVC）
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH="C:\Qt\6.10.1\msvc2022_64"

# 编译 Release
cmake --build . --config Release

# 运行
.\Release\DeployMaster.exe
```

### Visual Studio

项目根目录有 `DeployMaster.slnx` / `DeployMaster.vcxproj`，可直接用 VS2022 打开。需安装 Qt 6.10.1、Qt Visual Studio Tools 扩展，并在 VS 中配置 Qt 版本路径。

## 技术栈

| 组件 | 用途 |
|------|------|
| Qt 6.10.1 (Core/Gui/Widgets/Network/SerialBus/WebSockets) | 跨平台 UI + 网络 + Modbus |
| libcurl (lib/curl-8.20.0) | FTP 文件传输 |
| tinyvsoa (tinyvsoa/) | VSOA 诊断协议 |
| CMake 3.16+ | 构建系统 |
| Visual Studio 2022 | Windows 编译 |

## 代码架构

### 架构演进：MVP + EventBus（重构中）

项目正从单体架构（主窗口直接调用业务类）迁移到 MVP + EventBus 模式。详细设计见 `docs/superpowers/specs/2026-06-14-refactor-design.md`。

**当前状态**：基础框架已搭建（`src/framework/`），FTP 模块的 Presenter/Model 已创建，其他模块的 Presenter 仍是占位桩代码。主窗口 `DeployMaster.cpp` 混用了新旧两种模式。

### 三层组件

```
UI Layer (View)          Presenter Layer           Model Layer
─────────────────       ──────────────────       ─────────────────
DeployMaster.cpp        src/presenter/            src/model/
  ├─ Tab 页面组件         FtpPresenter (部分实现)    FtpManager (libcurl)
  ├─ DropListWidget       ModbusPresenter (桩)      [旧] TelnetClient
  └─ QSS 深色主题                                 [旧] ModbusCluster
                                                   [旧] OPC UA (演示)
        ↕                      ↕                   [旧] WebSocket
    EventBus (src/framework/)  ConnectionPool       [旧] DiagnosticClient/VSOA
        ↕                      AppState
```

### 核心框架组件（src/framework/）

- **EventBus**：单例，发布/订阅事件总线。通过 `DeployEvent::Type` 解耦模块间通信。使用方法：`subscribe(type, receiver, SLOT(...))` → `postEvent(event)`
- **ConnectionPool**：单例，按 IP+端口+类型 缓存连接（FTP/Telnet/Modbus/OPC UA/WebSocket），统一管理认证凭证
- **AppState**：单例，全局应用状态（选中设备列表、任务进度、忙闲状态）

### 事件类型（src/utils/DeployEvent.h）

`ConnectionStatusChanged` / `TaskStarted` / `TaskProgress` / `TaskFinished` / `ErrorOccurred` / `LogMessage` / `UploadRequest` / `CommandExecute`

所有通过 EventBus 传递的事件都使用 `DeployEvent`，中继 `QVariant data` 携带实际数据。

### 模块对应关系

| 功能 Tab | UI 类 | 业务逻辑 | 协议 |
|----------|-------|----------|------|
| 批量部署 | 直接集成在主窗口 | `FileDeploy.cpp` + `FtpManager` | FTP (libcurl) |
| 批量命令 | `TelnetDeploy` | `TelnetClient` + `TelnetManager` | Telnet (QTcpSocket) |
| 日志查询 | `LogQueryTab` | `FtpManager` | FTP (libcurl) |
| MODBUS 测试 | `ModbusCluster` | 直接使用 `QModbusTcpClient` | Modbus TCP |
| OPC UA 客户端 | `OpcUaClientTab` | 演示模式/桩 | OPC UA（待实现） |
| WebSocket | `WebSocketClient` | 内建 Server/Client 双模式 | WebSocket (QWebSocket) |
| 诊断分析 | `DiagnosticClient` | `tinyvsoa` + 解压引擎 | VSOA |

### 并发模型

- `QtConcurrent::run` 用于异步执行网络 IO 和重计算
- libcurl FTP 操作自带进度回调
- 部分模块使用 `QTimer` 定时刷新（Modbus 自动刷新、诊断状态轮询）
- 原子变量 (`std::atomic`) 控制 Telnet 运行/停止状态

### UI 布局

- 顶部全局配置栏（IP 列表 + FTP 凭证）
- QSplitter 分隔的左右区域：左侧 QTabWidget（功能模块）、右侧远端预览面板
- 底部日志区（共享 QTextEdit），通过 QSplitter 与工作区调整高度
- 深色主题样式表：`darkstyle.qss`（通过 `main.cpp` 加载）

## 设计文档

重构和 UI 设计的关键决策记录在 `docs/superpowers/` 中，修改架构前应先查阅：

- `docs/superpowers/specs/2026-06-14-refactor-design.md` — MVP+EventBus 架构重构设计
- `docs/superpowers/specs/2026-06-14-ui-modernization.md` — UI 现代化方案
- `docs/superpowers/specs/2026-06-19-ui-redesign-design.md` — 专业级布局重设计
- `docs/superpowers/specs/2026-06-19-compact-layout-design.md` — 紧凑布局方案

## 代码规范

- **语言**：全流程使用中文（注释、文档、会话），专业术语和论文引述除外
- **文档驱动**：以 `docs/` 中的设计文档为唯一标准，修改设计/接口/架构/功能时必须同步更新对应文档
- **命名**：类名 PascalCase、方法 camelCase、成员变量 `m_camelCase`、常量 UPPER_SNAKE_CASE
- **设计模式**：单例（EventBus/ConnectionPool/AppState）、观察者（EventBus 事件订阅）、工厂（连接创建）
- **Qt 约定**：UI 类继承 `QWidget`/`QMainWindow`，使用 `Q_OBJECT` 宏，通过 `Ui::` 命名空间引用 `.ui` 文件

## 注意事项

- **FtpManager 迁移**：旧版 `FtpManager.cpp/h` 已从根目录删除，现位于 `src/model/FtpManager.h` 和 `src/model/FtpManager.cpp`
- **OPC UA 模块为演示模式**：`OpcUaClientTab` 仅含硬编码演示数据，真实实现需引入 open62541 或 Qt OPC UA 模块
- **DiagnosticClient 依赖 VSOA**：依赖 `tinyvsoa/tinyvsoa.h`，通过 VSOA 协议连接远程设备触发诊断采集和下载
- **密码不持久化**：程序不保存密码，每次启动需手动输入
- **qRegisterMetaType**：`DeployEvent` 和 `DeployEvent::Type` 在 `main.cpp` 中注册了元类型，跨线程信号槽依赖此注册
