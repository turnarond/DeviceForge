#pragma once
#include <QString>

class DpapiCrypto {
public:
    // 加密 plain → base64 字符串；非 Windows 平台返回 plain + log warning
    static QString protect(const QString& plain);
    // 解密 cipher → 明文；非 Windows 平台返回 cipher + log warning
    static QString unprotect(const QString& cipher);
};
