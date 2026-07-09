# Changelog

## [2.1.0] — 2026-07-09

### 工具迁移 + 网络调试中继 + 首个测试目标

延续 2.0 的 Tool 架构，完成 Modbus 迁移、新增 NetRelayTool（网络调试中继 + 录制回放），并建立项目首个单元测试目标。

### 新增

#### NetRelayTool — 网络调试中继（`src/tools/NetRelayTool/`）
- **透明中继代理**：数据生产者（TCP/UDP 客户端）连接工具，工具原封不动双向转发到真实消费者，不影响生产数据流运行
- **TCP 透明代理**：`QTcpServer` 监听 + 每连接配对上游 `QTcpSocket`，双向 `readyRead→write` 中继
- **UDP 会话代理**：`QUdpSocket` 按源地址区分会话，上游转发 + 回程路由，空闲会话 5 分钟超时清理
- **实时 Hex+ASCII 视图**：区分方向（上行青绿 / 下行琥珀）+ 时间戳 + 会话号，单块渲染上限 64KB
- **流量录制（`.nrec` 自定义二进制格式）**：`RelayRecorder` 按方向 + 相对时间戳 + 会话号 + 原始字节顺序落盘；`RelayRecording` 读取并校验（magic / 版本 / 长度上限 / 截断检测）
- **按原始时序回放**：`RelayPlayer` 加载 `.nrec`，按相邻记录时间间隔重放上行数据到消费者（模拟生产者），支持原速 / 尽快、开始 / 暂停 / 停止 + 进度条
- **中继/回放互斥状态机**：`RelayMode{Idle,Relaying,Replaying}`，含 UDP 异步 DNS 窗口期的互斥守卫

#### Tool 迁移
- **ModbusTool**（`src/tools/ModbusTool/`）— ModbusBackend（QModbusTcpClient）+ ModbusWidget，从旧 `ModbusCluster` 迁移到 Backend + Widget 架构

#### 测试基础设施
- **tst_nrec**（`tests/`，QtTest + CTest，仅 CMake 构建）— 项目首个单元测试目标，覆盖 `.nrec` 录制往返、坏 magic / 坏版本 / 超长 length / 截断记录头拒绝、回放上行端到端（10 个用例）

### 安全加固（NetRelayTool，2026-07-08）
- 绑定地址 fail-closed 校验（非法输入直接拒绝，不再静默回退到 0.0.0.0；仅接受 IP 字面量 / localhost / any）
- 非回环监听时输出安全警告（中继无客户端鉴权）
- 连接/会话数上限（默认 50），TCP pending 缓冲上限 1MB，UDP 会话空闲超时清理
- 异步 DNS（`QHostInfo::lookupHost`）+ 代际令牌 + `m_dnsPending`，防止 stop/restart 后旧回调误触发与析构 UAF
- `.nrec` 读取器对不可信文件加固（magic / 版本 / 长度上限 / 截断校验）
- 导出前二次确认（提示含明文凭证/敏感数据）+ 导出/录制文件权限限制为仅所有者可读写
- 日志仅记录方向/字节数，不含原始内容（与 Telnet/WebSocket 一致）

### 修复
- `RelayPlayer` 回放末条 TCP 记录被 `close()` 丢弃 — 完成前等待 `bytesWritten` flush
- `RelayRecording` payload 长度无上限导致损坏文件触发超大分配 / `int` 溢出 — 按剩余文件字节校验
- NetRelayTool UDP 异步 DNS 窗口期中继/回放互斥漏洞 — `startReplay` 增加 `isRunning()` 守卫 + DNS 分发点即置 `Relaying`
- 回放失败时 UI 卡死（配置区被永久禁用）— 新增 replay-error 回调完整复位控件
- 中继启动失败时配置区被永久禁用 — errorCallback 增加 `isRunning()` 运行态守卫

### UI 主题重构（darkstyle.qss，「琴色是动词」）
- 强调色 #F0A030 从约 24 处静止控件（边框/填充）收回，仅保留于信号态：主操作按钮、`:focus` 输入框、选中/活跃项、分组标题、滑块 handle
- 静止边框（容器/输入/下拉/表头/tab/分隔条）统一改为中性石墨色（#252A33 / #333B48）
- 建立"能否操作"形态语言：输入框凹陷（填充 #0E1219）、次要按钮凸起（填充 #232A36）、容器扁平
- 进度条填充改青绿 #40C8A0（进行中语义，与琴色错开）
- QSplitter 拖动条移除琴色边框（消除"粗黄条"），改纯石墨窄条 + hover 提亮
- 修复 Qt QSS 不支持的 CSS 语法：`linear-gradient`/`radial-gradient` → 纯色；移除无效 `box-shadow`

---

## [2.0.0] — 2026-07-04

### 架构重构：可扩展通用设备运维平台

DeviceForge（原名 DeployMaster）从单体 Qt 应用升级为 **lwserverbase 服务核 + Qt Widget 壳** 双层插件化架构。2026-07-05 更名为 DeviceForge。

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
