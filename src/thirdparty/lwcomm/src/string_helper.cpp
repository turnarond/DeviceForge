
/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: string_helper.cpp .
 *
 * Date: 2024-03-23
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#include <algorithm>

#include "lwcomm/lwcomm.h"
#include <stdint.h>
#include <string.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#endif

#define DT_SIZE_TIMESTR 24  /* strlen("2008-01-01 12:12:12.000") + 1 */

/**
 * @brief Convert std::string to integer.
 * @param szValue Input std::string (e.g., "123")
 * @return Converted integer, or 0 if invalid.
 */
int LWStringHelper::StringToInt(const char *szValue)
{
    if (!szValue || (strlen(szValue) == 0)) return 0;
    char *end;
    long val = strtol(szValue, &end, 10);
    return (*end == '\0') ? static_cast<int>(val) : 0; // 可加错误检查
}

int LWStringHelper::StringToIntEx(const char *szValue, int nDefault)
{
    if (!szValue || (strlen(szValue) == 0)) return nDefault;
    char *end;
    long val = strtol(szValue, &end, 10);
    return (*end == '\0') ? static_cast<int>(val) : nDefault;
}

/**
 *  Convert std::string to int.
 *
 *  @param  -[in]  char *  szValue: [string to be converted]
 *  @return result value.
 *
 *  @version.
 */
double LWStringHelper::StringToDouble(const char *szValue)
{
    if (!szValue || (strlen(szValue) == 0)) return 0.f;
    char *end;
    double val = strtod(szValue, &end);
    return (*end == '\0') ? val : 0.f;
}

void RegulateFPathName (char *szPathName, int lPathNameLen)
{
    if (!szPathName || lPathNameLen <= 0) return;
    for (int i = 0; i < lPathNameLen && szPathName[i]; ++i) {
        if (szPathName[i] == '/' || szPathName[i] == '\\') {
            szPathName[i] = LW_OS_DIR_SEPARATOR_CHAR;
        }
    }
}

/**
 * @brief Ignore case comparisons
 * @param szCmp1
 * @param szCmp2
 * @return int
 */
int LWStringHelper::StriCmp (const char *szCmp1, const char *szCmp2)
{
    if (szCmp1 == nullptr || szCmp2 == nullptr) {
        return (szCmp1 == szCmp2) ? 0 : (szCmp1 ? 1 : -1);
    }
    return strcasecmp(szCmp1, szCmp2);
}

std::vector<std::string> LWStringHelper::StriSplit (const std::string str, const std::string pattern)
{
    std::vector<std::string> res;
    size_t start = 0, end;
    while ((end = str.find(pattern, start)) != std::string::npos) {
        res.push_back(str.substr(start, end - start));
        start = end + pattern.size();
    }
    if (start < str.size()) res.push_back(str.substr(start));
    return res;
}

int LWStringHelper::StrEscape (const char *szSrc, char *szEscape, size_t nEscapeBuffLen, char cEscaped, char cEscaping)
{
    if (!szSrc || !szSrc[0] || !szEscape) {
        return EC_ICV_CC_PARAMINVALID;
    }

    // 使用 vector 管理内存
    size_t srcLen = strlen(szSrc);
    std::vector<char> tempBuf(srcLen * 2 + 1, 0);

    char *pIter = tempBuf.data();
    for (size_t i = 0; szSrc[i]; ++i) {
        if (szSrc[i] == cEscaped) {
            *pIter++ = cEscaping;
        }
        *pIter++ = szSrc[i];
    }
    *pIter = '\0';

    return SafeStrNCpy(szEscape, tempBuf.data(), nEscapeBuffLen);
}

/**
 * @brief
 * @param sBig
 * @param sSrc
 * @param sDst
 */
void LWStringHelper::Replace (std::string &sBig, const std::string &sSrc, const std::string &sDst)
{
    std::string::size_type pos = 0;
    std::string::size_type srclen = sSrc.size();

    if (srclen == 0) {
        return;
    }

    std::string::size_type dstlen = sDst.size();
    while ((pos = sBig.find(sSrc, pos)) != std::string::npos) {
        sBig.replace(pos, srclen, sDst);
        pos += dstlen;
    }
}

int LWStringHelper::SafeStrNCpy (char *szDest, const char *szSource, int nDestBuffLen)
{
    if (NULL == szDest) {
        return EC_ICV_CC_PARAMINVALID;
    }

    if (NULL == szSource) {
        szDest[0] = '\0';
        return LW_SUCCESS;
    }

    /*
     * the most common condition
     */
    if ((int)strlen(szSource) < nDestBuffLen) {
        strcpy(szDest, szSource);
        return LW_SUCCESS;
    }

    /*
     * strlen(szSource) >= nDestBuffLen, some will be trimed
     */
    strncpy(szDest, szSource, nDestBuffLen - 1);
    szDest[nDestBuffLen - 1] = '\0';

    return EC_ICV_COMM_BUFFERTOOSHORT;
}

static const char HEX[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
/**
 *  @param		-[in]  const unsigned char * szBuffer :
 *              A data buffer that needs to be converted to a 1xadecimal display
 *  @param		-[in]  unsigned int nBuffeLen : Buffer length
 *  @param		-[out]  char * szHexBuf :
 *              The 16-base output buffer, when the incoming is empty, returns the desired length via pnHexBufLen
 *  @param		-[out]  unsigned int * pnHexBufLen :
 *              The length of the 16decimal output buffer, when szHexBuf is empty, pnHexBufLen returns the desired length
 *  @return		unsigned int.
 *
 *  @version
 */
int LWStringHelper::HexDumpBuf (const char *szBuffer, unsigned int nCharBuffLen, char *szHexBuf, unsigned int nHexBufLen,
                                unsigned int *pnRetHexBufLen)
{
    if ((NULL == szBuffer) || (nHexBufLen <= 0)) {
        return -1;
    }

    if (NULL == szHexBuf) {
        if (pnRetHexBufLen) {
            *pnRetHexBufLen = 0;
        }
        return EC_ICV_COMM_BUFFERTOOSHORT;
    }

    int nMaxHoldBufLen = (nHexBufLen) / 3;
    if ((int)nCharBuffLen > nMaxHoldBufLen) {
        nCharBuffLen = nMaxHoldBufLen;
    }

    /*
     * assure there is a terminal character
     */
    for (unsigned int i = 0; i < nCharBuffLen; i++) {
        unsigned char t = szBuffer[i];
        szHexBuf[i * 3] = HEX[t / 16];
        szHexBuf[i * 3 + 1] = HEX[t % 16];
        if (i < nCharBuffLen - 1) {
            szHexBuf[i * 3 + 2] = ' '; // 最后一个字节后不加空格
        }
    }

    // 正确终止
    unsigned int totalLen = nCharBuffLen > 0 ? (3 * nCharBuffLen - 1) : 0;
    if (totalLen < nHexBufLen) {
        szHexBuf[totalLen] = '\0';
    } else {
        szHexBuf[nHexBufLen - 1] = '\0';
    }

    if (pnRetHexBufLen) {
        *pnRetHexBufLen = totalLen;
    }

    return LW_SUCCESS;
}

/**
 * @brief 将十六进制字符串解码为二进制数据
 *        支持格式: "ABCD", "AB CD", "AB:CD", "ab cd" 等
 *        奇数长度自动前导补零：如 "ABC" → "0ABC" → [0x0A, 0xBC]
 *
 * @param szHexStr      [in]  十六进制字符串（如 "AB CD EF" 或 "ABCDEF"）
 * @param szBuffer      [out] 输出缓冲区，用于存放解码后的字节
 * @param pnBufLen      [in/out] 输入：szBuffer 的容量；输出：实际写入的字节数
 *
 * @return LW_SUCCESS 成功
 *         EC_ICV_INVALID_PARAM 参数错误
 *         EC_ICV_COMM_BUFFERTOOSHORT 缓冲区太小
 *         EC_ICV_COMM_INVALIDHEXSTR  包含非法十六进制字符
 */
int LWStringHelper::HexStr2Buffer(const char *szHexStr, char *szBuffer, unsigned int *pnBufLen)
{
    // 参数校验
    if (!szHexStr || !pnBufLen) {
        return EC_ICV_CC_PARAMINVALID;
    }

    // 输入为空字符串
    size_t hexLen = strlen(szHexStr);
    if (hexLen == 0) {
        *pnBufLen = 0;
        return LW_SUCCESS;
    }

    // 计算期望的输出长度（忽略空白和分隔符）
    std::string hex;
    for (size_t i = 0; i < hexLen; ++i) {
        char c = szHexStr[i];
        if (isxdigit(c)) {
            hex += c;
        } else if (c != ' ' && c != '\t' && c != ':' && c != '-' && c != ',') {
            return EC_ICV_COMM_INVALIDHEXSTR; // 非法字符
        }
    }
    if (hex.empty()) {
        *pnBufLen = 0;
        return LW_SUCCESS; // 仅包含空白或分隔符
    }
    if (hex.length() % 2 != 0) {
        hex = '0' + hex; // 补齐为偶数长度
    }
    size_t expectedLen = (hex.length()) / 2; // 每两个十六进制字符对应一个字节

    // 如果 szBuffer 为空，只返回所需长度
    if (!szBuffer) {
        *pnBufLen = expectedLen;
        return EC_ICV_COMM_BUFFERTOOSHORT;
    }

    // 检查缓冲区是否足够
    if (*pnBufLen < expectedLen) {
        *pnBufLen = expectedLen;
        return EC_ICV_COMM_BUFFERTOOSHORT;
    }

    // 开始解析
    unsigned int byteIdx = 0;
    uint8_t currentByte = 0;
    bool highNibble = true; // true: 解析高位；false: 解析低位

    for (size_t i = 0; i < hex.length(); ++i) {
        char c = hex[i];

        if (!isxdigit(c)) {
            if (c == ' ' || c == '\t' || c == ':' || c == '-' || c == ',') {
                continue; // 忽略分隔符
            }
            return EC_ICV_COMM_INVALIDHEXSTR;
        }

        int value = (c >= 'A') ? ((c & 0xDF) - 'A' + 10) : (c - '0'); // 统一转大写

        if (highNibble) {
            currentByte = value << 4;
            highNibble = false;
        } else {
            currentByte |= value;
            szBuffer[byteIdx++] = static_cast<char>(currentByte);
            highNibble = true;
        }
    }

    // 最后一个字节未完成（奇数个十六进制字符）
    if (!highNibble) {
        szBuffer[byteIdx++] = static_cast<char>(currentByte);
    }

    *pnBufLen = byteIdx;
    return LW_SUCCESS;
}

/*
 * end
 */
