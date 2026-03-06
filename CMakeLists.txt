cmake_minimum_required(VERSION 3.16)

# 项目名称和语言
project(DeployMaster VERSION 1.0 LANGUAGES CXX)

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

# 列出源文件
set(SOURCES
    main.cpp
    DeployMaster.cpp
	DropListWidget.cpp
	FileDeploy.cpp
	FtpManager.cpp
	TelnetClient.cpp
	OpcUaClient.cpp
	TelnetDeploy.cpp
	LogQueryTab.cpp
	ModbusCluster.cpp
	WebSocketClient.cpp
	DiagnosticClient.cpp
	tinyvsoa/tinyvsoa.cpp
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/tinyvsoa)


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
	${LIBCURL_IMPL_LIB} 
)

# （可选）设置资源文件或翻译文件
# qt6_add_resources(RESOURCES myapp.qrc)
# target_sources(DeployMaster PRIVATE ${RESOURCES})