/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: FileEncoding.h
 * Date: 2026-10-14
 * Author: turnarond
 *
 * Description: 文本编码探测/转换与远端冲突谓词（v2.12 F4 预览编辑器内核）—
 *              纯逻辑，无 Widgets 依赖，可独立单测（tests/file_encoding）。
 *              项目未链 Qt6::Core5Compat，GBK/System 编解码走 Win32 代码页 API。
 */

#pragma once
#include <QString>
#include <QStringList>
#include <cstdint>
#include <string>

struct FtpFileInfo;

// 编辑上限：超过即拒绝进内存编辑（规格 §4.2）
constexpr uint64_t kMaxEditBytes = 2 * 1024 * 1024;

// 探测结果：codec ∈ UTF-8 / GBK / UTF-16LE / UTF-16BE；eol ∈ LF / CRLF / mixed
struct TextProbe {
    bool looksBinary = false;
    QString codec = QStringLiteral("UTF-8");
    bool fromBom = false;
    QString eol = QStringLiteral("LF");
};

TextProbe probeTextBuffer(const QByteArray& raw);

// 严格 UTF-8 校验（BOM 无关；ASCII 通过）
bool utf8StrictValidate(const char* data, int len);

// GBK 可解码性试探（CP936 + MB_ERR_INVALID_CHARS；空输入平凡可解）
bool gbkDecodeable(const QByteArray& raw);

// 解码为编辑器文本（内部行尾一律归一为 LF，保存时按原 eol 还原）。
// GBK/System/Latin-1/UTF-16 非法输入 → 返回 false 且 outErr 置原因；
// UTF-8 分支按替换字符解码、不报错（探测失败时给用户手动切码留路）。
bool decodeText(const QByteArray& raw, const QString& codec, QString& outText, QString& outErr);

// 编码回字节：text 内部 LF → 按 eol 还原行尾 → 代码页编码；fromBom 时前置原 BOM。
// 码页存在不可表示字符（或未知候选码页）时兜底改按 UTF-8 编码整篇返回——
// fellBackToUtf8（可选，默认不取）非空时回传该决策：true=发生了兜底换码，
// 调用方（保存链）须向用户明示，保证编码下拉显示与实际写盘编码一致（#48）。
QByteArray encodeText(const QString& text, const QString& codec, const QString& eol, bool fromBom,
                      bool *fellBackToUtf8 = nullptr);

// 下拉候选：探测结果置首，去重追加 UTF-8/GBK/System/Latin-1（UTF-16 仅探测命中时出现）
QStringList suggestedCodecs(const QString& detected);

// 回传前冲突检查快照：hasSize=false 且 mtime 空 = 完全不可比（放行，见规格勘误 R4）
struct ConflictSnapshot {
    uint64_t size = 0;
    QString mtime;      // ISO "yyyy-MM-dd HH:mm:ss"，空=打开时不可得
    bool hasSize = true;
};
bool remoteChanged(const ConflictSnapshot& atOpen, const FtpFileInfo& now);
