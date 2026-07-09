cmake_minimum_required(VERSION 3.16)

# 项目名称和语言
project(DeviceForge VERSION 2.1.0 LANGUAGES C CXX)

# 启用 AUTOMOC、AUTOUIC、AUTORCC（自动处理 Qt 的 moc/uic/rcc）
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 Qt6 包（按需添加模块）
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    SerialBus
    WebSockets
)

set(LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)
find_library(LIBCURL_IMPL_LIB
    NAMES libcurl_imp libcurl_imp.lib
    PATHS ${LIB_DIR}
    NO_DEFAULT_PATH
)
if(NOT LIBCURL_IMPL_LIB)
    message(FATAL_ERROR "Could not find libcurl_imp.lib in ${LIB_DIR}")
endif()

# ============================================================
# Thirdparty 静态库（预编译 .lib，源码为 ACOINFO 内部库）
# 依赖链: lwcomm -> lwevent -> lwmsgq -> lwlog -> lwcommunicate -> lwserverbase
# ============================================================
set(THIRDPARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/thirdparty)

# --- lwcomm ---
add_library(lwcomm STATIC IMPORTED)
set_target_properties(lwcomm PROPERTIES IMPORTED_LOCATION "${LIB_DIR}/lwcomm.lib")
target_include_directories(lwcomm INTERFACE ${THIRDPARTY_DIR}/lwcomm ${THIRDPARTY_DIR})
target_compile_definitions(lwcomm INTERFACE lwcomm_EXPORTS)
if(WIN32)
    target_link_libraries(lwcomm INTERFACE ws2_32 iphlpapi)
endif()

# --- lwevent ---
add_library(lwevent STATIC IMPORTED)
set_target_properties(lwevent PROPERTIES IMPORTED_LOCATION "${LIB_DIR}/lwevent.lib")
target_include_directories(lwevent INTERFACE ${THIRDPARTY_DIR}/lwevent ${THIRDPARTY_DIR})
# lwevent 为静态库，无需 EXPORTS 宏
target_link_libraries(lwevent INTERFACE lwcomm)

# --- lwmsgq ---
add_library(lwmsgq STATIC IMPORTED)
set_target_properties(lwmsgq PROPERTIES IMPORTED_LOCATION "${LIB_DIR}/lwmsgq.lib")
target_include_directories(lwmsgq INTERFACE ${THIRDPARTY_DIR}/lwmsgq ${THIRDPARTY_DIR})
target_link_libraries(lwmsgq INTERFACE lwcomm lwevent)

# --- tinyxml2（MIT License，保留源码） ---
add_library(tinyxml2 STATIC
    ${THIRDPARTY_DIR}/tinyxml2/tinyxml2.cpp
)
target_include_directories(tinyxml2 PUBLIC ${THIRDPARTY_DIR}/tinyxml2 ${THIRDPARTY_DIR})

# --- lwlog ---
add_library(lwlog STATIC IMPORTED)
set_target_properties(lwlog PROPERTIES IMPORTED_LOCATION "${LIB_DIR}/lwlog.lib")
target_include_directories(lwlog INTERFACE ${THIRDPARTY_DIR}/lwlog ${THIRDPARTY_DIR})
target_compile_definitions(lwlog INTERFACE lwlog_EXPORTS)
target_link_libraries(lwlog INTERFACE lwcomm tinyxml2)

# --- lwcommunicate ---
add_library(lwcommunicate STATIC IMPORTED)
set_target_properties(lwcommunicate PROPERTIES IMPORTED_LOCATION "${LIB_DIR}/lwcommunicate.lib")
target_include_directories(lwcommunicate INTERFACE ${THIRDPARTY_DIR}/lwcommunicate ${THIRDPARTY_DIR})
target_link_libraries(lwcommunicate INTERFACE lwcomm lwlog lwevent)

# --- nanopb（zlib License，保留源码） ---
add_library(nanopb STATIC
    ${THIRDPARTY_DIR}/nanopb/pb_encode.c
    ${THIRDPARTY_DIR}/nanopb/pb_decode.c
    ${THIRDPARTY_DIR}/nanopb/pb_common.c
)
target_include_directories(nanopb PUBLIC ${THIRDPARTY_DIR}/nanopb ${THIRDPARTY_DIR})

# --- lwserverbase ---
add_library(lwserverbase STATIC IMPORTED)
set_target_properties(lwserverbase PROPERTIES IMPORTED_LOCATION "${LIB_DIR}/lwserverbase.lib")
target_include_directories(lwserverbase INTERFACE ${THIRDPARTY_DIR}/lwserverbase ${THIRDPARTY_DIR})
target_link_libraries(lwserverbase INTERFACE lwcomm lwlog lwmsgq lwevent)

# 列出源文件
set(SOURCES
    main.cpp
    DeployMaster.cpp
    OpcUaClient.cpp

    # Qt 资源（QRC：darkstyle.qss 深色主题，AUTORCC 处理）
    DeployMaster.qrc

    # 日志桥接
    src/logging/LogBridge.cpp

    # 协议适配器
    src/adapter/FtpAdapter.cpp
    src/adapter/TelnetAdapter.cpp
    src/adapter/ProtocolRegistry.cpp

    # 新框架文件
    src/framework/AppState.cpp
    src/framework/ManifestParser.cpp
    src/framework/ToolRegistry.cpp
    src/framework/ToolHost.cpp
    src/framework/ToolWidget.cpp

    # 新模型文件
    src/model/FtpManager.cpp

    # UI 组件
    src/ui/DeviceBusWidget.cpp

    # Tool 实现
    src/tools/ModbusTool/ModbusBackend.cpp
    src/tools/ModbusTool/ModbusWidget.cpp
    src/tools/FtpDeployTool/FtpDeployBackend.cpp
    src/tools/FtpDeployTool/FtpDeployWidget.cpp
    src/tools/TelnetTool/TelnetBackend.cpp
    src/tools/TelnetTool/TelnetWidget.cpp
    src/tools/WebSocketTool/WebSocketBackend.cpp
    src/tools/WebSocketTool/WebSocketWidget.cpp
    src/tools/NetRelayTool/NetRelayBackend.cpp
    src/tools/NetRelayTool/NetRelayWidget.cpp
    src/tools/NetRelayTool/RelayRecorder.cpp
    src/tools/NetRelayTool/RelayRecording.cpp
    src/tools/NetRelayTool/RelayPlayer.cpp

    # 新Presenter文件
    # src/presenter/FtpPresenter.cpp — 临时禁用: QPointer<FtpManager> 类型错误（待重构）
    # src/presenter/ModbusPresenter.cpp — 临时禁用: 桩代码依赖未就绪（待 Task 13）
)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)


# 创建可执行文件
add_executable(DeviceForge ${SOURCES})

# 链接 Qt 模块
target_link_libraries(DeviceForge PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::SerialBus
    Qt6::WebSockets
    lwlog
    lwcomm
    lwcommunicate
    lwserverbase
    tinyxml2
    ${LIBCURL_IMPL_LIB}
)

# 构建后复制 libcurl-x64.dll 为 libcurl.dll 到输出目录（运行时依赖）
# 注意：导入库 libcurl_imp.lib 指向的 DLL 名是 libcurl.dll，必须改名复制
# （与 vcxproj 的 PostBuild `copy libcurl-x64.dll libcurl.dll` 保持一致）
add_custom_command(TARGET DeviceForge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${LIB_DIR}/libcurl-x64.dll"
        "$<TARGET_FILE_DIR:DeviceForge>/libcurl.dll"
    COMMENT "Copying libcurl-x64.dll as libcurl.dll to output directory"
)

# （可选）设置资源文件或翻译文件
# qt6_add_resources(RESOURCES myapp.qrc)
# target_sources(DeviceForge PRIVATE ${RESOURCES})

# ============================================================
# 测试（QtTest + CTest，仅 CMake 构建，不影响 VS/vcxproj 工程）
# ============================================================
enable_testing()
add_subdirectory(tests)