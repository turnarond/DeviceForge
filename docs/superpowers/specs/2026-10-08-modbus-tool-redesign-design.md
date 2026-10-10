# MODBUS 测试工具重构设计（v2.12）

> 状态：待终审 ｜ 日期：2026-10-08 ｜ 路径：架构级 ｜ 分支：docs/modbus-redesign-spec
> 上游讨论：本轮 brainstorming 四节（数据模型 / 调度与线程 / 界面与呈现 / 测试与验收）已逐节获批，本文为其书面固化。

## 0. 背景与目标

现场连接多台 PLC（多数带 Modbus TCP），日常以**周期性读监看**为主、**偶发单点写**。当前 MODBUS 测试工具存在三类问题：

1. **卡**：结果表每轮 `setRowCount(0)` 整表销毁重建、结果回调 `findItems` 线性查找、单格塞 125~2000 个值拼成超长字符串
2. **端口无处编辑**：端口仅隐含于 `device.list`/`device.port`（默认 502），UI 无修改入口
3. **架构错位**：设备源用旧 `device.list` 未接 v2.11 档案体系；读参数是"全局一套广播所有设备"；挂起队列每客户端仅 1 槽、新动作覆盖旧动作（刷新+写并发丢动作）；核心读写路径零测试

**成功标准**：卡顿根治、端口编辑回归、多 PLC 多从站成为一等公民、设备身份统一接入档案；日常"左扫状态、右看数值、双击才写"。

**非目标（YAGNI）**：命名点位/tag 体系（v2.13 独立立项，本轮仅在数据模型留 `tagSchema` 版本占位，不填实现）；RTU/串口；批量写/配方下发；历史趋势。

## 1. 规模假设（决定架构，绑定）

- **规模 B**：5~20 台设备（含多从站），单台几十~几百点，刷新 500ms~2s
- IO 留在 GUI 线程（`QModbusTcpClient` 异步事件驱动，规模 B 无压力）；不引入工作线程与跨线程编组
- 后端保持 ServiceTask 生命周期壳，与全仓 Tool 惯例一致

## 2. 数据模型

### 2.1 设备身份与 Modbus 能力

- 设备身份 = `DeviceProfile`（v2.11，**结构不动**）。"该设备是否有 Modbus" = 档案 `endpoints[]` 中是否存在 `protocol=="modbus"` 端点（默认端口 502）。档案编辑器协议下拉已含 modbus（v2.11.1 落地），天然衔接
- 端口是连接属性 → 归属档案的 modbus 端点（`ip:port`）；改名/改址/删除复用 v2.11.1 的 `saveReplacing` 替换语义 + 旧 `device.list` 行清扫

### 2.2 ModbusTargetConfig（新持久化记录 `modbus.target`，key = deviceId）

```
{
  deviceId,
  targets: [                       // 同一设备可挂多从站
    { slaveId: 1..247,
      enabled: bool,
      poll: { regType: Holding|Input|Coil|Discrete,
              startAddr: 0..65535,
              count: 1..(125|2000), // 寄存器 125 / 位 2000，沿用 maxReadCountForType
              intervalMs: 500..60000 } }
  ],
  tagSchema: <版本占位 int，本轮恒为 0，不读不写内容>   // v2.13 命名点位入口
}
```

- **一 device 一记录**（targets 数组承载多从站），取代旧按从站拆 key 的 `modbus.slave`
- **旧 `modbus.slave` 迁移**：`slave:<id>` 记录一次性并入对应设备 targets；迁移失败不阻塞启动（`qWarning` + 回退读旧记录），迁移后新结构为准、不再写旧 key
- 无凭据面：Modbus TCP 无认证，不引入 credentialRef，`bindCredentials` 维持空实现
- 展示层行身份 = `deviceId#slaveId`（纯 UI 概念，不入库）

### 2.3 ModbusDataStore（内存）

- `targetId(deviceId#slaveId) → { 最新值 QVector<quint16>, 时间戳ms, 状态, 错误文本, skippedCount }`
- 只存最新一轮（规模 B 无需历史环）；视图从 store 拉，store 不触碰 UI

## 3. 调度与线程模型

`ModbusPollScheduler`（新组件，纯逻辑、可脱机测）：

- **单一驱动 QTimer（250ms tick）+ 每目标 nextDue 时间戳**，不搞 N 个定时器
- 每目标状态机：`Idle → Connecting → Polling → … / Error → Cooldown → Connecting`
- **在途保护**：目标上一请求未回（finished/超时）则本轮跳过，慢 PLC 不堆叠
- **每连接 FIFO 动作队列**（修旧"每客户端 1 槽互相覆盖"缺陷）：轮询读/手动读/写动作全部进同一队列；**写插队首**（用户动作优先周期任务）；队列上限 8，溢出丢最旧**轮询**项并 `skippedCount++`；写项永不丢弃（满则直接报错提示）
- **多从站共享连接**：同 `ip:port` 的多个 slaveId 串行走同一队列；不同主机并行
- **参数每目标独立**（来自 `modbus.target`），废"全局段广播"；工具栏仅全局动作：启动/暂停轮询、立即读取一次（选中目标）
- 生命周期：stop/切页/关窗 → 先停 tick、清队列，再逐 client `disconnectDevice`（沿用现有"先断信号后关"的死锁规避）；配置改动即时更新调度目标集 + 持久化（写失败不静默：状态列红 + 日志）

## 4. 界面与呈现

双栏 `QSplitter(Horizontal)`，沿用 WebSocket 已确立的版式族：

- **左·连接清单（320~460px）**：行 = 设备×从站，列 = 状态灯｜名称(优先)｜端点 ip:port(可编辑)｜从站｜段｜周期｜状态/耗时。顶部 `[＋添加目标][暂停轮询][立即读取]` + 搜索框(名称/IP 过滤)
- **右·寄存器视图（唯一垂直扩展主体）**：当前目标逐寄存器，一寄存器一行，列 = 地址｜十进制｜十六进制｜变化(↑/↓)｜更新时刻。工具栏全局十/十六进制切换、目标标题摘要。双击数值行 → 单点写（线圈给 0/1）
- 添加目标下拉**只列有 modbus 端点的设备** + "其它设备…"入口（选择即建端点，默认 502）；无需用户跳档案页
- 状态灯：OK 石墨 / 异常红 / 退避黄（"琴色是动词"，静止边框石墨）；错误文本进状态列（截断 + tooltip）
- 端口编辑：清单端点列改端口 → 经 `DeviceRegistry::saveReplacing` 写回档案 modbus 端点 + 旧行清扫，同设备多从站行同步刷新
- 呈现层 = `ModbusTargetListModel` / `ModbusPointModel`（`QAbstractTableModel`，纯逻辑可单测）；仅对可视区 `dataChanged`，不可见行只更新 store
- 无 registry 时回退 ip:port（与设备栏一致）；写确认框**默认取消**（保守方向，同任务中心风险确认精神）
- 列宽全 `Interactive` + 合理初始宽（v2.11.1 ③教训入族规）

代码形态：`ModbusWidget` 降为组装层；逻辑入调度器/模型/store 三处纯逻辑单元。

## 5. 测试与验收

新增 4 个 CTest 目标（全量 39→43），Qt API 全部 ≤6.9（`QModbusTcpServer` 自 5.14 稳定，本地 6.11.1 / CI 6.9.2 双验）：

1. **tst_modbus_target_store**（持久化+迁移，临时库）：编解码 round-trip、多从站数组、非法记录回退；旧 `modbus.slave` 一次性迁移（含失败回退）；端口写回联动（端点更新 + 同设备行同步）
2. **tst_modbus_scheduler**（纯逻辑，假时钟+假传输注入）：到期序、在途保护、写插队首、队列上限丢弃+计数、多从站串行互不吞（旧缺陷回归）、退避序列 1→2→5→10→30 封顶且成功复位、disabled 零流量
3. **tst_modbus_loopback**（`QModbusTcpServer` 于 127.0.0.1 自动端口，真集成）：轮询读真值、单点写 0x06 生效+耗时、线圈 0x05、服务器掉线→Error/Cooldown→恢复回 Polling。（多从站语义由②覆盖：Qt 服务器模拟多单元支持有限，不硬凑）
4. **tst_modbus_page**（Widget 级，复用 tst_ws_page_layout 的 armed-timer/findChild 模式）：清单来自 store+registry 名称优先、搜索过滤、右视图行数=count、**4xxxx 编址数据驱动**（H 基 40001 / I 基 30001 / C 基 0 / D 基 10001）、十/十六进制切换、变化高亮、列宽 Interactive、无 registry 回退、写确认默认取消
5. **性能验收**：tst_modbus_page 内一条宽容限用例（2000 行 reset + 单行 dataChanged，QElapsed < 500ms，只防结构级烂）；真机手工清单（20 目标×200 点@1s 滚动/切换不卡，刷新期间可编辑）——列入附录，人工签核

## 6. 回归面与纪律

- 主窗口 `DeviceForge::setupModbusClusterTab` 接线改动：注入 `setRegistry`
- 任务中心对含 modbus 端点档案行为不变（部署/命令路由忽略 modbus 端点）——加断言到④或现有 e2e
- tst_theme / tst_qss_pixels 保持绿（状态色全复用既有 token，不新增色值）
- 全量 43 目标两轮 + 冒烟退出码 0 + versioncheck 一致（版本号留 v2.12 统一提升）
- 文档同提交：用户手册 MODBUS 节重写 + AGENTS 目标数/映射表 + 架构图 websocket/modbus 双栏说明；CHANGELOG 待发版条目在本文 §0 已备好素材

## 7. 交付顺序（交 writing-plans 展开）

建议 4 任务：T1 数据模型+持久化+迁移（含①）→ T2 调度器+store（含②）→ T3 双栏 UI+两模型+写路径（含④）→ T4 环回集成+主窗口接线+回归+文档（含③）。每任务 TDD 红灯先行、独立提交、`--no-ff` 合回。

## 8. 实施勘误（v2.12 任务 1-4 落地后回填，任务 4）

实现与本文/计划接口块的偏离以代码为准，逐条登记如下（符号锚点，非行号）。

### 8.1 接口签名偏离（计划 R1-R4 任务接口块 ↔ 实际头文件）

- **R1（任务 1）**：`ModbusTargetStore::load` 语义收窄——计划"缺记录回退读旧 `modbus.slave`"之外，实际命中**新记录（含空 `targets` 数组）即不回退**（裁决 6 定稿：空记录 = 删除全部目标的落库形态）；`migrateLegacySlaves` 对**已有新记录的设备跳过**（任务 1 复审修复，防重迁覆盖用户数据）
- **R2（任务 2）**：`PollTarget` 增 `bool enabled` 字段（调度器据此过滤，`SlaveTarget.enabled` 由调用方带入）；`onResult(connKey, outcome)` 增第三参 `qint64 nowMs`（退避门限计算必需当前时钟，假时钟可测口径的极小扩展）；新增 `connInBackoff(connKey, nowMs)` 公开查询（裁决 5 退避黄接线点）；连接队列实现用 `QVector`（计划示意 `QDeque`），FIFO/插队首语义不变
- **R3（任务 3）**：`ModbusPointModel::setTarget` 实际为 `setTarget(targetId, spec, prev, cur)`——增 `PollSpec` 参（模型须自持行重建上下文）；`ModbusTargetListModel` 公开面增 `allIndexOfTarget`（裁决 1 全集归位）与 `setBackoffProbe`（裁决 5 注入点）；端口编辑实现为端点列 delegate 就地 `QLineEdit`（objectName `mbPortEdit`），非计划步骤示意的 `QInputDialog`；搜索过滤在模型内 `m_allRows→m_rows` 二段式实现（计划允许 QSortFilterProxyModel 或 refresh 内过滤二选一）
- **R4（任务 4）**：后端结果回调签名改为 `ResultCallback(const QString& targetId, values, elapsedMs, const QString& errorMsg)`（裁决 1：`deviceId#slaveId` 精确交付，废弃旧 `std::string device` ip 语义）；`setSchedulerCallbacks()` 收为**私有**（简报接口块列在公开面，实际纯内部接线）；新增 `inTargetBackoff(targetId)` / `isPolling()`（清单退避黄探针 / Widget 指纹重启判定）；`toolVersion` 2.1.0→2.2.0；驱动 timer 采用简报明示可接受的**固定 250ms**（`nextWakeMs` 动态唤醒为可选优化，未实施，注释已说明）

### 8.2 测试口径勘误（§5.3 环回）

- `QModbusTcpServer` 无公开 `listen()`/实际端口 getter：简报"127.0.0.1:0 自动端口"以 **QTcpServer 先探测空闲端口再让渡**给 `connectDevice()` 等价实现
- 简报"`server.close()`"：`QModbusTcpServer::close()` 为 protected，公开等价 = `disconnectDevice()`（同一实现）
- 多从站串行如简报只在调度器纯逻辑层覆盖（Qt TCP 服务器模拟多单元支持有限），环回仅单从站 + 线圈 0x05 补例

### 8.3 控制者裁决 1-7 落点

1. 结果携带 `targetId` 精确交付：`ModbusBackend::ResultCallback` + `ModbusWidget::handleBackendResult`（`allIndexOfTarget` 全集定位，废弃 ip 前缀匹配）
2. 调度器重复入队缺陷：`onTick` **入队即推进 `nextDueMs`**（派发侧不再记账）；回归测试 `tst_modbus_scheduler::noDuplicateEnqueueWhileInFlight`、`timeoutAlternatesWithCommErrorBackoff`；`PollTarget` 注释固化 targetId 唯一性契约（后端 `startPolling` 有重复剔除兜底）
3. `setSampleMeta` 可视区纪律：`ModbusPointModel::flushMetaInRange` + `metaDirty` 滚入补发（同 `applyUpdate` 虚拟化口径）
4. `statusColor` 缓存：`ModbusTargetListModel::cacheThemeColors()` 在 `refresh` 取一次，`data()` 不再查库
5. 退避黄：`ModbusScheduler::connInBackoff` → `ModbusBackend::inTargetBackoff` → `ModbusTargetListModel::setBackoffProbe`，色值复用既有琴色 token `#F0A030`（不新增色值）
6. 删除语义：删空目标 = `save` 空 `targets` 记录；`load` 命中空记录不回退旧行；清单侧空记录不合成默认从站行（测试 `emptyTargetsRecordBeatsLegacyFallback` / `emptyTargetRecordShowsNoRows`）
7. 主窗口注入：`DeviceForge::setupModbusClusterTab` 在 `setGlobalLogCallback` 后 `setRegistry(m_deviceRegistry.get())`；`ModbusWidget::onToolStart` 调 `migrateLegacySlaves` 一次（store 内 exists 判重幂等）；根 `CMakeLists.txt` SOURCES 补 `ModbusScheduler.cpp`

### 8.4 回归面补充发现（§6 之外）

- 任务中心 e2e 新增 `modbusEndpointProfileRoutingUnaffected`：modbus 端点置首时部署/命令路由不受影响（ftp/telnet 链路全通），但 `TaskDeviceOutcome.address` 呈现为**首端点** ip:port（v2.11 既有展示口径，与路由无关）——该字段语义未变，回归断言以 uploads/commands 完成为准

---

### 附录 A：验收签核清单（人工）
- [ ] 端口编辑：清单改端口 → 重启后档案生效、旧行不复活
- [ ] 多从站：同主机 slave 1/3/4 并行轮询互不吞
- [ ] 性能：20 目标×200 点@1s 交互不卡
- [ ] 变化高亮与 4xxxx 编址显示正确
- [ ] 写确认默认取消；线圈写为 0/1
- [ ] 旧 `modbus.slave` 配置迁移无感、失败不崩
