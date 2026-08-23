# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 行为准则

> 完整 16 条规范见 **`docs/00-开发规范/开发规范.md`**（强制行为准则，本处为摘要）。

1. **中文优先**：对话与文档一律使用中文，专业术语（MVP、EventBus、FTP、Modbus、TDD、SDD、ABI 等）与论文引述除外
2. **文档是唯一对外标准，必须保持最新、不得腐败**：文档（`docs/`，索引见 `docs/README.md`）是交互接口与对齐标准。修改代码若涉及需求、接口、设计、架构、手册、版本号等内容，必须**同一提交内**同步更新对应文档，且文档中不得出现与代码不一致的事实性信息；发现重复/英文残留/失效引用立即合并清理。改版本号时必须全仓同步（CMakeLists.txt / DeviceForge.rc / README.md / CHANGELOG.md / `docs/01-白皮书/白皮书.md` / CLAUDE.md 等），以 CMakeLists.txt 的 `project(... VERSION ...)` 为准，并用 `tools/devtools/versioncheck.py` 校验
3. **分支规范**：需求开发、变更、修复 issue 一律从 `main` 新建分支（`feature/xxx`、`fix/xxx`、`docs/xxx`、`refactor/xxx`、`chore/xxx`）开发，验证后 `--no-ff` 合并回 `main`；不在 `main` 堆叠未提交改动，一个分支只做一件事
4. **TDD**：永远遵循红-绿-重构循环；每次请求实现必须附带对应测试代码或指明要变绿的测试用例；红灯代码不提交
5. **SDD 工程化流程**：需求 → 方案设计 → 任务规划 → 实施 → 交付，关键阶段专家评审（**尤其方案设计**）；见 `docs/00-开发规范/SDD-工程化流程.md`
6. **整洁**：代码整洁（《代码整洁之道》）与架构整洁（《架构整洁之道》）——依赖方向 UI → 业务 → 适配器，复用 lwserverbase/lwmsgq/IProtocolAdapter/ConfigStore 等既有机制，全局考虑不绕开
7. **工具集**：配套工程工具统一放 `tools/`（Python 系列，独立架构 + 自带测试）：`versioncheck.py`（版本一致性）、`smoke.py`（UI 冒烟）
8. **其余条款**（详见总纲）：问题及时提 issue（供应商问题提 vendor，不混入本工程）；版本边界 + 打包节点打 tag `vX.Y.Z`（`docs/01-白皮书/版本发布与Tag规范.md`）；重构先讨论评审再独立分支；对外 SDK 接口向前兼容不破坏 ABI；长稳（内存/句柄泄漏）检查；Debug（Windows 用 cdb、Linux 用 gdb、Windows 不好调转 WSL）

## 项目概述

DeviceForge（原名 DeployMaster）是基于 Qt 6.11.1 + C++17 的**工业级系统部署调试工具**。提供 FTP/FTPS 批量部署、Telnet 批量命令、Modbus 集群测试、OPC UA 客户端、WebSocket 通信等功能。2026-07-05 更名为 DeviceForge。

## 构建命令

### 一键构建（build.bat，推荐）

根目录 `build.bat` 一键完成"清理旧缓存 → CMake 配置（VS2022 生成器）→ Release 编译"：

```bash
# 默认 Qt 路径 c:\Qt\6.11.1\msvc2022_64（如不同需编辑脚本顶部 QT_PREFIX）
./build.bat
```

脚本会先清理 `build/` 内的 `CMakeCache.txt`/`CMakeFiles`/`.vs` 等旧产物（避免平台/生成器不匹配），产物为 `build/Release/DeviceForge.exe`，并生成 `build/DeviceForge.sln` 供 VS 打开。

### CMake（手动）

```bash
# 配置（Windows MSVC，需提前安装 Qt 6.11.1）
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\6.11.1\msvc2022_64"

# 编译 Release
cmake --build . --config Release

# 运行（CMake 目标为 DeviceForge，生成 DeviceForge.exe）
.\Release\DeviceForge.exe
```

> **注意**：CMake `project()` 名与可执行目标均为 `DeviceForge`（见 `CMakeLists.txt`，`project(DeviceForge VERSION 2.8.0 ...)` + `qt_add_executable(DeviceForge ...)`），产物是 `DeviceForge.exe`。VS/vcxproj 工程已删除（2026-08-01），CMake 是唯一构建系统，产物名不再有二义性。

### 测试（CTest）

```bash
cd build
ctest -C Release --output-on-failure        # 全部测试
ctest -C Release -R tst_nrec --output-on-failure   # 单个测试（按名过滤）
```

现有 QtTest 目标（`tests/CMakeLists.txt`，`enable_testing()` + `add_subdirectory(tests)`）：
- `tst_nrec`：NetRelayTool `.nrec` 录制往返 / 坏文件拒绝 / 回放上行（源在 `tests/NetRelayTool/tst_nrec.cpp`）
- `tst_updatechecker`：OTA 更新检查逻辑（源在 `tests/tst_updatechecker.cpp`）
- `tst_dpapi_crypto`：Windows DPAPI + base64 加解密；非 Windows 为 no-op stub（源在 `tests/config/tst_dpapi_crypto.cpp`）
- `tst_config_store`：ConfigStore SQLite + JSON 导入导出（源在 `tests/config/tst_config_store.cpp`）
- `tst_opcua_encode`：open62541 编码路径隔离测试，C 语言，不连服务器（源在 `tests/opcua_encode/tst_opcua_encode.c`）
- `tst_opcua_loopback`：进程内 OPC UA 服务端-客户端环回测试（源在 `tests/opcua_encode/tst_opcua_loopback.cpp`）
- `tst_sftp_plan`：SFTP 上传规划纯逻辑测试（源在 `tests/sftp/tst_sftp_plan.cpp`）
- `tst_deploy_loop`：部署循环 mock 测试（源在 `tests/deploy/tst_deploy_loop.cpp`）
- `tst_remote_model`：RemoteFileModel 排序语义（`.`/`..` 置顶 + 目录优先 + 名称序，源在 `tests/remote_model/tst_remote_model.cpp`）
- `tst_theme`：ThemeUtils 主题路径映射 + 亮色 QSS 资源存在性（编译 QRC，源在 `tests/theme/tst_theme.cpp`）

> CTest 属性已通过 `ENVIRONMENT_MODIFICATION` 把 Qt `bin` 目录前插到 `PATH`，否则 Windows 直接跑测试会报 `0xc0000135`（DLL 缺失）。

### Visual Studio

旧 `DeployMaster.vcxproj`/`DeployMaster.sln` 已删除，统一用 CMake 构建。build.bat 会生成 `build/DeviceForge.sln`，用 VS2022 直接打开即可（CMake 的 POST_BUILD 已自动复制 `libcurl-x64.dll`/`libssh2.dll` 到输出目录，调试无需手动处理）。

## CI/CD

GitHub Actions（`.github/workflows/msbuild.yml`，workflow 名为 "CMake Build"）：push/PR 到 `main` 分支时触发，在 `windows-latest` 上通过 `jurplel/install-qt-action@v4` 安装 Qt 6.9.2（含 `qtserialport qtserialbus qtwebsockets` 模块），使用 **CMake + CTest** 编译并测试 Debug 配置（非 MSBuild，文件名 `msbuild.yml` 为历史遗留）。

> CI 使用的 Qt 版本（6.9.2）与本地开发版本（6.11.1）不同，注意避免使用仅新版才有的 API。

## 技术栈

| 组件 | 用途 |
|------|------|
| Qt 6.11.1 (Core/Gui/Widgets/Network/SerialBus/WebSockets/Sql/Concurrent/Test) | 跨平台 UI + 网络 + Modbus + 单元测试 + 配置持久化 |
| libcurl (8.16.0，lib/libcurl-x64.dll) | FTP 文件传输 |
| open62541 v1.5.5 (单文件分发) | OPC UA 客户端 |
| lwserverbase | 服务框架（ServiceTask 生命周期、ServiceManager、ConfigManager） |
| lwcommunicate | 网络通信库（TCP/UDP/Serial 连接池、自动重连） |
| lwcomm | 跨平台工具库（文件系统、Base64、字符串、时间） |
| lwlog | 日志库（控制台/文件 appender、pattern 格式化） |
| lwmsgq | 消息队列（发布/订阅、跨线程解耦） |
| lwevent | 事件信号库 |
| tinyxml2 | XML 解析（插件清单 manifest.xml） |
| nanopb | Protocol Buffers 编解码（消息队列序列化，待集成） |
| CMake 3.22+ | 构建系统 |
| Visual Studio 2022 | Windows 编译 |

## 代码架构

### 架构状态：DeviceForge (DeployMaster 2.0) Phase 0-2 完成，当前版本 2.8.0

项目已完成从 MVP+EventBus 单体架构到 **lwserverbase 服务核 + Qt Widget 壳** 双层架构的基础设施搭建 + 主要 Tool 迁移 + 安全加固 + 配置持久化 + FTP 双栏重构 + 布局现代化 + v2.5 功能补完 + SylixOS 适配 + v2.6 SFTP 批量部署 + 双栏面板模块化（FileBrowserPanel/IFileSource，2026-08）+ v2.7 UX 收尾（远程异步化/面板源选择器/系统拖入，2026-08）。

**架构模型**：Tool = ToolBackend (ServiceTask) + ToolWidget (QWidget)，通过 lwmsgq 双向解耦。统一 IProtocolAdapter 接口 + ProtocolRegistry 连接池。

**v2.5 功能补完（2026-07-26）**：
- FtpAdapter 新增 `renameFile()`（RNFR/RNTO）+ `makeDirectory()`（MKD）+ `cancelTransfer()` + `setCancelFlag()`；`deleteDirectory()` 递归删除（LIST→逐项 DELE→RMD）
- SshAdapter 新增 SFTP 子系统（8 个方法：sftpListDirectory/sftpUploadFile/sftpDownloadFile/sftpDeleteFile/sftpDeleteDirectory/sftpRename/sftpMakeDirectory/sftpSetProgressCallback）
- RemoteFileModel 精确对比（size 匹配替代文件名匹配）+ 列排序（sort()，目录优先、`.`/`..` 置顶）
- FtpDeployWidget：协议切换（FTP/SFTP 下拉）、选择性部署（selectedDevices()，未选中回退全部）、连接缓存复用、路径换行符清理、重命名路径穿越校验
- MultiProgressWidget 极简化：只保留总进度条 + 取消按钮（每设备状态通过日志展示）

**v2.6 SFTP 批量部署（2026-08-07）**：
- IDeployable 部署能力接口（`src/adapter/IDeployable.h`）：uploadFile/uploadFolder/clearRemoteDirectory/setProgressCallback/setCancelFlag，FtpAdapter/SshAdapter 声明实现
- SshAdapter 部署链路：`sftpUploadFolder()`（递归 mkdir + 逐文件上传）、`sftpClearDirectory()`（清空保留目录）、`sftpSetCancelFlag()` 取消支持
- FtpDeployBackend::startUpload 部署循环协议化：按协议参数从 ProtocolRegistry 取通道，FTP/SFTP 同一部署逻辑
- FtpDeployWidget 协议下拉选 SFTP 后可直接部署/拖拽上传（不再提示切换 FTP）
- 测试：tst_sftp_plan（上传规划纯逻辑 3 用例）+ tst_deploy_loop（部署循环 mock 3 用例）

**双栏面板模块化（2026-08，file-browser）**：
- `IFileSource` 文件源统一接口 + `LocalFileSource`/`RemoteFileSource` 两实现（`src/ui/`）；FtpDeployWidget 瘦身约 3 倍为双面板宿主（1134→396 行），文件浏览/导航/右键/拖拽全部下沉 `FileBrowserPanel`（统一表格视图 + 路径栏 + 面包屑，双击导航/F2/F5/F6/Tab/方向语义）
- 批量部署链路保留（FtpDeployBackend 零改动）；部署目标目录 = 右侧远程面板当前路径（先右侧导航再部署；空路径回退根目录）
- 状态栏 TC 化（快捷键提示条 + 部署方向指示）；QSS 清理 FTP 面板死规则（#panelTitle/#crumbCurrent/#localPathEdit/#browseBtn）
- 远程面板修复（2026-08-12）：设备添加/删除后自动连接刷新（deviceSelectionChanged → onRefreshRemote，修复 blockSignals 触发链断裂导致远程源永不创建）；工具栏「⟳ 刷新」按钮回归；连接状态点（灰/青绿/红，底色代码动态设置 + 双 QSS 圆角）；工具栏分组布局（连接组：协议/设备/端口/FTPS/状态点；部署组：清空/重启/刷新/部署，组间 QFrame VLine 分隔）；FileBrowserPanel 无源提示「未连接远程，请选择设备并刷新」
- 测试：新增 tst_file_source（IFileSource 本地源 4 用例）；回归 11 目标全过（含 tst_remote_model 等既有目标）

**v2.7 UX 收尾（2026-08，ux-finish）**：
- **远程列表/连接异步化**：FileBrowserPanel::loadDirectory 用 QtConcurrent::run + 代际令牌（旧回调丢弃）+ QueuedConnection 主线程回调，慢速/断网目录读取不再冻结 UI，失败面包屑「加载失败」；RemoteFileSource 加 QMutex 串行化（修复异步化引入的多 worker 并发崩溃/UAF/数据竞争，reconnect 拆 connectLocked 防非递归死锁）；onRefreshRemote 三分支 QtConcurrent 化 + 状态点 **Connecting 灰闪态**（500ms）+ 连接代际令牌 m_connGeneration 防陈旧回调挂载 + worker 捕获列表 QPointer 守卫
- **面板源选择器**：面板顶部源类型下拉（本地/FTP/SFTP）+ 面板级设备下拉；左面板可独立切远程浏览源（独立连接缓存 + 代际令牌 + busy 串行化，端口取设备自身）；右面板 ↔ 工具栏协议/设备双向联动（blockSignals 防循环）；左面板远程源时部署守卫拦截；「ssh」协议 id 归一化为 sftp
- **系统文件拖入上传恢复**：资源管理器文件拖入远程面板逐文件上传（面包屑汇总「上传完成」/「N 项失败」）+ 异步 refresh；与面板间拖拽共存（dropEvent 三分支）；DragEnter/DragMove 放开 hasUrls；空 url 防护「未检测到可上传的文件」
- **顺带项**：SshAdapter readdir 中途错误上报 lastError（自动重连盲区修复）；RemoteFileModel::sort 降序相等键短路（严格弱序 UB 修复）；面包屑双主题色值测试锁定
- 测试：新增 tst_panel_async（异步加载竞态/重连/UAF 定向回归/源选择器）+ tst_qss_pixels（双主题像素验证）；回归 13 目标全过

**v2.7.0 全量健康评审修复（2026-08-19，fix/v2.7-review，21 项）**：
- **ModbusTool**：寄存器类型映射修复（Critical——Holding 此前发 0x02 读错存储区）、设备列表注入修复（bindDevices 全仓无调用方，读取路径实际无目标设备；Widget 读/写前从 ConfigStore device.list 同源同步）、异常码中文展示（0x01-0x0B + errorString 红色行）、写入对话框接通（0x06 寄存器 / 0x05 线圈，设备下拉）、连接失败 errorOccurred 上报 + stateChanged 一次性连接、上限按类型（Holding/Input 125、Coils/Discrete 2000）
- **Telnet**：空超时假成功修复（Critical——无响应报「响应超时」）、提示驱动登录（login:/Password: 等待 + 「incorrect」立即断开；无认证服务器跳过认证向后兼容）、取消 100ms 内中断在途命令（原阻塞 300s）、QSettings → ConfigStore（telnet.prefs/securityWarning）、状态色双主题 QSS（QLabel state 属性）
- **WebSocket**：WSS 服务端自签名证书（RSA 2048 预生成打包入 QRC 加载式）、Token 认证接线（Server 设置区输入框 + ConfigStore 明文）、pubsub 消息进日志、首冒号切分（Client TOPIC 同款修复）、UNSUBSCRIBE + 退订按钮、stopClient 先断开再删除
- **NetRelay**：TCP 写背压（64KB 读缓冲 / 1MB 暂停 / 512KB 滞回恢复）、会话树 O(1) 索引
- **Ssh**：静态 known-hosts QSet 加互斥锁（并发数据竞争）
- 测试：新增 tst_modbus_mapping + tst_telnet_timeout；回归 15 目标全过

**SylixOS FTP 适配经验（重要，勿回退）**：
- **EPSV 不支持**：`CURLOPT_FTP_USE_EPSV=0` 强制 PASV（uploadFile 和 setupCommonOpts）
- **cd() 只支持单级路径**：必须用 `CURLFTPMETHOD_MULTICWD` 逐级 CWD（`CWD apps` → `CWD xxx`），SINGLECWD 发送 `CWD a/b` 多级相对路径会失败
- **QUOTE 命令用根 URL + 绝对路径**：DELE/RMD/RNFR/RNTO/MKD 用 `buildUrl("/")` + 绝对路径命令，避免 curl 对目标 URL 的 RETR 预检误报
- **DELE 失败多为文件不存在**（非权限问题）；挂载点（如 schneider_system_test）CWD/LIST 行为异常，需服务器端处理
- LIST 返回 Unix 格式，FtpListParser 已兼容；`. ` 条目需客户端过滤，`..` 需补充（嵌入式服务器不返回）

> **注意**：ToolHost::createTool() 只支持单活跃 Tool，目前所有 Tool 均通过 `DeviceForge` 主窗口直接创建 Backend + Widget（`setupXxxTab()` / `initToolTabs()`），绕过 ToolHost。`main.cpp` 中的 `ToolHost::registerBuiltinFactory()` 目前仅为预留。待 ToolHost 支持多 Tool 并发后切换。

**已完成（Phase 0-2）**：
- Phase 0（基础设施对齐）：thirdparty 库编译集成、LogBridge 日志桥接、适配器层（IProtocolAdapter/FtpAdapter/TelnetAdapter/ProtocolRegistry）
- Phase 1（框架搭建）：ToolBackend/ToolWidget 基类、ManifestParser 插件清单解析、ToolRegistry 工具注册表、ToolHost 桥接层、DeviceBusWidget 设备总线、darkstyle.qss 工业仪表盘色板、FtpDeployTool 首个完整 Tool
- Phase 2（工具迁移 + 新 Tool）：TelnetTool ✅、WebSocketTool ✅、FtpDeployTool ✅（UI 重设计、FTPS 加密）、ModbusTool ✅（`src/tools/ModbusTool/`，Backend + Widget，QModbusTcpClient）、NetRelayTool ✅（`src/tools/NetRelayTool/`，TCP/UDP/组播 透明中继代理，双向流量捕获 + Hex 实时视图）；旧 "批量部署" Tab 已隐藏；远端预览面板（v2.2.0 引入，v2.4 移除——功能由 FtpDeployWidget 双栏远程面板替代）

**安全加固（2026-07-05）**：
- FTPS 加密传输（FtpAdapter::setUseFtps + CURLOPT_USE_SSL + UI 复选框）
- libcurl 协议白名单（所有 curl handle CURLOPT_PROTOCOLS_STR = "ftp,ftps"）
- Telnet 认证失败阻断（凭证发送失败→立即断开）
- WebSocket 默认绑定 127.0.0.1 + 可选 Token 认证（URL 参数 ?token=）
- 内存密码安全擦除（AuthInfo::clear）
- 日志去敏感化（Telnet/WebSocket 消息内容改为记录字节数）
- 安全审查报告见 `docs/03-设计/方案设计/2026-07-05-FTP部署改进设计.md`

**配置持久化（2026-07-25，v2.3.0）**：
- ConfigStore 单例（`src/config/ConfigStore.cpp/.h`）：基于 SQLite 的键值配置持久化（设备列表/凭证/端点历史/Tool 设置），支持 JSON 导入导出
- DpapiCrypto（`src/config/DpapiCrypto.cpp/.h`）：Windows DPAPI 加密封装（CryptProtectData/CryptUnprotectData），用于加密存储敏感字段（凭证密码等）；非 Windows 平台为 no-op stub，明文存储
- SettingsDialog（`src/config/SettingsDialog.cpp/.h`）：设置面板 UI（通用/网络/安全 三页），入口在主窗口菜单栏
- 各 Tool 已接入 ConfigStore：DeviceBusWidget 设备记录持久化 + 启动加载、OpcUaClientWidget endpoint 历史下拉、FtpDeployWidget 凭证 DPAPI 加密持久化、SettingsDialog 设置面板

**NetRelayTool 安全加固（2026-07-08）**：
- 绑定地址 fail-closed 校验（非法输入直接拒绝，不再静默回退到 Null→0.0.0.0；仅接受 IP 字面量/localhost/any）
- 非回环监听时输出安全警告（中继无客户端鉴权，暴露到网络需环境可信）
- 连接/会话数上限（默认 50），TCP pending 缓冲上限 1MB，UDP 会话空闲 5min 超时清理
- 异步 DNS（QHostInfo::lookupHost）+ 代际令牌 + m_dnsPending，防止 stop/restart 后旧回调误触发与析构 UAF
- 导出前二次确认（提示含明文凭证/敏感数据）+ 导出文件权限限制为仅所有者可读写
- 单块 Hex 渲染上限 64KB（防大数据块导致 UI 内存尖峰）
- 日志仅记录方向/字节数，不含原始内容（与 Telnet/WebSocket 一致）

**待完成**：
- QPluginLoader DLL 加载
- SCP 支持（实现 IDeployable + 复用 ssh 协议键，集成到 FTP 双栏远程面板中，v2.8 候选）
- ToolHost 多 Tool 并发支持（当前 Tool 通过 DeviceForge 主窗口直接创建）
- NetRelayTool 非阻塞增强项（Phase 4 审查记录，非 ship-blocker）：非回环绑定改为模态确认弹窗、客户端来源 allowlist（暴露到不可信网段时必需）
- FtpDeployWidget 远程表头排序指示器刷新后失配（cosmetic，低优先）

详细设计见 `docs/03-设计/方案设计/2026-07-04-工具框架设计.md`。
实施计划见 `docs/03-设计/实施计划/2026-07-04-工具框架计划.md`。

### 双层架构

```
UI Layer (Qt Widget)         Framework Layer            Adapter Layer
──────────────────           ─────────────────          ──────────────
DeviceForge.cpp              ToolHost (桥接层)          IProtocolAdapter
  ├─ DeviceBusWidget          ToolRegistry (注册表)       ├─ FtpAdapter
  ├─ Tool Navigation          ToolBackend (基类)          ├─ TelnetAdapter
  └─ QSS 双主题（暗/亮）     ToolWidget (基类)           └─ ProtocolRegistry
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
- **FtpAdapter**：实现 IProtocolAdapter，内部复用 libcurl。额外暴露 FTP 特有操作（uploadFile/uploadFolder/downloadFile/listDirectory/listDirectoryParsed/deleteFile/deleteDirectory/clearRemoteDirectory/renameFile/makeDirectory/cancelTransfer/setCancelFlag）；deleteDirectory 递归删除（LIST→逐项 DELE→RMD）；SylixOS 适配（EPSV 禁用、MULTICWD、QUOTE 根 URL）
- **TelnetAdapter**：实现 IProtocolAdapter，基于 `lwcommunicate::LWTcpClient`。支持请求-响应 + 流模式
- **SshAdapter**：实现 IProtocolAdapter，基于 libssh2。提供 SSH 加密通道（TelnetTool 中作为 TelnetAdapter 的安全替代）+ SFTP 文件子系统（sftpListDirectory/sftpUploadFile/sftpDownloadFile/sftpDeleteFile/sftpDeleteDirectory/sftpRename/sftpMakeDirectory）+ 部署链路（sftpUploadFolder 递归 mkdir 上传 / sftpClearDirectory 清空保留目录 / sftpSetCancelFlag 取消支持，IDeployable）
- **OpcUaAdapter**：实现 IProtocolAdapter，封装 open62541 客户端。支持读/写/订阅/浏览，`UA_MULTITHREADING=100` 编译
- **ProtocolRegistry**（单例）：协议适配器工厂注册表（registerFactory/create/isRegistered/registeredProtocols）

### 日志桥接（src/logging/）

- **LogBridge**：Qt → lwlog 桥接。`LogBridge::install()` 全局安装 QtMessageHandler，将 qDebug/qWarning/qCritical/qInfo 映射到 lwlog 的 DEBUG/WARN/ERROR/INFO 级别，同时保留 stderr 控制台输出

### UI 组件（src/ui/）

- **DeviceBusWidget**：顶部紧凑设备栏。胶囊形 QPushButton 水平流式布局，支持多选、添加、删除设备。石墨底 + 琴色选中态边框。工业仪表盘风格
- **FileBrowserPanel**：通用双栏文件面板（FTP 双栏宿主组装；源可配置）。统一表格视图（RemoteFileModel/FtpFileInfo 渲染）+ 面板顶部路径栏（Enter 跳转）+ 底部面包屑（当前路径文本）+ **面板源选择器行**（源类型下拉 本地/FTP/SFTP + 面板级设备下拉，远程源时显示；setSource 显示自动同步，blockSignals 防回环）；双击进入目录/`..` 退出；TC 快捷键 F2 重命名 / F5 复制到对面 / F6 移动到对面 / Tab 焦点切到对面面板；右键菜单（进入/新建目录/重命名/删除/复制到对面/移动到对面/复制路径/刷新）；面板间拖拽（方向语义：拖入对面=复制，本地↔远程=上传/下载，远程↔远程提示暂不支持）；**系统文件拖入**（资源管理器文件拖入远程面板逐文件上传，空 url 防护）；**异步加载**（loadDirectory QtConcurrent + 代际令牌，慢速目录不冻结 UI，失败面包屑「加载失败」+ 重连重试一次）
- **IFileSource**：文件源统一接口（list/mkdir/rename/remove(path,isDir)/clearDirectory/upload/download/connect/isConnected/lastError/进度回调/取消标志），与协议无关地操作文件系统。实现：**LocalFileSource**（本地 std::filesystem，纯逻辑可单测）与 **RemoteFileSource**（包装 FtpAdapter FTP/FTPS / SshAdapter SFTP）

### 已完成的 Tool（src/tools/）

六个 Tool 均遵循 Backend (继承 ToolBackend / ServiceTask) + Widget (继承 ToolWidget / QWidget) 配对模式：

- **FtpDeployTool**（`src/tools/FtpDeployTool/`）：首个完整 Tool。FtpDeployBackend（通过 ProtocolRegistry 按协议获取 FtpAdapter/SshAdapter）+ FtpDeployWidget（双面板宿主重构：QSplitter 组装两个 FileBrowserPanel——左侧本地（LocalFileSource，固定，v2.7 起可经面板源选择器独立切 FTP/SFTP 远程浏览）、右侧远程（RemoteFileSource，按工具栏协议/设备/端口/FTPS 配置重建），统一表格视图 + 路径栏（Enter 跳转）+ 底部面包屑，双击进入目录/`..` 退出，TC 快捷键 F2 重命名 / F5 复制到对面 / F6 移动到对面（方向语义：本地↔远程=上传/下载，远程↔远程提示暂不支持）/ Tab 切栏，右键菜单（进入/新建目录/重命名/删除/复制到对面/移动到对面/复制路径/刷新），面板间拖拽 + 系统文件拖入上传），支持 FTPS 加密 + SFTP 批量部署（v2.6 部署循环协议化：FTP/SFTP 同一部署逻辑，协议下拉选 SFTP 直接部署）；文件浏览/操作全部下沉 `src/ui/FileBrowserPanel` + `IFileSource`，批量部署链路保留（部署目标目录 = 右侧面板当前路径，先右侧导航再部署；左面板为远程浏览源时部署被守卫拦截）；工具栏双组布局（连接组：协议/设备/端口/FTPS 加密/连接状态点，状态点灰=未连接/青绿=已连接/红=失败/Connecting 灰闪，底色代码动态设置；部署组：清空/重启/「⟳ 刷新」/「▶ 部署到 N 台设备」，组间 QFrame VLine 分隔），设备总线添加/删除设备后自动连接刷新（deviceSelectionChanged → onRefreshRemote）；连接与列表加载全异步化（QtConcurrent + 代际令牌 m_connGeneration 防陈旧回调 + QPointer 守卫，断网超时不再冻结 UI）；面板源选择器与工具栏双向联动（blockSignals 防循环，右面板源变化回写协议/设备并自动重建）
- **TelnetTool**（`src/tools/TelnetTool/`）：TelnetBackend（TelnetAdapter → lwcommunicate / SshAdapter → libssh2）+ TelnetWidget，批量 Shell 命令，支持 Telnet/SSH 切换，认证失败阻断
- **WebSocketTool**（`src/tools/WebSocketTool/`）：WebSocketBackend（QWebSocket）+ WebSocketWidget，Server/Client，默认绑定 127.0.0.1 + 可选 Token 认证
- **ModbusTool**（`src/tools/ModbusTool/`）：ModbusBackend（QModbusTcpClient）+ ModbusWidget，批量读写寄存器（读：0x01-0x04 按类型正确映射 + 异常码中文展示；写：0x06 寄存器 / 0x05 线圈对话框），QTimer 自动刷新
- **NetRelayTool**（`src/tools/NetRelayTool/`）：NetRelayBackend（QTcpServer + QUdpSocket）+ NetRelayWidget，TCP/UDP/组播(Multicast) 透明中继代理，双向流量双向原样转发，Hex+ASCII 实时视图 + 导出；支持流量录制（`.nrec` 自定义二进制格式）+ 按原始时序回放上行到消费者（RelayRecorder/RelayRecording/RelayPlayer，RelayMode 中继/回放互斥状态机）；组播录制零影响加入组抄收(.nrec protocol=2 存组地址)+ 回灌原组 + **实时转发**（M→U 转单播 / M→M 转另一组播组，UI 二次确认防混叠，地址校验纯逻辑收敛于 NetRelayTypes.h：isValidMulticastAddress/isValidForwardTarget）
- **OpcUaClientTool**（`src/tools/OpcUaClientTool/`）：OpcUaClientBackend（OpcUaAdapter → open62541）+ OpcUaClientWidget，支持批量读/写节点、DataChange 订阅（`UA_MULTITHREADING=100` 线程安全）、地址空间 5 列浏览（DisplayName/NodeId/Type/Value/Actions + × 删除按钮），endpoint 历史下拉（ConfigStore 持久化）

### 模块对应关系

| 功能 Tab | UI 类 | 业务逻辑 | 协议 | 架构状态 |
|----------|-------|----------|------|----------|
| 文件部署 | FtpDeployWidget (Tool) | FtpDeployBackend → FtpAdapter/SshAdapter | FTP/FTPS (libcurl) / SFTP (libssh2) | ✅ 已迁移 + FTPS 加密 + SFTP 批量部署 + 双栏面板化（FileBrowserPanel/IFileSource，TC 交互）+ v2.7 UX 收尾（异步加载/源选择器/系统拖入） |
| 批量命令 | TelnetWidget (Tool) | TelnetBackend → TelnetAdapter/SshAdapter | Telnet (lwcommunicate) / SSH (libssh2) | ✅ 已迁移 + SSH 支持 |
| WebSocket | WebSocketWidget (Tool) | WebSocketBackend → QWebSocket | WebSocket (QWebSocket) | ✅ 已迁移 + 绑定/认证 |
| 日志查询 | 已删除 | 所有 Tool 日志统一路由到全局日志面板（v2.4 日志统一） |  | 🗑 已移除 |
| MODBUS 测试 | ModbusWidget (Tool) | ModbusBackend → QModbusTcpClient | Modbus TCP | ✅ 已迁移 |
| 网络调试 | NetRelayWidget (Tool) | NetRelayBackend → QTcpServer/QUdpSocket | TCP/UDP/组播 透明代理 + 录制回放 + 组播实时转发 | ✅ 新增 (2026-07-08) |
| OPC UA 客户端 | OpcUaClientWidget (Tool) | OpcUaClientBackend → OpcUaAdapter | OPC UA (open62541) | ✅ 已实现（读/写/订阅/浏览，v2.3.0） |

### UI 资源

- **QRC**：`src/app/DeviceForge.qrc` — 打包 `darkstyle.qss`（`:/styles/darkstyle.qss`）+ `darkstyle-light.qss`（`:/styles/darkstyle-light.qss`）+ `app.ico`（`:/icons/app.ico`）
- **QSS**：`src/app/darkstyle.qss` — 工业仪表盘深色主题「琴色是动词」体系：深黑底 (#0B0E14) + 中性石墨结构边框 (#252A33/#333B48)，琴色 (#F0A030) 仅用于信号态（主按钮/focus/选中/活跃），进度条青绿 (#40C8A0)
- **UI 文件**：`src/app/DeviceForge.ui`（主窗口）

### 源码文件清单

**应用壳（src/app/）**：
`main.cpp` / `ThemeUtils.h`（主题路径映射）/ `DeviceForge.cpp/.h`（主窗口类）/ `DeviceForge.ui` / `DeviceForge.qrc` / `DeviceForge.rc` / `resource.h` / `app.ico` / `darkstyle.qss` / `darkstyle-light.qss` — 2026-08-02 由根目录迁入，统一应用壳入口

**新架构源码（src/）**：
- `src/framework/`：ToolBackend.h / ToolWidget.h(.cpp) / ToolHost(.cpp/.h) / ToolRegistry(.cpp/.h) / ManifestParser(.cpp/.h) / DeviceInfo.h / AppState(.cpp/.h)
- `src/adapter/`：IProtocolAdapter.h / ProtocolCapability.h / FtpAdapter(.cpp/.h) / TelnetAdapter(.cpp/.h) / SshAdapter(.cpp/.h) / OpcUaAdapter(.cpp/.h) / ProtocolRegistry(.cpp/.h)
- `src/config/`：ConfigStore(.cpp/.h) / DpapiCrypto(.cpp/.h) / SettingsDialog(.cpp/.h) — SQLite 配置持久化 + DPAPI 加密 + 设置面板
- `src/logging/`：LogBridge(.cpp/.h)
- `src/ui/`：DeviceBusWidget(.cpp/.h) / FileBrowserPanel(.cpp/.h) / IFileSource.h / LocalFileSource(.cpp/.h) / RemoteFileSource(.cpp/.h) — 双栏文件面板 + 文件源统一接口（FTP 双栏面板化核心）
- `src/tools/FtpDeployTool/`：FtpDeployBackend(.cpp/.h) / FtpDeployWidget(.cpp/.h)
- `src/tools/TelnetTool/`：TelnetBackend(.cpp/.h) / TelnetWidget(.cpp/.h)
- `src/tools/WebSocketTool/`：WebSocketBackend(.cpp/.h) / WebSocketWidget(.cpp/.h)
- `src/tools/ModbusTool/`：ModbusBackend(.cpp/.h) / ModbusWidget(.cpp/.h)
- `src/tools/NetRelayTool/`：NetRelayBackend(.cpp/.h) / NetRelayWidget(.cpp/.h) / NetRelayTypes.h / RelayRecorder(.cpp/.h) / RelayRecording(.cpp/.h) / RelayPlayer(.cpp/.h) — TCP/UDP/组播 透明代理，双向流量捕获 + .nrec 录制/回放(含组播录制零影响加入/回灌)
- `src/tools/OpcUaClientTool/`：OpcUaClientBackend(.cpp/.h) / OpcUaClientWidget(.cpp/.h) — OPC UA 客户端 Tool（open62541）
- `src/adapter/`（补充）：OpcUaAdapter(.cpp/.h)（实现 IProtocolAdapter，封装 open62541 客户端）
- `src/updater/`：见下方「在线更新（OTA）子系统」
- `src/utils/`：FormatUtils.h（工具函数）
- `src/thirdparty/`：lwcomm / lwlog / lwmsgq / lwevent / lwcommunicate / lwserverbase / tinyxml2 / nanopb / open62541 / multimedia

### 在线更新（OTA）子系统（src/updater/）

采用「主程序内检查 + 独立进程替换」的双进程模型（Windows 无法替换正在运行的 exe，故拆分）：

- **UpdateChecker**（`UpdateChecker.cpp/.h`）：在 DeviceForge 主进程内运行。查询远端版本清单、比对当前版本、下载更新包。逻辑有单元测试 `tst_updatechecker`
- **UpdateDialog**（`UpdateDialog.cpp/.h`）：更新提示/进度 UI
- **UpdateTypes.h**：版本信息 / 更新清单数据结构
- **Updater.exe**（`UpdaterMain.cpp`）：**独立的纯 Win32 可执行文件，不链接 Qt**（`add_executable(Updater ...)` + `WIN32_EXECUTABLE`，约 100KB 静态链接 CRT）。由主程序退出前拉起，流程：等待 DeviceForge 进程退出 → 备份 installDir → 用 tempDir 覆盖 → 失败自动回滚 → 清理临时文件 → 重启新版。通过 `--manifest <json>`（含 tempDir/installDir/exeName/backupDir）+ `--log <path>` 参数驱动，manifest 用内置极简 JSON 解析器读取（不引第三方库）

> CMake `POST_BUILD` 会把 `Updater.exe` 复制到 DeviceForge 输出目录，便于部署包一起打包。

### 已删除的死代码

**Phase 0-1 清理**：
- `src/framework/EventBus.h/.cpp` — 由 lwmsgq 消息队列替代
- `src/framework/ConnectionPool.h/.cpp` — 由 lwcommunicate::LWConnPool 替代
- `TelnetManager.cpp` — 1 字节空文件
- `FileDeploy.h/.cpp` — 空壳类（仅 1 行构造函数）

**2026-08-02 清理（工程更名 + 目录整理）**：
- `ModbusCluster.cpp/.h` — 旧架构，无任何构建引用（CMake 不编译）
- `OpcUaClient.cpp/.h` — 旧演示 Tab（`OpcUaClientTab`），仅被废弃的前向声明引用，已移除
- `tab_modbus_cluster_test.ui` / `tab_opcua_client.ui` — 旧 Tab 布局（随上两个类删除）
- `ui_tab_opcua_client.h` — uic 生成物被误跟踪
- `src/model/FtpManager.cpp/.h` — 旧 FTP 管理器，无外部引用（由 FtpAdapter 替代）
- `src/presenter/FtpPresenter.cpp/.h` / `ModbusPresenter.cpp/.h` — 旧 Presenter，仅被注释引用
- `src/utils/DeployEvent.h` — 事件残留（EventBus 已移除）

### 并发模型

- `QtConcurrent::run` 用于异步执行网络 IO 和重计算
- libcurl FTP 操作自带进度回调
- `std::async` 用于 TelnetAdapter 异步请求
- 部分模块使用 `QTimer` 定时刷新（Modbus 自动刷新）
- 原子变量 (`std::atomic`) 控制 Telnet/Curl 运行/停止状态
- lwserverbase::ServiceManager 管理 Tool 服务生命周期

### UI 布局

- 左侧 56px 固定宽 NavBar（竖排图标导航栏，琴色活跃态 + 石墨色非活跃态）
- NavBar 右侧：顶部胶囊式 DeviceBusWidget + 中间 QStackedWidget（工具工作区）+ 底部可折叠日志区
- 日志区默认可折叠，新日志到达时折叠条琴色闪烁提示
- 状态栏 TC 化（2026-08-10）：左侧常驻快捷键提示条（`#shortcutHint`，双 QSS 基色）「F2 重命名 · F5 复制 → · F6 移动 → · Tab 切栏」+ 中央部署方向指示（`#directionLabel`，左面板路径 → 右面板路径，仅部署工具存在且部署页激活时显示，其余 Tool 页隐藏）
- 深色主题样式表：`darkstyle.qss`（通过 `main.cpp` 按 ConfigStore `appearance.theme` 配置加载，默认暗色；亮色为 `darkstyle-light.qss`），工业仪表盘色板，双主题可切换
- 所有 Tool 日志统一路由到底部全局日志（ToolWidget::setGlobalLogCallback）

## 设计文档

文档结构以 `docs/README.md` 为索引（`00-开发规范 / 01-白皮书 / 02-需求分析 / 03-设计 / 04-使用手册 / 05-部署文档 / 06-运营与文章`）。修改架构前应先查阅：

**对外交付文档**：
- `docs/04-使用手册/架构设计.md` — 架构设计
- `docs/04-使用手册/接口参考.md` — 接口参考
- `docs/04-使用手册/用户手册.md` — 用户手册
- `docs/05-部署文档/构建指南.md` — 构建指南
- `docs/05-部署文档/部署文档.md` — 部署文档
- `docs/05-部署文档/安全说明.md` — 安全说明
- `docs/05-部署文档/调试指南.md` — 调试指南
- `docs/06-运营与文章/运营流程.md` — 运营流程
- `docs/images/` — README 使用的界面截图（文件部署/批量命令/MODBUS/WebSocket/OPCUA）

**DeviceForge（原名 DeployMaster 2.0）设计文档**：
- `docs/03-设计/方案设计/2026-07-04-工具框架设计.md` — DeployMaster 2.0 通用设备运维平台设计（lwserverbase + Tool 框架 + IProtocolAdapter + 插件化架构）
- `docs/03-设计/实施计划/2026-07-04-工具框架计划.md` — Phase 0-1 框架搭建实施计划（15 Tasks，已全部完成）

**历史设计文档（MVP+EventBus 架构，已过时，归档于 03-设计/方案设计）**：
- `docs/03-设计/方案设计/2026-06-14-重构设计.md` — MVP+EventBus 架构重构设计
- `docs/03-设计/方案设计/2026-06-14-UI现代化设计.md` — UI 现代化方案
- `docs/03-设计/方案设计/2026-06-19-UI重设计.md` — QSplitter 动态可调整布局重设计
- `docs/03-设计/方案设计/2026-06-19-紧凑布局设计.md` — 1366x768 低分辨率紧凑布局方案

**历史实施计划（归档于 03-设计/实施计划）**：
- `docs/03-设计/实施计划/2026-06-14-重构计划.md` — 旧版重构计划
- `docs/03-设计/实施计划/2026-06-14-UI现代化计划.md` — QSS 创建/QRC 更新计划
- `docs/03-设计/实施计划/2026-06-19-UI重设计计划.md` — 布局重构计划

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

- **增量构建陈旧 obj 类布局错位坑（2026-08-23 实录，勿指望 CI 兜底）**：MSBuild 增量编译可能漏编「包含了被修改头文件」的 TU——814654a 给 `FtpDeployBackend` 扩容（新增 `m_reportMutex`/`m_lastReport`）后，`tst_deploy_loop.obj` 停留在旧类尺寸而 `FtpDeployBackend.obj` 已按新布局重编，混链产物里测试槽函数以旧 sizeof 在栈上构造 backend 局部对象、成员函数却按新偏移写入越界，踩毁 GS cookie 与相邻帧数据，表现为 `tst_deploy_loop` 四用例逐一运行均确定性 `0xc0000409`（`__report_gsfailure`），而不含该头文件的目标（如 `tst_deploy_runner`）不受累；CI 全新构建永不复现此类污染。**处置＝真·全量清理重建**：删除整个 `build/` 目录后重新配置编译。两个工程事实：① `build.bat` 清理步骤只删缓存文件（CMakeCache/CMakeFiles/.vs 等）不清陈旧 `.obj`，无法治愈本类污染；② 其 cmake 配置步骤可能打印成功却返回非零，脚本在编译前走错误分支退出（错误分支误报），此时直接执行 `cmake --build build --config Release` 即可。预防纪律：改类布局的头文件提交后，交付验证前先做全量重建

- **thirdparty 静态库依赖链**：
  ```
  lwcomm → lwevent → lwmsgq → lwlog → lwcommunicate → lwserverbase
  ```
  所有 thirdparty 库编译为 STATIC 库并链接进最终可执行文件（CMake: `DeviceForge.exe`）。修改 CMakeLists.txt 时需注意依赖顺序。

- **旧架构文件已清理（2026-08-02）**：`src/model/FtpManager`、`src/presenter/FtpPresenter/ModbusPresenter`、`src/utils/DeployEvent.h`、根目录 `ModbusCluster`/`OpcUaClient`/`tab_*.ui` 均已删除。所有协议走 `FtpAdapter`（IProtocolAdapter），事件走 lwmsgq


- **OPC UA 客户端（open62541 v1.5.5，单文件分发版）**：`OpcUaClientWidget` + `OpcUaClientBackend`（`src/tools/OpcUaClientTool/`）+ `OpcUaAdapter`（`src/adapter/`）。首期 None 安全策略 + 匿名认证，支持批量读/写节点、DataChange 订阅、地址空间浏览。open62541 单文件版功能开关由 `open62541.h` 顶部 `#define` 块控制（**不能**用 `-DUA_ENABLE_X=0` 覆盖，open62541 用 `#ifdef`/`defined()` 判断，传 `=0` 反而定义宏激活代码路径）；已在头文件关闭 `UA_ENABLE_ENCRYPTION_MBEDTLS`（移除未 vendored 的 mbedTLS 依赖）。open62541 以 `UA_MULTITHREADING=100` 编译（内部已加锁，`UA_THREADSAFE` 函数可跨线程并发调用）；`OpcUaAdapter` 的 `recursive_mutex` 主要保护适配器自身状态（`m_subscriptionId`/`m_monContexts`/`m_connected` 等）。旧 `OpcUaClientTab` 演示桩已随 2026-08-02 清理删除

- **密码持久化（v2.3.0）**：FTP 凭证密码可通过 DPAPI 加密后持久化到 SQLite（ConfigStore + DpapiCrypto）；若未启用加密或非 Windows 平台，密码不持久化，每次启动需手动输入

- **测试现状**：已有 16 个 QtTest/CTest 目标（tst_nrec / tst_relay_addr / tst_updatechecker / tst_dpapi_crypto / tst_config_store / tst_opcua_encode / tst_opcua_loopback / tst_sftp_plan / tst_deploy_loop / tst_remote_model / tst_theme / tst_file_source / tst_panel_async / tst_qss_pixels / tst_modbus_mapping / tst_telnet_timeout），覆盖 NetRelay 录制回放与组播/转发地址校验、OTA 更新检查、DPAPI 加解密、ConfigStore 持久化、OPC UA 编解码、SFTP 上传规划、部署循环、远程列表排序、主题映射、文件源抽象、面板异步加载（竞态/重连/源选择器/UAF 定向回归）、双主题像素验证、Modbus 寄存器映射与上限、Telnet 空超时失败断言。Phase 0-1 设计规划的单元测试（FtpAdapter/TelnetAdapter/ToolRegistry/DeviceBusWidget）尚未补充，计划在后续迁移时同步完善

- **深色主题色板**（darkstyle.qss，2026-07-09 重构为「琴色是动词」体系）：
  - 背景底层: #0B0E14 | 面板/容器: #141820 | 输入凹槽: #0E1219 | 次按钮凸面: #232A36
  - 结构边框: #252A33 | 控件边框: #333B48 | 边框 hover 提亮: #3A4250
  - 主要文字: #C8CCD4 | 次要文字: #7B8494
  - 强调色（琴色）: #F0A030 | 强调色 hover: #F0B840 | 强调色 pressed: #D48820
  - 进行中/成功（青绿）: #40C8A0 | 错误色: #E85848

- **双主题（v2.6）**：`appearance.theme`（ConfigStore，默认 dark）。亮色板 darkstyle-light.qss 色值对应：背景 #F5F6F8 / 面板 #FFFFFF / 凹槽 #ECEFF3 / 边框 #D0D5DD/#C4CAD4 / 主文字 #2A2F38 / 次文字 #6B7480 / 琴色 #D48820（亮底加深，语义不变）/ 青绿 #2FA88A / 错误 #D6453A。「琴色是动词」语义跨主题一致：琴色仅信号态
- **紧凑密度（v2.6）**：NavBar 56px、列表行高 ~22px、双主题同密度

- **主题设计原则「琴色是动词」（关键，勿回退）**：琴色 #F0A030 **仅**用于标记"此刻可动作/此刻活跃"的信号态，绝不用作静止结构边框或大面积填充：
  - 静止边框（容器/输入/下拉/分隔条/表头/tab pane）一律用中性石墨色（#252A33 / #333B48）
  - 琴色仅出现在：主操作按钮填充（`#btnPrimary`，全局少数）、`:focus` 输入框边框、`:selected`/`:checked`/活跃 tab、分组标题文字、滑块 handle
  - 控件"能否操作"靠形态区分：输入框=凹的（填充比面板暗 #0E1219）、次按钮=凸的（填充比面板亮 #232A36）、容器=平的（仅石墨细边）
  - 进度条用青绿 #40C8A0（进行中），与琴色语义错开
  - 分隔条 QSplitter::handle 无边框，纯石墨窄条，hover 提亮（**不要**加琴色边框，那会变成"粗黄条"）
  - **Qt QSS 限制**：不支持 CSS `linear-gradient`/`radial-gradient`/`box-shadow`，一律用纯色或 `qlineargradient(...)`

- **main.cpp 初始化顺序**（关键）：
  1. `QApplication` 创建
  2. `LogBridge::install()` — Qt → lwlog 桥接
  3. `ProtocolRegistry` 工厂注册（FtpAdapter + TelnetAdapter + SshAdapter + OpcUaAdapter）
  4. 加载主题样式表（`ThemeUtils::themeQssPath`，按 ConfigStore `appearance.theme` 选择 darkstyle.qss / darkstyle-light.qss）
  5. `DeviceForge window` — 构造函数：NavBar（7项） + DeviceBusWidget（胶囊式） + QStackedWidget + 全部 Tool Widget + 底部可折叠日志
  6. `ToolRegistry` 注册内置 Tool 元数据（ftp.deploy / telnet.command / websocket.comm）
  7. `ToolHost` 工厂注册（预留，当前 Tool 通过 DeviceForge 主窗口直接创建）
  8. `window.initToolTabs()` — 无操作（所有 Tool 已在构造函数中按导航栏顺序创建）
  9. `window.show()`
