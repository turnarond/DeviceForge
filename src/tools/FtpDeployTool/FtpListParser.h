/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: FtpListParser.h
 * Date: 2026-07-25
 * Author: turnarond
 *
 * Description: FTP LIST 响应解析器 — 自动检测 Unix (ls -l) / Windows (dir) 格式，
 *              解析为统一的 FtpFileInfo 列表。跳过 . 和 .. 条目。
 */

#pragma once
#include "FtpFileInfo.h"
#include <vector>
#include <string>

class FtpListParser {
public:
    /// 解析 FTP LIST 命令的原始文本输出，返回结构化文件列表
    static std::vector<FtpFileInfo> parse(const std::string& rawList);

private:
    /// 尝试按 Unix "ls -l" 格式解析一行: -rw-r--r-- 1 user group 4096 Jul 20 14:30 name
    static bool tryParseUnixLine(const std::string& line, FtpFileInfo& out);

    /// 尝试按 Windows "dir" 格式解析一行: 07/20/2026  02:30 PM    1,234,567 name
    static bool tryParseWindowsLine(const std::string& line, FtpFileInfo& out);

    /// 判断一行是否为 Unix 格式（以 - d l c b p s 开头）
    static bool looksLikeUnix(const std::string& line);

    /// 从 "Jul 20 14:30" 或 "Jul 20  2025" 转为 ISO 8601
    static std::string normalizeUnixDateTime(const std::string& monthAbbr,
                                              const std::string& day,
                                              const std::string& yearOrTime);
};
