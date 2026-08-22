/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: FtpListParser.cpp
 * Date: 2026-07-25
 * Author: turnarond
 *
 * Description: FTP LIST 响应解析器实现。
 */

#include "FtpListParser.h"
#include <sstream>
#include <regex>
#include <chrono>
#include <iomanip>
#include <algorithm>

// --- 格式检测 ---

bool FtpListParser::looksLikeUnix(const std::string& line)
{
    if (line.empty()) return false;
    char c = line[0];
    return c == '-' || c == 'd' || c == 'l' || c == 'c' || c == 'b' || c == 'p' || c == 's';
}

// --- 日期规范化 ---

std::string FtpListParser::normalizeUnixDateTime(const std::string& monthAbbr,
                                                  const std::string& day,
                                                  const std::string& yearOrTime)
{
    // 月份缩写 → 数字
    static const std::string months[] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul","Aug","Sep","Oct","Nov","Dec"
    };
    int mon = 0;
    for (int i = 0; i < 12; ++i) {
        if (monthAbbr == months[i]) { mon = i + 1; break; }
    }
    if (mon == 0) return {};

    int d = std::stoi(day);
    int year = 0;
    int hour = 0, min = 0, sec = 0;

    if (yearOrTime.find(':') != std::string::npos) {
        // "14:30" 格式 → 当年（线程安全，使用 std::chrono 获取当前年份）
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        struct tm result{};
        localtime_s(&result, &tt);
        year = result.tm_year + 1900;
        std::sscanf(yearOrTime.c_str(), "%d:%d", &hour, &min);
    } else {
        // "2025" 格式
        year = std::stoi(yearOrTime);
    }

    std::ostringstream oss;
    oss << std::setfill('0')
        << year << "-" << std::setw(2) << mon << "-" << std::setw(2) << d
        << " " << std::setw(2) << hour << ":" << std::setw(2) << min << ":" << std::setw(2) << sec;
    return oss.str();
}

// --- Unix 格式解析 ---
// 示例: drwxr-xr-x 2 user group 4096 Jul 20 14:30 filename
//       -rw-r--r-- 1 user group 1234567 Jul 20 14:30 filename

bool FtpListParser::tryParseUnixLine(const std::string& line, FtpFileInfo& out)
{
    if (!looksLikeUnix(line)) return false;

    // 分割为字段
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);

    if (tokens.size() < 9) return false;

    // 权限字段 (tokens[0])
    out.permissions = tokens[0].substr(1); // 去掉首字符（类型标识）
    // 注：符号链接 (类型 'l') 无法从 LIST 输出可靠判断目标是否为目录，
    // 此处统一标记为非目录。对目录符号链接双击将尝试 CWD 进入，失败时回退为文件下载
    out.isDir = (tokens[0][0] == 'd');

    // 跳过 user(group) size 等，找到日期位置
    // 标准格式: perms links user group size month day year|time name...
    // 我们需要尝试多种格式来定位 size/date 字段

    // 策略：找到月份缩写的位置
    size_t dateIdx = 0;
    for (size_t i = 1; i < tokens.size(); ++i) {
        for (const auto& m : {"Jan","Feb","Mar","Apr","May","Jun",
                               "Jul","Aug","Sep","Oct","Nov","Dec"}) {
            if (tokens[i] == m) { dateIdx = i; break; }
        }
        if (dateIdx > 0) break;
    }
    if (dateIdx < 2) return false;

    // size 在 dateIdx - 1 位置
    try {
        out.size = std::stoull(tokens[dateIdx - 1]);
    } catch (...) {
        return false;
    }

    // 日期: month day year|time
    // 文件名: tokens[dateIdx + 3] 开始（可能含空格）
    if (dateIdx + 3 >= tokens.size()) return false;

    out.dateTime = normalizeUnixDateTime(tokens[dateIdx], tokens[dateIdx + 1], tokens[dateIdx + 2]);

    // 文件名从日期第三字段之后开始，可能含空格。
    // 按 token 序推进到第 dateIdx+2 个字段末尾——容忍任意空格数与 tab
    // （标准 ls -l 对单数字日右对齐补双空格，旧实现用单空格拼接子串做精确查找会整行丢失）。
    {
        size_t pos = 0;
        size_t tok = 0;
        while (tok <= static_cast<size_t>(dateIdx) + 2 && pos < line.length()) {
            while (pos < line.length() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;   // 字段间空白
            while (pos < line.length() && line[pos] != ' ' && line[pos] != '\t') ++pos;     // 当前字段内容
            ++tok;
        }
        while (pos < line.length() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;       // 文件名前空白
        out.name = (pos < line.length()) ? line.substr(pos) : std::string();
        // 处理符号链接 "link -> target" — 只取 link 名
        auto arrowPos = out.name.find(" -> ");
        if (arrowPos != std::string::npos) {
            out.name = out.name.substr(0, arrowPos);
        }
    }

    return !out.name.empty();
}

// --- Windows 格式解析 ---
// 示例: 07/20/2026  02:30 PM         1,234,567 filename
//       07/20/2026  02:30 PM    <DIR>               dirname
// 日期部分也可能不含前导零: 7/20/2026

bool FtpListParser::tryParseWindowsLine(const std::string& line, FtpFileInfo& out)
{
    // Windows 格式特征：以数字开头（日期 MM/DD/YYYY）
    if (line.empty() || !std::isdigit(static_cast<unsigned char>(line[0])))
        return false;

    // 使用正则匹配: date time <DIR|size> name
    // 日期: \d{1,2}/\d{1,2}/\d{4}
    // 时间: \d{1,2}:\d{2}\s*(AM|PM)
    static const std::regex winRe(
        R"(^(\d{1,2}/\d{1,2}/\d{4})\s+(\d{1,2}:\d{2}\s*(?:AM|PM))\s+(<DIR>|[\d,]+)\s+(.+)$)",
        std::regex::icase
    );
    std::smatch m;
    if (!std::regex_match(line, m, winRe)) return false;

    std::string dateStr = m[1].str();
    std::string timeStr = m[2].str();
    std::string sizeOrDir = m[3].str();
    out.name = m[4].str();

    // . 和 .. 保留，由 RemoteFileModel 处理导航

    out.isDir = (sizeOrDir == "<DIR>");
    if (!out.isDir) {
        // 去掉数字中的逗号
        std::string numStr = sizeOrDir;
        numStr.erase(std::remove(numStr.begin(), numStr.end(), ','), numStr.end());
        try { out.size = std::stoull(numStr); } catch (...) { return false; }
    }

    // 日期转 ISO 8601
    int mo = 0, dy = 0, yr = 0;
    std::sscanf(dateStr.c_str(), "%d/%d/%d", &mo, &dy, &yr);
    int hr = 0, mn = 0;
    std::sscanf(timeStr.c_str(), "%d:%d", &hr, &mn);
    // 转换 PM 时间。注意：regex icase 只影响匹配，捕获组保留原始大小写
    // （小写服务器输出 "pm" 很常见），必须做大小写不敏感判断而非只查 'P'。
    const bool isPm = timeStr.find('P') != std::string::npos ||
                      timeStr.find('p') != std::string::npos;
    if (isPm) {
        if (hr != 12) hr += 12;
    } else if (hr == 12) {
        hr = 0;
    }

    std::ostringstream oss;
    oss << std::setfill('0')
        << yr << "-" << std::setw(2) << mo << "-" << std::setw(2) << dy
        << " " << std::setw(2) << hr << ":" << std::setw(2) << mn << ":00";
    out.dateTime = oss.str();

    return true;
}

// --- 公开入口 ---

std::vector<FtpFileInfo> FtpListParser::parse(const std::string& rawList)
{
    std::vector<FtpFileInfo> result;
    std::istringstream stream(rawList);
    std::string line;

    // 先检测格式：取第一条有效行判断
    std::vector<std::string> lines;
    while (std::getline(stream, line)) {
        if (!line.empty()) lines.push_back(line);
    }

    // 扫描所有行检测格式，跳过头行（如 "total N"）
    bool isUnix = false;
    bool detected = false;
    for (const auto& l : lines) {
        if (looksLikeUnix(l)) { isUnix = true; detected = true; break; }
        if (!l.empty() && std::isdigit(static_cast<unsigned char>(l[0]))) { isUnix = false; detected = true; break; }
    }
    if (!detected) return result; // 无法检测格式

    for (const auto& l : lines) {
        FtpFileInfo info;
        bool ok = isUnix ? tryParseUnixLine(l, info) : tryParseWindowsLine(l, info);
        if (ok) result.push_back(std::move(info));
    }

    return result;
}
