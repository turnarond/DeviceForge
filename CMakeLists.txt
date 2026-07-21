cmake_minimum_required(VERSION 3.22)

# 项目名称和版本
project(DeviceForge VERSION 2.2.0 LANGUAGES C CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 全局 PDB 共享标志（防止并行编译 C1041）
add_compile_options(/FS)
add_link_options(/DEBUG)

# ============================================================
# 运行时库一致性配置
# 预编译静态库（lwlog/lwcomm 等）使用 Release 运行时（MD）
# 为确保链接兼容，统一使用 Release 运行时，包括 Debug 配置
# ============================================================
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")  # /MD (Release runtime)

# ============================================================
# Qt 6 配置（使用 qt_standard_project_setup 实现完整 Qt 集成）
# Qt 6.8+ 推荐此方式
# ============================================================

# 必须在 qt_standard_project_setup 之前设置
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR})

# 全局编译选项：MSVC 使用 UTF-8 源代码/执行字符集，
# 允许源码中包含中文字符串字面量和注释(否则 GBK 代码页下会 C4819 警告 + 编译失败)
if(MSVC)
    add_compile_options(/utf-8)
endif()

# 查找 Qt6 包（按需添加模块）
# 注意：当前系统安装的是 Qt 6.11.1，请勿随意降级
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    SerialBus
    WebSockets
    Concurrent
    Sql
)
qt_standard_project_setup(REQUIRES 6.8)

# ============================================================
# libcurl（FTP 文件传输）
# ============================================================
set(LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)
find_library(LIBCURL_IMPL_LIB
    NAMES libcurl_imp libcurl_imp.lib
    PATHS ${LIB_DIR}
    NO_DEFAULT_PATH
)
if(NOT LIBCURL_IMPL_LIB)
    message(FATAL_ERROR "Could not find libcurl_imp.lib in ${LIB_DIR}")
endif()

# libssh2（SSH 适配器底层依赖，软依赖——未找到时仅警告，构建继续）
find_library(LIBSSH2_LIB
    NAMES libssh2
    PATHS ${LIB_DIR}
    NO_DEFAULT_PATH
)
if(NOT LIBSSH2_LIB)
    message(WARNING "libssh2.lib not found in ${LIB_DIR} — SSH adapter will not link")
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

# ============================================================
# 系统头文件目录（全局可见）
# ============================================================
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include  # libcurl (curl/curl.h)
    ${CMAKE_CURRENT_SOURCE_DIR}/src      # 框架头文件 (framework/DeviceInfo.h, adapter/ProtocolRegistry.h 等)
    ${CMAKE_CURRENT_SOURCE_DIR}/src/thirdparty
)

# ============================================================
# 源文件列表
# ============================================================
set(SOURCES
    # 旧架构源文件（根目录）
    main.cpp
    DeployMaster.cpp
    OpcUaClient.cpp
    DeployMaster.qrc  # Qt 资源（RCC 自动编译）

    # 日志桥接
    src/logging/LogBridge.cpp

    # 协议适配器
    src/adapter/FtpAdapter.cpp
    src/adapter/TelnetAdapter.cpp
    src/adapter/SshAdapter.cpp
    src/adapter/OpcUaAdapter.cpp
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
    src/tools/OpcUaClientTool/OpcUaClientBackend.cpp
    src/tools/OpcUaClientTool/OpcUaClientWidget.cpp

    # 在线更新模块
    src/updater/UpdateChecker.cpp
    src/updater/UpdateDialog.cpp

    # 本地配置存储（ConfigStore v2.3）
    src/config/DpapiCrypto.cpp
    src/config/ConfigStore.cpp

    # Thirdparty 源文件
    src/thirdparty/tinyxml2/tinyxml2.cpp
    src/thirdparty/nanopb/pb_encode.c
    src/thirdparty/nanopb/pb_decode.c
    src/thirdparty/nanopb/pb_common.c
    src/thirdparty/open62541/open62541.c
)

# ============================================================
# 创建可执行文件（qt_add_executable 替代 add_executable）
# WIN32：GUI 子系统，不弹 console 黑框
# ============================================================
qt_add_executable(DeviceForge
    WIN32
    ${SOURCES}
    DeployMaster.rc  # Windows 资源（图标+版本信息）
)

# 设置所有配置类型的调试信息生成（包括 RelWithDebInfo）
# RelWithDebInfo 默认不生成 PDB，需要显式启用才能调试
set_target_properties(DeviceForge PROPERTIES
    DEBUG_POSTFIX "d"
    RELWITHDEBINFO_POSTFIX ""
)

# --- open62541 (LGPL/MPL, 单文件分发版 v1.5.5) ---
add_subdirectory(src/thirdparty/open62541)

# ============================================================
# Qt 资源文件（Qt6 官方推荐 qt_add_resources）
# ============================================================
qt_add_resources(DeviceForge
    DeployMaster.qrc
)

# 链接 Qt 模块 + thirdparty 库
set(QT_LIBS
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Concurrent Qt6::Sql
)
if(TARGET Qt6::SerialBus)
    list(APPEND QT_LIBS Qt6::SerialBus)
endif()
if(TARGET Qt6::WebSockets)
    list(APPEND QT_LIBS Qt6::WebSockets)
endif()
target_link_libraries(DeviceForge PRIVATE
    ${QT_LIBS}
    lwlog
    lwcomm
    lwcommunicate
    lwserverbase
    tinyxml2
    ${LIBCURL_IMPL_LIB}
    ${LIBSSH2_LIB}
    open62541
)
if(WIN32)
    target_link_libraries(DeviceForge PRIVATE crypt32)
endif()

# ============================================================
# 在线更新模块 — 版本宏定义
# ============================================================
target_compile_definitions(DeviceForge PRIVATE
    DEVICEFORGE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    DEVICEFORGE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    DEVICEFORGE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

# ============================================================
# Updater.exe — 独立 Win32 文件替换进程（不链接 Qt）
# ============================================================
add_executable(Updater
    src/updater/UpdaterMain.cpp
)
set_target_properties(Updater PROPERTIES WIN32_EXECUTABLE ON)

# Updater.exe 输出到 DeviceForge 输出目录(便于部署包打包)
add_custom_command(TARGET DeviceForge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Updater>"
        "$<TARGET_FILE_DIR:DeviceForge>"
    COMMENT "Copying Updater.exe to DeviceForge output directory"
)

# 构建后复制 libcurl-x64.dll 为 libcurl.dll 到输出目录（运行时依赖）
# 注意：导入库 libcurl_imp.lib 指向的 DLL 名是 libcurl.dll，必须改名复制
add_custom_command(TARGET DeviceForge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${LIB_DIR}/libcurl-x64.dll"
        "$<TARGET_FILE_DIR:DeviceForge>/libcurl.dll"
    COMMENT "Copying libcurl-x64.dll as libcurl.dll to output directory"
)

# 构建后复制 libssh2.dll 到输出目录（SSH 适配器运行时依赖）
add_custom_command(TARGET DeviceForge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${LIB_DIR}/libssh2.dll"
        "$<TARGET_FILE_DIR:DeviceForge>"
    COMMENT "Copying libssh2.dll to output directory"
)

# ============================================================
# 测试（QtTest + CTest，仅 CMake 构建，不影响 VS/vcxproj 工程）
# ============================================================
enable_testing()
add_subdirectory(tests)
