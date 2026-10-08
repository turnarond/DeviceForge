/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: tst_ws_loopback.cpp
 *
 * Date: 2026-10-08
 *
 * Author: turnarond
 *
 * Description: WebSocket Backend 本机环回集成测试（v2.12 页面重构）：
 *              真 QWebSocketServer↔QWebSocket 于 127.0.0.1 自动分配端口，
 *              验证订阅→发布→推送全链路的 WsEvent 管线与 UTF-8 字节口径。
 *              不连任何外部网络。
 */

#include <QtTest>
#include <QMutex>
#include <QVector>

#include "tools/WebSocketTool/WebSocketBackend.h"
#include "tools/WebSocketTool/WsEventTypes.h"

namespace {

// 回调在主线程直接触发（socket 对象均创建于测试主线程事件循环）
class Recorder {
public:
    void record(const wsproto::WsEvent& e)
    {
        QMutexLocker locker(&m_mutex);
        m_events.append(e);
    }
    bool find(const QString& direction, const QString& action, const QString& topic,
              wsproto::WsEvent* out) const
    {
        QMutexLocker locker(&m_mutex);
        for (const auto& e : m_events) {
            if (e.direction == direction && e.action == action && e.topic == topic) {
                if (out)
                    *out = e;
                return true;
            }
        }
        return false;
    }
    bool has(const QString& direction, const QString& action, const QString& topic) const
    {
        return find(direction, action, topic, nullptr);
    }

private:
    mutable QMutex m_mutex;
    QVector<wsproto::WsEvent> m_events;
};

constexpr const char* kTopic = "sensor/温度";
constexpr const char* kBody = "温度23.5℃";

} // namespace

class TstWsLoopback : public QObject {
    Q_OBJECT

private slots:
    void subscribePublishPushPipeline();
};

void TstWsLoopback::subscribePublishPushPipeline()
{
    WebSocketBackend server;
    WebSocketBackend client;
    Recorder serverRec, clientRec;

    server.setAuthToken("");
    server.setMessageCallback([&serverRec](const wsproto::WsEvent& e) { serverRec.record(e); });
    client.setMessageCallback([&clientRec](const wsproto::WsEvent& e) { clientRec.record(e); });

    server.startServer(0, false); // 端口 0 = 系统自动分配
    QVERIFY2(server.isRunning(), "Server 环回启动失败");
    const int port = server.serverListenPort();
    QVERIFY2(port > 0, "未取得监听端口");

    client.startClient(
        QStringLiteral("ws://127.0.0.1:%1").arg(port).toStdString());
    QTRY_VERIFY_WITH_TIMEOUT(client.isRunning(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(server.serverClientPeers().size(), 1);

    const QString topic = QString::fromUtf8(kTopic);
    const QString body = QString::fromUtf8(kBody);

    client.subscribe(topic.toStdString());
    QTRY_VERIFY_WITH_TIMEOUT(serverRec.has(QStringLiteral("收"), QStringLiteral("订阅"), topic), 5000);
    QVERIFY(clientRec.has(QStringLiteral("发"), QStringLiteral("订阅"), topic));

    client.publish(topic.toStdString(), body.toStdString());

    wsproto::WsEvent serverSeen;
    QTRY_VERIFY_WITH_TIMEOUT(
        serverRec.find(QStringLiteral("收"), QStringLiteral("发布"), topic, &serverSeen), 5000);
    QCOMPARE(serverSeen.preview, body);
    QCOMPARE(serverSeen.byteCount, wsproto::utf8Bytes(body));
    QVERIFY(serverSeen.byteCount > quint64(body.size())); // UTF-8 口径 > UTF-16 字数（旧口径缺陷回归点）

    wsproto::WsEvent clientPushed;
    QTRY_VERIFY_WITH_TIMEOUT(
        clientRec.find(QStringLiteral("收"), QStringLiteral("推送"), topic, &clientPushed), 5000);
    QCOMPARE(clientPushed.body, body);
    QCOMPARE(clientPushed.byteCount, wsproto::utf8Bytes(body));

    client.stopClient();
    server.stopServer();
}

QTEST_MAIN(TstWsLoopback)
#include "tst_ws_loopback.moc"
