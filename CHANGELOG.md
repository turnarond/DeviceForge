# Changelog

## [2.3.0] — 2026-07-21

### 本地配置持久化（ConfigStore）

新增 SQLite + Windows DPAPI 配置层，所有 Tool 的输入面板（设备、OPC UA endpoint、FTP/SSH/Telnet 凭证、Modbus 从站、NetRelay 监听参数、WebSocket URL）首次启动即可恢复历史；密码字段经 Windows DPAPI 加密入库。

### 新增

- **ConfigStore 单例**（`src/config/ConfigStore.{h,cpp}`）：SQLite 单表 `config_items(type, key, value, created_at, updated_at)`，`ON CONFLICT` upsert；`save/load/exists/remove/list/exportTo/importFrom` API；时间戳统一毫秒
- **DpapiCrypto 跨平台封装**（`src/config/DpapiCrypto.{h,cpp}`）：Windows `CryptProtectData/CryptUnprotectData` + base64；非 Windows 平台 stub + qCWarning
- **SettingsDialog 设置面板**（`src/config/SettingsDialog.{h,cpp}`）：文件菜单 → 设置（Ctrl+,）；类型过滤、搜索、查看（编辑为只读 JSON 弹窗）、删除二次确认、清空全部、JSON 导入/导出；敏感字段遮罩
- **设备总线集成**：设备添加持久化 `device.list`（key=`ip:port`），启动加载历史；用户/密码失焦保存到 `ftp.credential`（密码 DPAPI）
- **OPC UA Tool 集成**：`m_endpointEdit` 改为可编辑 QComboBox，启动加载历史 `opcua.endpoint`；连接成功时 upsert 当前 URL
- **WebSocket Tool 集成**：保存/恢复 server 端口与 client URL
- **Modbus Tool 集成**：保存/恢复从站 ID 与寄存器参数
- **NetRelay Tool 集成**：保存/恢复监听与上游地址
- **Telnet Tool 集成**：保存/恢复协议与超时偏好（凭证统一走设备总线）
- **应用启动/关闭**：`main.cpp` 在 `DeployMaster` 前后分别 `ConfigStore::open()` / `close()`

### 测试

- **tst_config_store**（`tests/config/tst_config_store.cpp`）：基本读写往返、唯一键 upsert、list 按 updated_at 倒序、remove、import/export round-trip；启动测试用 temp 数据库隔离
- **tst_dpapi_crypto**（`tests/config/tst_dpapi_crypto.cpp`）：Windows DPAPI 加解密往返；非 Windows 平台由 stub 通过（无 DPAPI 路径）

### 部署注意

- 部署时需包含 `plugins/sqldrivers/qsqlite.dll`（Qt SQL SQLite 驱动）
- 密码密文绑定 Windows 当前用户账号；复制数据库到其他机器/用户无法解密

---

## [2.2.0] — 2026-07-21

### OPC UA 客户端完整化 + UI 重构 + 第一个连接鲁棒性补丁

完成 OPC UA 客户端的端到端打通（地址空间浏览 → DataChange 订阅），重写浏览面板 UI（5 列 + 类型友好名 + 节点类色块 + 分批渲染 + × 删除按钮），并对 open62541 amalgamation 打了一个针对非规范服务端的连接鲁棒性 patch。

### 新增

#### OPC UA 客户端增强（`src/adapter/OpcUaAdapter.cpp` + `src/tools/OpcUaClientTool/`）
- **地址空间浏览 5 列重构**：`# | 显示名 | NodeId | 数据类型 | 节点类`（`OpcUaClientWidget.cpp`）
  - 数据类型友好名映射：`i=24 → Int32`、`i=12 → String` 等，覆盖 OPC UA 内置 16 种类型
  - 节点类色块：Variable（青绿）/ Object（石蓝）/ Folder（深石）/ Method（琴色）
  - 搜索框即时过滤 + "共 N 项 · 匹配 M" 计数
- **浏览性能优化**：每 100 行 `viewport()->update()` 分批渲染，避免 300+ 节点时 UI 卡顿
- **× 删除按钮**：批量表（读/写）和订阅表每行末尾加删除按钮，点击删除该行；按几何反查行号避免索引漂移
- **Tab 文字提亮**：`QTabBar::tab` 未选中态 `#7B8494 → #A8ACB8`（在深黑底上对比度 +30%）
- **连接状态文字着色**：已连接态 `#40C8A0`（青绿）/ 未连接态 `#C8CCD4`（主文字色），通过 `QPalette` 实现

#### 测试基础设施
- **tst_opcua_encode**：本机编码隔离测试，不连服务器，验证 open62541 类型注册完整（`AnonymousIdentityToken`、`ActivateSessionRequest` 等）
- **tst_opcua_loopback**：进程内 `UA_Server` + `UA_Client` 端到端环回测试，验证客户端类使用正确（与远端设备无关）

### 修复

#### OPC UA 连接鲁棒性（`src/thirdparty/open62541/open62541.c`）
- **`activateSessionAsync` policyId 深拷贝**：某些设备（GetEndpoints 返回的 EndpointUrl 与连接 URL 不一致，如 `opc.tcp://0.0.0.0:4840` 或主机名 URL）会触发 open62541 内部端点 UAF，使 `utp->policyId` 在 ActivateSession 时已是悬垂指针。DeviceForge patch 在使用前对 policyId 做 `UA_String_copy` 深拷贝，匿名认证回退为空 policyId。UaExpert（基于不同栈）同样容忍此类服务端
- **`browseRoot` NodeId 深拷贝**：`varNodeIds.append(ref.nodeId.nodeId)` 浅拷贝在 `UA_BrowseResponse_clear` 后会让 varNodeIds 中的 string/bytestring/guid NodeId 持有悬垂指针；改为 `UA_NodeId_copy` 深拷贝，循环结束统一 `UA_NodeId_clear`

#### OPC UA 客户端类清理（`src/adapter/OpcUaAdapter.cpp`）
- 移除 `connect()` 中重复的 `UA_ClientConfig_setDefault`（`UA_Client_new` 已内部调用），消除 `securityPolicies` / `applicationUri` 浅拷贝覆盖已分配指针的内存泄漏
- 移除诊断 stdout 重定向（`_dup`/`freopen`），正常连接由 open62541 内部 logger 记录到 stderr

### 已知问题

- 部分非规范 OPC UA 服务端（如触发 UAF 的设备）需要补丁才能连接；DeviceForge patch 已在 amalgamation 内闭环，无需用户配置
- 仅 None 安全策略 + 匿名认证，Basic256Sha256 / 用户名 / 证书认证为后续版本计划

---

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

### 产品化打磨（去公司标识 + 图标 + 版本信息 + 关 console）

#### 去公司标识
- 全项目去除「南京翼辉」与「ACOINFO」字样，改为个人身份 `turnarond`（LICENSE 用真名 `yanchaodong`）
- 24 个源码版权头 `ACOINFO CloudNative Team` → `turnarond`；作者去公司邮箱
- `DeployMaster.rc` CompanyName → `turnarond`；`QSettings` 组织名 → `turnarond`
- 保留 lw* 第三方库的客观来源描述（`architecture.md` / `CMakeLists.txt`）

#### 应用图标
- 新增 `app.ico`（16/32/48/256 多尺寸，由 logo.png 生成）
- QRC 打包 `:/icons/app.ico`；`main.cpp` 加 `setWindowIcon`（任务栏 + 窗口左上角）

#### exe 版本/版权信息
- CMake 编译 `DeployMaster.rc`（图标 + VERSIONINFO 进 exe）
- 字段定稿：ProductName=DeviceForge / CompanyName=turnarond / FileDescription=工业级设备批量运维平台 / 版本 2.1.0

#### 关闭 console 黑框
- CMake `add_executable(DeviceForge WIN32 ...)` — PE 子系统=2(GUI)，双击不再弹黑框
- 与 vcxproj `SubSystem=Windows` 对齐

### 组播录制回灌（NetRelayTool Multicast）

- **组播录制**：以额外订阅者身份加入组播组抓收数据（`bind(AnyIPv4, ShareAddress|ReuseAddressHint)` 零影响），录制 .nrec（protocol=2 + reserved 区存组 IPv4 地址/端口）
- **组播回灌**：按原始时序重发录制数据回原组播组（默认原组可改），支持网卡选择
- `.nrec` 格式扩展：version 不变，用 reserved 16 字节存 port(u16)+ipv4(u32)，旧文件向后兼容
- UI：协议下拉加 Multicast + 网卡下拉

### SSH 适配器（批量命令扩展）

- 扩展现有 Telnet Tab 支持 SSH：新建 `SshAdapter`（IProtocolAdapter 子类，libssh2 实现）
- 密码认证 + TOFU 主机密钥（首次自动接受，指纹变化告警）；TelnetBackend 协议感知化
- libssh2 以预编译库集成（CMake + vcxproj 双构建，DLL 自动复制）
- UI：TelnetWidget 加协议下拉（Telnet / SSH）

### 构建修复

- **QRC 未纳入 CMake**：`.qrc` 加入 SOURCES，修复 CMake 版 exe 从未内嵌深色主题的缺陷
- **libcurl DLL 命名**：CMake POST_BUILD 改名复制 `libcurl-x64.dll` → `libcurl.dll`（与 vcxproj 对齐）——此前冒烟测试靠手动 cp 掩盖
- vcxproj PostBuild 修复：合并 libcurl + libssh2 复制为单 Command，避免相互覆盖
- 记入项目记忆 `cmake-vcxproj-parity-traps`：此后修改构建配置须逐项比对两套系统对等

### 质量提升

- **首个单元测试目标** `tst_nrec`（QtTest + CTest，CMake 构建）12 用例，覆盖 .nrec 往返/损坏拒绝/回放上行/组播往返/旧文件兼容
- SSH adapter 代码审查：发现 13 处问题（2 Critical + 7 Important + 4 Minor），全部修复
- 两次全分支 opus 审查 + 预合并复审

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
