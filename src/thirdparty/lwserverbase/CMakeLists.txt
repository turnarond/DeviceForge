# CMakeLists.txt for lwserverbase framework
#
# Date: 2026-03-10
# Author: Yan.chaodong <yanchaodong@acoinfo.com>
#
# Purpose: Generate lwserverbase library and headers for microservices development
# Output: All artifacts placed under src/ext/lwserverbase directory

cmake_minimum_required(VERSION 3.16)

# 项目基本信息
project(lwserverbase
VERSION 1.0.0
DESCRIPTION "Lightweight service framework for microservices development"
LANGUAGES CXX
)

# include(../../CMakeCommonLib) - removed for standalone build

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

# 构建类型设置
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# 设置输出目录 - 全部放置在 lwserverbase 目录下
set(LWSERVERBASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# 包含头文件目录
include_directories(${PROJECT_ROOT}/include/libjson 
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}
    ${LW_COMMON_DIR}
    ${LW_INCLUDE_DIR}/libjson
)

# 源文件列表 - 合并为单一库
set(LWSERVERBASE_SOURCES
    ${LWSERVERBASE_DIR}/core/ServiceBase.cpp
    ${LWSERVERBASE_DIR}/core/ServiceManager.cpp
    ${LWSERVERBASE_DIR}/core/ServiceTask.cpp
    ${LWSERVERBASE_DIR}/config/ConfigManager.cpp
    ${LWSERVERBASE_DIR}/logging/Logger.cpp
    ${LWSERVERBASE_DIR}/metrics/MetricsCollector.cpp
    ${LWSERVERBASE_DIR}/process/ProcessController.cpp
)

link_directories(${LW_LIB_DIR})

# 构建单一的 lwserverbase 库 - 简化用户使用
add_library(lwserverbase SHARED ${LWSERVERBASE_SOURCES})

# Windows DLL 导出所有符号
if(WIN32)
    set_target_properties(lwserverbase PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
endif()

# 预编译头 — 加速编译 30-50%
target_precompile_headers(lwserverbase PRIVATE
    <string> <vector> <map> <set> <sstream> <fstream>
    <memory> <functional> <mutex> <thread> <atomic> <chrono>
    <cstdint> <cstring> <cctype> <csignal>
    <lwcomm/lwcomm.h> <lwlog/lwlog.h>
)

# 链接依赖库
if(${SYSTEM_NAME} MATCHES "windows")
    target_link_libraries(lwserverbase PRIVATE
        lwcomm
        lwevent
        lwlog
        vsoa
        ${TARGET_LIBRARIES}
    )
else()
    target_link_libraries(lwserverbase PRIVATE
        lwcomm
        lwevent
        lwlog
        vsoa-json
        pthread
    )
    if(SYLIXOS)
        target_link_libraries(lwserverbase PRIVATE ${TARGET_LIBRARIES})
    endif()
endif()

if(WIN32)
    # SCM APIs (StartServiceCtrlDispatcherA, RegisterServiceCtrlHandlerExA,
    # SetServiceStatus) live in advapi32. MSVC usually links it implicitly, but
    # be explicit so the new ServiceManager SCM code resolves at link time.
    target_link_libraries(lwserverbase PRIVATE advapi32)
endif()

# 版本配置头文件
configure_file(
    ${PROJECT_SOURCE_DIR}/cmake/lwserverbase_version.h.in
    ${LWSERVERBASE_DIR}/include/lwserverbase_version.h
    @ONLY
)

# 打印配置信息
message(STATUS "==========================================")
message(STATUS "Project: ${PROJECT_NAME} ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Output directory: ${LWSERVERBASE_DIR}")
message(STATUS "Library output: ${LWSERVERBASE_DIR}/lib")
message(STATUS "Binary output: ${LWSERVERBASE_DIR}/bin")
message(STATUS "==========================================")

install(
    TARGETS lwserverbase
    LIBRARY DESTINATION ${LW_LIB_DIR}
    ARCHIVE DESTINATION ${LW_LIB_DIR}
    RUNTIME DESTINATION ${LW_BIN_DIR}
)

# ---- install 公开头文件到 include/lwserverbase（保留目录结构） ----
install(FILES ${LWSERVERBASE_DIR}/core/ServiceBase.h           DESTINATION ${LW_INCLUDE_DIR}/lwserverbase/core)
install(FILES ${LWSERVERBASE_DIR}/core/ServiceManager.h        DESTINATION ${LW_INCLUDE_DIR}/lwserverbase/core)
install(FILES ${LWSERVERBASE_DIR}/core/ServiceTask.h           DESTINATION ${LW_INCLUDE_DIR}/lwserverbase/core)
install(FILES ${LWSERVERBASE_DIR}/config/ConfigManager.h       DESTINATION ${LW_INCLUDE_DIR}/lwserverbase/config)
install(FILES ${LWSERVERBASE_DIR}/logging/Logger.h             DESTINATION ${LW_INCLUDE_DIR}/lwserverbase/logging)
install(FILES ${LWSERVERBASE_DIR}/metrics/MetricsCollector.h   DESTINATION ${LW_INCLUDE_DIR}/lwserverbase/metrics)
install(FILES ${LWSERVERBASE_DIR}/process/ProcessController.h  DESTINATION ${LW_INCLUDE_DIR}/lwserverbase/process)
install(FILES ${LWSERVERBASE_DIR}/include/lwserverbase_version.h DESTINATION ${LW_INCLUDE_DIR}/lwserverbase)
message(STATUS "Install lwserverbase headers to ${LW_INCLUDE_DIR}/lwserverbase/{core,config,logging,metrics,process}")

# ---- 编译后自动复制头文件（无需手动 install） ----
add_custom_command(TARGET lwserverbase POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LW_INCLUDE_DIR}/lwserverbase/core"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LW_INCLUDE_DIR}/lwserverbase/config"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LW_INCLUDE_DIR}/lwserverbase/logging"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LW_INCLUDE_DIR}/lwserverbase/metrics"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LW_INCLUDE_DIR}/lwserverbase/process"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LWSERVERBASE_DIR}/core/ServiceBase.h" "${LW_INCLUDE_DIR}/lwserverbase/core/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LWSERVERBASE_DIR}/core/ServiceManager.h" "${LW_INCLUDE_DIR}/lwserverbase/core/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LWSERVERBASE_DIR}/core/ServiceTask.h" "${LW_INCLUDE_DIR}/lwserverbase/core/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LWSERVERBASE_DIR}/config/ConfigManager.h" "${LW_INCLUDE_DIR}/lwserverbase/config/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LWSERVERBASE_DIR}/logging/Logger.h" "${LW_INCLUDE_DIR}/lwserverbase/logging/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LWSERVERBASE_DIR}/metrics/MetricsCollector.h" "${LW_INCLUDE_DIR}/lwserverbase/metrics/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LWSERVERBASE_DIR}/process/ProcessController.h" "${LW_INCLUDE_DIR}/lwserverbase/process/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LWSERVERBASE_DIR}/include/lwserverbase_version.h" "${LW_INCLUDE_DIR}/lwserverbase/"
    COMMENT "Copying lwserverbase headers to ${LW_INCLUDE_DIR}/lwserverbase/"
)

# ==================== VSOA Node Framework ====================
SUBDIRS(vsoa_node)

# ==================== 示例配置 ====================
option(BUILD_EXAMPLES "Build lwserverbase examples" OFF)

if(BUILD_EXAMPLES)
    message(STATUS "Building examples...")
    
    # classic_service 示例
    add_executable(example_classic_service examples/example_classic_service.cpp)
    target_link_libraries(example_classic_service PRIVATE lwserverbase)
    set_target_properties(example_classic_service PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${LWSERVERBASE_DIR}/examples)

    # VSOA 系统监控节点示例 (需 VsoaNode，依赖 platform_sdk)
    add_executable(vsoa_system_monitor examples/vsoa_system_monitor.cpp)
    target_link_libraries(vsoa_system_monitor PRIVATE lwserverbase vsoa_node platform_sdk)
    set_target_properties(vsoa_system_monitor PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${LWSERVERBASE_DIR}/examples)

    message(STATUS "Examples configured.")
endif()

# ==================== 测试配置 ====================
option(BUILD_TESTS "Build lwserverbase tests" OFF)

if(BUILD_TESTS)
    message(STATUS "Building tests...")
    
    # Counter 测试
    target_link_libraries(test_counter PRIVATE lwserverbase)
    target_include_directories(test_counter PRIVATE ${LWSERVERBASE_DIR}/tests)
    
    # Gauge 测试
    target_link_libraries(test_gauge PRIVATE lwserverbase)
    target_include_directories(test_gauge PRIVATE ${LWSERVERBASE_DIR}/tests)
    
    # Histogram 测试
    target_link_libraries(test_histogram PRIVATE lwserverbase)
    target_include_directories(test_histogram PRIVATE ${LWSERVERBASE_DIR}/tests)
    
    # Prometheus 导出测试
    target_link_libraries(test_prometheus_export PRIVATE lwserverbase)
    target_include_directories(test_prometheus_export PRIVATE ${LWSERVERBASE_DIR}/tests)
    
    # 高级测试（性能、并发、边界情况）
    target_link_libraries(test_advanced PRIVATE lwserverbase)
    target_include_directories(test_advanced PRIVATE ${LWSERVERBASE_DIR}/tests)

    # v1.5.0 修复验证测试
    target_link_libraries(test_fixes PRIVATE lwserverbase)
    target_include_directories(test_fixes PRIVATE ${LWSERVERBASE_DIR}/tests)


    # 添加测试到 CTest
    enable_testing()

    
    # 设置测试输出目录
    set_target_properties(test_counter test_gauge test_histogram
                          test_prometheus_export test_advanced test_fixes
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${LWSERVERBASE_DIR}/tests
    )
    
    message(STATUS "Tests configured. Run 'ctest' to execute all tests.")
endif()
