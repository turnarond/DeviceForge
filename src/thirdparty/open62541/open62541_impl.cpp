// open62541 编译包装 — vcxproj/MSVC 用
//
// 功能开关由 open62541.h 顶部的 #define 块直接控制（单文件分发版特性）。
// 不要在此 #define UA_ENABLE_X 0——open62541 用 #ifdef/defined() 判断，
// 定义为 0 反而会激活对应代码路径。已在 open62541.h 关闭 mbedTLS 加密。
#include "open62541.c"
