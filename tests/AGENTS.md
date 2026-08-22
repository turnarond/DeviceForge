# tests — QtTest/CTest 单元测试

16 个测试目标，**子目录名 = 被测模块**，目标名 `tst_<模块>`。

## 目录 ↔ 被测模块映射

| 子目录 | 目标 | 被测物 | 类型 |
|---|---|---|---|
| NetRelayTool/ | tst_nrec | RelayRecorder/Recording/Player | 录制回放（编入产品源码） |
| NetRelayTool/ | tst_relay_addr | NetRelayTypes.h | 纯逻辑（仅头文件） |
| config/ | tst_config_store / tst_dpapi_crypto | ConfigStore / DpapiCrypto | SQLite 持久化 / DPAPI |
| deploy/ | tst_deploy_loop | IDeployable 部署循环 | 协议化逻辑 |
| file_source/ | tst_file_source | IFileSource Local 实现 | 文件系统 |
| modbus/ | tst_modbus_mapping | Modbus 地址映射 | 纯逻辑 |
| opcua_encode/ | tst_opcua_encode(.c) / tst_opcua_loopback | open62541 编解码 | C 语言直测 |
| panel_async/ | tst_panel_async | FileBrowserPanel 异步加载 | 异步竞态回归（代际令牌） |
| remote_model/ sftp/ telnet/ theme/ | 各自 tst_* | 远程模型/SFTP 计划/Telnet 超时/QSS 像素 | 见各子目录 |
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

FtpAdapter/TelnetAdapter 协议交互、ToolRegistry/ToolHost、DeviceBusWidget、NavBar 尚无专属目标——新增相关行为时优先在此补测。
