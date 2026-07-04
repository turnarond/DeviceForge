# CMakeLists.txt for vsoa_node - VSOA Node Framework
#
# Date: 2026-04-29
# Author: Yan.chaodong <yanchaodong@acoinfo.com>
#
# Purpose: Build vsoa_node library providing VsoaNode base class for VSOA microservices

cmake_minimum_required(VERSION 3.16)

project(vsoa_node
    VERSION 1.0.0
    DESCRIPTION "VSOA Node Framework - Node abstraction layer for VSOA microservices"
    LANGUAGES CXX
)

# include(../../../CMakeCommonLib) - removed for standalone build

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 包含头文件目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${PROJECT_SOURCE_DIR}
    ${LW_COMMON_DIR}
    ${LW_INCLUDE_DIR}
    ${LW_SRC_DIR}/utils
)

link_directories(${LW_LIB_DIR})

# 源文件列表
set(VSOA_NODE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/VsoaNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LogFormatter.cpp
)

# 构建 vsoa_node 库
add_library(vsoa_node SHARED ${VSOA_NODE_SOURCES})

# Windows DLL 导出所有符号
if(WIN32)
    set_target_properties(vsoa_node PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
endif()

# 预编译头 — 加速编译 30-50%（nlohmann/json 仅编译一次）
target_precompile_headers(vsoa_node PRIVATE
    <string> <vector> <map> <set> <sstream> <fstream>
    <memory> <functional> <mutex> <thread> <atomic> <chrono>
    <cstdint> <cstring> <iostream>
    <platform_sdk/server.h>
    <platform_sdk/client.h>
    <platform_sdk/master.h>
    <platform_sdk/master_cli.h>
    <platform_sdk/init.h>
    <nlohmann/json.hpp>
    <lwcomm/lwcomm.h>
)

# 链接依赖库
target_link_libraries(vsoa_node PUBLIC
    lwserverbase
    platform_sdk
)

# Windows: vsoa.lib contains all VSOA components
# Linux/SylixOS: vsoa-json is separate
if(WIN32)
    target_link_libraries(vsoa_node PUBLIC vsoa vsoa_dto)
else()
    target_link_libraries(vsoa_node PUBLIC vsoa-json)
endif()

if(UNIX)
    target_link_libraries(vsoa_node PRIVATE pthread)
endif()

# ---- install ----
install(
    TARGETS vsoa_node
    LIBRARY DESTINATION ${LW_LIB_DIR}
    ARCHIVE DESTINATION ${LW_LIB_DIR}
    RUNTIME DESTINATION ${LW_BIN_DIR}
)
install(FILES
    VsoaNode.h NodeConfig.h HealthState.h
    LogFormatter.h
    DESTINATION ${LW_INCLUDE_DIR}/lwserverbase/vsoa_node)
#DISABLED message(STATUS "Install vsoa_node headers to ${LW_INCLUDE_DIR}/lwserverbase/vsoa_node")
#DISABLED 
#DISABLED # ---- 编译后自动复制头文件（无需手动 install） ----
#DISABLED add_custom_command(TARGET vsoa_node POST_BUILD
#DISABLED     COMMAND ${CMAKE_COMMAND} -E make_directory "${LW_INCLUDE_DIR}/lwserverbase/vsoa_node"
#DISABLED     COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/VsoaNode.h" "${LW_INCLUDE_DIR}/lwserverbase/vsoa_node/"
#DISABLED     COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/NodeConfig.h" "${LW_INCLUDE_DIR}/lwserverbase/vsoa_node/"
#DISABLED     COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/HealthState.h" "${LW_INCLUDE_DIR}/lwserverbase/vsoa_node/"
#DISABLED     COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/LogFormatter.h" "${LW_INCLUDE_DIR}/lwserverbase/vsoa_node/"
#DISABLED     COMMENT "Copying vsoa_node headers to ${LW_INCLUDE_DIR}/lwserverbase/vsoa_node/"
#DISABLED )
#DISABLED 
#DISABLED # 构建示例
#DISABLED target_link_libraries(vsoa_node_example PRIVATE vsoa_node)
#DISABLED 
#DISABLED # 构建测试
#DISABLED option(BUILD_TESTS "Build vsoa_node tests" OFF)
#DISABLED if(BUILD_TESTS)
#DISABLED     enable_testing()
#DISABLED 
#DISABLED     # NodeConfig 测试
#DISABLED     add_executable(test_vsoa_node_config tests/TestNodeConfig.cpp)
#DISABLED     target_link_libraries(test_vsoa_node_config PRIVATE vsoa_node)
#DISABLED 
#DISABLED     # LogFormatter 测试
#DISABLED     add_executable(test_vsoa_node_log tests/TestLogFormatter.cpp)
#DISABLED     target_link_libraries(test_vsoa_node_log PRIVATE vsoa_node)
#DISABLED 
#DISABLED     # HealthState 测试
#DISABLED     add_executable(test_vsoa_node_health tests/TestHealthState.cpp)
#DISABLED     target_link_libraries(test_vsoa_node_health PRIVATE vsoa_node)
#DISABLED 
#DISABLED     # 测试
#DISABLED     add_test(NAME VsoaNodeConfigTests COMMAND test_vsoa_node_config)
#DISABLED     add_test(NAME VsoaNodeLogTests COMMAND test_vsoa_node_log)
#DISABLED     add_test(NAME VsoaNodeHealthTests COMMAND test_vsoa_node_health)
#DISABLED 
#DISABLED     # 集成测试（需要 vsoa_master 运行时环境）
#DISABLED     # add_executable(test_vsoa_node_integration tests/TestIntegration.cpp)
#DISABLED     # target_link_libraries(test_vsoa_node_integration PRIVATE vsoa_node)
#DISABLED endif()
#DISABLED 
#DISABLED message(STATUS "==========================================")
#DISABLED message(STATUS "Project: ${PROJECT_NAME} ${PROJECT_VERSION}")
#DISABLED message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
#DISABLED message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
#DISABLED message(STATUS "==========================================")
