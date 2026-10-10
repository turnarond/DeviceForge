# MODBUS 工具重构 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 subagent-driven-development（推荐）或 executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 重构 MODBUS 测试工具为"档案身份 + 每从站独立轮询 + 双栏（连接清单 / 逐寄存器视图）"，根治表格卡顿、恢复端口编辑、支持多 PLC 多从站。

**架构：** 后端持有 `QModbusTcpClient` 作为传输；纯逻辑 `ModbusScheduler`（假时钟可测，单驱动 tick + 每目标 nextDue + 每连接 FIFO 队列 + 退避）与 `ModbusDataStore`（内存最新值）解耦；呈现层 `ModbusTargetListModel` / `ModbusPointModel`（`QAbstractTableModel`）+ `ModbusWidget` 组装。IO 留 GUI 线程。

**技术栈：** Qt 6.11.1（CI 6.9.2）/ C++17 / QtSerialBus（QModbusTcpClient + 测试用 QModbusTcpServer）/ QtTest / CMake。

**规格：** `docs/superpowers/specs/2026-10-08-modbus-tool-redesign-design.md`

## 全局约束

- IO 留 GUI 线程，不引入工作线程；后端保持 ServiceTask 生命周期壳（规格 §1）
- 规模假设绑死：5~20 设备（含多从站）、单台几十~几百点、刷新 500ms~2000ms；intervalMs 范围 500..60000，count 上限沿用 `maxReadCountForType`（寄存器 125 / 位 2000）（规格 §1）
- 设备身份 = `DeviceProfile`（结构不动）；"是否有 Modbus" = 档案 `endpoints[]` 含 `protocol=="modbus"` 端点（默认端口 502）；端口写回复用 `DeviceRegistry::saveReplacing` + 旧 `device.list` 行清扫（规格 §2.1）
- 持久化用 ConfigStore（记录类型 `modbus.target`，key=deviceId），**禁用 QSettings**；无凭据面，`bindCredentials` 维持空实现（规格 §2.2）
- 状态灯色复用既有 token（OK 石墨 / 异常红 / 退避黄 / 琴色高亮），**不新增 QSS 色值**，保持 tst_theme/tst_qss_pixels 绿（规格 §6）
- Qt API 全部 ≤6.9.2；`QModbusTcpServer` 自 5.14 稳定可用（本地 6.11.1 已核实头文件存在）
- 日志/文案全中文；TDD 红-绿-重构强制；每任务独立提交、`--no-ff` 合回
- 4xxxx 编址显示基（规格 §5.4 逐字锁定）：Holding=40001、Input=30001、Discrete=10001、Coil=0（即地址 0 显示 "00000"、coil addr=5 显示 "00005"）；显示为 5 位零填充十进制

## 审查重点（Review Focus）

- **慢 PLC 在途堆积**：某目标请求超时/未回时，正常期待"不堆叠、不雪崩"——该目标后续 tick 应跳过直到 finished/timeout；由 `tst_modbus_scheduler::inFlightGuardNoPileup` 锁定。
- **刷新与写并发丢动作**（旧缺陷根因）：正常期待"写永不丢、轮询可丢弃但有计数"——由 `tst_modbus_scheduler::writeNeverDroppedAndPollEvictsWithCount` 锁定。
- **同主机多从站串行**：正常期待"共用一条连接、轮流、互不覆盖"——由 `tst_modbus_scheduler::multiSlaveShareConnSerialized` 锁定。
- **旧 `modbus.slave` 无 deviceId 的迁移语义**（规格缝隙，本计划已定）：见 §文件结构"迁移决定"；正常期待"迁移失败不崩、可回退"——由 `tst_modbus_target_store::migrationFailureFallback` 锁定。
- **空设备 / 无 modbus 端点**：正常期待"添加目标只列有 modbus 端点设备，无则给建端点入口而非崩溃"——由 `tst_modbus_page::addTargetListsOnlyModbusDevices` 锁定。
- **无 registry 注入**（工具被单独实例化/测试）：正常期待"回退显示 ip:port，不空指针"——由 `tst_modbus_page::nullRegistryFallsBackToAddress` 锁定。

---

## 文件结构

**新建：**
- `src/tools/ModbusTool/ModbusTargetTypes.h` —— 数据模型 struct + `addressLabel` + `makeTargetId` + 编解码自由函数（纯逻辑，无 IO）
- `src/tools/ModbusTool/ModbusTargetStore.h/.cpp` —— ConfigStore 读写 + 旧记录迁移（IO）
- `src/tools/ModbusTool/ModbusScheduler.h/.cpp` —— 纯逻辑调度器（假时钟注入可测）
- `src/tools/ModbusTool/ModbusDataStore.h/.cpp` —— 内存最新值表
- `src/tools/ModbusTool/ModbusTargetListModel.h/.cpp` —— 左清单模型
- `src/tools/ModbusTool/ModbusPointModel.h/.cpp` —— 右寄存器视图模型
- 测试：`tests/modbus/tst_modbus_target_store.cpp`、`tst_modbus_scheduler.cpp`、`tst_modbus_page.cpp`、`tst_modbus_loopback.cpp`

**修改：**
- `src/tools/ModbusTool/ModbusBackend.h/.cpp` —— 移除"全局广播 + 每客户端 1 挂起槽"，改为驱动 `ModbusScheduler` 的传输执行器
- `src/tools/ModbusTool/ModbusWidget.h/.cpp` —— 重写为双栏组装层，暴露 `setRegistry`
- `src/app/DeviceForge.cpp:274-292` —— `setupModbusClusterTab` 增 `widget->setRegistry(m_deviceRegistry.get())`
- `src/app/darkstyle.qss`、`darkstyle-light.qss` —— 仅复用既有 token 追加清单/视图选择器（不新增色值）
- `tests/CMakeLists.txt` —— 注册 4 新目标（链 `Qt6::SerialBus`，loopback 另需服务器类）
- `docs/04-使用手册/用户手册.md`（MODBUS 节重写）、`AGENTS.md`（目标数 39→43）、`tests/AGENTS.md`（映射表增 modbus 行）

**迁移决定（规格缝隙的锁定）：** 旧 `modbus.slave` 记录 key=`slave:<sid>`、value 含单个全局 PollSpec，且**无 deviceId**。迁移算法：读取全部 `modbus.slave` 行（通常 1~3 条），对注册表中**每个含 modbus 端点的 profile**，为每个 `slave:<sid>` 生成一个 `SlaveTarget{sid, enabled=true, poll=<该行配置>}` 写入其 `modbus.target`。旧记录**不删除**（迁移后仍以新结构为准读取；新结构缺失时回退读旧行）。若注册表无 modbus 设备或写库失败 → `qWarning` 且不抛，返回已迁移数（可为 0）。

---

## 任务 1：数据模型 + 持久化 + 迁移

**文件：**
- 创建：`src/tools/ModbusTool/ModbusTargetTypes.h`
- 创建：`src/tools/ModbusTool/ModbusTargetStore.h`、`src/tools/ModbusTool/ModbusTargetStore.cpp`
- 测试：`tests/modbus/tst_modbus_target_store.cpp`
- 修改：`tests/CMakeLists.txt`

**Interfaces（后续任务逐字复用）：**
```cpp
// ModbusTargetTypes.h —— namespace modbus
struct PollSpec { QModbusDataUnit::RegisterType regType = QModbusDataUnit::HoldingRegisters; int startAddr=0; int count=1; int intervalMs=1000; };
struct SlaveTarget { int slaveId=1; bool enabled=true; PollSpec poll; };
struct DeviceTargets { std::string deviceId; QVector<SlaveTarget> targets; int tagSchema=0; };
QString addressLabel(QModbusDataUnit::RegisterType type, int addr);          // "40001" 等，5 位零填充
QString makeTargetId(const std::string& deviceId, int slaveId);             // "<deviceId>#<slaveId>"
QVariantMap encodeDeviceTargets(const DeviceTargets&);
std::optional<DeviceTargets> decodeDeviceTargets(const QVariantMap&);
// ModbusTargetStore.h —— class，record type "modbus.target"
bool load(const std::string& deviceId, DeviceTargets* out) const;            // 缺记录回退读旧 modbus.slave（返回是否命中）
bool save(const DeviceTargets&);
bool remove(const std::string& deviceId);
std::vector<std::string> allDeviceIds() const;
int migrateLegacySlaves(class DeviceRegistry* registry);                     // 返回迁移的 profile 数
```

- [ ] **步骤 1：编写失败测试** `tst_modbus_target_store.cpp`，含槽：`addressLabelFormatting`（数据驱动：Holding+0→"40001"、Input+0→"30001"、Discrete+0→"10001"、Coil+5→"00005"）、`codecRoundTrip`（含多从站、enabled、tagSchema=0）、`decodeMalformedFallsBack`（缺字段/非法 regType 越界→ nullopt 或安全默认）、`saveLoadRemove`（临时库 round-trip）、`migrateLegacySlavesBackfills`（种两条 `modbus.slave`：`slave:1`、`slave:3` + 一个含 modbus 端点 profile → migrate → 该 deviceId 的 targets 含 slaveId 1 与 3，poll 来自旧行）、`migrationFailureFallback`（registry 传 nullptr 或无 modbus 设备 → 返回 0、不崩、旧行仍可读）
- [ ] **步骤 2：运行验证失败** `cmake --build build --config Release --target tst_modbus_target_store && ctest -R tst_modbus_target_store`；预期：编译失败（符号未定义）
- [ ] **步骤 3：实现 `ModbusTargetTypes.h`**（`addressLabel` 用 `snprintf %05d` 加基址；encode 用 `utf8`/`QVariant`；regType 存 int）+ `ModbusTargetStore`（仿 `DeviceProfileCodec`/`TaskTemplateStore` 用 ConfigStore `save/list/remove`；`load` 缺记录时回退：调用 `migrateLegacySlaves` 不在此触发，仅读旧 `modbus.slave` 合成临时 targets）。迁移算法照"文件结构·迁移决定"逐字实现
- [ ] **步骤 4：运行验证通过** `ctest -R tst_modbus_target_store --output-on-failure`；预期：全 PASS
- [ ] **步骤 5：Commit** `git add src/tools/ModbusTool/ModbusTargetTypes.h src/tools/ModbusTool/ModbusTargetStore.* tests/modbus/tst_modbus_target_store.cpp tests/CMakeLists.txt && git commit -m "feat(modbus): 目标数据模型、持久化与旧配置迁移"`

---

## 任务 2：调度器 + 内存数据表

**文件：**
- 创建：`src/tools/ModbusTool/ModbusScheduler.h/.cpp`
- 创建：`src/tools/ModbusTool/ModbusDataStore.h/.cpp`
- 测试：`tests/modbus/tst_modbus_scheduler.cpp`
- 修改：`tests/CMakeLists.txt`

**Interfaces：**
```cpp
// ModbusDataStore.h —— namespace modbus; class，纯内存，非线程安全（GUI 线程独占）
struct Sample { QVector<quint16> values; qint64 timestampMs=0; qint64 elapsedMs=0; QString error; bool ok=false; };
void updatePoll(const QString& targetId, const QVector<quint16>&, qint64 elapsedMs);
void updateError(const QString& targetId, const QString& err, qint64 elapsedMs);
std::optional<Sample> sample(const QString& targetId) const;
void clear(const QString& targetId);
// ModbusScheduler.h —— namespace modbus; class，无 Qt IO，时钟/派发经参数与返回，可脱机测
struct PollTarget { QString targetId; QString connKey; int slaveId; PollSpec spec; };
enum class Kind { Poll, Write };
struct Dispatch { QString targetId; QString connKey; int slaveId; Kind kind; PollSpec poll; int writeAddr=0; quint16 writeValue=0; };
enum class Outcome { Ok, CommError, Timeout };
void reset(const QVector<PollTarget>& targets, qint64 nowMs);
QVector<Dispatch> onTick(qint64 nowMs);                       // 返回本 tick 应派发动作（含队首写）
void enqueueWrite(const QString& targetId, int addr, quint16 value); // 插该连接队首，永不丢弃
void onDispatched(const QString& connKey);                     // 标记该连接在途
void onResult(const QString& connKey, Outcome outcome);        // 清在途 + 退避推进/复位
int skippedCount(const QString& targetId) const;               // 轮询被溢出丢弃计数
qint64 nextWakeMs(qint64 nowMs) const;                          // 供驱动 timer 计算下次唤醒
```
常量：`kTickMs=250`、`kQueueCap=8`、退避序列 `{1000,2000,5000,10000,30000}`（末值封顶）。

- [ ] **步骤 1：编写失败测试** `tst_modbus_scheduler.cpp` 槽：`dueOrderingSingleTick`（两目标 interval 500/1000，now=0→500 只 500 到期）、`inFlightGuardNoPileup`（派发后未 onResult，下个 tick 同连接不再派发）、`writePreparedHead`（先 reset 有轮询，enqueueWrite 后下一 onTick 该连接首个 Dispatch.kind==Write）、`writeNeverDroppedAndPollEvictsWithCount`（连灌 10 个轮询填满队列 cap=8 → 最旧轮询丢弃且 skippedCount>0；期间 enqueueWrite 的写仍在队列、不被丢）、`multiSlaveShareConnSerialized`（同 connKey 两 slave，串行：一个在途时另一不派发）、`backoffSequenceAndReset`（连续 CommError → nextWake 间隔依序 1s/2s/5s/10s/30s/30s；一次 Ok 复位回 interval）、`disabledAndEmptyZeroFlow`（reset 空/无 enabled → onTick 返回空）。`ModbusDataStore` 用例：`storeUpdateAndReadback`、`storeErrorKeepsOldSampleTimestamp`
- [ ] **步骤 2：运行验证失败** `ctest -R tst_modbus_scheduler`；预期编译失败
- [ ] **步骤 3：实现**：调度器内部 `QMap<connKey,Conn{inFlight, QDeque<Dispatch> q, backoffIdx, connNextReadyMs}>` + `QVector<PollTarget>` 各 `nextDueMs`。`onTick`：①对到期（now≥nextDueMs 且 now≥该 conn 退避门限）且 enabled 的目标，向 conn 队尾 push Poll；队列 size>kQueueCap 时弹出队首**轮询**项 skippedCount++（**不弹写**）；②对每个 !inFlight、q 非空、now≥connNextReadyMs 的 conn，pop front→加入返回、onDispatched 语义置 inFlight=true、记录该 target nextDueMs=now+interval。`enqueueWrite` 找 targetId→conn，push front（写不计数丢弃）。`onResult` CommError/Timeout→backoffIdx++（封顶序列末）、connNextReadyMs=now+backoff[idx]，Ok→backoffIdx=0。`ModbusDataStore` 用 `QHash<QString,Sample>`；`updateError` 保留旧 values、只更新 error/时间
- [ ] **步骤 4：运行验证通过** `ctest -R tst_modbus_scheduler --output-on-failure`；预期全 PASS
- [ ] **步骤 5：Commit** `... git commit -m "feat(modbus): 纯逻辑轮询调度器与内存数据表"`

---

## 任务 3：视图模型 + 双栏 Widget 重写

**文件：**
- 创建：`src/tools/ModbusTool/ModbusTargetListModel.h/.cpp`
- 创建：`src/tools/ModbusTool/ModbusPointModel.h/.cpp`
- 修改：`src/tools/ModbusTool/ModbusWidget.h`、`src/tools/ModbusTool/ModbusWidget.cpp`（重写）
- 测试：`tests/modbus/tst_modbus_page.cpp`
- 修改：`src/app/darkstyle.qss`、`src/app/darkstyle-light.qss`、`tests/CMakeLists.txt`

**Interfaces：**
```cpp
// ModbusWidget.h 关键公开面
void setBackend(ModbusBackend*);
void setRegistry(DeviceRegistry*);          // 新增；空则回退 ip:port 显示
// 观察点（测试用）：findChild objectName：
//   "mbTargetTable"(QTableView) "mbPointTable"(QTableView) "mbAddTarget" "mbPauseToggle"(QPushButton,checkable)
//   "mbReadOnce" "mbHexToggle"(checkable) "mbSearch"(QLineEdit) "mbPortEdit"(清单端点列 delegate 入口，见步骤)
// ModbusTargetListModel : QAbstractTableModel, 列 = Status,Name,Endpoint,Slave,Range,Interval,Detail (7)
// ModbusPointModel : QAbstractTableModel, 列 = Address,Decimal,Hex,Changed,UpdatedAt (5); setTarget(targetId, prev, cur); hex 开关
```
- [ ] **步骤 1：编写失败测试** `tst_modbus_page.cpp`（复用 tst_ws_page_layout 的 `QTemporaryDir`+ConfigStore::open 与 findChild 模式）槽：`nullRegistryFallsBackToAddress`（不 setRegistry，构 Widget，setRegistry(nullptr) 不崩，模型 indexName 显示 ip:port）、`addTargetListsOnlyModbusDevices`（registry 放两台，仅一台有 modbus 端点；触发添加目标下拉，断言只列该台 + 含"其它设备…"项）、`pointModelAddressAndCount`（给定 PollSpec Holding start=0 count=3，模型 3 行、Address 列 "40001/40002/40003"）、`hexToggleChangesDisplay`（值 1234，hex 关 "1234"、开 "0x04D2"）、`changedFlagOnlyOnDelta`（prev=[1,2] cur=[1,3] → 第 2 行 Changed="↑"/非空，第 1 行空）、`pointColumnsAreResizable`（两表水平头 0..n-2 列 mode==Interactive）、`writeConfirmDefaultsCancel`（双击/触发写→ QMessageBox 默认按钮为 Cancel，用 findChild 注入或跳过模态、仅断言确认逻辑函数 `shouldConfirmWrite` 返回需确认）。`ModbusTargetListModel::filterBySearch` 纯函数单测
- [ ] **步骤 2：运行验证失败** `ctest -R tst_modbus_page`；预期编译失败
- [ ] **步骤 3：实现** 两模型（`data()` 内仅按需格式化，`ModbusPointModel` 暴露 `setTarget` 做整表 reset、`applyUpdate(prev,cur)` 增量：仅对值变化行 `emit dataChanged`，Changed 由 store 上次快照比对；`ModbusTargetListModel::refresh` 读 registry+store+dataStore 重建行，搜索框过滤用 `QSortFilterProxyModel` 或在 refresh 里按前缀过滤）。Widget：`QSplitter(Horizontal)` 左 `mbTargetTable`+工具条、右 `mbPointTable`+标题+`mbHexToggle`；选清单行→设 point model target；端口编辑：清单端点列双击→`QInputDialog::getInt` 端口→经 `DeviceRegistry` 取 profile、改 modbus 端点 port、`saveReplacing` 写回、`refresh` 同设备其余行；添加目标弹窗列 modbus-端点设备 + "其它设备…"（选后建默认 502 端点写回档案）；写：双击视图数值行→确认框默认 Cancel→`backend->writeRegister`。QSS 仅用既有颜色 token 追加 `QTableView#mbTargetTable` 等选择器与状态色 `QLabel`/前景通过 delegate 用 `themeStatusColor` 同款取色（不新增十六进制色值）
- [ ] **步骤 4：运行验证通过** `ctest -R "tst_modbus_page|tst_theme|tst_qss_pixels" --output-on-failure`；预期全 PASS（主题测试须仍绿=未新增色值）
- [ ] **步骤 5：Commit** `... git commit -m "feat(modbus): 双栏连接清单与逐寄存器视图"`

---

## 任务 4：后端传输接线 + 环回集成 + 主窗口注入 + 回归文档

**文件：**
- 修改：`src/tools/ModbusTool/ModbusBackend.h/.cpp`（改为 scheduler 驱动的传输执行器）
- 修改：`src/app/DeviceForge.cpp:274-292`（`setupModbusClusterTab` 注入 registry）
- 测试：`tests/modbus/tst_modbus_loopback.cpp`
- 修改：`tests/CMakeLists.txt`
- 修改：`docs/04-使用手册/用户手册.md`、`AGENTS.md`、`tests/AGENTS.md`

**Interfaces：**
```cpp
// ModbusBackend 新公开面（供 Widget 驱动）
void setSchedulerCallbacks();                        // 内部：dispatch→send，reply→onResult+dataStore
void startPolling(const QVector<modbus::PollTarget>&, ModbusTargetStore*, ModbusDataStore*);
void stopPolling();
void requestReadOnce(const QString& targetId);
void enqueueWriteTarget(const QString& targetId, int addr, quint16 value); // 转 scheduler.enqueueWrite
ModbusDataStore* dataStore();                        // Widget 读取用于两模型
```
- [ ] **步骤 1：编写失败测试** `tst_modbus_loopback.cpp`：`holdingReadAndWriteLoopback`（起 `QModbusTcpServer` 于 127.0.0.1:0，`setMap`/`setData` 预置 Holding[0]=1234；backend startPolling 单目标 interval 500ms；`QTRY_VERIFY` dataStore 该 target values[0]==1234 且 ok；`enqueueWriteTarget(id,1,4321)`；`QTRY` 服务端 Holding[1]==4321）、`serverDropThenRecover`（读通后 server.close() → 目标转 error/backoff；server 重听 → `QTRY` 回 ok）
- [ ] **步骤 2：运行验证失败** `ctest -R tst_modbus_loopback`；预期编译失败（后端接口未实现）
- [ ] **步骤 3：实现后端**：删旧 `enqueueAction`/单挂起槽；持 `ModbusScheduler` 实例 + 每 conn 一个 `QModbusTcpClient`（沿用 `getOrCreateClient`，保留"先断信号后关"死锁规避）；一个 250ms 驱动 `QTimer`→`scheduler.onTick(now)`→对每个 Dispatch：Poll=`sendReadRequest`、Write=0x06/0x05；reply finished→`dataStore.update*`+`scheduler.onResult(conn,Ok/CommError/Timeout)`+`scheduler.onDispatched` 已在派发时置位；`nextWakeMs` 动态调整驱动 timer interval（可选优化，最简固定 250ms 亦可，注释说明）。改 `DeviceForge::setupModbusClusterTab`：在 `setGlobalLogCallback` 后加 `widget->setRegistry(m_deviceRegistry.get())`（`m_deviceRegistry` 已在构造函数早于 index3 建好，见 :87）
- [ ] **步骤 4：运行验证通过** `ctest -R tst_modbus_loopback --output-on-failure` 绿；再全量 `ctest -C Release`（43 目标）两轮，仅允许既有 `tst_deploy_runner` flake 命中基线；`python tools/devtools/smoke.py build/Release/DeviceForge.exe` 退出码 0（Qt bin 入 PATH）
- [ ] **步骤 5：文档与回归** 重写用户手册 MODBUS 节（双栏、端口编辑、多从站、变化高亮、4xxxx、十/十六进制、写确认默认取消）；`AGENTS.md` 目标数 39→43、`tests/AGENTS.md` 映射表增 `modbus/` 四目标行；`tst_task_center_e2e` 或 `tst_modbus_page` 补一条断言"含 modbus 端点 profile 不影响任务中心部署/命令路由"（若 §回归面 §6 要求）
- [ ] **步骤 6：Commit** `git add -A src/tools/ModbusTool src/app/DeviceForge.cpp tests/modbus/tst_modbus_loopback.cpp tests/CMakeLists.txt docs AGENTS.md tests/AGENTS.md && git commit -m "feat(modbus): 后端调度接线、环回集成与文档同步"`
- [ ] **步骤 7：合回** `git checkout main && git merge --no-ff <branch> -m "merge: integrate v2.12 modbus tool redesign"`（版本号留 v2.12 统一提升，本计划不碰 versioncheck 九源）

---

## 自检结论（编写者已核）

- 规格覆盖度：§2 数据模型→任务1；§3 调度→任务2；§4 界面→任务3；§5 测试→任务1/2/3/4 各自测试步；§6 回归+文档→任务4 步骤5。无遗漏。
- 类型一致性：`PollSpec`/`SlaveTarget`/`DeviceTargets`（任务1）被任务2 `PollTarget.spec`、任务3 模型、任务4 后端逐字引用；`ModbusScheduler`/`ModbusDataStore` 签名在任务2定义、任务3/4使用一致；`makeTargetId`/`addressLabel` 单一定义点。
- 审查重点 6 条均已挂测试。
- 比例：约 4 任务 / 规格 115 行为骨架 + 关键算法行，未抄函数体。
