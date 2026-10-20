# Changelog

## [未发布]

### v2.16-B 设备栏分组与"打开这台设备"（在途）

- **标签规范化单源化（用户可见，§4/§6③）**：模板按标签圈选设备时，selector 与档案标签**两侧都先去首尾空白再比较**（唯一真源 `src/device/DeviceBusGroups.h` 的 `devicebus::normalizeTag`）——带首尾空格的模板标签选择器（如 `" 车间A "`）过去**静默匹配不上**，v2.16 起匹配得上，**相关模板的实际部署目标集可能变大**（从漏到不漏，发布说明必须显性交代）。大小写与内部空格口径不变（`车间 A`、`车间a` 仍不匹配 `车间A`）。另两处（`DeviceRegistry::normalize` 存库前、档案对话框逗号切分）现状本就去首尾空白，本版仅换用同一真源，行为等值；`DeviceProfileCodec` 读侧不动（避免第二条真源）。

## [2.15.0] — 2026-10-20

### v2.15 波1（留池清零）

- **ConfigStore 截断根治（#49-5）**：`list()` 默认 limit 1000→-1（-1=全表读取，offset 忽略）；显式传 limit 的调用方行为逐字不变。根治"遍历全表语义被默认 limit 静默截断"的潜在数据不可见类。`tst_config_store` 附 >1000 行夹具红-绿。
- **legacy 清扫与模板清单全表化（#49-8）**：`DeviceRegistry` 三处 device.list 清扫/回退扫描（saveReplacing/load/remove）与 `TaskTemplateStore::list()` 去 1000 上限，>1000 行残留一条不漏；`tst_device_registry_store`/`tst_task_template_store` 各附超界可见性用例。收口评审补迁：`DeviceRegistry::load()` 主存储 device.profile 回读（清点残余站点，控制器裁定纳入波1）同去 1000 上限，回归钉 `loadAboveLegacyCapReadsEveryProfile`（1005 条档案一条不漏，RED 先证 actual 1000）——波1 limit 清点就此闭合，待裁残余 0。
- **运行终态聚合单源（#49-6）**：新纯逻辑头 `src/task/TaskRunAggregate.h`（`taskrun::aggregateRunStatus`），`TaskCenterWidget::aggregateStatus` 薄转发、`tst_task_center_e2e` 删复制实现改 include 纯头——Failed>Cancelled>Running>Success 优先级规则从机制上消除复制漂移；规则用例并入 `tst_task_center_widget` 直测纯头（不新增目标，CTest 计数不变）。
- **导入通道统一（E）**：`TaskCenterWidget::importTemplateFromFile` 同名冲突三分支改走 `TaskTemplateStore::saveUniqueByName` 单源落库（与部署页「保存为模板」共用冲突弹窗/匹配口径/改名递增循环/覆盖 ID 规则；覆盖 version 定档为 max(本机既有, 入站) 抬升归一）。widget 侧独立改名循环与 hasTemplateName 扫描删除。
- **telnet 工装有界化（D）**：`tst_telnet_timeout` 挂 ctest `TIMEOUT 60`（独立无条件属性调用，置于 `_qt_bin_dir` 守卫外），负载下挂起形态从拖死整轮降为单目标有界红；分诊口径（单跑复核+全量两遍）登记 `tests/AGENTS.md`。
- **波1 文档收口（本票）**：用户手册同名策略版本口径就实（覆盖=继承 ID + 版本取较大值；导入与保存为模板共用落库通道；设备/模板清单无隐性条数上限就实）、`TaskTemplateStore` 注释失效引用清理（"不重构导入路径（留债观察）"与 widget 侧改名循环同源引用已失效）、CHANGELOG 汇总腿与规格执行注记（残余 limit 站点清点入册）。

### v2.15 波2（文件夹条目支持）

- **目录展开纯头（T7）**：新纯逻辑头 `src/deploy/FolderExpand.h`（`folderexpand::expand`/`scanDir`/`composeRemotePath`）——目录以**自身名为镜像相对路径首段**、顶层文件 rel 空扁平、递归全深度不追符号链接（QDir::Files|NoSymLinks）、展开后文件条目 > `kMaxExpandedItems=5000` **整体拒绝**（中文文案携实际数与上限数）、空目录 0 条不算错、文件系统根退化（dirName 空）整体拒绝。新目标 `tst_folder_expand`（scanner 注入脱盘直测）。
- **传输层父目录确保能力（T8）**：`ITransferChannel` 末尾追加**可选虚函数** `makeDirectory`（单层创建，默认实现返回不支持——旧通道零改动向前兼容），`TransferCapabilities` 末尾增 `makeDirectory` 能力位（默认 false）；`TransferExecutor` 上传前对**不存在**的目标做父目录逐级确保（stat 自最浅缺失层起、序贯创建、建目录失败后复检该层存在则继续——闭合瞬断被收口成确定性红的窗口，评审 I-1）。FtpAdapter 单层 MKD 经 SylixOS 三禁核对（根 URL+绝对路径、MULTICWD 逐级、显式 `CURLOPT_FTP_USE_EPSV=0`），SshAdapter 走 sftpMakeDirectory，LocalTransferChannel 单层真 mkdir。`tst_transfer_executor` 增父目录确保用例组与既有钉桩（目标存在即跳过确保步、stat 增量零漂移）。
- **三入口目录展开接线（T9）**：新装配纯头 `src/deploy/DeployEntryBuilder.h`（`deployentry::build`：分类以**点击时刻磁盘实况** QFileInfo::isDir 为准、面板行快照丢弃重算——闭合快照过期绕过 5000 上限的窗口，评审 I-1/I-2）；部署页单发、模板编辑器 teAddDir（`addDeployDirectoryEntries` 直调缝，展开即文件条目入表、拒绝纯头文案逐字弹显表格零改动）、部署页保存为模板三入口同口径。`DeployJob` 增**每条目远端路径贯通**（`entryRemotePaths` 平行向量：非空=显式条目模式，Job 不派生 basename、不目录遍历、跳过自身 mkdir 环，远端子目录全由执行器确保步创建；requestKey 纳入每条目远端路径——同清单仅布局变化不再误判同一请求）；空向量=v2.14 扁平口径逐字不变。新目标 `tst_deploy_entry_builder`；`tst_deploy_loop`/`tst_task_center_widget` 增镜像与直调用例。
- **SFTP 单发落点加深一层（行为变更，用户批准裁定）**：放弃 v2.14 及更早 `DeployJob` legacy 目录展开的协议不对称（FTP 包含顶层目录名 / SFTP 上传目录内容），三入口统一为**镜像口径（目录名为首段）**——由此 **SFTP 单发部署所选目录的远端落点比 v2.14 深一层**（此前 `/apps` 选 `pkg/` 内容直落 `/apps/`，现落 `/apps/pkg/`；FTP 落点不变）。属现场可见变更，依赖旧 SFTP 落点的脚本/站点需在 v2.15 发布说明中知悉并复核目标路径。
- **波2 收口（T10）**：`tst_deploy_loop` 增**真盘目录树环回集成用例**（scanDir 实扫→deployentry::build 装配→Backend/Job 显式条目模式→TransferExecutor 确保步→LocalTransferChannel 真盘单层 mkdir→目标树与源树镜像逐文件一致，路径集合+字节双断言、无 .part 残留）——"远端子目录由执行器创建"这条新链路的唯一端到端防线（摘除确保步即红的 neuter 取证）。勘误三处（RELEASE_NOTES v2.14.1/CHANGELOG [2.14.1] 段末注记 + 用户手册就地改实况）、接口参考 makeDirectory/能力位/DeployJob 双路径与 uploadFolder 预留口径就实、用户手册目录语义+5000 上限+SFTP 落点变更+teAddDir 行为补写、规格 §3.1 与计划初稿装配措辞勘误注记、`tests/AGENTS.md` 计数定版 **51**（波2 新增 `tst_folder_expand`/`tst_deploy_entry_builder` 两目标；计划初稿写 50 系未计任务9 新增目标）。
  > 待办（2026-10-20，波2 终审 I-3 登记波3，本波不实现）：`DeployJob` 每条目独立构造通道+执行器，对**非存在**目标会重复 stat 本轮已 ensure 的祖先目录，到 5000 条目上限时慢速 FTP 约多 ~15k 次控制往返；正解=运行级「已确保祖先集合」经 `DeployJob::Params` 贯穿供同轮复用。评审裁定「早修受益、非合并阻塞」，登记于波3（窄屏工具栏同波处理），本波仅记录不动码。**（波3 已落地，见下节首条。）**

### v2.15 波3（性能债、编辑器体验与窄屏工具栏）

- **运行级「已确保远端目录」复用（波2 终审 I-3）**：新纯头 `src/transfer/EnsuredDirCache.h`（QSet 容器，**刻意无锁**——每设备一份、由 `DeployJob` 值成员持有，job 内单线程；`DeploymentRunner` 的并发粒度是每 job 一线程）+ `TransferExecutor` 构造函数**末尾**追加可选指针参数 `xfer::EnsuredDirCache* ensuredDirs = nullptr`。传 nullptr 即逐字复现波2 的 stat 序列（既有计数钉桩零改动，另配 `nullEnsuredDirCacheKeepsParentProbeSequence` 钉住这层等价性）。缓存只在三条「已确认存在」臂上记入（该层 stat 报存在 / MKD 成功 / MKD 失败后复检报存在），同轮后续文件对这些祖先层直接跳过 stat——到 5000 条目上限、目录深 3-4 层的形态下按估算可省上万次控制往返（慢速 SylixOS FTP 现场收益最大）。新增钉桩 `statArmExistencePopulatesCacheForLaterFiles`、`reStatArmExistencePopulatesCacheForLaterFiles`。**口径更正**：实现报告里的「ABI 加法」应为**源码级兼容**——默认参/默认实现虚函数的形态保证源码向前兼容，二进制兼容未单独取证。
- **目录展开批量刷表与空目录提示**：编辑器 `addDeployDirectoryEntries` 把整段插入包进 `blockSignals` 窗口，且仅在 `added > 0` 时**恰好一次**尾部 `flushDeployTable()`——近上限添加目录时的逐行 `cellChanged` 风暴（O(N²) 刷表）就此消失；钉桩断言批窗口内 `cellSpy` 计数为 0 且切步往返后条目序与 collect 结果保真。所选目录为空（或仅含空子目录）时不再静默返回，改给中文提示「所选目录为空，未添加任何文件条目」并仍返回 0。
- **部署页工具栏两行化（任务13 方案 C，窄屏右侧裁切根治）**：`FtpDeployWidget::setupToolbar` 原单行 `QHBoxLayout` 承载 23 控件、超过 1920 屏宽（本波实测，与生产同口径=Fusion 样式 + `:/styles/darkstyle.qss` + 默认内容，`totalMinimumSize().width()`=**1866px**；v2.14 双屏诊断现场实测约 2010px，差值归因设备名长度与窗口边框/DPI），而 `QHBoxLayout` 最小宽=子控件之和且从不换行 → 右侧「导出报告/重试失败设备」在 1366/1280 屏被裁。改为容器 `deployToolbar` 内 `QVBoxLayout`（行距 4）+ 两个 `QHBoxLayout` 行：**第 1 行=连接组（协议/设备/端口/FTPS/状态点）│ 分隔条 │ 主操作（⟳ 刷新 / ▶ 部署到 N 台设备 / 保存为模板）**，**第 2 行=部署选项（清空/重启 + 协议/命令/超时/重试四参）│ 分隔条 │ 结果操作（⬇ 导出报告 / ↻ 重试失败设备）**，各行尾 `addStretch()`。既有控件的创建顺序/对象名/初值/tooltip/ConfigStore `ftp.deploy.prefs` 读写/32 处 `connect` 逐字不变（仅归属布局改变）；新增对象名仅容器 `deployToolbar` 一个，供布局回归定位。**窄屏不再右侧裁切**（同一测法两行态实测 **933px**，1366 屏可用宽 1270、1280 屏可用宽 1184 均在内；台数文案实测不影响最小宽——QSS `min-width` 主导最小宽、长文案只涨 sizeHint），代价工具栏垂直 +1 行 ~34px。新目标 `tst_deploy_toolbar_layout`（6 用例）直构 widget 断言两行结构、逐行成员归属（连接组三件套与重启四参、分隔条、含反向归属）与**目的钉桩** `toolbarFitsNarrowScreen`（`totalMinimumSize` ≤1270），并配 `qssFloorIsInEffect` 守卫——无生产 QSS 时量得 834px 会伪装成「宽度过关」。CTest 计数 **51→52**。

### 工程与流水线（非产品行为）

- **CI runner 钉版本**：`ci.yml` 与 `release-validation.yml` 的 `runs-on` 由 `windows-latest` 钉到 `windows-2022`。`windows-latest` 已切至 `windows-2025-vs2026` 镜像、其中**没有 VS2022 实例**，而工作流的 CMake 生成器串是 `"Visual Studio 17 2022"`，于是 configure 第一步即 `could not find any instance of Visual Studio`——查 `gh run list` 得**至少自 2026-10-08 起每次 push 全红**。钉版后 configure 与 build 转为通过，本仓 CI 十二天来第一次真正跑到 ctest（因此也首次暴露：`tst_ws_page_layout` 在 runner 上确定性红，而该工具与用例自 v2.14.1 起零改动，属环境相关红、非 v2.15 引入）。该红随后由下条修复，本仓 CI 自此首次全绿。
- **配套工具输出契约=UTF-8**：`versioncheck.py` 与 `smoke.py` 的 CLI 入口把 `sys.stdout`/`sys.stderr` `reconfigure(encoding="utf-8", errors="replace")`。实测坏形态两条：cp1252 控制台打印中文报告行抛 `UnicodeEncodeError`（versioncheck 在 CI 上表现为 4 个用例 ERROR）；`smoke.py` 打印中文用法行同样崩，使约定退出码 **2 变成 1**——"工具自己崩了"冒充"参数校验未通过"。配套先红后绿的码页用例、读侧显式 `encoding="utf-8"`、以及规约（`tools/devtools/AGENTS.md` 第 6 条）同批落地。
- **测试计数定版 52**（波3 新增 `tst_deploy_toolbar_layout`）；`tests/AGENTS.md` 新增**「零输出红的取证口径」**：`***Failed` 而无任何输出时，当轮必须补 `ctest -V` 取退出码 + 直跑 `-o file,txt` 非空守卫 + 终验留 `--output-junit`，未归因前不得写进 flake 基线。

- **`tst_ws_page_layout` 几何断言改屏宽无关判据（PR #63，仅测试侧）**：`eventTableIsTheOnlyVerticalExpander` 假设顶层窗口真能拿到请求的 1400 逻辑宽。本地 `QT_SCALE_FACTOR=2` 复现出与 runner 同形的红——请求被窗口管理器钳到 962，`setChildrenCollapsible(false)` 让两栏按最小宽等比压缩成 360/580，「右栏 > 2×左栏」因此**假红而扩展策略未变**。改法：无条件判「序」（谁是扩展主体），「倍数」放进 `widget.width() >= 1100` 门内，并用 `qInfo()` 打出实走分支与实际尺寸（不留「这条断言在 CI 跑没跑」的悬案）。非虚设性以产品 `setStretchFactor` 主次对调做变异核，判序那格红在 738/640；`QT_SCALE_FACTOR=1/1.5/2/2.5` 下 16/16。规约入 `tests/AGENTS.md` 第 6 条（几何断言须与屏宽/缩放无关）。

### 已知问题

- **SylixOS FTP 三条禁令未单源化（GitHub #59，改动排 v2.16）**：`FtpAdapter` 里三禁**没有单一来源**——逐级 CWD（`CURLFTPMETHOD_MULTICWD`）只在公共选项函数 `setupCommonOpts` 内设过一次（`FtpAdapter.cpp:173`），禁用 EPSV 散在四处行内设置（`:399` / `:581` / `:638` / `:891`），于是自建选项序列的 **4 条腿**（`uploadFile`/`downloadFile`/`readFileToMemory`/`writeFileFromMemory`）拿不到逐级 CWD、走公共函数的 **6 条腿**（`connect`/`listDirectory`/`listDirectoryParsed`/`deleteFile`/`deleteDirectory`/`renameFile`）拿不到 `EPSV=0`。逐腿实况（行号取 main；`clearRemoteDirectory:915` 与 `request:285` 只做委托/分发，不自建设柄）：`uploadFile:356`=有 EPSV=0(:399) + `FTP_CREATE_MISSING_DIRS`(:390)、**无 MULTICWD**；`uploadFolder:424`=委托 `uploadFile`（:466）⇒ 与 `uploadFile` 同（无逐级 CWD、有 EPSV=0；全仓无调用方的预留接口）；`downloadFile:475`=**两者皆无**；`readFileToMemory:555`=有 EPSV=0(:581)、无 MULTICWD；`writeFileFromMemory:609`=有 EPSV=0(:638) + `CREATE_MISSING_DIRS`(:632)、无 MULTICWD；`connect:196` / `listDirectory:661` / `listDirectoryParsed:708` / `deleteFile:741` / `deleteDirectory:778` / `renameFile:827`（三条 QUOTE 腿）走 `setupCommonOpts` ⇒ **有 MULTICWD、无 EPSV=0**；`makeDirectory:874`（波2 新增）两者齐备。影响面按证据收口：**目前无现场失败记录**——缺 MULTICWD 的腿另有 `FTP_CREATE_MISSING_DIRS`（libcurl 走「CWD 失败→MKD→重新 CWD」）或本就是单层操作，故本条是**一致性与可维护性债**（下次改动可能无声打破某一腿），不是已观察到的功能故障；单源化改造与真机验证清单排 v2.16。
  > 发布前自陈勘误（2026-10-20）：本条初稿写「`downloadFile`/`readFileToMemory`/`writeFileFromMemory` 未设 `MULTICWD`；`listDirectory` 与**四条** QUOTE 命令腿、以及 `downloadFile` 未设 `EPSV=0`」并断言「**上传**与本次新增的建目录腿不受影响」。逐行核 main 后订正三点：① 漏了**上传腿 `uploadFile` 同样缺 `MULTICWD`**（`uploadFolder` 经它委托，一并缺）——恰是最常走的路径，不该被写成"不受影响"；② QUOTE 腿实为**三条**（`deleteFile`/`deleteDirectory`/`renameFile`；`makeDirectory` 是齐备的那条）；③ `EPSV=0` 缺失面还包含 `connect` 与 `listDirectoryParsed`。错因=把建票时的旧清点当证据、发布前未重新 grep。下表按行号重取。

> 运营渠道交代（2026-10-20 发版当日）：v2.12/v2.14 两批勘误的**外部已发布文章快照未回改**——CSDN/掘金/知乎/公众号四平台的既有稿件保持原样，本轮更正只落在仓内文档与本版本 GitHub Release。回改触发条件定为「下一次新文章发布时统一带回修订段」，不单独为一条勘误重发渠道稿。

## [2.14.1] — 2026-10-18

### 修复与就实（2026-10-18，v2.14 代码评审后续波补丁，评审 I-1/I-2/Minor）
- **传输层目录条目口径钉桩**：`tst_transfer_executor` 新增用例断言文件夹作为上传源在触碰远端前即失败（LocalIo/零通道调用）——单发部署与任务模板同走 TransferExecutor，本就无「单发能传文件夹」分叉；`FtpDeployWidget::collectLocalFiles` 处宣称 backend uploadFolder 递归处理文件夹的失实注释改写为实况（uploadFolder 全仓无调用方）。用户手册文件部署首段与操作步骤、保存为模板映射表同批改就实（仅普通文件可部署）。
  > 勘误（2026-10-20，v2.15 波2 收口自陈）：执行器层钉桩本身成立（裸目录进执行器确实于触碰远端前失败），失实的是「单发与模板同走 TransferExecutor 故无分叉」的推断——当时取证 grep 范围未打开 `DeployJob.cpp`，其单发路径一直有本地目录遍历与自建远端目录，选文件夹部署实际可成；「仅普通文件可部署」对单发路径不成立。据以改写的手册三处表述已随波2 就地改回实况（见下一未发布段「波2 文件夹支持」）；`collectLocalFiles` 注释与钉桩用例本身不受勘误影响（注释此后再度就实为入口展开口径，用例保留为执行器侧纵深防御）。历史正文不改写，就此注记归位。
- **主窗口初始几何主屏优先兑现**：`WindowPlacement.h` 契约写明「调用方按主屏优先传」，`DeviceForge.cpp` 实际按 `QGuiApplication::screens()` 任意序传——收集屏几何改为 primaryScreen 置顶再补其余，兑现文档与测试意图。
- **文档失效引用勘误**：v2.13 编辑器规格中「至少一条命令」文案已被 #49-3 单源为「缺少命令列表」，段末补勘误注记（历史正文不改写）。

## [2.14.0] — 2026-10-18

### 修复（2026-10-18，v2.14 双屏「看不全」初始放置钳制）

- **主窗口启动后可能超出屏幕右侧被裁**（双屏实测捕获：工具栏单行最小宽 ~2010px 超过 1920 主屏可用宽，OS 默认放置可落在装不下它的屏）：新增纯逻辑头 `app/WindowPlacement.h`——`initialFrameGeometry(需求尺寸, 各屏可用几何)` 选第一块能完整容纳的屏居中放置（主屏优先），全部装不下时选面积最大屏逐轴压缩并居中（最终仍受窗口最小尺寸钳制）；`DeviceForge` 构造尾部收集 `screens()` 应用之。新目标 `tst_window_placement` 4 用例（跳屏选择/主屏优先/最大屏压缩/空屏兜底）。工具栏窄屏换行/折叠为更大改动，转 v2.15 池。全量 CTest **49** 目标绿。

### 修复（2026-10-17，v2.14 冒烟修复批：编辑器按钮接线 + 部署条目移除 + 页显刷新）

- **部署页保存的模板在任务中心不可见（页显刷新缺失）**：任务中心模板列表只在构造/本页操作时刷新，部署页「保存为模板」属外部落库源、无通知路径——保存成功日志可见但切到任务中心列表仍是旧的。`TaskCenterWidget` 覆写 `showEvent` 页显即 `refreshTemplates()`（选中与搜索过滤由既有语义保持，代价一次 SQLite 读页），收敛一切外部变更源。用例 `taskCenter_showEventRefreshesExternallySavedTemplates` 非模态 show 直测。
- **模板编辑器「保存/取消」按钮无响应（v2.13 起带病发布，冒烟实测捕获）**：`TaskTemplateEditorDialog` 底部 `QDialogButtonBox` 从未接线——原注释误称「QDialog 自动接线 accepted→accept()」（Qt 实际必须手动 connect，仓内先例 `deviceProfileEditor` 即手动连），导致点「保存」「取消」均无反应、只能关窗口 ✕，编辑器 UI 无法保存任何模板。补 `connect(accepted→QDialog::accept)`/`connect(rejected→QDialog::reject)`（accept 为虚覆写，走 vtable 分派到校验闸），注释就实。自动化逃逸原因复盘：#54 用例全部直调 validate()/collect() 绕过按钮，按钮流登记为人工冒烟覆盖而 v2.13 发版未跑该格——本次以 `editorDialog_buttonBoxWiringClosesDialog` 非模态 click 直测补上（合法输入点保存→关闭且 Accepted；点取消→Rejected）。
- **部署步骤文件条目只加不删（#54 设计缺口）**：teItemList 此前仅有「添加文件…/添加目录…」入口，无移除按钮、无 Del 键、无右键菜单，添加错了只能整步删除重配。新增「移除选中行」按钮（`teRemoveItem`，无选中禁用），点击删表行并即时 `flushDeployTable()` 写回步骤真值（removeRow 不发 cellChanged，必须手动 flush；程序化绑定窗口内忽略防误删）。用例 `editorDialog_removeItemRowDeletesEntry` 锁定：删行后 collect 出的 items 只剩未删条目。
- 两修复同源于 v2.14 波1「保存为模板」GUI 冒烟；`tst_task_center_widget` 32/32 绿。

### 文档（2026-10-17，v2.14 勘误收口与全量核验波）

- **对外文档勘误收口**：本文件 v2.12 段「事件流含对端」句段末追加勘误注记（历史正文不改写）；产品路线图「对端」表述归位 v2.14、「事件过滤」挂账就实为 v2.14 完成交付；2026-10-15 运营文章「v2.13 已立项…事件过滤」句改 v2.14 并加文首修订说明（外部渠道已发布快照无法回改，列为待人类渠道同步事项）。措辞就实：用户手册与 `wsDroppedLabel` tooltip「停止或清除后归零」改「重新启动或清除事件流后归零」（实况 `onStartClicked`/`onClearEventsClicked` 归零、单点停止不归零），手册对端列全称判断补 Server「发」行对端固定记「订阅者」既有形态（`serverPublishToSubscribers` 实证）；v2.14 #49-3 段 `tst_task_step_validation`「12 用例」就实为 10 用例（逐函数清点）。规格 `2026-10-17-v2.14-hardening-pack-design.md` §4.2/§4.3 加执行偏差注记（reboot 命令可用性分层澄清、惰性清退替代收口同步淘汰），原文保留。全量终验：Release 构建绿、CTest 48 目标连续两遍全绿、versioncheck [OK] 2.13.0（本波不动版本九源）、UI 冒烟通过。

### 增强（2026-10-17，v2.14 #50-3 WebSocket Token 常量时间比较+长度上限）

- **Token 常量时间比较与 256 长度上限（#50-3）**：WebSocket Server 握手认证此前用 `std::string::operator!=` 短路逐字节比较 token（泄露「前 N 字节匹配」时序签名的侧信道面）且无长度上限。`WsEventTypes.h` 新增纯逻辑 `wsproto::constantTimeEquals`（等长全扫描、长度差异循环后并入判定、volatile 落点防 -O2 优化回短路；只防时序旁路、不防长度泄露，如实标注）与 `kAuthTokenMaxLen=256` 上限常量；`onNewConnection` 认证比较换用之。`WebSocketBackend::setAuthToken` 签名 void→bool 加长度闸——超限拒绝写入（保持旧值）返回 false；Widget `onStartClicked` 检查返回值，false 时弹「Token 不得超过 256 字符」且不启动；Token 输入框 `setMaxLength(256)` 同源限制。测试：`tst_ws_event_types` 新增 `constantTimeEqualsBehavior`（等值/单字节差/双向长度差/空串组；时序性质单测不可靠断言、规格定档不做计时断言）与 `authTokenMaxLengthConstant`；`tst_ws_loopback` 补 Server+Token 环回空白——`authTokenRejectsWrongConnection`（错误 token 拒连以「认证失败」日志行取证、正确 token 正常入在线列表）与 `authTokenLengthGateKeepsPreviousValue`（空串/恰 256 合法回 true、300 字符回 false 且旧值保持：持被拒超限 token 的客户端被拒、持旧 token 的客户端正常接入）。全量 CTest **48** 目标绿。

### 增强（2026-10-17，v2.14 #50-2 WebSocket 事件流方向/主题过滤）

- **事件流展示层过滤（#50-2）**：WebSocket 右栏工具行（自动滚动与清除之间）新增方向过滤下拉（`wsFilterDirection`：全部/收/发，itemData 依次 ""/收/发）与主题子串输入框（`wsFilterTopic`，placeholder「主题过滤」，大小写不敏感、空=不过滤）。过滤只作用展示层：不匹配行 `setRowHidden`，全量事件仍入表模型，FIFO 2000 上限与丢弃计数不受过滤影响；「清除」仍清全量与计数、过滤条件本身保留。谓词收敛进 `WsEventTypes.h` 纯逻辑：`wsproto::matchesFilterFields(direction, topic, directionFilter, topicSubstring)` 为单一判据（Widget 行过滤直读方向/主题单元格调用它，null 单元格视作空串防御），`matchesFilter(WsEvent,…)` 规格签名委托之（防事件对象路径与表格路径两套逻辑漂移，测试锁定）；`pushEvent` 新行插cell后、自动滚动前按当前过滤即时判定。测试：`tst_ws_event_types` 新增 `filterFieldsSemantics`（四组语义：全通过/方向全等/大小写不敏感子串/组合退化）与 `filterPredicateDelegatesToFields`（委托等价矩阵）；`tst_ws_page_layout` 新增 `eventFilterHidesRowsKeepsModel`（隐藏不删行、新行即时过滤、还原全可见、活跃过滤下清除归零）。全量 CTest **48** 目标绿。

### 增强（2026-10-17，v2.14 #50-1/4/5 WebSocket 事件流对端列+运行输入守卫+丢弃计数说明）

- **事件流对端列（#50-1）**：WebSocket 事件流表新增第六列「对端」，列序定档 时间|方向|主题|对端|字节|内容预览（ColPeer 插在 ColTopic 后、ColBytes 前，初始宽 130，Interactive/末列 Stretch 逻辑随枚举名自动跟随）；数据直取后端已填的 `WsEvent::peer`（Server 模式=客户端地址、Client 模式="server"），后端零改动。测试：`tst_ws_page_layout` 新增 `peerColumnShowsSource`；既有用例预览列索引断言（4→5）随六列契约同步更新。
- **运行输入守卫（#50-5）**：`applyRunningUi` 运行中将六个连接参数控件（wsBindAddr/wsPort/wsWss/wsToken/wsUrl/wsTrustCert）`setEnabled(!running)` 锁死——此前守卫只动按钮与模式切换、输入框运行中仍可修改，改后不生效且状态卡仍显运行配置（误导面）；停止/启动失败回落/错误回调/对端断开收口均走本函数 false 分支一并恢复，构造期不调用本函数、初态默认可用。测试：新增 `runningGuardsDisableConnectionInputs` 经 Server 模式真实 start→stop 驱动断言六控件禁用→恢复（QTcpServer 探针取空闲端口规避占用 flake）。
- **丢弃计数 tooltip（#50-4 定档，行为不改）**：`wsDroppedLabel` 增加「丢弃计数仅统计本会话：重新启动或清除事件流后归零，不跨重启累计」静态说明，与 `onStartClicked`/`onClearEventsClicked` 的归零实况复核一致。测试：新增 `droppedTooltipDocumentsSessionScope`。用户手册 WebSocket 节同步五列→六列表述、计数会话内语义与运行守卫说明。全量 CTest **48** 目标绿。

### 文档（2026-10-17，v2.14 #49-1 AuthResolver 凭据降级语义定档）

- 任务链凭据取档案首端点可解析引用（定档，非缺陷修复）：v2.11 规格 §7 增补「凭据解析降级契约（v2.14 #49-1 定档）」小节，将引擎 per-device AuthResolver 的降级语义（同档案多 credentialRef 取首个可解析引用、解析顺序=端点表序、其余端点共用该凭据、告警仅设备名与引用数、跨协议独立凭据 v2.14 明确不演进）从代码注释升格为书面契约；`TaskCenterWidget.h` 头注释同步指回该契约。纯文档票，不改任何 .cpp 逻辑。

### 修复（2026-10-17，v2.14 #49-7 DeviceRegistry 落盘失败快照回滚）

- **落盘失败不留半成品内存**：`DeviceRegistry::save`/`saveReplacing` 在任何失败返回前恢复调用前 `m_profiles` 快照并 `rebuildWarnings()`（票面对象经复核为 DeviceRegistry，非 TaskTemplateStore）；`saveReplacing` 的 `stored` 出参改为仅成功时填充，空 deviceId 分支运行时实证不可达、保留为不变量防御。连带调用方修复：`DeviceBusWidget::editProfileFor` 失败分支改为按入参档案记警（不读 stored）→ `refreshFromRegistry()` → 早退，失败不再写 legacy 行、不广播 `deviceProfileEdited`。`tst_device_registry_store` 新增 3 用例（save 失败回滚 / saveReplacing 失败端点回滚+stored 不写 / 失败返回前内存零残留），失败注入=成功落库后 `ConfigStore::instance().close()`（save 首行 ensureOpen 短路，用例首行运行时背书）。全量 CTest 48 目标绿。

### 修复（2026-10-17，v2.14 #49-4 lineage/32-run 错配文案分派）

- **阶段恢复拒绝文案三分派与 lineage 惰性清退（#49-4）**：引擎历史上限 `kHistoryLimit=32` FIFO 淘汰，而任务中心 widget 的 `m_lineageByEngineRun` 只插不删——会话内超 32 轮后重试旧运行，lineage 命中但引擎已淘汰存档，`retryFailed` 返回空却被笼统报「阶段恢复被引擎拒绝」，未说明真因。引擎新增只读查询 `hasRunInHistory(runId)`（锁内 `m_history.find`，与 retryFailed 查找同源），`kHistoryLimit` 由 private 上移 public 作单源；widget 新增公共静态纯函数 `retryRejectionMessage(lineageKnown, engineHasHistory)` 三分派文案（本会话外运行/引擎 32 轮已淘汰/仍有存档但被拒（活跃运行）），`onRetryClicked` 在 retryFailed 拒绝且引擎无存档时**惰性清退**该运行的 lineage 与 store→engine 映射（点击即校正，不对引擎 FIFO 淘汰顺序做同构假设，偏差处置记于规格 §7 风险 1）。测试：`tst_task_execution_engine` 新增场景十一——缺命令端点快速失败模板（校验失败同样入档）连跑 33 轮，断言第 32 轮收口时首轮仍在档、第 33 轮后已淘汰且 `retryFailed` 被拒（变异对照验证非假绿）；`tst_task_center_widget` 新增 `retryRejectionMessagesAreDistinguishable` 直测三分支关键词与两两不同（轮数断言单源引用 `kHistoryLimit`）。全量 CTest **48** 目标绿。

### 修复（2026-10-17，v2.14 #49-9 引擎异常路径脱敏）

- **引擎 catch 旁路异常文本过脱敏后入日志（#49-9）**：`TaskExecutionEngine` 两处 `catch (const std::exception&)` 的 `qWarning()` 原先直送 `error.what()` 进全局日志（qWarning→LogBridge→文件日志），而执行链底层（libcurl/libssh2 等）常把含 userinfo 的 URL 塞进异常文本，构成凭据泄露旁路。两处出口（协调线程 Runnable、`executeRun` 收口）统一改为 `sanitizeTaskErrorText(error.what())` 脱敏后再入日志，保留原中文措辞。测试：`tst_task_execution_engine` 新增场景十——fake 命令适配器 connect 抛含 `ftp://admin:pass@…` 形态的 `std::runtime_error`，经 `qInstallMessageHandler` 捕获 qWarning 行（RAII 装卸成对、互斥锁保护捕获容器、以 `onRunFinished` 回调为同步点），断言捕获行不含凭据原文且含 `://***@` 脱敏形态；红-绿已验证。全量 CTest **48** 目标绿。

### 重构（2026-10-17，v2.14 #49-3 步骤参数校验共享纯函数）

- **步骤参数校验文案引擎/编辑器单源**：新增纯逻辑头 `src/task/TaskStepValidation.h`（`taskstep::parseDeployItems` 自引擎 cpp 匿名 ns 整体搬移、行为零变化；`taskstep::stepParamsValidationError` 收敛四类步骤的参数级判定），引擎 `validateDeviceSteps`（Validating）与编辑器 `validate()` 此前两处并行实现、commands 分支文案分叉（编辑器「至少一条命令」vs 引擎「缺少命令列表」）统一为引擎原形「步骤 %1（%2）缺少命令列表」。commands 判定采编辑器 trim 后判空语义——比引擎旧裸列表 isEmpty 更严（纯空白命令行视为无效），引擎随之刻意收紧。端点/设备存在性校验**刻意留在引擎执行期**（模板可跨机，导入/编辑期本机端点未必存在，v2.13 悬空引用语义延伸）。新增 `tst_task_step_validation` 纯逻辑目标（10 用例）；全量 CTest **48** 目标绿。

### Added（2026-10-17，v2.14 #49-2 单次部署配置存为任务模板）

- **部署页「保存为模板」全链接线（兑现 v2.11 规格 §4 单次配置捕获承诺，#49-2 收口）**：「文件部署」页工具栏部署组「▶ 部署」旁新增「保存为模板」按钮（`deploySaveTemplateBtn`，部署进行中与部署按钮同节奏禁用）。槽内先走与部署入口同源的拒绝分支（左面板远程浏览源/空文件清单），再把单次配置经 `deploymap::buildTemplateFromOnceConfig`（新纯映射头 `DeployTemplateMap.h`）固化为预填模板——文件清单+目标目录→deploy 步 `files+remoteDirectory` 简写（编辑器打开归一 items[]）、勾选设备→deviceIds（**未勾选=未绑定留空，不展开为全部设备**；空档案 ID 逐个过滤）、重启勾选→可选 reboot 步（空命令回退 `reboot`）；部署前清空/FTPS/端口/并发度刻意不进模板（工具级/连接级配置）。预填经 v2.13 `TaskTemplateEditorDialog` 模态编辑后，由主窗口注入的 `std::function` 缝（widget 不直连 store）经 `TaskTemplateStore::saveUniqueByName`（新增）落库：同名（大小写敏感全等）冲突三选「覆盖/改名保存/取消」弹窗（**默认=改名**，与 v2.13 导入冲突同口径）——覆盖继承既有 templateId 且 version 取 max(既有,入站)、改名「名称 (2)」递增找空位另存新 ID、取消不落库回中文原因；保存通道未注入时止步日志提示（无宿主兜底）。
- 测试：新增 `tst_deploy_template_map`（纯映射 5 用例：简写参数承载/重启步进生成/空命令回退 reboot/未勾选=未绑定/不勾选仅一步）；`tst_task_template_store` +4 用例（saveUniqueByName 无冲突新建 ID/取消零写入/覆盖继承 ID 与版本/改名递增后缀）。按钮→编辑器模态流沿 #54 先例走人工冒烟（tests/AGENTS.md 覆盖空白段已登记）。全量 CTest **47** 目标绿。

### 修复（2026-08-21，fix/v2.7-编译修复）

- **ModbusBackend**：修复 Qt 6.11 编译错误（由 v2.7 评审修复提交引入，任何 Qt 版本均无法编译）——`QObject::connect` 的 context 误传非 QObject 的 `this`（改为 3 参数 connect、以 client 为 context）；`sendWriteSingleCoil`/`sendWriteSingleRegister` 为不存在的 API（改为 `sendWriteRequest` + `QModbusDataUnit`，保持 0x05 线圈/0x06 寄存器语义）；日志回调 QString 直传 `std::string` 参数（补 `.toStdString()`）
- **WebSocketBackend**：修复 WSS 自签名证书 API 在 Qt 6.11 已不存在导致的编译错误（`QSslKey` 构造参数错序 + `QSslCertificate::generateSelfSignedCertificate` 已移除），改为加载 `src/app/certs/` 预生成测试证书（RSA 2048、SAN 含 localhost，随 QRC 打包）

## [2.13.0] — 2026-10-16


### Fixed/工具（2026-10-16，#55 v2.13 终审缓行包）

- **formatVersion 严格整数闸**：`decodeTemplateFile` 拒绝 `1.5`/`true` 经 `toInt()` 静默转换伪装成 v1 的手改文件（要求数值类型且为整数值）；缺 version 用例补文案断言。
- **`.json` 后缀防重**：`sanitizedTemplateFileName` 对已以 .json 结尾（大小写同判）的名字不再追加，杜绝 `x.json.json` 默认名。
- **编辑器版本死限解除**：teVersion 上限 999 → Int 全域（store 无上限、导入 version 任意，999 封顶会把高版本模板钳进降级闸永久拒绝的编辑死锁）；手册同步"≥1 整数"口径。
- **导出失败用例跨机化**：`Z:/nope.json` 盘符假设 → 指向已存在目录（Windows 打开写必失败），消除他机假绿/假红。
- **direction 单元格往返断言补挂**（deploy 面板三列口径全钉）；**覆盖版本口径就实**：手册明示"导入文件自带版本号在覆盖时不生效"（维持继承本机版本的既定裁定，仅补文档）。
- 测试：codec 数据驱动 +2 拒绝形态、widget 断言 3 处扩展；全量 ctest 46/46。


### Added（2026-10-16，#54 v2.13 任务模板编辑器）

- **任务模板编辑器（新建/编辑/删除产品内正规入口）**：任务中心页左栏上方新增「新建模板/删除模板」小工具条（新建恒可用、删除选中启用，删除确认框默认「否」），中栏摘要行下方新增「编辑模板」（选中启用），模板列表双击行=编辑同款。编辑器对话框 `TaskTemplateEditorDialog`：元信息（名称/描述/标签/version 1..999 手动可编辑、不自动 bump）、目标设备多选（本机档案 ∪ 模板引用并集，悬空引用「（本机无档案）」保留展示且默认勾选——与导入的悬空口径一致，建好同端点档案后自动接上）、步骤列表（添加按 deploy→commands→reboot→recover 轮转/删除/上移下移/类型切换即重置该步参数）、deploy 页（items 4 列表格即时写回 + 「添加文件…/添加目录…」非原生批量添加；`teRemoteDir` 仅为批量拼接前缀的 UI 辅助、**不是持久化参数键**；旧 `files`+`remoteDirectory` 简写加载即经 `canonicalDeployParameters` 归一为 items[]）、commands/recover 共用面板（多行命令一行一条、空行忽略；timeoutSec 1..600 默认 10；recover 顶部说明 label）、reboot 静态说明页；共享 `teProtocol` 协议过滤下拉在参数栈外按步读写。保存闸=validate（必填+步骤参数预检，不过弹警告停留对话框）→ collect 组装（templateId/unknownFields 原样携带）→ 编辑原位覆盖保 id、新建由 store 生成 id；删除经 `TaskTemplateStore::remove`（本次一并落地存储腿），历史执行记录为不可变快照不受影响。兑现 v2.11 规格 §4「产品内产出模板」承诺（部署侧一键捕获转后续增强）。
- 测试：`tst_task_template_store` +1 用例（remove 存储腿：删除生效/不存在 false/空 ID false）；`tst_task_center_widget` +9 用例（编辑器对话框 6：元信息与悬空引用往返/validate 拒绝空名空命令/简写归一/deploy 面板往返/commands 拆行与钳制/跨步切换改动存活〔含 Critical 修复回归〕；任务中心接线 2：三按钮存在与启用态+删除三态/applyEditorResult 落库编排；e2e 1：编辑器 collect 产物→applyEditorResult 落库生成 id→startTemplate 全链发起并收口落库，FTP-only 档案不触网口径）。全量 CTest 46 目标绿。

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

## [2.12.0] — 2026-10-14

### Added
- **WebSocket 双栏控制台重构**：上部连接/操作区 + 下部事件流（2000 行环形、方向/对端/字节数/内容预览，UTF-8 截断安全）；事件模型 `WsEventTypes` 纯逻辑独立可测；Token 凭证迁移 DPAPI 存储，写路径落盘时清扫旧明文行。
- **MODBUS 工具重构（多 PLC 主看偶写）**：设备身份统一至 DeviceRegistry 档案（档案含 modbus 端点=能力开关），目标档案持久化 `modbus.target`（旧 `modbus.slave` 一次性自动迁移、幂等跳过）；250ms 单驱动轮询调度器（每连接 FIFO 队列、写插队、在途保护、指数退避重连）；双栏目标清单 + 逐寄存器视图（变化高亮、跳过计数、退避着色）；QModbusTcpServer 本机环回集成测试。
- **文件管理增强（FTP/SFTP 部署双栏）**：元数据列（所有者/修改时间/权限，缺失显示 `—`；创建时间 FTP/SFTP 协议不可得，不设该列）；Ctrl+M 目录对比（元数据口径：size 主判 + 修改时间分钟辅助，差异着色 + 行对齐 + 占位行 + 仅显示差异过滤 + 汇总条，刷新自动退出）；F4 预览编辑器（UTF-8/GBK/UTF-16 BOM 自动探测 + 手动切换重解码、>2MB/二进制/混合行尾只读、保存保持原行尾、异步回传含远端变化冲突确认"仍要覆盖/放弃并刷新"）；libcurl/libssh2 内存读写原语（无临时文件中转，SylixOS 句柄纪律与既有传输一致）。

> 勘误（2026-10-17）：上文 WebSocket 条目「方向/对端/字节数/内容预览」中，事件流「对端」列随 v2.14 交付（#50-1，六列口径：时间|方向|主题|对端|字节|内容预览），当时表述先于实现；v2.12 落版事件流实为五列（无「对端」）。历史条目正文不改写，以本注记就实。

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
