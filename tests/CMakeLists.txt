find_package(Qt6 REQUIRED COMPONENTS Core Network Test Sql SerialBus)

set(NETRELAY_DIR ${CMAKE_SOURCE_DIR}/src/tools/NetRelayTool)

add_executable(tst_nrec
    NetRelayTool/tst_nrec.cpp
    ${NETRELAY_DIR}/RelayRecorder.cpp
    ${NETRELAY_DIR}/RelayRecording.cpp
    ${NETRELAY_DIR}/RelayPlayer.cpp
)
target_include_directories(tst_nrec PRIVATE
    ${NETRELAY_DIR}
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_nrec PRIVATE Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME tst_nrec COMMAND tst_nrec)

# 让 ctest 直接运行时能找到 Qt 运行时 DLL（否则 Windows 报 0xc0000135 DLL_NOT_FOUND）
get_target_property(_qt_core_dll Qt6::Core IMPORTED_LOCATION_RELEASE)
if(NOT _qt_core_dll)
    get_target_property(_qt_core_dll Qt6::Core IMPORTED_LOCATION)
endif()
if(_qt_core_dll)
    get_filename_component(_qt_bin_dir "${_qt_core_dll}" DIRECTORY)
    get_filename_component(_qt_prefix "${_qt_bin_dir}" DIRECTORY)
    set(_qt_plugin_dir "${_qt_prefix}/plugins")
    set_tests_properties(tst_nrec PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}")
endif()

# --- NetRelayTool 组播/转发地址校验单元测试（纯逻辑，仅依赖 NetRelayTypes.h 头）---
add_executable(tst_relay_addr
    NetRelayTool/tst_relay_addr.cpp
)
target_include_directories(tst_relay_addr PRIVATE
    ${NETRELAY_DIR}
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_relay_addr PRIVATE Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME tst_relay_addr COMMAND tst_relay_addr)
if(_qt_bin_dir)
    set_tests_properties(tst_relay_addr PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}")
endif()

# --- DpapiCrypto 单元测试（Windows DPAPI + base64；非 Windows 为 no-op stub）---
add_executable(tst_dpapi_crypto
    config/tst_dpapi_crypto.cpp
    ${CMAKE_SOURCE_DIR}/src/config/DpapiCrypto.cpp
)
target_include_directories(tst_dpapi_crypto PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_dpapi_crypto PRIVATE Qt6::Core Qt6::Test)
if(WIN32)
    target_link_libraries(tst_dpapi_crypto PRIVATE crypt32)
endif()
add_test(NAME tst_dpapi_crypto COMMAND tst_dpapi_crypto)
if(_qt_bin_dir)
    set_tests_properties(tst_dpapi_crypto PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}")
endif()

# --- ConfigStore 单元测试（SQLite + JSON 导入导出）---
add_executable(tst_config_store
    config/tst_config_store.cpp
    ${CMAKE_SOURCE_DIR}/src/config/ConfigStore.cpp
)
target_include_directories(tst_config_store PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_config_store PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
add_test(NAME tst_config_store COMMAND tst_config_store)
if(_qt_bin_dir)
    set_tests_properties(tst_config_store PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${_qt_bin_dir};QT_PLUGIN_PATH=set:${_qt_plugin_dir}")
endif()

# --- OPC UA 编码隔离测试（不连服务器，纯本机验证 open62541 编码路径）---
add_executable(tst_opcua_encode opcua_encode/tst_opcua_encode.c)
target_link_libraries(tst_opcua_encode PRIVATE open62541)
add_test(NAME tst_opcua_encode COMMAND tst_opcua_encode)

# --- OPC UA 环回隔离测试（进程内起服务端，验证客户端类正确性，与远端无关）---
add_executable(tst_opcua_loopback opcua_encode/tst_opcua_loopback.cpp)
target_link_libraries(tst_opcua_loopback PRIVATE open62541)
add_test(NAME tst_opcua_loopback COMMAND tst_opcua_loopback)

# --- 在线更新检查单元测试（版本解析 + 比较 + ReleaseInfo 默认值）---
add_executable(tst_updatechecker tst_updatechecker.cpp)
target_include_directories(tst_updatechecker PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_updatechecker PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_updatechecker COMMAND tst_updatechecker)
if(_qt_bin_dir)
    set_tests_properties(tst_updatechecker PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}")
endif()

# --- SFTP 上传规划纯逻辑测试（不连服务器）---
# 注：planFolderUpload 定义于 SshAdapter.cpp，故测试目标需编译该 TU 并链接
#     libssh2/lwlog/Qt6::Network（SshAdapter.h 引用 QTcpSocket 头），
#     运行时需 libssh2.dll（PATH 前插 ${CMAKE_SOURCE_DIR}/lib）
add_executable(tst_sftp_plan
    sftp/tst_sftp_plan.cpp
    ${CMAKE_SOURCE_DIR}/src/adapter/SshAdapter.cpp
)
target_include_directories(tst_sftp_plan PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_sftp_plan PRIVATE
    Qt6::Core Qt6::Network Qt6::Test
    ${LIBSSH2_LIB}
    lwlog
)
add_test(NAME tst_sftp_plan COMMAND tst_sftp_plan)
if(_qt_bin_dir)
    set_tests_properties(tst_sftp_plan PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${_qt_bin_dir};PATH=path_list_prepend:${CMAKE_SOURCE_DIR}/lib")
endif()

# --- 部署循环 mock 测试（不连服务器，验证设备迭代/取消/失败收集）---
# 注：brief 片段只列测试源，实际还需编译 FtpDeployBackend.cpp（startUpload
#     定义）+ ProtocolRegistry.cpp（工厂注册/创建）+ FtpAdapter.cpp（部署循环
#     dynamic_cast<FtpAdapter*> 需要其 RTTI/vtable）+ FtpListParser.cpp（被
#     FtpAdapter.cpp 引用），并链接 Qt6::Concurrent（QtConcurrent::run）+
#     lwserverbase（ServiceTask 生命周期 + lwlog）+ libcurl（FtpAdapter 依赖）。
add_executable(tst_deploy_loop
    deploy/tst_deploy_loop.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/FtpDeployTool/FtpDeployBackend.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/FtpDeployTool/DeployJob.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/FtpDeployTool/FtpListParser.cpp
    ${CMAKE_SOURCE_DIR}/src/adapter/ProtocolRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/adapter/FtpAdapter.cpp
)
target_include_directories(tst_deploy_loop PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_deploy_loop PRIVATE
    Qt6::Core Qt6::Concurrent Qt6::Test
    lwserverbase
    ${LIBCURL_IMPL_LIB}
)
add_test(NAME tst_deploy_loop COMMAND tst_deploy_loop)
if(_qt_bin_dir)
    set_tests_properties(tst_deploy_loop PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}")
endif()

# --- RemoteFileModel 排序语义测试（纯 model，无网络）---
add_executable(tst_remote_model remote_model/tst_remote_model.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/FtpDeployTool/RemoteFileModel.cpp
)
target_include_directories(tst_remote_model PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_remote_model PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME tst_remote_model COMMAND tst_remote_model)
if(_qt_bin_dir)
    set_tests_properties(tst_remote_model PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}")
endif()

# --- 主题工具测试（QSS 资源与路径映射）---
add_executable(tst_theme theme/tst_theme.cpp
    ${CMAKE_SOURCE_DIR}/src/app/DeviceForge.qrc
)
target_include_directories(tst_theme PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_theme PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME tst_theme COMMAND tst_theme)
if(_qt_bin_dir)
    set_tests_properties(tst_theme PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}")
endif()

# --- 双主题像素验证（#18：离屏渲染采样 + 色值字符串断言保底）---
# 渲染依赖 QPA 平台插件（QApplication），故同样前插 Qt bin 并显式指定插件路径
add_executable(tst_qss_pixels theme/tst_qss_pixels.cpp
    ${CMAKE_SOURCE_DIR}/src/app/DeviceForge.qrc
)
target_include_directories(tst_qss_pixels PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_qss_pixels PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
add_test(NAME tst_qss_pixels COMMAND tst_qss_pixels)
if(_qt_bin_dir)
    set_tests_properties(tst_qss_pixels PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${_qt_bin_dir};QT_PLUGIN_PATH=set:${_qt_plugin_dir}")
endif()

# --- 文件源抽象测试（LocalFileSource 纯本地操作） ---
# 注：RemoteFileSource 依赖适配器/协议注册，测试仅在 Task 4 集成后手动验证
add_executable(tst_file_source file_source/tst_file_source.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/LocalFileSource.cpp
)
target_include_directories(tst_file_source PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_file_source PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_file_source COMMAND tst_file_source)
if(_qt_bin_dir)
    set_tests_properties(tst_file_source PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}")
endif()

# --- 面板异步加载测试（代际令牌竞态/重连重试；mock 源注入 FileBrowserPanel）---
# 注：与 brief 片段差异——测试仅引用 FileBrowserPanel/RemoteFileModel/LocalFileSource，
#     RemoteFileSource/FtpAdapter/SshAdapter/ProtocolRegistry 不被 mock 测试触及，
#     故不编译（避免引入 libcurl/libssh2/lwlog/Qt6::Network 链接依赖）
add_executable(tst_panel_async
    panel_async/tst_panel_async.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/FileBrowserPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/LocalFileSource.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/FtpDeployTool/RemoteFileModel.cpp
)
target_include_directories(tst_panel_async PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_panel_async PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent Qt6::Test
)
add_test(NAME tst_panel_async COMMAND tst_panel_async)
if(_qt_bin_dir)
    set_tests_properties(tst_panel_async PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}")
endif()

# --- Modbus 寄存器类型映射 + 数量上限纯逻辑测试（不连设备）---
# 注：仅引用 ModbusBackend.h 内的 inline 纯函数，不编译 ModbusBackend.cpp；
#     但头文件依赖 ToolBackend.h（lwserverbase）与 QModbusTcpClient（SerialBus），
#     故链接 lwserverbase（静态库，无符号被引用则不会拉入对象）+ Qt6::SerialBus
add_executable(tst_modbus_mapping modbus/tst_modbus_mapping.cpp)
target_include_directories(tst_modbus_mapping PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_modbus_mapping PRIVATE
    Qt6::Core Qt6::SerialBus Qt6::Test
    lwserverbase
)
add_test(NAME tst_modbus_mapping COMMAND tst_modbus_mapping)
if(_qt_bin_dir)
    set_tests_properties(tst_modbus_mapping PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}")
endif()

# --- Telnet 超时/免认证逻辑测试（本地 QTcpServer，不连外部设备）---
# 注：编译 TelnetAdapter.cpp（Pimpl 实现），链接 lwcommunicate（LWTcpClient，
#     INTERFACE 拉入 lwcomm/lwlog/lwevent）+ lwlog；TcpServer 握手由内核完成，
#     无需事件循环，接受连接但不回包即可驱动"响应超时"路径
add_executable(tst_telnet_timeout
    telnet/tst_telnet_timeout.cpp
    ${CMAKE_SOURCE_DIR}/src/adapter/TelnetAdapter.cpp
)
target_include_directories(tst_telnet_timeout PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_telnet_timeout PRIVATE
    Qt6::Core Qt6::Network Qt6::Test
    lwcommunicate lwlog
)
add_test(NAME tst_telnet_timeout COMMAND tst_telnet_timeout)
if(_qt_bin_dir)
    set_tests_properties(tst_telnet_timeout PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}")
endif()

# --- Updater 文件操作原语测试（issue #22：备份守卫的前提语义 = copyDirectory 失败必须返回 false）---
# 编入产品源码 UpdaterFileOps.cpp（纯 Win32 API，无 Qt 依赖；QtTest 仅作测试壳）
add_executable(tst_updater_fileops
    updater/tst_updater_fileops.cpp
    ${CMAKE_SOURCE_DIR}/src/updater/UpdaterFileOps.cpp
)
target_include_directories(tst_updater_fileops PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_updater_fileops PRIVATE
    Qt6::Core Qt6::Test
)
add_test(NAME tst_updater_fileops COMMAND tst_updater_fileops)
if(_qt_bin_dir)
    set_tests_properties(tst_updater_fileops PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}")
endif()

# --- FtpListParser LIST 解析格式矩阵测试（issue #21：最易碎纯逻辑部件的回归防线）---
# 编入产品源码 FtpListParser.cpp（仅依赖同目录 header-only 的 FtpFileInfo.h，无网络）
add_executable(tst_ftplist_parser
    ftplist/tst_ftplist_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/FtpDeployTool/FtpListParser.cpp
)
target_include_directories(tst_ftplist_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/tools/FtpDeployTool
)
target_link_libraries(tst_ftplist_parser PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_ftplist_parser COMMAND tst_ftplist_parser)
if(_qt_bin_dir)
    set_tests_properties(tst_ftplist_parser PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}")
endif()

# --- 部署报告数据结构与 CSV/HTML 渲染纯函数测试（v2.8 并行批量部署 Task 1）---
# 编入产品源码 DeployReport.cpp（仅标准库，无 Qt/网络依赖；QtTest 仅作测试壳）
add_executable(tst_deploy_report
    deploy/tst_deploy_report.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/FtpDeployTool/DeployReport.cpp
)
target_include_directories(tst_deploy_report PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/tools/FtpDeployTool
)
target_link_libraries(tst_deploy_report PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_deploy_report COMMAND tst_deploy_report)
if(_qt_bin_dir)
    set_tests_properties(tst_deploy_report PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}")
endif()

# --- DeploymentRunner 并发调度测试（v2.8 并行批量部署 Task 3，零网络）---
# 编入产品源码 DeploymentRunner.cpp + DeployJob.cpp（run() 实际执行路径）+
#     ProtocolRegistry.cpp（mock 工厂注册/创建）。DeployJob 的 useFtps 分支引用
#     FtpAdapter::setUseFtps 符号，故须编入 FtpAdapter.cpp+FtpListParser.cpp 并链接
#     libcurl 导入库（仅满足链接，测试走 mock 适配器不触网）
add_executable(tst_deploy_runner
    deploy/tst_deploy_runner.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/FtpDeployTool/DeploymentRunner.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/FtpDeployTool/DeployJob.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/FtpDeployTool/FtpListParser.cpp
    ${CMAKE_SOURCE_DIR}/src/adapter/ProtocolRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/adapter/FtpAdapter.cpp
)
target_include_directories(tst_deploy_runner PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(tst_deploy_runner PRIVATE
    Qt6::Core Qt6::Test
    ${LIBCURL_IMPL_LIB}
)
add_test(NAME tst_deploy_runner COMMAND tst_deploy_runner)
if(_qt_bin_dir)
    set_tests_properties(tst_deploy_runner PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}")
endif()
