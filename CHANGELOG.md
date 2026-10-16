# Changelog

## [未发布]

### Added（2026-10-16，v2.13 任务模板跨机迁移）

- **任务模板导出/导入（单模板文件通道）**："一台机器调好的批量部署模板，U 盘带到另一条产线直接导入"。任务中心页右栏新增「导出模板/导入模板」按钮（`TaskCenterWidget::exportTemplateToFile`/`importTemplateFromFile`，未选中导出禁用、导入恒可用；默认文件名=模板名清洗非法字符+`.json`，空回退 `task-template.json`）。文件格式为公开契约：UTF-8 JSON、格式魔数 `deviceforge.task-template` + `formatVersion=1` 版本闸（更高版本明确拒绝并提示升级，不猜测解码）；**导出丢弃 templateId**（本机主键不外溢；新模板导入由 store 生成新 ID，覆盖同名模板时继承既有 ID，见下）、**文件零秘密值**（模板只存凭据引用；解码继承 store 的敏感字段拒收守卫，手工构造恶意文件同样被拒）。导入同名冲突三选「覆盖/改名/取消」（**默认按钮=改名**防回车误覆盖）：覆盖=原位覆写既有同名模板并继承其 ID 与版本（历史执行记录为不可变快照不受影响），改名=「名称 (2)」递增找空位不设上限；悬空 deviceIds **原样保留**并在成功框提示「建好同端点档案后自动接上」（档案 ID 由端点身份确定性派生，支持先带模板后建档）；解码四道校验（JSON/魔数/版本闸/行解码）全过才一次落库、不落半成品，拒绝原因中文弹窗。编解码外提 `src/task/TaskTemplateCodec.{h,cpp}`（store 行编码与文件通道同源，字段口径唯一）。
- 测试：新增 `tst_task_template_codec`（纯逻辑：行/文件往返、templateId 不外溢、悬空引用与 unknownFields 保留、坏输入数据驱动四类拒绝、敏感字段拒收），`tst_task_center_widget` +7 用例（导出契约文件/未选中禁用/选择清空复位；导入新建+悬空提示、覆盖原位替换、改名 (2)、取消不落库、坏文件零副作用）；store 既有用例不改全绿（重构守卫）。全量 CTest 46 目标绿。

### Fixed（2026-10-16，#48 F4 编辑器静默换码）

- **GBK 不可表示字符不再静默回退 UTF-8**：`encodeText`/`currentBytes` 新增 `fellBackToUtf8` 出参（默认参数向前兼容），保存链检测到所选编码无法表示全文时弹窗二选一——「用 UTF-8 保存」（下拉与面板编码态同步切换，显示与实写恒一致）/「返回编辑」（不落盘、脏位保留）。此前行为：悄悄以 UTF-8 重写整个远端文件且下拉仍显示原选择。`tst_file_encoding` +2 用例（不可表示→标记且 UTF-8 全文无损 / 可表示→不标记且 GBK 字节回环），并经验证变异负对照可抓红。

### 工具/文档（2026-10-16，#51）

- **smoke.py 环境预检自动化**：启动前探测 Qt bin（QT_DIR → D:/Qt、C:/Qt 的 6.x msvc 目录）并前置注入 PATH，探测失败直接归因拒跑（不再以 0xC0000005 假崩面目出现）；主窗口查找改为**标题匹配 + 属于本次启动进程 PID** 双条件，外部同名实例不再劫持 WM_CLOSE/优雅退出超时判定——只报告外部实例（PID+路径）供人工分诊，绝不杀非自身进程。
- 文件管理规格 §4.3 脏守卫措辞对齐实现裁定（R9：仅关闭守脏，导航不打断编辑），用户手册 F4 条同步补写。
- CHANGELOG 结构归位 Keep-a-Changelog：唯一 `# Changelog` 顶题、[未发布] 置顶、版本小节降序（历史遗留的文中重复标题与错位 [未发布] 清除）。

### Added（2026-10-16，#46 设备档案端点同键校验）

- **档案保存同键检测与确认**：`DeviceRegistry::endpointConflicts()` 新增只读查询——入站档案端点与其他档案存在协议+IP+端口全同的端点时逐条报冲突（归一化口径与存储一致；按 deviceId 排除编辑自身；跨协议同址不误报）。设备总线「添加/双击编辑」两保存入口接线确认框：可保存但需确认（默认「否」），放弃时回到编辑对话框不丢内容。堵 #29 遗留的源端空白（同址重复档案=同一目标，部署清目录互踩、任务串行重复操作）。
- 测试：`tst_device_registry` +4 用例（冲突命中/排除自身/跨协议豁免/多冲突逐条），全量 CTest 45 目标绿。

### 修复（2026-10-16，#45 测试抖动）

- `tst_deploy_runner::singleFileFailure` flake（基线 7-13%）根因修复：fixture 拷 `params[0]` 致多台共享同一内存远端写同名路径，触发 TransferExecutor「远端目标改变」并发保护；改 `makeParams(N)` 逐台独立构造，尺寸断言附 dumpReport 诊断。
- `tst_panel_async::editor_secondSaveNoFalseConflict`（实测 7/10 抖）：二次保存后裸 `QVERIFY(!isDirty)` 抢在 QueuedConnection 回调 `markSaved()` 之前，改 `QTRY_VERIFY_WITH_TIMEOUT` 等待模式。

### 修复（2026-08-21，fix/v2.7-编译修复）

- **ModbusBackend**：修复 Qt 6.11 编译错误（由 v2.7 评审修复提交引入，任何 Qt 版本均无法编译）——`QObject::connect` 的 context 误传非 QObject 的 `this`（改为 3 参数 connect、以 client 为 context）；`sendWriteSingleCoil`/`sendWriteSingleRegister` 为不存在的 API（改为 `sendWriteRequest` + `QModbusDataUnit`，保持 0x05 线圈/0x06 寄存器语义）；日志回调 QString 直传 `std::string` 参数（补 `.toStdString()`）
- **WebSocketBackend**：修复 WSS 自签名证书 API 在 Qt 6.11 已不存在导致的编译错误（`QSslKey` 构造参数错序 + `QSslCertificate::generateSelfSignedCertificate` 已移除），改为加载 `src/app/certs/` 预生成测试证书（RSA 2048、SAN 含 localhost，随 QRC 打包）

## [2.12.0] — 2026-10-14

### Added
- **WebSocket 双栏控制台重构**：上部连接/操作区 + 下部事件流（2000 行环形、方向/对端/字节数/内容预览，UTF-8 截断安全）；事件模型 `WsEventTypes` 纯逻辑独立可测；Token 凭证迁移 DPAPI 存储，写路径落盘时清扫旧明文行。
- **MODBUS 工具重构（多 PLC 主看偶写）**：设备身份统一至 DeviceRegistry 档案（档案含 modbus 端点=能力开关），目标档案持久化 `modbus.target`（旧 `modbus.slave` 一次性自动迁移、幂等跳过）；250ms 单驱动轮询调度器（每连接 FIFO 队列、写插队、在途保护、指数退避重连）；双栏目标清单 + 逐寄存器视图（变化高亮、跳过计数、退避着色）；QModbusTcpServer 本机环回集成测试。
- **文件管理增强（FTP/SFTP 部署双栏）**：元数据列（所有者/修改时间/权限，缺失显示 `—`；创建时间 FTP/SFTP 协议不可得，不设该列）；Ctrl+M 目录对比（元数据口径：size 主判 + 修改时间分钟辅助，差异着色 + 行对齐 + 占位行 + 仅显示差异过滤 + 汇总条，刷新自动退出）；F4 预览编辑器（UTF-8/GBK/UTF-16 BOM 自动探测 + 手动切换重解码、>2MB/二进制/混合行尾只读、保存保持原行尾、异步回传含远端变化冲突确认"仍要覆盖/放弃并刷新"）；libcurl/libssh2 内存读写原语（无临时文件中转，SylixOS 句柄纪律与既有传输一致）。

### Fixed
- WebSocket 评审修复波：后端错误路径连接悬挂致 UAF（onClientError 断链清理 + 析构无条件收尾）、字节计数口径修正为原始字节、Legacy 明文 Token 行清扫。
- 文件管理评审修复波：编辑器双开串包（打开即推进代际 + 路径/内容值对绑定回传，杜绝"甲内容写进乙路径"误覆盖）、同对话框二次保存假冲突（保存成功续用快照）、读侧超限转只读横幅、协议不支持时错误原因不被空覆盖清空、对比态中刷新退出后恢复名称排序、同名"目录↔文件"判差异、内存传输补齐 `CURLOPT_FTP_USE_EPSV=0`。

### Tests
- 全量 CTest 45 目标：WebSocket +3（`tst_ws_event_types` / `tst_ws_page_layout` / `tst_ws_loopback`）、MODBUS +4（`tst_modbus_target_store` / `tst_modbus_scheduler` / `tst_modbus_page` / `tst_modbus_loopback`）、文件管理 +2（`tst_dir_compare` / `tst_file_encoding`）。

## [2.11.1] — 2026-10-08

### Fixed（现场反馈可用性补丁）
- 设备档案编辑器协议列改为下拉选择（ftp/ftps/sftp/ssh/telnet/modbus，未知/留空回退 ftp），修复手填空协议导致任务执行校验失败。
- 设备档案编辑器凭据引用列改为下拉选择已有 `ftp.credential` 记录，并支持"＋ 新建凭据…"就地创建（用户名/密码，密码经 DPAPI 加密入库，明文不落盘）；旧凭证输入区保存逻辑复用同一入口（`devicebus::storeFtpCredential`）。
- 综合日志面板折叠条移出日志区、常驻可见（修复旧版折叠后无法再展开）；初始比例钳制（工具区至少保留 100px），折叠态与展开高度经 ConfigStore `app.panel/logPanel` 记忆。
- 批量命令执行结果表列宽改为可拖动（Interactive 模式 + 合理初始宽度），修复 ResizeToContents 模式拖动弹回。

### Tests
- 新增 2 个 QtTest/CTest 目标：`tst_batch_result_header`（表头可拖回归）、`tst_log_panel_state`（日志面板状态纯逻辑）；全量 CTest 36 目标。

## [2.11.0] — 2026-10-08

### Added
- 新增统一设备档案层（`src/device/`）：`DeviceProfile`/`DeviceRegistry` 多协议端点档案，ConfigStore `device.profile` 持久化，兼容只读回退旧 `device.list` 记录，删除对迁移设备持久。
- 新增设备任务中心（`src/task/`）：`TaskTemplateStore` 版本化任务模板持久化（含凭据秘密值拒绝）、`TaskExecutionEngine` 按"文件部署→批量命令→重启→恢复检查"编排执行（设备间串行、重启断开按预期成功、`retryFailed` 阶段恢复跳过已交付文件与已成功设备）、`TaskRunStore` 执行记录持久化（设备名称/地址不可变快照、历史筛选、保留清理、错误文本脱敏）、`TaskCenterWidget` 三栏任务中心页（模板/目标设备/执行历史，风险确认默认拒绝）。保留清理于应用启动自动执行：先对账崩溃遗留的 running 记录为失败（中断），再删除超期终态记录（默认保留 90 天，ConfigStore `task/retention.days` 可覆盖）。
- 设备总线胶囊栏名称优先展示、悬浮端点详情、双击打开档案编辑对话框写回注册表（`DeviceBusWidget`）。
- 部署报告新增任务执行记录 CSV/HTML 渲染（`DeployReport` 附加 `renderTaskRunCsv`/`renderTaskRunHtml`，既有 API 不变）；逐设备进度面板支持档案名称显示（`MultiProgressWidget::setDeviceInfo`）。

### Tests
- 新增 7 个 QtTest/CTest 目标：`tst_device_registry`、`tst_device_registry_store`、`tst_task_template_store`、`tst_task_execution_engine`、`tst_task_run_store`、`tst_task_center_widget`、`tst_task_center_e2e`（端到端：档案圈选→部署→命令→重启→恢复→持久化记录→阶段恢复，Fake 适配器零网络）。

## [2.10.0] — 2026-09-29

### Added
- 统一批量命令下发与部署后重启闭环，支持 Telnet/SSH、超时、重试、取消和成功设备筛选。
- FTP 部署工具持久化重启参数配置。

## [2.9.1] — 2026-09-26

### 修复

- 修复 FTP `QUOTE` 操作默认写入 GUI 标准输出导致的 `CURLE_WRITE_ERROR`。
- 批量部署遇到瞬时 FTP 响应写入错误时自动重连并重试当前文件。

## [2.9.0] — 2026-09-16

### 可靠传输闭环

- 双栏上传/下载、本地复制与移动、批量部署统一接入 TransferScheduler → TransferExecutor → ITransferChannel。
- 增加瞬时错误重试、SHA-256 校验、临时文件提交、原子/非原子交付告警、项目级取消和失败恢复跳过。
- 批量部署保留 FTP/SFTP 目录映射、清目录事务与部署报告，并修正根目录及首次连接失败恢复边界。
- 同步 Commander 快捷键、同文件移动保护及 v2.9 可靠性文档说明。

## [2.8.0] — 2026-08-22

### 新增（2026-08-21，feature/netrelay-multicast-forward）

- **NetRelayTool 组播实时转发**：抓收组播的同时可勾选实时转发——**M→U**（转指定单播消费者）与 **M→M**（转另一组播组，启动前二次确认防混叠/环路）；转发与录制并行，归入 Relaying 状态（与回放互斥）
- **UI 易用性**：协议下拉联动（选 Multicast 隐藏无效的「监听地址/端口」、目标标签动态改「组播组地址」）；回放面板**依据 .nrec 文件头**判断组播（原误用协议下拉当前值）并提示目标语义（留空=回灌原组）；组播转发配置区（默认关）
- 地址校验收敛为纯逻辑函数（`NetRelayTypes.h`，拒绝 IPv4 简写如 `239.1.2`）
- 测试：新增 tst_relay_addr（组播/转发目标地址校验 6 用例）；回归 tst_nrec 组播录制通过

### 并行批量部署（feature/v28-parallel-deploy）

- **多设备并行批量部署**：新增 DeploymentRunner 并发调度器（私有 QThreadPool），多设备部署从「逐台串行」升级为「上限并发」——并发度可配 1-8（SettingsDialog 滑块，ConfigStore 键 `deploy.concurrency` 持久化）；单台事务抽取为 DeployJob（自 FtpDeployBackend 设备循环体串行等价平移）；全局取消传播贯穿调度检查点（未启动台次预检跳过、传输中台次经文件循环头/协议栈检查点中止，统一归 Cancelled 态，failedFiles 明细保留）
- **每设备实时进度**：多设备进度组件并行实装——deviceProgress 回调按设备键（ip:port）独立上报 0-100（线程契约固定：池线程并发触发、消费方 QueuedConnection 编组回 GUI 线程）；聚合总进度 100ms 时间戳节流 + 完成时强制末次发射（节流门闸初值取 now()-100ms 修复首拍溢出 UB）
- **部署报告导出**：DeployReport 结果聚合数据结构 + CSV/HTML 双格式渲染纯函数（纯逻辑零 Qt 依赖）——CSV 供 Excel 归档、HTML 为打印友好黑白表格；逐设备状态/失败文件/错误摘要/耗时明细
- **失败设备一键重试**：部署结束后对失败设备集合一键重跑，无需重新勾选设备与文件

### 顺带缺陷修复（已并入 main，随本版本发布）

- **FtpListParser 解析缺陷两处（#21）**：格式矩阵回归测试（Unix/Windows 双格式多变体用例）铺开后修复「单数字日期」（如 `Aug  4`）与「小写 pm」两种 LIST 行解析失败
- **更新检查退出阻塞（#23）**：UpdateChecker 挂接取消回调 + 收紧连接超时，消除应用退出窗口期偶发的进程阻塞
- **FTP 长传输误判断流（#20）**：FtpAdapter 传输超时由固定总时长改为闲置判定（有进度即续命），大文件慢链路不再被误杀（`02cdb23`/`7ce6fb4`）
- **Updater 备份失败守卫（#22）**：替换前备份失败即中止更新流程，不再带着半更新状态继续（`c4c8261`/`63d0d64`）

## [2.7.0] — 2026-08-19

### UX 收尾（feature/ux-finish）

- **远程列表异步加载**：FileBrowserPanel::loadDirectory 用 `QtConcurrent::run` 后台 `list()` + **代际令牌**防竞态（旧回调直接丢弃）+ `QMetaObject::invokeMethod`（QueuedConnection）主线程回调分派；慢速/断网目录读取不再冻结 UI；连接失效自动重连重试（reconnect + 重试一次），失败路径面包屑「加载失败」+ 路径栏回写；空目录语义与 v2.6 一致
- **RemoteFileSource 加锁串行化**：adapter 操作公共入口（list/reconnect/mkdir/rename/remove/clearDirectory/upload/download/connect 等）QMutex 串行化——修复异步化后多 worker 并发 list 同一 SFTP session（libssh2 非线程安全）崩溃、reconnect 替换 m_adapter 的 UAF 窗口与 m_lastError 数据竞争；reconnect 拆无锁私有体 `connectLocked()` 避免非递归死锁；worker 内 `QPointer` 守卫防面板析构后回调悬垂
- **连接异步化**：onRefreshRemote 三分支（configChanged / 原位重连 / 已连接纯刷新）QtConcurrent 化，约 10s 断网超时不再冻结 UI；状态点新增 **Connecting 态**（500ms 灰闪）；连接代际令牌 `m_connGeneration` 防陈旧回调挂载（含 busy 拒绝分支作废在途回调）；worker 捕获列表构造 guard 防退出期 UAF
- **面板源选择器**：FileBrowserPanel 顶部新增源类型下拉（**本地/FTP/SFTP**）+ 面板级设备下拉（远程源时显示）；左面板可独立切远程浏览源（独立连接缓存 + 代际令牌 + busy 串行化，端口取设备自身、FTPS 恒关）；右面板 ↔ 工具栏协议/设备**双向联动**（blockSignals 防循环）；左面板为远程浏览源时部署被守卫拦截（日志提示「左侧面板当前为远程浏览源，请切换为「本地」后再部署」）；「ssh」协议 id 归一化
- **系统文件拖入上传恢复**：资源管理器文件直接拖入**远程面板**逐文件上传（面包屑汇总「上传完成」/「上传完成，N 项失败」）+ 异步 refresh；与面板间拖拽共存（dropEvent 三分支：面板间复制 → 系统文件上传 → 本地面板提示「拖拽上传仅支持远程面板」）；DragEnter/DragMove 放开 `hasUrls()` 接受；空 url 防护（全部非 file:// 时提示「未检测到可上传的文件」）
- **顺带项（v2.6 终审裁定 + 测试基建）**：SshAdapter `sftpListDirectory` readdir 中途错误上报 lastError（修复自动重连盲区，错误路径同样关闭句柄）；`RemoteFileModel::sort` 降序分支**相等键短路**（修复严格弱序违规 UB，QString::compare 顺带单次化）；面包屑双主题色值测试锁定（`#panelBreadcrumb`，暗 #7B8494 / 亮 #6B7480）
- **测试**：新增 **tst_panel_async**（异步加载竞态/重连重试/源选择器行为/UAF 定向回归 30 循环门控）+ **tst_qss_pixels**（双主题像素验证：离屏渲染采样面板/导航栏/主按钮/进度条 4 控件精确色值 + QSS 块级字符串保底断言）；tst_remote_model 补降序相等键用例；回归 **13 目标全过**

### 全量健康评审修复（21 项，2026-08-19）

**ModbusTool（6 项）**
- **寄存器类型映射修复（Critical）**：界面「保持寄存器/输入寄存器/离散输入」此前分别发送 0x02/0x01/0x04（读错 PLC 存储区，静默数据错误），现按 `registerTypeFromComboIndex()` 正确映射（Holding→0x03 / Input→0x04 / Coils→0x01 / Discrete→0x02）
- **设备列表注入修复**：`ModbusBackend::bindDevices` 全仓无调用方，读取路径此前无目标设备（功能形同虚设）；Widget 读/写前从 ConfigStore `device.list`（与设备总线同源）同步设备
- **异常码展示**：读失败时 Modbus 异常码（0x01-0x0B 中文名）+ errorString 红色行展示（原为静默空读「(42ms)」）
- **写入对话框接通**：「写入」按钮接通：单点写寄存器 0x06 / 写线圈 0x05（非零→ON），设备下拉选择
- 连接失败经 errorOccurred 上报日志 + stateChanged 一次性连接（原处理器无限累积 + 重连风暴）
- 数量上限按类型：Holding/Input 125、Coils/Discrete 2000

**Telnet/SSH 命令（5 项）**
- **空超时假成功修复（Critical）**：命令无任何响应时不再报成功，`success=false`「响应超时」（原死设备显示绿色成功行）
- **提示驱动登录**：等待 `login:`/`Password:` 提示（各 3s 超时），检测「incorrect」立即断开报「用户名或密码错误」；无认证服务器（如 SylixOS telnetd）跳过认证向后兼容
- **取消可中断**：停止按钮 100ms 内中断在途命令（原 `future.get()` 阻塞至 300s 超时），报「已取消」
- QSettings → ConfigStore（`telnet.prefs/securityWarning`）；状态色改 QLabel state 属性 + 双主题 QSS 规则

**WebSocketTool（7 项）**
- **WSS 服务端修复**：加载随包预生成的自签名测试证书（RSA 2048，QRC 打包；原无证书导致 TLS 握手必然失败）
- **Token 认证接线**：Server 设置区新增 Token 输入框 + ConfigStore 持久化（`websocket.endpoint` map `token` 键，明文）+ `setAuthToken` 接通（原为死代码）
- SUBSCRIBE/PUBLISH/UNSUBSCRIBE 消息进入消息日志（原不可见）
- 协议解析改**首冒号切分**（topic 可含 `:`，Client 侧 TOPIC 同款 bug 一并修复）
- 新增 UNSUBSCRIBE wire 处理 + Widget「退订」按钮；stopClient 先断开再删除（对齐 stopServer 模式）；死代码 m_cancelled 移除

**NetRelayTool（2 项）**
- **TCP 写背压**：双端 64KB 读缓冲 + 写缓冲 1MB 高水位暂停转发 / 512KB 低水位滞回恢复（原慢消费者 Qt 缓冲无界积压；bytesWritten 恢复回调连接对创建时一次性挂接，关闭时连带清理）
- 会话树更新 O(n) → O(1)（`QHash<int, QTreeWidgetItem*>` 索引，clear 同步）

**SshAdapter（1 项）**
- 静态 known-hosts QSet 加静态 QMutex（原 Telnet SSH 批量 + FTP 面板 SFTP 并发数据竞争）

**测试**：新增 **tst_modbus_mapping**（4 映射 + 4 上限断言）+ **tst_telnet_timeout**（本地 QTcpServer 空响应，断言失败路径）；回归 **15 目标全过**


### 双栏面板模块化（file-browser，TC 化交互）

- **IFileSource 文件源统一接口**（`src/ui/IFileSource.h`）：list / mkdir / rename / remove(path, isDir) / clearDirectory / upload / download / connect / 进度回调 / 取消标志，与协议无关地操作文件系统；实现 LocalFileSource（本地 std::filesystem）与 RemoteFileSource（FTP/FTPS/SFTP）
- **FileBrowserPanel 通用双栏面板**（`src/ui/FileBrowserPanel.{h,cpp}`）：统一表格视图（FtpFileInfo 结构，本地远程同一渲染路径）+ 面板顶部路径栏（回车跳转）+ 底部面包屑；双击进入目录 / `..` 返回上级；TC 快捷键 **F2 重命名 / F5 复制到对面 / F6 移动到对面 / Tab 切栏**；右键菜单（进入/新建目录/重命名/删除/复制到对面/移动到对面/复制路径/刷新）；面板间拖拽（方向语义：本地→远程=上传、远程→本地=下载、远程↔远程提示暂不支持）
- **FtpDeployTool 双面板宿主重构**：FtpDeployWidget 瘦身约 3 倍（1134→396 行）为双栏宿主（QSplitter 组装两个 FileBrowserPanel），仅保留工具级批量部署配置（协议/设备/端口/FTPS/清空/重启）+ 部署入口；文件浏览/导航/操作全部下沉面板层；FtpDeployBackend 零改动，批量部署链路保留
- **部署目标目录语义**：部署目标 = 右侧远程面板当前路径（先右侧导航再部署；空路径回退根目录 + SFTP 根目录清空守卫）
- **状态栏 TC 化**：左侧常驻快捷键提示条（F2 重命名 · F5 复制 → · F6 移动 → · Tab 切栏）+ 中央部署方向指示（左面板路径 → 右面板路径，仅部署页激活时显示）
- **QSS 死代码清理**：移除 FTP 旧面板失效规则（#panelTitle / #crumbCurrent / #localPathEdit / #browseBtn，双主题同步）
- **远程面板修复（2026-08-12）**：设备添加/删除后自动连接刷新（deviceSelectionChanged → onRefreshRemote，修复 blockSignals 导致触发链断裂、远程源永不创建的问题）；「⟳ 刷新」按钮回归（onRefreshRemote 的 UI 入口）；**连接状态点**（工具栏圆形指示灯：灰=未连接/未配置、青绿=连接成功、红=连接失败，底色代码动态设置 + 双 QSS 圆角）；工具栏分组布局（连接组：协议/设备/端口/FTPS/状态点；部署组：清空/重启/刷新/「▶ 部署到 N 台设备」）；FileBrowserPanel 无源提示（「未连接远程，请选择设备并刷新」替代静默空列表）
- **测试**：新增 tst_file_source（IFileSource 本地源 4 用例）；回归 11 目标全过（含 tst_remote_model 等既有目标）

## [2.6.0] — 2026-08-07

### SFTP 批量部署（传输能力补完）

- **IDeployable 部署能力接口**（`src/adapter/IDeployable.h`）：uploadFile/uploadFolder/clearRemoteDirectory/setProgressCallback/setCancelFlag；FtpAdapter/SshAdapter 声明实现
- **SshAdapter 部署链路**：`sftpUploadFolder()`（递归 mkdir + 逐文件上传）、`sftpClearDirectory()`（清空保留目录）、`sftpSetCancelFlag()` 取消支持
- **部署循环协议化**：`FtpDeployBackend::startUpload` 按协议参数从 ProtocolRegistry 取通道，FTP/SFTP 同一部署逻辑
- **UI 解锁**：FTP 双栏协议下拉选 SFTP 后可直接部署/拖拽上传（不再提示切换 FTP）
- **测试**：tst_sftp_plan（上传规划纯逻辑 3 用例）+ tst_deploy_loop（部署循环 mock 3 用例）

### 远程面板体验修复

- 远程文件列表按名称排序（目录优先，`.`/`..` 置顶）
- 连接失效自动重连重试（空闲断开场景：LIST 失败 → 断开重连 → 重试一次 → 明确报错）
- SFTP 死连接阻塞超时 10s → 5s
- Qt 全局禁用系统代理（SFTP/WebSocket 局域网直连，修复系统代理劫持导致的 ProxyTypeError）

### 说明
- SCP 支持留待 v2.7（SCP 无目录列表，仅部署上传场景；技术路径：libcurl scp://）

### UX 改进

- 双主题：亮色主题（SettingsDialog 外观下拉即时切换，ConfigStore 持久化）
- 全局紧凑：NavBar 56px、列表行高 22px、间距收窄（TC 密度）
- 菜单接功能：文件→退出、部署→开始/取消部署（跨 Tab）、移除空壳 action
- 组件级样式双主题化（终审修复波）：FTP 面板标题/路径框/浏览按钮/面包屑当前段、NetRelay 方向箭头、状态栏版本标签、OPC UA 连接状态标签由内联暗色样式迁移至双 QSS（亮色下可读），devicePill online/offline 色值对齐 spec 色板（#2FA88A/#D6453A）；SettingsDialog 主题下拉非法值兜底

## [2.5.0] — 2026-07-26

### FTP 远程文件管理补完

- **FtpAdapter 新增**：`renameFile()`（RNFR/RNTO）、`makeDirectory()`（MKD）、`cancelTransfer()`、`setCancelFlag()`；`deleteDirectory()` 改为递归删除（LIST → 逐项 DELE → RMD）
- **远程文件精确对比**：RemoteFileModel 按 size 精确匹配着色（绿=一致，黄=有差异），替代原文件名匹配
- **远程列表排序**：点击列头排序，目录优先、`.`/`..` 置顶
- **重命名路径穿越校验**：拒绝包含 `/` 或 `..` 的名称

### SFTP 文件管理

- **SshAdapter SFTP 子系统**：sftpListDirectory / sftpUploadFile / sftpDownloadFile / sftpDeleteFile / sftpDeleteDirectory（递归+深度上限）/ sftpRename / sftpMakeDirectory
- **FTP 双栏协议切换**：工具栏 FTP/SFTP 下拉，端口自动切换（21/22）
- 注：SFTP 批量部署待支持（部署/拖拽上传会提示切换 FTP）

### SylixOS FTP 适配

- **EPSV 禁用**：SylixOS FTP 服务器不支持 EPSV，强制 PASV
- **SINGLECWD → MULTICWD**：SylixOS `cd()` 只支持单级路径，改为逐级 CWD
- **QUOTE 命令根 URL**：DELE/RMD/RNFR/RNTO/MKD 用根 URL + 绝对路径，避免 curl RETR 预检误报
- **路径清理**：过滤路径中的换行符，修复双击导航 CURLcode=3

### 其他

- **选择性部署**：勾选设备胶囊即可只部署到部分设备（未选中回退全部）
- **连接缓存**：同设备导航复用 FTP 连接，避免重复登录
- **部署取消**：进度回调中断机制，取消按钮可中止传输
- **设备胶囊右键删除**（带确认弹窗）
- **MultiProgressWidget 极简化**：只保留总进度条 + 取消按钮
- **本地面板重新设计**：树结构 + 紧凑路径栏
- **目录递归删除**：区分文件/目录，FTP 递归 DELE→RMD

## [2.4.0] — 2026-07-25

### FTP 部署双栏重构

- **QSplitter 双栏文件管理器**：左侧本地目录树（QFileSystemModel）+ 右侧远程 FTP 表格（自定义 RemoteFileModel）
- **拖拽上传**：系统文件拖入远程面板直接上传
- **面包屑导航**：远程目录任意层级跳转
- **FtpListParser**：FTP LIST 响应 Unix/Windows 双格式解析
- **多设备并行部署**：MultiProgressWidget 进度组件

### 主窗口布局现代化

- **左侧 72px 竖排导航栏**（NavBar）：图标 + 中文标签，琴色活跃态
- **QTabWidget → QStackedWidget**：工具工作区全宽
- **设备栏胶囊式重构**：石墨底 + 琴色选中态边框
- **底部日志可折叠**：新日志到达时折叠条闪烁提示
- **远端预览面板移除**：功能由 FTP 双栏远程面板替代

### 日志统一

- **6 个 Tool 独立日志区移除**：所有消息统一路由到底部全局日志（ToolWidget 回调机制）
## [2.4.0] — 2026-07-26

### FTP 双栏重构 + 主窗口布局现代化 + 日志统一

### 新增

#### FTP 双栏文件管理器（`src/tools/FtpDeployTool/`）
- **FtpListParser**：FTP LIST 响应 Unix/Windows 双格式解析（total 头/符号链接/正则复用）
- **FtpAdapter::listDirectoryParsed**：结构化远程目录列表（FtpFileInfo）
- **RemoteFileModel**：远程 FTP 目录 `QAbstractTableModel`
- **MultiProgressWidget**：多设备并行部署进度条
- **FtpDeployWidget 双栏重构**：QSplitter 本地（QFileSystemModel）+ 远程（RemoteFileModel）双栏文件管理器
- **拖拽上传**：系统文件拖入远程面板直接上传；异步删除/下载连接失败错误处理、重命名/新建目录用户提示
- **移除远端预览面板**（旧 tab_deploy 页面，功能由远程面板替代）

#### 主窗口布局现代化
- 左侧导航栏（NavBar）+ `QStackedWidget` 替代 `QTabWidget`
- DeviceBusWidget 胶囊式紧凑重设计（online 视觉指示器）
- 底部日志可折叠 — 折叠条 + 新日志到达琴色闪烁提示

### 日志统一
- 所有 Tool 日志统一路由到底部全局日志，移除各 Tool `m_logView`，删除日志查询 Tab

### 修复
- OTA 更新全链路问题（closes #15）：findAsset 无法匹配 zip 资产等
- OPC UA 订阅卡死：请求超时 + 后台线程 + 移除冗余轮询
- CI Debug 模式下 tst_config_store SEGFAULT

---

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
