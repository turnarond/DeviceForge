/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: WsEventTypes.h
 *
 * Date: 2026-10-08
 *
 * Author: turnarond
 *
 * Description: WebSocket 帧协议解析与事件结构的纯逻辑（可测性收敛，参照
 *              NetRelayTypes.h 模式）。协议（与 WebSocketBackend 现行为一致）：
 *   SUBSCRIBE:<topic>
 *   UNSUBSCRIBE:<topic>
 *   PUBLISH:<topic>:<body>   （按第一个 ':' 切分，topic 可含 ':'）
 *   TOPIC:<topic>:<body>     （服务端向订阅者推送）
 *   其余文本按普通消息处理。
 */

#pragma once

#include <QString>
#include <QtGlobal>

namespace wsproto {

enum class FrameKind { Subscribe, Unsubscribe, Publish, TopicPush, Plain };

struct ParsedFrame {
    FrameKind kind = FrameKind::Plain;
    QString topic;   // Subscribe/Unsubscribe/Publish/TopicPush 时有值
    QString body;    // Publish/TopicPush/Plain 时有值
};

// 事件流展示单元：direction 取 "收" / "发"
struct WsEvent {
    qint64 timestampMs = 0;
    QString direction;
    QString peer;      // 对端标识（Server: 客户端地址；Client: "server"）
    QString action;    // 订阅/退订/发布/推送/文本
    QString topic;
    quint64 byteCount = 0;   // UTF-8 字节数（修复旧实现误用 UTF-16 size 的口径）
    QString preview;         // 正文预览（UTF-8 边界安全截断，表格展示用）
    QString body;            // 完整正文（仅供 UI tooltip 查看；严禁写入日志/持久化）
};

constexpr int kPreviewMaxBytes = 200;

inline ParsedFrame parseFrame(const QString& text)
{
    ParsedFrame f;
    if (text.startsWith(QStringLiteral("SUBSCRIBE:"))) {
        f.kind = FrameKind::Subscribe;
        f.topic = text.mid(10);
        return f;
    }
    if (text.startsWith(QStringLiteral("UNSUBSCRIBE:"))) {
        f.kind = FrameKind::Unsubscribe;
        f.topic = text.mid(12);
        return f;
    }
    if (text.startsWith(QStringLiteral("PUBLISH:")) || text.startsWith(QStringLiteral("TOPIC:"))) {
        const bool push = text.startsWith(QStringLiteral("TOPIC:"));
        const QString rest = text.mid(push ? 6 : 8);
        const int sep = rest.indexOf(QLatin1Char(':'));
        f.kind = push ? FrameKind::TopicPush : FrameKind::Publish;
        f.topic = (sep < 0) ? rest : rest.left(sep);
        f.body = (sep < 0) ? QString() : rest.mid(sep + 1);
        return f;
    }
    f.kind = FrameKind::Plain;
    f.body = text;
    return f;
}

inline QString encodeSubscribe(const QString& topic)
{
    return QStringLiteral("SUBSCRIBE:") + topic;
}

inline QString encodeUnsubscribe(const QString& topic)
{
    return QStringLiteral("UNSUBSCRIBE:") + topic;
}

inline QString encodePublish(const QString& topic, const QString& body)
{
    return QStringLiteral("PUBLISH:") + topic + QLatin1Char(':') + body;
}

inline QString encodeTopicPush(const QString& topic, const QString& body)
{
    return QStringLiteral("TOPIC:") + topic + QLatin1Char(':') + body;
}

inline quint64 utf8Bytes(const QString& text)
{
    return quint64(text.toUtf8().size());
}

// UTF-8 边界安全截断：超限截到 maxBytes 内最后一个完整字符并追加省略号
inline QString truncatePreview(const QString& text, int maxBytes = kPreviewMaxBytes)
{
    const QByteArray utf8 = text.toUtf8();
    if (utf8.size() <= maxBytes)
        return text;
    int cut = maxBytes - 1; // 预留省略号（'…' 恰为 3 字节，见下方边界回退）
    // 回退到 UTF-8 字符起始字节（0xxxxxx 或 11xxxxxx）
    while (cut > 0 && (quint8(utf8.at(cut)) & 0xC0) == 0x80)
        --cut;
    // 若截点落在 3 字节序列中间导致少切了省略号空间，直接取整字符边界
    QString result = QString::fromUtf8(utf8.left(cut));
    if (result.toUtf8().size() + 3 > maxBytes)
        result.chop(1);
    result += QStringLiteral("…");
    return result;
}

} // namespace wsproto
