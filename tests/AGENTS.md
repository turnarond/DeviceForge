# tests — QtTest/CTest 单元测试

51 个测试目标（v2.12 MODBUS 重构 +4 = 43；文件管理增强 +2 = 45；v2.13 模板编解码 +1 = 46；v2.14 部署模板映射 +1 = 47；v2.14 步骤参数校验 +1 = 48；v2.14 双屏窗口放置 +1 = 49；v2.15 波2 文件夹支持 +2 = 51，计数定版 2026-10-20；波2 计划初稿写 50 系未计任务9 新增 `tst_deploy_entry_builder`），**子目录名 = 被测模块**，目标名 `tst_<模块>`。

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

## 约定（TDD 红-绿-重构）

1. **红灯先行**：新功能/修 bug 先写失败用例，再实现；修复类必须先复现缺陷
2. **可测性设计**：纯逻辑收敛为自由函数/独立头（参照 `NetRelayTypes.h`、Modbus 映射），Backend 依赖注入适配器，Widget 逻辑尽量下沉
3. 测试类继承 QObject + private slots；数据驱动用 `QTest::addColumn/addRow`
4. 异步行为用 QSignalSpy/QEventLoop + 超时兜底，禁止 sleep 式等待（参照 tst_panel_async）
5. 禁止为绿灯删测试/skip 关键断言

## 命令

```bash
cd build && ctest -C Release --output-on-failure            # 全量
cd build && ctest -C Release -R tst_nrec --output-on-failure # 单目标
```

## 已知覆盖空白

FtpAdapter/TelnetAdapter 协议交互、ToolRegistry/ToolHost、NavBar 尚无专属目标（DeviceBusWidget 已由 tst_task_center_widget 覆盖）——新增相关行为时优先在此补测。模板编辑器对话框（v2.13 #54）的 tePageDeploy 文件选择器 leg：teAddDir 的**目录展开逻辑自 v2.15 波2 起已直测**（addDeployDirectoryEntries 直调缝经 tst_task_center_widget 覆盖展开/镜像预填/根退化与超上限拒绝，装配语义由 tst_deploy_entry_builder 直测），**仅余 QFileDialog 选人腿**（teAddDir 选目录与 teAddFiles 选文件弹窗）与新建/编辑按钮的模态对话框流程为人工冒烟覆盖，其余（validate/collect/面板往返/落库编排/e2e「新建→执行」链）均经 tst_task_center_widget 直调缝覆盖。部署页「保存为模板」按钮（v2.14 #49-2）→TaskTemplateEditorDialog 模态流（含同名冲突三分支弹窗）为人工冒烟覆盖（对话框 exec() 沿 #54 teAddFiles 先例不设自动化 UI 目标），映射/落库腿已由 tst_deploy_template_map / tst_task_template_store 直调覆盖，其中**文件夹条目**的展开映射腿自 v2.15 波2 起由 tst_deploy_entry_builder（装配）+ tst_task_center_widget（条目表直调）覆盖。
