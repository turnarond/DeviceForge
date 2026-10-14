/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: FileEncoding.cpp
 * Date: 2026-10-14
 * Author: turnarond
 *
 * Description: 编码探测/转换与冲突谓词实现（GBK/System 走 Win32 代码页；
 *              UTF-8 严格校验用自带 DFA——MultiByteToWideChar(65001) 不支持
 *              MB_ERR_INVALID_CHARS，无法当校验器用）。
 */

#include "ui/FileEncoding.h"
#include "ui/DirCompare.h"
#include "tools/FtpDeployTool/FtpFileInfo.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace {

QString normalizedCrlf(const QString& s)
{
    QString out = s;
    out.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    return out;
}

void codecFromUtf16Bytes(const QByteArray& raw, bool bigEndian, QString& outText)
{
    // 输入已过 BOM 判定；此处剥离可能存在的 BOM 后按端序两两成对
    int off = 0;
    if (raw.size() >= 2) {
        const uchar b0 = static_cast<uchar>(raw[0]);
        const uchar b1 = static_cast<uchar>(raw[1]);
        if ((b0 == 0xFF && b1 == 0xFE) || (b0 == 0xFE && b1 == 0xFF))
            off = 2;
    }
    const int n = (raw.size() - off) / 2;
    QVector<ushort> units(n);
    for (int i = 0; i < n; ++i) {
        const uchar lo = static_cast<uchar>(raw[off + 2 * i]);
        const uchar hi = static_cast<uchar>(raw[off + 2 * i + 1]);
        units[i] = bigEndian ? (static_cast<ushort>(hi) << 8 | lo)
                             : (static_cast<ushort>(lo) | static_cast<ushort>(hi) << 8);
    }
    outText = normalizedCrlf(QString::fromUtf16(units.constData(), n));
}

} // namespace

bool utf8StrictValidate(const char* data, int len)
{
    int i = 0;
    while (i < len) {
        const unsigned char c = static_cast<unsigned char>(data[i]);
        int extra = 0;
        unsigned int cp = 0;
        if (c < 0x80) { ++i; continue; }
        else if ((c >> 5) == 0x6) { extra = 1; cp = c & 0x1Fu; }
        else if ((c >> 4) == 0xE) { extra = 2; cp = c & 0x0Fu; }
        else if ((c >> 3) == 0x1E) { extra = 3; cp = c & 0x07u; }
        else return false;
        if (i + extra >= len) return false;             // 截断序列
        for (int k = 1; k <= extra; ++k) {
            const unsigned char b = static_cast<unsigned char>(data[i + k]);
            if ((b & 0xC0u) != 0x80u) return false;    // 后续字节必须是 continuation
            cp = (cp << 6) | (b & 0x3Fu);
        }
        if (extra == 1 && cp < 0x80u) return false;    // 过长编码
        if (extra == 2 && cp < 0x800u) return false;
        if (extra == 3 && (cp < 0x10000u || cp > 0x10FFFFu)) return false;
        if (cp >= 0xD800u && cp <= 0xDFFFu) return false;   // lone surrogate
        i += extra + 1;
    }
    return true;
}

bool gbkDecodeable(const QByteArray& raw)
{
    if (raw.isEmpty())
        return true;
    const int need = MultiByteToWideChar(936, MB_ERR_INVALID_CHARS,
                                         raw.constData(), raw.size(), nullptr, 0);
    if (need == 0)
        return false;
    QVector<wchar_t> buf(need);
    return MultiByteToWideChar(936, MB_ERR_INVALID_CHARS, raw.constData(), raw.size(),
                               buf.data(), need) == need;
}

TextProbe probeTextBuffer(const QByteArray& raw)
{
    TextProbe p;
    // BOM 判定先于二进制启发：UTF-16 文本天然含大量 0x00，不能误判二进制
    if (raw.size() >= 2) {
        const uchar b0 = static_cast<uchar>(raw[0]);
        const uchar b1 = static_cast<uchar>(raw[1]);
        if (b0 == 0xFF && b1 == 0xFE) { p.codec = QStringLiteral("UTF-16LE"); p.fromBom = true; return p; }
        if (b0 == 0xFE && b1 == 0xFF) { p.codec = QStringLiteral("UTF-16BE"); p.fromBom = true; return p; }
    }
    if (raw.size() >= 3) {
        const uchar b0 = static_cast<uchar>(raw[0]);
        const uchar b1 = static_cast<uchar>(raw[1]);
        const uchar b2 = static_cast<uchar>(raw[2]);
        if (b0 == 0xEF && b1 == 0xBB && b2 == 0xBF) {
            p.codec = QStringLiteral("UTF-8");
            p.fromBom = true;
            // 继续走文本形态判定（BOM 后内容仍可二进制）
        }
    }

    const int scan = qMin(raw.size(), 8192);
    int nul = 0, ctrl = 0, printable = 0;
    for (int i = 0; i < scan; ++i) {
        const uchar c = static_cast<uchar>(raw[i]);
        if (c == 0) ++nul;
        else if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') ++ctrl;
        else ++printable;
    }
    if (nul > 0) { p.looksBinary = true; return p; }
    if (scan > 16 && static_cast<double>(ctrl) / static_cast<double>(scan) > 0.30) {
        p.looksBinary = true;
        return p;
    }

    // 行尾探测（CRLF 对计数 vs 独立 LF 计数）
    int crlf = 0, lf = 0;
    for (int i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\n') {
            if (i > 0 && raw[i - 1] == '\r') ++crlf; else ++lf;
        }
    }
    if (crlf > 0 && lf == 0) p.eol = QStringLiteral("CRLF");
    else if (crlf > 0 && lf > 0) p.eol = QStringLiteral("mixed");
    else p.eol = QStringLiteral("LF");

    if (p.fromBom) return p;   // UTF-8 BOM 分支：codec 已定

    if (utf8StrictValidate(raw.constData(), raw.size())) {
        p.codec = QStringLiteral("UTF-8");
        return p;
    }
    if (gbkDecodeable(raw)) {
        p.codec = QStringLiteral("GBK");
        return p;
    }
    p.codec = QStringLiteral("UTF-8");   // 兜底：替换字符解码，用户可手动切码
    return p;
}

bool decodeText(const QByteArray& raw, const QString& codec, QString& outText, QString& outErr)
{
    outText.clear();
    outErr.clear();

    if (codec == QStringLiteral("UTF-8")) {
        // 剥离 BOM 再解码（保存时经 encodeText(fromBom) 还原）
        int off = 0;
        if (raw.size() >= 3
            && static_cast<uchar>(raw[0]) == 0xEF && static_cast<uchar>(raw[1]) == 0xBB
            && static_cast<uchar>(raw[2]) == 0xBF)
            off = 3;
        outText = normalizedCrlf(QString::fromUtf8(raw.constData() + off, raw.size() - off));
        return true;   // 替换字符策略：不报错，切码权交给用户
    }
    if (codec == QStringLiteral("Latin-1")) {
        outText = normalizedCrlf(QString::fromLatin1(raw));
        return true;
    }
    if (codec == QStringLiteral("UTF-16LE") || codec == QStringLiteral("UTF-16BE")) {
        if ((raw.size() % 2) != 0 && raw.size() >= 2) {
            outErr = QStringLiteral("UTF-16 字节数为奇，无法解码");
            return false;
        }
        codecFromUtf16Bytes(raw, codec == QStringLiteral("UTF-16BE"), outText);
        return true;
    }

    // GBK(936) / System(GetACP)：严格模式解码
    const UINT cp = (codec == QStringLiteral("GBK"))
        ? 936u
        : (codec == QStringLiteral("System") ? GetACP() : 0u);
    if (cp == 0) {
        outErr = QStringLiteral("未知编码: %1").arg(codec);
        return false;
    }
    if (raw.isEmpty()) { outText.clear(); return true; }
    const int need = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS,
                                         raw.constData(), raw.size(), nullptr, 0);
    if (need <= 0) {
        outErr = QStringLiteral("无法用 %1 解码（存在非法字节，请尝试其他编码）").arg(codec);
        return false;
    }
    QVector<wchar_t> buf(need);
    MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, raw.constData(), raw.size(), buf.data(), need);
    outText = normalizedCrlf(QString::fromUtf16(reinterpret_cast<const ushort*>(buf.constData()), need));
    return true;
}

QByteArray encodeText(const QString& text, const QString& codec, const QString& eol, bool fromBom)
{
    QString body = text;
    if (eol == QStringLiteral("CRLF"))
        body.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));

    if (codec == QStringLiteral("UTF-8")) {
        QByteArray out;
        if (fromBom) out.append("\xEF\xBB\xBF", 3);
        out.append(body.toUtf8());
        return out;
    }
    if (codec == QStringLiteral("Latin-1"))
        return body.toLatin1();
    if (codec == QStringLiteral("UTF-16LE") || codec == QStringLiteral("UTF-16BE")) {
        const bool be = codec == QStringLiteral("UTF-16BE");
        QByteArray out;
        if (fromBom)
            out.append(be ? QByteArray("\xFE\xFF", 2) : QByteArray("\xFF\xFE", 2));
        const ushort* u = reinterpret_cast<const ushort*>(body.unicode());
        const int n = body.size();
        for (int i = 0; i < n; ++i) {
            const ushort v = u[i];
            if (be) { out.append(static_cast<char>(v >> 8)); out.append(static_cast<char>(v & 0xFF)); }
            else    { out.append(static_cast<char>(v & 0xFF)); out.append(static_cast<char>(v >> 8)); }
        }
        return out;
    }

    const UINT cp = (codec == QStringLiteral("GBK")) ? 936u
                  : (codec == QStringLiteral("System") ? GetACP() : 0u);
    if (cp == 0) return body.toUtf8();   // 未知候选：UTF-8 兜底

    QVector<wchar_t> wbuf(body.size());  // 尺寸必须按行尾还原后的 body 计（CRLF 会多字符）
    const int wlen = body.isEmpty() ? 0 :
        body.toWCharArray(wbuf.data());
    // 宽松转换 + used-default-char 检测：部分码页对 WC_ERR_INVALID_CHARS 行为
    // 不一致（CP936 实测失败），以 "?" 缺字符出现判定"有字符不可表示"→ UTF-8 兜底
    BOOL usedDefault = FALSE;
    const int need = WideCharToMultiByte(cp, 0,
                                         wbuf.constData(), wlen, nullptr, 0, nullptr, &usedDefault);
    if (need <= 0 || usedDefault)
        return body.toUtf8();   // 存在不可表示字符（如切码后混入生僻字）：UTF-8 兜底不丢内容
    QByteArray out(need, '\0');
    if (WideCharToMultiByte(cp, 0, wbuf.constData(), wlen,
                            out.data(), need, nullptr, &usedDefault) != need || usedDefault)
        return body.toUtf8();
    return out;
}

QStringList suggestedCodecs(const QString& detected)
{
    QStringList out;
    if (!detected.isEmpty())
        out << detected;
    for (const QString& c : { QStringLiteral("UTF-8"), QStringLiteral("GBK"),
                              QStringLiteral("System"), QStringLiteral("Latin-1") }) {
        if (!out.contains(c))
            out << c;
    }
    return out;
}

bool remoteChanged(const ConflictSnapshot& atOpen, const FtpFileInfo& now)
{
    if (atOpen.hasSize && now.size != atOpen.size)
        return true;
    if (!atOpen.mtime.isEmpty() && !now.dateTime.empty()
        && !mtimeMinuteEqual(atOpen.mtime.toStdString(), now.dateTime))
        return true;
    return false;
}
