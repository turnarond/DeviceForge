/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: tst_ws_event_types.cpp
 *
 * Date: 2026-10-08
 *
 * Author: turnarond
 *
 * Description: WebSocket 帧协议纯逻辑回归（v2.12 WebSocket 页面重构第一步）。
 */

#include <QtTest>
#include <QByteArray>

#include "tools/WebSocketTool/WsEventTypes.h"

using wsproto::FrameKind;
using wsproto::ParsedFrame;

class TstWsEventTypes : public QObject {
    Q_OBJECT

private slots:
    void parsesProtocolFrames_data();
    void parsesProtocolFrames();
    void publishSplitsOnFirstColonOnly();
    void encodersRoundTripThroughParser();
    void byteCountIsUtf8Based();
    void previewTruncationIsUtf8BoundarySafe();
};

void TstWsEventTypes::parsesProtocolFrames_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("kind");
    QTest::addColumn<QString>("topic");
    QTest::addColumn<QString>("body");

    QTest::newRow("subscribe") << QStringLiteral("SUBSCRIBE:sensors")
        << int(FrameKind::Subscribe) << QStringLiteral("sensors") << QString();
    QTest::newRow("unsubscribe") << QStringLiteral("UNSUBSCRIBE:sensors")
        << int(FrameKind::Unsubscribe) << QStringLiteral("sensors") << QString();
    QTest::newRow("publish") << QStringLiteral("PUBLISH:sensors:hello")
        << int(FrameKind::Publish) << QStringLiteral("sensors") << QStringLiteral("hello");
    QTest::newRow("topic-push") << QStringLiteral("TOPIC:sensors:hello")
        << int(FrameKind::TopicPush) << QStringLiteral("sensors") << QStringLiteral("hello");
    QTest::newRow("topic-push-empty-body") << QStringLiteral("TOPIC:t:")
        << int(FrameKind::TopicPush) << QStringLiteral("t") << QString();
    QTest::newRow("topic-push-no-colon") << QStringLiteral("TOPICConly")
        << int(FrameKind::Plain) << QString() << QStringLiteral("TOPICConly");
    QTest::newRow("plain-with-colon") << QStringLiteral("hello:world")
        << int(FrameKind::Plain) << QString() << QStringLiteral("hello:world");
    QTest::newRow("plain-empty") << QString()
        << int(FrameKind::Plain) << QString() << QString();
}

void TstWsEventTypes::parsesProtocolFrames()
{
    QFETCH(QString, text);
    QFETCH(int, kind);
    QFETCH(QString, topic);
    QFETCH(QString, body);
    const ParsedFrame f = wsproto::parseFrame(text);
    QCOMPARE(int(f.kind), kind);
    QCOMPARE(f.topic, topic);
    QCOMPARE(f.body, body);
}

void TstWsEventTypes::publishSplitsOnFirstColonOnly()
{
    const ParsedFrame f = wsproto::parseFrame(QStringLiteral("PUBLISH:a:b:c:d"));
    QCOMPARE(int(f.kind), int(FrameKind::Publish));
    QCOMPARE(f.topic, QStringLiteral("a"));
    QCOMPARE(f.body, QStringLiteral("b:c:d"));
}

void TstWsEventTypes::encodersRoundTripThroughParser()
{
    QCOMPARE(wsproto::parseFrame(wsproto::encodeSubscribe(QStringLiteral("t"))).kind,
             FrameKind::Subscribe);
    QCOMPARE(wsproto::parseFrame(wsproto::encodeUnsubscribe(QStringLiteral("t"))).kind,
             FrameKind::Unsubscribe);
    const ParsedFrame p = wsproto::parseFrame(
        wsproto::encodePublish(QStringLiteral("t"), QStringLiteral("bo:dy")));
    QCOMPARE(int(p.kind), int(FrameKind::Publish));
    QCOMPARE(p.topic, QStringLiteral("t"));
    QCOMPARE(p.body, QStringLiteral("bo:dy"));
    const ParsedFrame t = wsproto::parseFrame(
        wsproto::encodeTopicPush(QStringLiteral("t"), QStringLiteral("body")));
    QCOMPARE(int(t.kind), int(FrameKind::TopicPush));
    QCOMPARE(t.body, QStringLiteral("body"));
}

void TstWsEventTypes::byteCountIsUtf8Based()
{
    // "温度" 两个汉字 = 6 个 UTF-8 字节；旧口径 UTF-16 size 会算成 2
    QCOMPARE(wsproto::utf8Bytes(QStringLiteral("温度")), quint64(6));
    QCOMPARE(wsproto::utf8Bytes(QStringLiteral("abc")), quint64(3));
}

void TstWsEventTypes::previewTruncationIsUtf8BoundarySafe()
{
    const QString chinese = QStringLiteral("中文中文中文中文"); // 12 字 × 3 字节 = 36
    const QString cut = wsproto::truncatePreview(chinese, 10);
    const QByteArray utf8 = cut.toUtf8();
    QVERIFY2(utf8.size() <= 10, "预览字节数超限");
    // 截断必须落在合法 UTF-8 边界：回读不乱码（U+FFFD 不出现）
    QVERIFY(!cut.contains(QChar(0xFFFD)));
    QCOMPARE(utf8.size() % 3, 0); // 本串全 3 字节字符，合法截断必为 3 的倍数
    QVERIFY(cut.size() < chinese.size());

    // 不超限内容原样返回
    QCOMPARE(wsproto::truncatePreview(QStringLiteral("short"), 100), QStringLiteral("short"));
}

QTEST_MAIN(TstWsEventTypes)
#include "tst_ws_event_types.moc"
