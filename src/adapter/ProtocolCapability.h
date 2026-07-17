#pragma once
#include <string>
#include <cstdint>
#include "framework/DeviceInfo.h"

// 协议能力声明 — 框架据此判断 Tool 需要的协议是否可用
struct ProtocolCapability {
    bool requestResponse  = true;   // 支持请求-响应模式（FTP、HTTP、Modbus）
    bool streaming        = false;  // 支持流模式（Telnet、WebSocket）
    bool broadcast        = false;  // 支持广播/多播（UDP）
    bool publishSubscribe = false;  // 支持发布/订阅（MQTT）
    int  maxConnections   = 1;      // 单适配器最大并发连接数
};

// 通用请求
struct Request {
    std::string path;       // 远程路径或命令
    std::string payload;    // 数据载荷
    int         timeoutMs = 5000;
};

// 通用响应
struct Response {
    bool        success     = false;
    std::string data;
    std::string errorMessage;
    int         statusCode  = 0;
};
