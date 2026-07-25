/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: FtpFileInfo.h
 * Date: 2026-07-25
 * Author: turnarond
 *
 * Description: FTP 文件信息结构体 — FtpListParser 解析 LIST 命令输出后的统一表示。
 */

#pragma once
#include <string>
#include <cstdint>

struct FtpFileInfo {
    std::string name;         // 文件/目录名
    bool        isDir = false;
    uint64_t    size = 0;
    std::string permissions;  // Unix: "rw-r--r--", Windows: ""
    std::string dateTime;     // ISO 8601 "2026-07-20 14:30:00"
};
