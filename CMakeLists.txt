cmake_minimum_required(VERSION 3.16)

# 项目名称和语言
project(DeployMaster VERSION 1.0 LANGUAGES C CXX)

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
# Thirdparty 静态库编译
# 依赖链: lwcomm -> lwevent -> lwmsgq -> lwlog -> lwcommunicate -> lwserverbase
# ============================================================
set(THIRDPARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/thirdparty)

# --- lwcomm（跨平台工具库） ---
file(GLOB LWCOMM_SOURCES ${THIRDPARTY_DIR}/lwcomm/src/*.cpp)
add_library(lwcomm STATIC ${LWCOMM_SOURCES})
target_include_directories(lwcomm
    PUBLIC  ${THIRDPARTY_DIR}/lwcomm ${THIRDPARTY_DIR}
    PRIVATE ${THIRDPARTY_DIR}/lwcomm/src
)
target_compile_definitions(lwcomm PUBLIC lwcomm_EXPORTS)
if(WIN32)
    target_link_libraries(lwcomm PUBLIC ws2_32 iphlpapi)
endif()

# --- lwevent（事件信号） ---
add_library(lwevent STATIC
    ${THIRDPARTY_DIR}/lwevent/lwevent.c
)
target_include_directories(lwevent PUBLIC ${THIRDPARTY_DIR}/lwevent ${THIRDPARTY_DIR})
target_compile_definitions(lwevent PRIVATE LWEVENT_EXPORTS)
target_link_libraries(lwevent PUBLIC lwcomm)

# --- lwmsgq（消息队列） ---
add_library(lwmsgq STATIC
    ${THIRDPARTY_DIR}/lwmsgq/lwmsgq.c
    ${THIRDPARTY_DIR}/lwmsgq/list.c
)
target_include_directories(lwmsgq PUBLIC ${THIRDPARTY_DIR}/lwmsgq ${THIRDPARTY_DIR})
target_link_libraries(lwmsgq PUBLIC lwcomm lwevent)

# --- tinyxml2（XML 解析） ---
# 必须放在 lwlog 之前，因为 lwlog 依赖 tinyxml2
add_library(tinyxml2 STATIC
    ${THIRDPARTY_DIR}/tinyxml2/tinyxml2.cpp
)
target_include_directories(tinyxml2 PUBLIC ${THIRDPARTY_DIR}/tinyxml2 ${THIRDPARTY_DIR})

# --- lwlog（日志库） ---
file(GLOB LWLOG_SOURCES ${THIRDPARTY_DIR}/lwlog/src/*.cpp)
add_library(lwlog STATIC ${LWLOG_SOURCES})
target_include_directories(lwlog
    PUBLIC  ${THIRDPARTY_DIR}/lwlog ${THIRDPARTY_DIR}
    PRIVATE ${THIRDPARTY_DIR}/lwlog/src
)
target_compile_definitions(lwlog PUBLIC lwlog_EXPORTS)
target_link_libraries(lwlog PUBLIC lwcomm tinyxml2)

# --- lwcommunicate（网络通信库） ---
file(GLOB LWCOMMUNICATE_ALL ${THIRDPARTY_DIR}/lwcommunicate/*.cpp)
list(FILTER LWCOMMUNICATE_ALL EXCLUDE REGEX "lwconn_test\\.cpp$")
add_library(lwcommunicate STATIC ${LWCOMMUNICATE_ALL})
target_include_directories(lwcommunicate PUBLIC ${THIRDPARTY_DIR}/lwcommunicate ${THIRDPARTY_DIR})
target_compile_definitions(lwcommunicate PRIVATE NOMINMAX)
target_link_libraries(lwcommunicate PUBLIC lwcomm lwlog lwevent)

# --- nanopb（Protobuf 编解码） ---
add_library(nanopb STATIC
    ${THIRDPARTY_DIR}/nanopb/pb_encode.c
    ${THIRDPARTY_DIR}/nanopb/pb_decode.c
    ${THIRDPARTY_DIR}/nanopb/pb_common.c
)
target_include_directories(nanopb PUBLIC ${THIRDPARTY_DIR}/nanopb ${THIRDPARTY_DIR})

# --- lwserverbase（服务框架） ---
file(GLOB_RECURSE LWSERVERBASE_ALL ${THIRDPARTY_DIR}/lwserverbase/*.cpp)
list(FILTER LWSERVERBASE_ALL EXCLUDE REGEX ".*/tests/.*")
list(FILTER LWSERVERBASE_ALL EXCLUDE REGEX ".*/examples/.*")
list(FILTER LWSERVERBASE_ALL EXCLUDE REGEX ".*/ConfigManager\\.cpp$")
list(FILTER LWSERVERBASE_ALL EXCLUDE REGEX ".*/ProcessController\\.cpp$")
list(FILTER LWSERVERBASE_ALL EXCLUDE REGEX ".*/vsoa_node/.*")
add_library(lwserverbase STATIC ${LWSERVERBASE_ALL})
target_include_directories(lwserverbase PUBLIC ${THIRDPARTY_DIR}/lwserverbase ${THIRDPARTY_DIR})
target_link_libraries(lwserverbase PUBLIC lwcomm lwlog lwmsgq lwevent)

# 列出源文件
set(SOURCES
    main.cpp
    DeployMaster.cpp
    DropListWidget.cpp
    TelnetClient.cpp
    OpcUaClient.cpp
    LogQueryTab.cpp
    ModbusCluster.cpp
    WebSocketClient.cpp

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
    src/tools/FtpDeployTool/FtpDeployBackend.cpp
    src/tools/FtpDeployTool/FtpDeployWidget.cpp
    src/tools/TelnetTool/TelnetBackend.cpp
    src/tools/TelnetTool/TelnetWidget.cpp

    # 新Presenter文件
    # src/presenter/FtpPresenter.cpp — 临时禁用: QPointer<FtpManager> 类型错误（待重构）
    # src/presenter/ModbusPresenter.cpp — 临时禁用: 桩代码依赖未就绪（待 Task 13）
)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)


# 创建可执行文件
add_executable(DeployMaster ${SOURCES})

# 链接 Qt 模块
target_link_libraries(DeployMaster PRIVATE
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

# 构建后复制 libcurl-x64.dll 到输出目录（运行时依赖）
add_custom_command(TARGET DeployMaster POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${LIB_DIR}/libcurl-x64.dll"
        "$<TARGET_FILE_DIR:DeployMaster>"
    COMMENT "Copying libcurl-x64.dll to output directory"
)

# （可选）设置资源文件或翻译文件
# qt6_add_resources(RESOURCES myapp.qrc)
# target_sources(DeployMaster PRIVATE ${RESOURCES})