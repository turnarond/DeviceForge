# tests — QtTest/CTest 单元测试

53 个测试目标（v2.12 MODBUS 重构 +4 = 43；文件管理增强 +2 = 45；v2.13 模板编解码 +1 = 46；v2.14 部署模板映射 +1 = 47；v2.14 步骤参数校验 +1 = 48；v2.14 双屏窗口放置 +1 = 49；v2.15 波2 文件夹支持 +2 = 51，计数定版 2026-10-20；波2 计划初稿写 50 系未计任务9 新增 `tst_deploy_entry_builder`；v2.15 波3 部署页工具栏两行化 +1 = 52 `tst_deploy_toolbar_layout`；v2.16-B 设备栏分组 +1 = 53 `tst_device_bus_groups`，计数定版 2026-10-21：本波计划预估"净新增 1"与实盘 `ctest -C Release -N` 的 `Total Tests: 53`、`tests/CMakeLists.txt` 的 `add_test` 计数 53 三者吻合，无波2 那次初稿漏计的缺口），**子目录名 = 被测模块**，目标名 `tst_<模块>`。

## 目录 ↔ 被测模块映射

| 子目录 | 目标 | 被测物 | 类型 |
|---|---|---|---|
| NetRelayTool/ | tst_nrec | RelayRecorder/Recording/Player | 录制回放（编入产品源码） |
| NetRelayTool/ | tst_relay_addr | NetRelayTypes.h | 纯逻辑（仅头文件） |
| config/ | tst_config_store / tst_dpapi_crypto | ConfigStore / DpapiCrypto | SQLite 持久化 / DPAPI |
| deploy/ | tst_deploy_loop | IDeployable 部署循环（v2.15 波2 含真盘目录树环回集成用例：入口装配→Job 显式条目模式→执行器父目录确保→LocalTransferChannel 实盘 mkdir/上传，目标树镜像逐文件断言） | 协议化逻辑 + 环回集成 |
| deploy/ | tst_deploy_template_map | DeployTemplateMap.h OnceConfig→TaskTemplate 纯映射（简写参数/未勾选=未绑定/重启步与空命令回退） | 纯逻辑（仅头文件，v2.14 #49-2） |
| deploy/ | tst_folder_expand | FolderExpand.h 目录展开纯头（镜像首段/顶层扁平/5000 上限整体拒绝/根退化拒绝/空目录不算错；scanner 注入脱盘） | 纯逻辑（仅头文件，v2.15 波2 任务7） |
| deploy/ | tst_deploy_entry_builder | DeployEntryBuilder.h 入口装配纯头（混合选择扁平+镜像条目、超上限逐字拒绝、全空目录拒绝、无目录 v2.14 扁平口径、快照过期按点击时刻实况重分类） | 纯逻辑（仅头文件，v2.15 波2 任务9） |
| deploy/ | tst_deploy_toolbar_layout | FtpDeployWidget 部署页工具栏两行化（deployToolbar=QVBoxLayout+两 QHBoxLayout；行1 连接组三件套+主操作／行2 重启四参+结果操作，含分隔条与反向归属；既有控件零新增对象名，按可见文本/类型/尺寸特征定位）+ **目的钉桩** toolbarFitsNarrowScreen（totalMinimumSize ≤1270 = 1366−导航栏 56−右侧 VBox margin 16−页面 margin 24）；**必须**先 `QStyleFactory::create("Fusion")` 再把 `src/app/DeviceForge.qrc` 编入目标并加载生产 darkstyle.qss，另有 qssFloorIsInEffect 守卫——无 QSS 时量得 834px 会伪装成过关（实测 Fusion+QSS：两行 933px、单行 1866px） | QtTest UI 回归（v2.15 波3 任务13 方案 C） |
| device/ | tst_device_bus_groups | `src/device/DeviceBusGroups.h` 纯头（`devicebus::{normalizeTag/normalizedTags/hasNormalizedTag/ungroupedTag/matchesFilter/segments}`：规格 §2.1 五条规则逐条——N 标签入 N 组/无标签与规范化后全空落「未分组」/组序区分大小写且未分组永远最后/过滤后 `命中数` 与 `total` 分列、0 命中组与空「未分组」不进结果、全不命中出空表；§2.4 同组去重；`Segment.profileIndexes` 载体的空 `deviceId` 判据用例 `emptyDeviceIdProfilesStillGroup`；`normalizeTag` 边界=只去首尾空白、纯分隔符判空、大小写与内部空格不动；`hasNormalizedTag` 空 selector 恒不匹配）+ `src/ui/DeviceOpenRouter.h` 纯头（本目标同域共夹具，一并直测 `deviceopen::{Page,Route,routeTo}`：五类协议各返回正确 page＋命中端点 ip/port 同源、逐个端点扫描跳过空 ip/不认识协议取首个可解析者、旧行空协议→ftp 兜底、有档案但无可用端点→`Page::None`＋中文提示；`opcua` 仅纯头级，UI 侧不可达分支） | 纯逻辑（仅头文件 ×2，v2.16-B 任务1/任务2；同目录既有 `tst_device_registry`/`tst_device_registry_store` 本表素来未列，此处一并登记子目录归属） |
| file_source/ | tst_file_source | IFileSource Local 实现 + readFile/writeFile 原语 | 文件系统 |
| dir_compare/ | tst_dir_compare | DirCompare.h/.cpp 目录对比纯逻辑（分类/对齐/分钟精度） | 纯逻辑 |
| file_encoding/ | tst_file_encoding | FileEncoding 编码探测/转换/冲突谓词（Win32 CP，禁 QTextCodec） | 纯逻辑 |
| modbus/ | tst_modbus_mapping | Modbus 地址映射 | 纯逻辑 |
| modbus/ | tst_modbus_target_store | ModbusTargetStore 持久化/迁移（临时库） | SQLite 持久化 |
| modbus/ | tst_modbus_scheduler | ModbusScheduler/ModbusDataStore（假时钟） | 纯逻辑调度 |
| modbus/ | tst_modbus_page | ModbusWidget 双栏 + 两视图模型 | QtTest UI 回归 |
| modbus/ | tst_modbus_loopback | ModbusBackend↔QModbusTcpServer 本机环回 | 环回集成（GUILESS） |
| opcua_encode/ | tst_opcua_encode(.c) / tst_opcua_loopback | open62541 编解码 | C 语言直测 |
| panel_async/ | tst_panel_async | FileBrowserPanel 异步加载 | 异步竞态回归（代际令牌） |
| remote_model/ sftp/ telnet/ theme/ | 各自 tst_* | 远程模型/SFTP 计划/Telnet 超时/QSS 像素 | 见各子目录 |
| WebSocketTool/ | tst_ws_event_types / tst_ws_page_layout / tst_ws_loopback | WsEventTypes.h 纯逻辑 / 双栏页面布局 / 本机 Server↔Client 环回 | 纯逻辑 + QtTest UI 回归 + 环回集成 |
| task/ | tst_task_template_codec | TaskTemplateCodec 模板行/文件编解码（魔数与 formatVersion 版本闸/敏感字段拒收/templateId 不外溢/悬空引用保留） | 纯逻辑（v2.13 模板跨机迁移） |
| task/ | tst_task_step_validation | TaskStepValidation.h 步骤参数级校验纯函数（引擎/编辑器文案单源/parseDeployItems 口径/commands trim 判空） | 纯逻辑（仅头文件，v2.14 #49-3） |
| app/ telnet(表头)/ | tst_log_panel_state / tst_window_placement / tst_batch_result_header | 日志面板状态纯逻辑 / 主窗口初始屏选择与居中纯逻辑 / 结果表头属性 | 纯逻辑（仅头文件） |
| （根） | tst_updatechecker | UpdateChecker | OTA 检查 |

## 注册模板（tests/CMakeLists.txt）

```cmake
add_executable(tst_xxx
    <子目录>/tst_xxx.cpp
    # ${CMAKE_SOURCE_DIR}/src/... 需要编入的产品源码（纯头文件逻辑则不编）
)
target_include_directories(tst_xxx PRIVATE <被测目录> ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_xxx PRIVATE Qt6::Core Qt6::<所需模块> Qt6::Test)
add_test(NAME tst_xxx COMMAND tst_xxx)
set_tests_properties(tst_xxx PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}")   # 缺它 Windows 报 0xc0000135
```

**测试属性登记（在册特例）**：`tst_telnet_timeout` 另挂 `TIMEOUT 60`（v2.15 D）——vendored lwcommunicate 在全套并发下偶发无限挂起（v2.14.0 终验实踩），TIMEOUT 把该形态从"拖死整轮"降为**有界红**（单目标 60s 超时失败）；该 `set_tests_properties` 刻意置于 `if(_qt_bin_dir)` 守卫之外，保证 TIMEOUT 无条件生效。分诊口径：命中 TIMEOUT 红按既有 flake 处理——单目标单跑复核 + 全量复跑，两遍口径都过即不判波失败（典型耗时 ~1.3s，基线曾观测单次负载下 15.6s，距 60s 上限有大余量），但复跑记录与命中实况必须写进终验报告，不藏红。

**零输出红的取证口径（v2.15 波3 合并结果实踩立规）**：全量轮次里若某目标 `***Failed` 且 `--output-on-failure` 底下**一个字节都没打**（本环境实测出现过：`tst_transfer_executor` 0.30s 退出码 8、无任何 FAIL 行；后续 60 次直跑 + 8 遍全量 + 8 轮闭包循环全绿，WER 无 Application Error 事件、无 dump），**当轮立刻**补三件事，否则证据就随日志覆盖丢失：① `ctest -C Release -V -R <该目标>` 复跑取**退出码**（小整数=断言失败条数；`0xc0000135`=Qt DLL/PATH 属性形态；`0xc0000409`=fail-fast；无输出且码为杀进程类=外部干预）；② 直跑必须 `-o file,txt` 并 `[ -s file ]` 判非空（裸跑 stdout 恒 0 字节不可当证据）；③ 全量终验一律附 `--output-junit <file>.xml` 留档。分诊结论未落到"产品缺陷"之前，不得写进 flake 基线，也不得以"复现不到"结案——按未闭合观察披露（同波2 `tst_panel_async` 先例）。

**「×」命中区几何守卫变红的分诊口径（v2.16-B 任务7 立规，任务8 自任务 7 报告落档于此，2026-10-21）**：`tst_deploy_toolbar_layout::chipCloseZoneCoversDrawnMarkUnderProductionQss` 是**漂移报警器**，不是"红了就有产品缺陷"的断言。它用生产同一式子（胶囊自身 `font()` 下 `QFontMetricsF` 量尾标 `"  ×"` 的字宽 + `QStyle::PM_ButtonMargin`）在测试侧独立复算区宽，再离屏画出胶囊、扫描实际墨区，断言"「×」中心列落在区内"。**当前余量只有 1px**——Fusion + 生产 `darkstyle.qss`、96dpi/100% 缩放下 101px 宽胶囊实测：区 [85,101]（区宽 16px）、画出的墨区 x∈[84,89]、中心列 86 ⇒ 判据 `86 ≥ 85` 仅剩 1px。字形最左 1px 尖端出区是**已知且方向安全**的形态（该列的 release 走"主体单击＝不改选择"臂，不存在误删）。**命中该用例变红时按序处置，不许直接调阈值**：① 先查本轮是否动了字体/DPI/`#devicePill` 的 `padding` 或 `font-size`——任一项都会平移区左缘，属预期漂移；② 读该用例自带的 `qInfo` 行（markAdvance / PM_ButtonMargin / zone / 墨区 / 中心 / 区左缘 / pillFont 全套实测值都打），把新数字回写规格 §2.3/§7 与视觉稿「实测基线」表（两处口径互指）；③ 只有"中心列落到区外"且排除①的样式变动时才按产品缺陷立案，先报告再动码。非虚设性已实测：区宽 −2px 杀哨兵、−11px 杀配套不变式断言；**−1px 杀不动**（16−1=15 ≥ 89−84+1=6 且哨兵 86≥86 仍成立）——别把"−1 就红"当成这条守卫的能力宣称。**形态口径**：生产 QSS 下 `QStyleSheetStyle` 把 `PM_ButtonMargin` 解析为 **0**（padding 接管样式层边距），区宽 **16px**；任务 5 记的 **22px** 是**默认样式（无 QSS）**下的读数（PM=6），两者不是同一形态，不得拿 22 当现行值。

## 约定（TDD 红-绿-重构）

1. **红灯先行**：新功能/修 bug 先写失败用例，再实现；修复类必须先复现缺陷
2. **可测性设计**：纯逻辑收敛为自由函数/独立头（参照 `NetRelayTypes.h`、Modbus 映射），Backend 依赖注入适配器，Widget 逻辑尽量下沉
3. 测试类继承 QObject + private slots；数据驱动用 `QTest::addColumn/addRow`
4. 异步行为用 QSignalSpy/QEventLoop + 超时兜底，禁止 sleep 式等待（参照 tst_panel_async）
5. 禁止为绿灯删测试/skip 关键断言
6. **几何类断言必须与屏宽/缩放无关**：顶层窗口 `resize(W,H)` 只是**请求**，会被窗口管理器钳到可用屏宽（实踩：`QT_SCALE_FACTOR=2` 下 1400 请求变 962 逻辑宽，GitHub windows runner 的虚拟屏同理），此时 `setChildrenCollapsible(false)` 的双栏按最小宽等比压缩，**比值断言会假红而策略没变**。写法：无条件判"序"（谁是扩展主体），把"倍数"这种依赖兑现宽度的判据放进 `if (widget.width() >= 阈值)`，并 `qInfo` 打出走了哪条分支与实际尺寸——否则"这条断言在 CI 到底跑没跑"不可考。非虚设性用变异核：把产品 `setStretchFactor` 主次对调，判序那格必须红（本仓实测 738/640 红）。

## 命令

```bash
cd build && ctest -C Release --output-on-failure            # 全量
cd build && ctest -C Release -R tst_nrec --output-on-failure # 单目标
```

## 已知覆盖空白

FtpAdapter/TelnetAdapter 协议交互、ToolRegistry/ToolHost、NavBar 尚无专属目标（DeviceBusWidget 已由 tst_task_center_widget 覆盖；它的两个纯头协作者 `src/device/DeviceBusGroups.h` 与 `src/ui/DeviceOpenRouter.h` 自 v2.16-B 起由 tst_device_bus_groups 直测）——新增相关行为时优先在此补测。模板编辑器对话框（v2.13 #54）的 tePageDeploy 文件选择器 leg：teAddDir 的**目录展开逻辑自 v2.15 波2 起已直测**（addDeployDirectoryEntries 直调缝经 tst_task_center_widget 覆盖展开/镜像预填与根退化逐字拒绝文案；空目录中文提示与批量刷表收口（批窗口零逐行 cellChanged、批末单次 flush 经切步往返+collect 保序保真）自 v2.15 波3 任务12 起亦经该直调缝钉桩；**teAddFiles 选文件 leg 的批量刷表窗口（同 lambda 内 blockSignals 批插入+批末单次 flush）不经自动化钉桩**——批窗口钉桩仅覆盖 teAddDir/`addDeployDirectoryEntries` 腿（cellSpy==0 + 切步往返），teAddFiles 腿仅经 QFileDialog 可达（人工冒烟在册空白），为其钉桩需为 10 行窗口另抽缝（评审 I-1 裁决不做），等价性由代码审查（与已钉桩的 addDeployDirectoryEntries 批窗口同法）背书，勿视同已覆盖；超上限（5000）拒绝**不经该直调缝**——由 tst_folder_expand `overCapRejectedWithBothNumbers` 与 tst_deploy_entry_builder `overCapRejectsWithPureHeaderErrorVerbatim` 两处钉桩，条目表用例刻意不造 5000 条目夹具；装配语义由 tst_deploy_entry_builder 直测），**仅余 QFileDialog 选人腿**（teAddDir 选目录与 teAddFiles 选文件弹窗）与新建/编辑按钮的模态对话框流程为人工冒烟覆盖，其余（validate/collect/面板往返/落库编排/e2e「新建→执行」链）均经 tst_task_center_widget 直调缝覆盖。部署页「保存为模板」按钮（v2.14 #49-2）→TaskTemplateEditorDialog 模态流（含同名冲突三分支弹窗）为人工冒烟覆盖（对话框 exec() 沿 #54 teAddFiles 先例不设自动化 UI 目标），映射/落库腿已由 tst_deploy_template_map / tst_task_template_store 直调覆盖，其中**文件夹条目**的展开映射腿自 v2.15 波2 起由 tst_deploy_entry_builder（装配）+ tst_task_center_widget（条目表直调）覆盖。真盘目录树环回集成用例（tst_deploy_loop）跑 Local↔Local 通道，而 `LocalTransferChannel::stat` 对目录返回失败（「本地路径不是普通文件」），确保步在盘符/已存在目录层因此全部落入 mkdir 幂等成功臂；真实服务端形态「父存在→stat 短路跳过创建」仅由 tst_transfer_executor 的 MockChannel 用例钉桩（`parentEnsureSkippedWhenParentExists` 与 `parentEnsureStopsAtExistingAncestorAndCreatesOnlyDeeperLevels` 的 makeDirectory 零调用/仅深层调用断言），环回用例不构成对该臂的实证。

**v2.16-B 设备栏波次的在册空白（2026-10-21 计数定版时如实登记，四条 + 规格 §5 点名的一条）**：
1. **`DeviceForge::openDeviceAt` 没有任何单测接缝**——`src/app/DeviceForge.cpp` 只编进 app 目标（`CMakeLists.txt:189` 的 `SOURCES` 段），**没有任何测试目标链接它**，也没有测试目标编入 `src/app/DeviceForge.qrc`。于是"信号→切 `QStackedWidget` 页→部署页 `selectDeviceByIp`"这条编排只能**人工/真机核对**（任务 6 据此裁定不开新目标，避免动本波定版的 CTest 计数）；可测的半边（`routeTo` 判定与 `selectDeviceByIp` 的命中/未命中）分别由 `tst_device_bus_groups`、`tst_deploy_toolbar_layout` 钉桩，**不得**据"套件全绿"推断编排段被覆盖。
2. **chip 右键菜单的 `QMenu::exec` 弹层段未自动化**——用例驱动的是 `buildPillMenu()` + `dispatchPillMenuAction(pill, role)` 这条抽出的缝（任务 5 定档的备选缝；依据是本环境实测 `QTest::mouseClick(RightButton)` 打不出 `customContextMenuRequested`，真弹层不可驱动）。弹出位置、键盘在弹层里选择、`aboutToShow` 重建时机三条只能人工核对。
3. **真输入下的双击可达性只到"构造事件序列 + `isVisible()`"这一层**——序列用例（press→release→press(DblClick)→release，中途断言胶囊仍可见）证的是"命中测试有机会发生"，**OS 级命中测试与真键盘 Space/Enter 激活仍是人工项**（任务 9 真机清单在册）；单事件注入（`QTest::mouseDClick`）一律不得再被当作可达性证据（规格 §10 第 8 条）。
4. **「×」命中区的布局守卫只有 1px 余量**（中心列 86 vs 区左缘 85，生产 QSS 形态区宽 16px）——它是刻意留的漂移报警器，红了的分诊程序见上文**「×」命中区几何守卫变红的分诊口径**，不得基线化、不得调阈值。
5. **「设备 ▾」子菜单的弹出与 Qt 内建上下滚箭头**属窗口级交互，直测只能到"菜单结构构建正确"（分组/计数/未分组最后/0 命中组不列/底部两项作用域）——**子菜单滚动可见性无法直测，就此登记不假装覆盖**（规格 §5 计数段点名）。
