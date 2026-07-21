#include "DpapiCrypto.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>

// CryptProtectData / CryptUnprotectData 需要完整签名；不可用 (DATA_BLOB*, DATA_BLOB*) 简化指针
static QByteArray dpapiProtect(const QByteArray& in)
{
    DATA_BLOB inBlob{};
    inBlob.cbData = static_cast<DWORD>(in.size());
    inBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(in.constData()));
    DATA_BLOB outBlob{};
    if (!CryptProtectData(&inBlob, nullptr, nullptr, nullptr, nullptr, 0, &outBlob)) {
        return {};
    }
    QByteArray out(reinterpret_cast<char*>(outBlob.pbData),
                   static_cast<int>(outBlob.cbData));
    LocalFree(outBlob.pbData);
    return out;
}

static QByteArray dpapiUnprotect(const QByteArray& in)
{
    DATA_BLOB inBlob{};
    inBlob.cbData = static_cast<DWORD>(in.size());
    inBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(in.constData()));
    DATA_BLOB outBlob{};
    if (!CryptUnprotectData(&inBlob, nullptr, nullptr, nullptr, nullptr, 0, &outBlob)) {
        return {};
    }
    QByteArray out(reinterpret_cast<char*>(outBlob.pbData),
                   static_cast<int>(outBlob.cbData));
    LocalFree(outBlob.pbData);
    return out;
}

QString DpapiCrypto::protect(const QString& plain)
{
    const QByteArray bin = dpapiProtect(plain.toUtf8());
    return QString::fromLatin1(bin.toBase64());
}

QString DpapiCrypto::unprotect(const QString& cipher)
{
    const QByteArray bin = QByteArray::fromBase64(cipher.toLatin1());
    const QByteArray plain = dpapiUnprotect(bin);
    return QString::fromUtf8(plain);
}

#else
// 非 Windows stub：返回原值 + warning（v2.3 仅支持 Windows）
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcDpapi, "deviceforge.dpapi")

QString DpapiCrypto::protect(const QString& plain)
{
    qCWarning(lcDpapi) << "DpapiCrypto::protect 在非 Windows 平台是 no-op（明文存储）";
    return plain;
}

QString DpapiCrypto::unprotect(const QString& cipher)
{
    qCWarning(lcDpapi) << "DpapiCrypto::unprotect 在非 Windows 平台是 no-op（明文返回）";
    return cipher;
}
#endif
