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

#include <string>

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
    void filterFieldsSemantics();
    void filterPredicateDelegatesToFields();
    void constantTimeEqualsBehavior();
    void authTokenMaxLengthConstant();
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

// #50-2：展示层过滤字段判定——四组语义：全通过 / 方向全等命中 /
// 主题大小写不敏感子串 / 组合空串退化（两条件皆空=不过滤，皆非空=逻辑与）
void TstWsEventTypes::filterFieldsSemantics()
{
    using wsproto::matchesFilterFields;
    // 组1 全通过：两条件皆空 → 任意事件匹配（含空 topic 事件）
    QVERIFY(matchesFilterFields(QStringLiteral("收"), QStringLiteral("abc"), QString(), QString()));
    QVERIFY(matchesFilterFields(QStringLiteral("发"), QString(), QString(), QString()));
    // 组2 方向过滤：非空时与 direction 全等才命中
    QVERIFY(matchesFilterFields(QStringLiteral("收"), QStringLiteral("abc"),
                                QStringLiteral("收"), QString()));
    QVERIFY(!matchesFilterFields(QStringLiteral("发"), QStringLiteral("abc"),
                                 QStringLiteral("收"), QString()));
    // 组3 主题子串：大小写不敏感、任意位置
    QVERIFY(matchesFilterFields(QStringLiteral("收"), QStringLiteral("abc"),
                                QString(), QStringLiteral("AB")));
    QVERIFY(matchesFilterFields(QStringLiteral("收"), QStringLiteral("xyzABq"),
                                QString(), QStringLiteral("ab")));
    QVERIFY(!matchesFilterFields(QStringLiteral("收"), QStringLiteral("xyz"),
                                 QString(), QStringLiteral("ab")));
    // 组4 组合退化：两条件同时生效取交集；空方向过滤=方向不限
    QVERIFY(matchesFilterFields(QStringLiteral("收"), QStringLiteral("abc"),
                                QStringLiteral("收"), QStringLiteral("AB")));
    QVERIFY(!matchesFilterFields(QStringLiteral("发"), QStringLiteral("abc"),
                                 QStringLiteral("收"), QStringLiteral("ab")));
}

// #50-2：matchesFilter（规格签名入口）必须委托 matchesFilterFields——
// 二者行为一致由本用例锁定，防 Widget 单元格路径与事件对象路径两套逻辑漂移
void TstWsEventTypes::filterPredicateDelegatesToFields()
{
    wsproto::WsEvent e;
    e.direction = QStringLiteral("收");
    e.topic = QStringLiteral("abc");
    const char* dirFilters[] = {"", "收", "发"};
    const char* topicSubs[] = {"", "AB", "zz"};
    for (const char* d : dirFilters) {
        for (const char* t : topicSubs) {
            const QString dirFilter = QString::fromUtf8(d);
            const QString topicSubstring = QString::fromUtf8(t);
            QCOMPARE(wsproto::matchesFilter(e, dirFilter, topicSubstring),
                     wsproto::matchesFilterFields(e.direction, e.topic,
                                                  dirFilter, topicSubstring));
        }
    }
    // 规格四组直例
    QVERIFY(wsproto::matchesFilter(e, QString(), QString()));                          // 全通过
    QVERIFY(wsproto::matchesFilter(e, QStringLiteral("收"), QString()));               // 方向命中
    QVERIFY(!wsproto::matchesFilter(e, QStringLiteral("发"), QString()));              // 方向不命中
    QVERIFY(wsproto::matchesFilter(e, QString(), QStringLiteral("AB")));               // 大小写不敏感子串
    QVERIFY(wsproto::matchesFilter(e, QStringLiteral("收"), QStringLiteral("abc")));   // 组合命中
}

// #50-3：constantTimeEquals 行为正确性组。
// 【定档说明】本用例只验行为正确性（等值/单字节差/长度差/空串）；时序性质
// （运行时长与匹配前缀无关）在单测里不可靠断言——调度噪声远大于比较耗时，
// 计时断言只会制造 flake，故规格定档不做时序断言，勿"更严格"地补计时用例。
void TstWsEventTypes::constantTimeEqualsBehavior()
{
    // 等值 → true
    QVERIFY(wsproto::constantTimeEquals("secret", "secret"));
    QVERIFY(wsproto::constantTimeEquals("a", "a"));
    // 单字节差（等长）→ false
    QVERIFY(!wsproto::constantTimeEquals("secret", "secreu"));
    QVERIFY(!wsproto::constantTimeEquals("secret", "xecret"));
    // 长度差 → false（两侧各验一向）
    QVERIFY(!wsproto::constantTimeEquals("secret", "secr"));      // a 长 b 短
    QVERIFY(!wsproto::constantTimeEquals("secr", "secret"));      // a 短 b 长
    // 长度差且公共前缀全等（短路比较的最坏情形）→ false
    QVERIFY(!wsproto::constantTimeEquals("token123", "token"));
    // 空串对空串 → true
    QVERIFY(wsproto::constantTimeEquals("", ""));
    // 空串对非空 → false
    QVERIFY(!wsproto::constantTimeEquals("", "x"));
    QVERIFY(!wsproto::constantTimeEquals("x", ""));
}

// #50-3：Token 长度上限常量锁定（256），与 setAuthToken 长度闸/UI setMaxLength 同源
void TstWsEventTypes::authTokenMaxLengthConstant()
{
    QCOMPARE(wsproto::kAuthTokenMaxLen, std::size_t(256));
}

QTEST_MAIN(TstWsEventTypes)
#include "tst_ws_event_types.moc"
