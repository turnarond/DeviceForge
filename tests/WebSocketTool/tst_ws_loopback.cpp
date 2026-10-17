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

// 收集 Server 侧日志行（互斥锁保护）：#50-3 用「认证失败」行作拒绝路径**已执行**
// 的可观测证据——与「连接还没到达服务端」区分，避免 QTRY 竞态造成假绿。
// （日志脱敏约束下本行只含地址，不含 token 内容）
class LogRecorder {
public:
    void record(const std::string& line)
    {
        QMutexLocker locker(&m_mutex);
        m_lines.append(QString::fromStdString(line));
    }
    bool contains(const QString& needle) const
    {
        QMutexLocker locker(&m_mutex);
        for (const auto& l : m_lines)
            if (l.contains(needle))
                return true;
        return false;
    }

private:
    mutable QMutex m_mutex;
    QStringList m_lines;
};

} // namespace

class TstWsLoopback : public QObject {
    Q_OBJECT

private slots:
    void subscribePublishPushPipeline();
    void authTokenRejectsWrongConnection();
    void authTokenLengthGateKeepsPreviousValue();
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

// #50-3：Server+Token 环回——错误 token 必须被拒（此前仓内无 token 拒连的环回覆盖，
// 既有管线用例恒用空 token；本用例补该空白）
void TstWsLoopback::authTokenRejectsWrongConnection()
{
    WebSocketBackend server;
    LogRecorder logRec;
    server.setLogCallback([&logRec](const std::string& s) { logRec.record(s); });
    // bool 化签名：合法长度写入返回 true
    QVERIFY(server.setAuthToken("secret-token"));
    server.startServer(0, false);
    QVERIFY2(server.isRunning(), "Server 环回启动失败");
    const int port = server.serverListenPort();
    const QString base = QStringLiteral("ws://127.0.0.1:%1").arg(port);

    // 错误 token：HTTP 握手成功后被认证分支立即关闭——「认证失败」日志行是
    // 比较路径已执行的取证点；随后在线列表恒空、客户端落回断开态
    WebSocketBackend bad;
    bad.startClient((base + QStringLiteral("?token=wrong")).toStdString());
    QTRY_VERIFY_WITH_TIMEOUT(logRec.contains(QStringLiteral("认证失败")), 5000);
    QCOMPARE(server.serverClientPeers().size(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(!bad.isRunning(), 5000);

    // 正确 token：正常接入并进在线列表
    WebSocketBackend good;
    good.startClient((base + QStringLiteral("?token=secret-token")).toStdString());
    QTRY_VERIFY_WITH_TIMEOUT(good.isRunning(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(server.serverClientPeers().size(), 1);

    good.stopClient();
    bad.stopClient();
    server.stopServer();
}

// #50-3：setAuthToken 长度闸——超限拒写入（返回 false、旧值保持），且拒绝后
// 服务端行为按旧 token 语义生效（超限值从未进入比较路径）
void TstWsLoopback::authTokenLengthGateKeepsPreviousValue()
{
    WebSocketBackend server;
    // 合法写回 true：空串与恰为上限 256（边界值）；超限 300 回 false 且不写入
    QVERIFY(server.setAuthToken(""));
    QVERIFY(server.setAuthToken(std::string(wsproto::kAuthTokenMaxLen, 'y')));
    QVERIFY(server.setAuthToken("keep-me"));
    QVERIFY(!server.setAuthToken(std::string(300, 'x')));

    // 长度闸拒绝后启动：生效的仍是旧值 "keep-me"——若超限值真被写入（旧 void
    // 行为），下面持 300-x token 的客户端会被接受；断言拒绝是实时防线非返回值装饰
    LogRecorder logRec;
    server.setLogCallback([&logRec](const std::string& s) { logRec.record(s); });
    server.startServer(0, false);
    QVERIFY2(server.isRunning(), "Server 环回启动失败");
    const int port = server.serverListenPort();
    const QString base = QStringLiteral("ws://127.0.0.1:%1").arg(port);

    WebSocketBackend stale; // 持被拒的 300-x token：应被认证失败拒绝
    stale.startClient((base + QStringLiteral("?token=") + QString(300, QLatin1Char('x'))).toStdString());
    QTRY_VERIFY_WITH_TIMEOUT(logRec.contains(QStringLiteral("认证失败")), 5000);
    QCOMPARE(server.serverClientPeers().size(), 0);

    WebSocketBackend ok;    // 持旧值 keep-me：应正常接入
    ok.startClient((base + QStringLiteral("?token=keep-me")).toStdString());
    QTRY_VERIFY_WITH_TIMEOUT(ok.isRunning(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(server.serverClientPeers().size(), 1);

    ok.stopClient();
    stale.stopClient();
    server.stopServer();
}

QTEST_MAIN(TstWsLoopback)
#include "tst_ws_loopback.moc"
