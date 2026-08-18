find_package(Qt6 REQUIRED COMPONENTS Core Network Test Sql)

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
