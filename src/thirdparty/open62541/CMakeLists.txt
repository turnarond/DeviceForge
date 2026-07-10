# open62541 OPC UA 客户端库（单文件分发版 v1.5.5）
#
# 重要：功能开关由 open62541.h 顶部的 #define 块直接控制（单文件分发版特性），
# 不能通过 -DUA_ENABLE_X=0 覆盖——open62541 用 #ifdef/defined() 判断，
# 传 =0 反而会“定义”该宏、激活对应代码路径。故此处不传任何 UA_ENABLE_* 定义。
# 已在 open62541.h 中关闭 UA_ENABLE_ENCRYPTION_MBEDTLS（移除 mbedTLS 依赖，首期无加密）。
add_library(open62541 STATIC
    open62541.c
)
target_include_directories(open62541 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Windows 下 amalgamation 的 POSIX 架构守卫已在头文件处理（#ifndef _WIN32）
if(WIN32)
    target_link_libraries(open62541 PUBLIC ws2_32 iphlpapi)
endif()
