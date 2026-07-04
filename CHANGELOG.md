# Changelog

## [2.0.0] — 2026-07-04

### 架构重构：可扩展通用设备运维平台

DeployMaster 从单体 Qt 应用升级为 **lwserverbase 服务核 + Qt Widget 壳** 双层插件化架构。
这是自项目创建以来最大规模的重构。

### 新增

#### 框架层 (`src/framework/`)
- **ToolBackend** — Tool 后端基类，继承 `lwserverbase::core::ServiceTask`，获得完整服务生命周期
- **ToolWidget** — Tool 前端基类，继承 `QWidget`，统一 Tool UI 接口
- **ToolHost** — 桥接层单例，负责 Tool 的创建/配对/销毁
- **ToolRegistry** — Tool 注册表，管理内置 + DLL 插件 Tool 元数据
- **ManifestParser** — 基于 tinyxml2 的 XML 插件清单解析器

#### 适配器层 (`src/adapter/`)
- **IProtocolAdapter** — 协议适配器统一接口（connect/disconnect/request/subscribe）
- **FtpAdapter** — FTP 适配器（libcurl），340+ 行完整实现
- **TelnetAdapter** — Telnet 适配器（lwcommunicate::LWTcpClient），支持请求-响应 + 流模式
- **ProtocolRegistry** — 协议适配器工厂注册表单例

#### 基础组件
- **LogBridge** (`src/logging/`) — Qt → lwlog 日志管道桥接
- **DeviceBusWidget** (`src/ui/`) — 设备总线 UI 组件，水平胶囊形设备列表 + 凭证管理

#### Tool 实现
- **FtpDeployTool** (`src/tools/FtpDeployTool/`) — 首个完整 Tool（Backend + Widget），迁移自旧 FTP 部署逻辑

#### Thirdparty 集成
- **lwcomm** — 跨平台工具库（文件系统、Base64、时间）
- **lwevent** — 事件信号库
- **lwmsgq** — 消息队列库（发布/订阅解耦）
- **lwlog** — 管道式日志库（Filter→Formatter→Appender）
- **lwcommunicate** — 网络通信库（TCP/UDP/Serial + 连接池 + 自动重连）
- **lwserverbase** — 微服务框架（ServiceTask 生命周期、ConfigManager、MetricsCollector）
- **tinyxml2** — 轻量 XML 解析器
- **nanopb** — 嵌入式 Protocol Buffers 编解码

#### UI 升级
- 工业仪表盘深色主题（#0B0E14 深黑底 + #F0A030 琥珀强调色 + #40C8A0 青绿状态色）
- 设备总线替代旧的 IP 列表 + 认证面板
- 日志区默认高度翻倍（125→250px）

### 移除
- `src/framework/EventBus` — 由 lwmsgq 消息队列替代
- `src/framework/ConnectionPool` — 由 lwcommunicate::LWConnPool 替代
- `FileDeploy.h/.cpp` — 空壳类
- `TelnetManager.cpp` — 1 字节空文件
- 顶部冗余 IP 列表面板（`frame_commonConfig`）— 由 DeviceBusWidget 替代

### 修复
- DeviceBusWidget 静态 `QPixmap` 在 `QApplication` 之前构造导致的启动崩溃
- `FtpDeployBackend` 异步上传 UAF（Use-After-Free）— 析构前等待 `QFuture`
- `curl_global_init` 每实例调用改为进程级 RAII 守卫
- `TelnetAdapter` 并发请求响应串扰 — 添加请求互斥锁
- `TelnetAdapter::subscribe()` 永真逻辑条件

---

## [1.0.0] — 2026-06-14 及之前

### 已有功能
- FTP 批量部署（libcurl）
- Telnet 批量命令（QTcpSocket）
- 日志查询与下载（FTP）
- Modbus 集群测试（QModbusTcpClient）
- WebSocket 客户端/服务端（QWebSocket）
- OPC UA 客户端（演示模式）
- 深色主题 QSS
- 拖拽文件上传
- 远端文件预览

### MVP+EventBus 架构（已废弃）
- EventBus 事件总线
- ConnectionPool 连接池（未使用）
- AppState 全局状态管理
- FtpPresenter（仅 FTP 模块迁移完成）
