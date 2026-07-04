// time_helper.cpp
#include "lwcomm/lwcomm.h"
#include <ctime>
#include <cstring>
#include <cstdio>
#ifndef _WIN32
#include <sys/time.h>
#endif

// 用于格式 "2008-01-01 12:12:12" 或 "2008-01-01 12:12:12.000"
#define DT_SIZE_TIMESTR 24

static int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
int LWTimeHelper::String2Timestamp(const char* time_str, int64_t* timestamp_ms) {
    if (!time_str || !timestamp_ms) {
        return EC_ICV_CC_PARAMINVALID;
    }

    int year, month, day, hour, minute, second, ms = 0;
    int fields = std::sscanf(time_str, "%d-%d-%d %d:%d:%d.%d", &year, &month, &day, &hour, &minute, &second, &ms);
    
    if (fields < 6) {
        return EC_ICV_CC_PARAMINVALID;
    }

    if (year < 1900 || year > 9999) return EC_ICV_CC_PARAMINVALID;
    if (month < 1 || month > 12) return EC_ICV_CC_PARAMINVALID;
    if (day < 1 || day > 31) return EC_ICV_CC_PARAMINVALID;
    if (hour < 0 || hour > 23) return EC_ICV_CC_PARAMINVALID;
    if (minute < 0 || minute > 59) return EC_ICV_CC_PARAMINVALID;
    if (second < 0 || second > 59) return EC_ICV_CC_PARAMINVALID;
    if (ms < 0 || ms > 999) return EC_ICV_CC_PARAMINVALID;

    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
        days_in_month[1] = 29; // 闰年
    }
    if (day > days_in_month[month - 1]) {
        return EC_ICV_CC_PARAMINVALID;
    }

    std::tm tm_ = {};
    tm_.tm_year = year - 1900;
    tm_.tm_mon = month - 1;
    tm_.tm_mday = day;
    tm_.tm_hour = hour;
    tm_.tm_min = minute;
    tm_.tm_sec = second;
    tm_.tm_isdst = -1; // 让系统判断

    std::time_t t = std::mktime(&tm_);
    // 只有year超出范围
    if (t == -1) {
        return EC_ICV_CC_PARAMINVALID;
    }

    *timestamp_ms = static_cast<int64_t>(t) * 1000 + ms;
    return LW_SUCCESS;
}

int LWTimeHelper::Timestamp2String(int64_t timestamp_ms, char* buffer, size_t buf_len, bool include_ms) {
    if (!buffer || buf_len == 0) {
        return EC_ICV_CC_PARAMINVALID;
    }

    int64_t sec = timestamp_ms / 1000;
    int ms = static_cast<int>(timestamp_ms % 1000);

    std::time_t t = static_cast<std::time_t>(sec);
    std::tm tm_;

#ifdef _WIN32
    if (localtime_s(&tm_, &t) != 0) {
        return EC_ICV_CC_PARAMINVALID;
    }
#else
    if (localtime_r(&t, &tm_) == nullptr) {
        return EC_ICV_CC_PARAMINVALID;
    }
#endif

    int len;
    if (include_ms) {
        len = std::snprintf(buffer, buf_len, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                            tm_.tm_year + 1900, tm_.tm_mon + 1, tm_.tm_mday,
                            tm_.tm_hour, tm_.tm_min, tm_.tm_sec, ms);
    } else {
        len = std::snprintf(buffer, buf_len, "%04d-%02d-%02d %02d:%02d:%02d",
                            tm_.tm_year + 1900, tm_.tm_mon + 1, tm_.tm_mday,
                            tm_.tm_hour, tm_.tm_min, tm_.tm_sec);
    }

    if (len < 0) {
        return EC_ICV_CC_PARAMINVALID;
    }
    if (len >= static_cast<int>(buf_len)) {
        buffer[buf_len - 1] = '\0'; // 确保 null-terminated
        return EC_ICV_COMM_BUFFERTOOSHORT;
    }

    return LW_SUCCESS;
}

int64_t LWTimeHelper::GetCurrentTimestampMs()
{
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    // FILETIME = 100ns intervals since 1601-01-01. Convert to ms since Unix epoch.
    return static_cast<int64_t>(uli.QuadPart / 10000ULL - 11644473600000ULL);
#else
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
#endif
}

int LWTimeHelper::GetCurrentTimeString(char* buffer, size_t buf_len, bool include_ms) 
{
    int64_t ts = GetCurrentTimestampMs();
    return Timestamp2String(ts, buffer, buf_len, include_ms);
}

int64_t LWTimeHelper::UtcString2Timestamp(const char* time_str) 
{
    if (!time_str) {
        return EC_ICV_CC_PARAMINVALID;
    }

    int year, month, day, hour, minute, second, ms = 0;
    int fields = std::sscanf(time_str, "%d-%d-%d %d:%d:%d.%d", &year, &month, &day, &hour, &minute, &second, &ms);

    if (fields < 6) {
        return EC_ICV_CC_PARAMINVALID;
    }

    if (year < 1900 || year > 9999) return EC_ICV_CC_PARAMINVALID;
    if (month < 1 || month > 12) return EC_ICV_CC_PARAMINVALID;
    if (day < 1 || day > 31) return EC_ICV_CC_PARAMINVALID;
    if (hour < 0 || hour > 23) return EC_ICV_CC_PARAMINVALID;
    if (minute < 0 || minute > 59) return EC_ICV_CC_PARAMINVALID;
    if (second < 0 || second > 59) return EC_ICV_CC_PARAMINVALID;
    if (ms < 0 || ms > 999) return EC_ICV_CC_PARAMINVALID;

    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
        days_in_month[1] = 29; // 闰年
    }
    if (day > days_in_month[month - 1]) {
        return EC_ICV_CC_PARAMINVALID;
    }

    std::tm tm_ = {};
    tm_.tm_year = year - 1900;
    tm_.tm_mon = month - 1;
    tm_.tm_mday = day;
    tm_.tm_hour = hour;
    tm_.tm_min = minute;
    tm_.tm_sec = second;
    tm_.tm_isdst = 0; // UTC 不考虑夏令时

    // 使用 timegm (Linux) 或 _mkgmtime (Windows) 将 UTC tm 转为 time_t
    std::time_t t;
#ifdef _WIN32
    t = _mkgmtime(&tm_);
#else
    t = timegm(&tm_);
#endif

    if (t == static_cast<std::time_t>(-1)) {
        return EC_ICV_CC_PARAMINVALID;
    }

    return static_cast<int64_t>(t) * 1000 + ms;
}

int LWTimeHelper::Timestamp2UtcString(int64_t timestamp_ms, char* buffer, size_t buf_len, bool include_ms /* = false */) 
{
    if (!buffer || buf_len == 0) {
        return EC_ICV_CC_PARAMINVALID;
    }

    int64_t sec = timestamp_ms / 1000;
    int ms = static_cast<int>(timestamp_ms % 1000);

    std::time_t t = static_cast<std::time_t>(sec);
    std::tm tm_;

#ifdef _WIN32
    if (gmtime_s(&tm_, &t) != 0) {
        return EC_ICV_CC_PARAMINVALID;
    }
#else
    if (gmtime_r(&t, &tm_) == nullptr) {
        return EC_ICV_CC_PARAMINVALID;
    }
#endif

    int len;
    if (include_ms) {
        len = std::snprintf(buffer, buf_len, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                            tm_.tm_year + 1900, tm_.tm_mon + 1, tm_.tm_mday,
                            tm_.tm_hour, tm_.tm_min, tm_.tm_sec, ms);
    } else {
        len = std::snprintf(buffer, buf_len, "%04d-%02d-%02d %02d:%02d:%02d",
                            tm_.tm_year + 1900, tm_.tm_mon + 1, tm_.tm_mday,
                            tm_.tm_hour, tm_.tm_min, tm_.tm_sec);
    }

    if (len < 0) {
        return EC_ICV_CC_PARAMINVALID;
    }
    if (len >= static_cast<int>(buf_len)) {
        buffer[buf_len - 1] = '\0'; // 确保 null-terminated
        return EC_ICV_COMM_BUFFERTOOSHORT;
    }

    return LW_SUCCESS;
}

int LWTimeHelper::GetCurrentUtcString(char* buffer, size_t buf_len, bool include_ms /* = false */) 
{
    int64_t ts = GetCurrentTimestampMs();
    return Timestamp2UtcString(ts, buffer, buf_len, include_ms);
}