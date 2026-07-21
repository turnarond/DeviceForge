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
