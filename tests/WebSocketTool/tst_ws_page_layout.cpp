/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: tst_ws_page_layout.cpp
 *
 * Date: 2026-10-08
 *
 * Author: turnarond
 *
 * Description: WebSocket 页面重构（v2.12）布局回归：
 *              双栏结构、事件表为唯一垂直扩展主体（拉伸不散架）、
 *              列宽 Interactive、2000 行 FIFO 上限、Token 掩码与显隐。
 */

#include <QtTest>
#include <QCheckBox>
#include <QHostAddress>
#include <QTemporaryDir>
#include <QWebSocket>
#include <QWebSocketServer>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>

#include "tools/WebSocketTool/WebSocketWidget.h"
#include "tools/WebSocketTool/WebSocketBackend.h"
#include "tools/WebSocketTool/WsEventTypes.h"
#include "config/ConfigStore.h"

namespace {

QLineEdit* findLine(QWidget* w, const char* name)
{
    return w->findChild<QLineEdit*>(QString::fromLatin1(name));
}

QPushButton* findBtn(QWidget* w, const char* name)
{
    return w->findChild<QPushButton*>(QString::fromLatin1(name));
}

} // namespace

namespace {

wsproto::WsEvent makeEvent(const QString& body)
{
    wsproto::WsEvent e;
    e.timestampMs = 1u;
    e.direction = QStringLiteral("收");
    e.peer = QStringLiteral("127.0.0.1");
    e.action = QStringLiteral("文本");
    e.byteCount = wsproto::utf8Bytes(body);
    e.preview = wsproto::truncatePreview(body);
    e.body = body;
    return e;
}

} // namespace

class TstWsPageLayout : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void clientModeStartStaysBusyUntilConnected();
    void clientModeStartRefusedResetsUi();
    void constructorRestoresLastClientEndpoint();
    void constructorSweepsLegacyTokenRows();
    void hugeBodyTooltipIsCapped();
    void eventTableIsTheOnlyVerticalExpander();
    void eventColumnsAreUserResizable();
    void eventStreamCapsAtCapacityWithDroppedCount();
    void tokenFieldMaskedWithVisibilityToggle();
    void modeSwitchSelectsConfigPageAndSubscriptionAvailability();
};

void TstWsPageLayout::initTestCase()
{
    static QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(ConfigStore::instance().open(dir.filePath(QStringLiteral("tst_ws.db"))));
}

void TstWsPageLayout::cleanupTestCase()
{
    ConfigStore::instance().close();
}

void TstWsPageLayout::clientModeStartStaysBusyUntilConnected()
{
    QWebSocketServer srv(QStringLiteral("stub"), QWebSocketServer::NonSecureMode);
    QVERIFY(srv.listen(QHostAddress::LocalHost, 0));
    QWebSocket* peer = nullptr;
    QObject::connect(&srv, &QWebSocketServer::newConnection, [&srv, &peer]() {
        peer = srv.nextPendingConnection();
    });

    WebSocketBackend backend;
    {
        WebSocketWidget widget;
        widget.setBackend(&backend);
        findBtn(&widget, "wsModeClient")->click();
        findLine(&widget, "wsUrl")
            ->setText(QStringLiteral("ws://127.0.0.1:%1").arg(srv.serverPort()));
        findBtn(&widget, "wsStartButton")->click();

        // Critical 回归：异步连接期间必须保持"连接中"忙态
        //（旧缺陷：startClient 后同步 isRunning 恒假 → UI 立即回落，停止按钮失效）
        auto* start = findBtn(&widget, "wsStartButton");
        auto* stop = findBtn(&widget, "wsStopButton");
        QVERIFY2(!start->isEnabled(), "启动后应禁用启动按钮（忙态）");
        QVERIFY2(stop->isEnabled(), "启动后应允许停止（忙态）");
        auto* status = widget.findChild<QLabel*>(QStringLiteral("wsStatusText"));
        QVERIFY(status);
        QVERIFY2(!status->text().contains(QStringLiteral("启动失败")),
                 qPrintable(QStringLiteral("误报启动失败：").append(status->text())));

        QTRY_VERIFY_WITH_TIMEOUT(backend.isRunning(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(peer != nullptr, 5000);

        // 对端断开 → UI 必须复位（旧缺陷：断开回调不 reset，状态冻结）
        peer->close();
        QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(), 5000);
        QVERIFY(!stop->isEnabled());
        backend.stopClient();
    }
    delete peer;
}

void TstWsPageLayout::clientModeStartRefusedResetsUi()
{
    WebSocketBackend backend;
    WebSocketWidget widget;
    widget.setBackend(&backend);
    findBtn(&widget, "wsModeClient")->click();
    // 127.0.0.1 未监听端口：错误回调应把忙态复位
    findLine(&widget, "wsUrl")->setText(QStringLiteral("ws://127.0.0.1:1"));
    findBtn(&widget, "wsStartButton")->click();
    auto* start = findBtn(&widget, "wsStartButton");
    QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(), 5000);
}

void TstWsPageLayout::constructorRestoresLastClientEndpoint()
{
    QVariantMap row;
    row.insert(QStringLiteral("mode"), QStringLiteral("client"));
    row.insert(QStringLiteral("url"), QStringLiteral("ws://10.9.8.7:7000"));
    row.insert(QStringLiteral("trustCert"), true);
    QVERIFY(ConfigStore::instance().save(QStringLiteral("websocket.endpoint"),
                                         QStringLiteral("ws://10.9.8.7:7000"), row));

    WebSocketWidget widget;
    auto* url = findLine(&widget, "wsUrl");
    QVERIFY2(url->text() == QStringLiteral("ws://10.9.8.7:7000"),
             "构造应从 websocket.endpoint 恢复上次配置（回归 v2.11 行为）");
    auto* trust = widget.findChild<QCheckBox*>(QStringLiteral("wsTrustCert"));
    QVERIFY(trust && trust->isChecked());
    auto* stack = widget.findChild<QStackedWidget*>(QStringLiteral("wsConfigStack"));
    QVERIFY(stack && stack->currentIndex() == 1);
}

void TstWsPageLayout::constructorSweepsLegacyTokenRows()
{
    QVariantMap legacy;
    legacy.insert(QStringLiteral("mode"), QStringLiteral("server"));
    legacy.insert(QStringLiteral("port"), 9101);
    legacy.insert(QStringLiteral("token"), QStringLiteral("hunter2"));
    QVERIFY(ConfigStore::instance().save(QStringLiteral("websocket.endpoint"),
                                         QStringLiteral("server:9101"), legacy));

    { WebSocketWidget widget; }  // 构造触发清扫

    const QVariantMap swept = ConfigStore::instance().load(
        QStringLiteral("websocket.endpoint"), QStringLiteral("server:9101"));
    QVERIFY2(!swept.contains(QStringLiteral("token")),
             "存量明文 Token 行必须在构造时清除（导出脱敏面闭环）");
    QCOMPARE(swept.value(QStringLiteral("port")).toInt(), 9101); // 其余字段保持
}

void TstWsPageLayout::hugeBodyTooltipIsCapped()
{
    WebSocketWidget widget;
    wsproto::WsEvent e = makeEvent(QString(64 * 1024, QChar(QLatin1Char('A'))));
    widget.pushEvent(e);
    auto* table = widget.findChild<QTableWidget*>(QStringLiteral("wsEventTable"));
    const QString tip = table->item(0, 4)->toolTip();
    QVERIFY2(tip.size() < 64 * 1024, "超长正文必须限制驻留（长稳内存约束）");
    QVERIFY(tip.contains(QStringLiteral("已截断")));
}

void TstWsPageLayout::eventTableIsTheOnlyVerticalExpander()
{
    WebSocketWidget widget;
    widget.resize(1400, 900);
    widget.show();
    QCoreApplication::processEvents();

    auto* splitter = widget.findChild<QSplitter*>(QStringLiteral("wsSplitter"));
    auto* table = widget.findChild<QTableWidget*>(QStringLiteral("wsEventTable"));
    QVERIFY(splitter);
    QVERIFY(table);

    const QList<int> sizes = splitter->sizes();
    QCOMPARE(sizes.size(), 2);
    // 右栏（事件流）承担绝大部分宽度
    QVERIFY2(sizes.at(1) > sizes.at(0) * 2,
             qPrintable(QStringLiteral("期望右栏显著宽于左栏，实际 %1/%2")
                            .arg(sizes.at(0)).arg(sizes.at(1))));
    // 垂直方向：事件表占据面板主体（拉伸归一），旧版散架的根因即无扩展主体
    QVERIFY2(table->height() >= widget.height() * 0.6,
             qPrintable(QStringLiteral("事件表未成为垂直扩展主体，高度 %1").arg(table->height())));
    widget.hide();
}

void TstWsPageLayout::eventColumnsAreUserResizable()
{
    WebSocketWidget widget;
    auto* table = widget.findChild<QTableWidget*>(QStringLiteral("wsEventTable"));
    QVERIFY(table);
    for (int col = 0; col < 4; ++col) {
        QCOMPARE(table->horizontalHeader()->sectionResizeMode(col),
                 static_cast<QHeaderView::ResizeMode>(QHeaderView::Interactive));
        QVERIFY2(table->columnWidth(col) >= 40,
                 qPrintable(QStringLiteral("列 %1 缺少初始宽度").arg(col)));
    }
    // 末列预览拉伸铺满
    QCOMPARE(table->horizontalHeader()->sectionResizeMode(4),
             static_cast<QHeaderView::ResizeMode>(QHeaderView::Stretch));
}

void TstWsPageLayout::eventStreamCapsAtCapacityWithDroppedCount()
{
    WebSocketWidget widget;
    widget.show();
    QCoreApplication::processEvents();
    auto* table = widget.findChild<QTableWidget*>(QStringLiteral("wsEventTable"));
    auto* dropped = widget.findChild<QLabel*>(QStringLiteral("wsDroppedLabel"));
    QVERIFY(table);
    QVERIFY(dropped);

    for (int i = 0; i < WebSocketWidget::kMaxEventRows + 50; ++i)
        widget.pushEvent(makeEvent(QStringLiteral("m%1").arg(i)));

    QCOMPARE(table->rowCount(), WebSocketWidget::kMaxEventRows);
    QVERIFY(dropped->isVisible());
    QVERIFY(dropped->text().contains(QStringLiteral("50")));

    // 最旧 50 条被挤掉：首行应是 m50
    QCOMPARE(table->item(0, 4)->text(), QStringLiteral("m50"));

    auto* clear = widget.findChild<QPushButton*>(QStringLiteral("wsClearEvents"));
    QVERIFY(clear);
    clear->click();
    QCOMPARE(table->rowCount(), 0);
    QVERIFY(!dropped->isVisible());
}

void TstWsPageLayout::tokenFieldMaskedWithVisibilityToggle()
{
    WebSocketWidget widget;
    auto* token = widget.findChild<QLineEdit*>(QStringLiteral("wsToken"));
    auto* eye = widget.findChild<QPushButton*>(QStringLiteral("wsTokenEye"));
    QVERIFY(token);
    QVERIFY(eye);
    QCOMPARE(token->echoMode(), QLineEdit::Password);
    eye->click();
    QCOMPARE(token->echoMode(), QLineEdit::Normal);
    eye->click();
    QCOMPARE(token->echoMode(), QLineEdit::Password);
}

void TstWsPageLayout::modeSwitchSelectsConfigPageAndSubscriptionAvailability()
{
    WebSocketWidget widget;
    widget.show();
    QCoreApplication::processEvents();
    auto* stack = widget.findChild<QStackedWidget*>(QStringLiteral("wsConfigStack"));
    auto* clientBtn = widget.findChild<QPushButton*>(QStringLiteral("wsModeClient"));
    auto* serverBtn = widget.findChild<QPushButton*>(QStringLiteral("wsModeServer"));
    auto* url = widget.findChild<QLineEdit*>(QStringLiteral("wsUrl"));
    auto* subscribe = widget.findChild<QPushButton*>(QStringLiteral("wsSubscribeButton"));
    QVERIFY(stack);
    QVERIFY(clientBtn);
    QVERIFY(serverBtn);
    QVERIFY(subscribe);

    QCOMPARE(stack->currentIndex(), 0);          // 默认 Server
    QVERIFY(!subscribe->isEnabled());            // 无 backend / Server 模式：不可订阅
    clientBtn->click();
    QCOMPARE(stack->currentIndex(), 1);
    QVERIFY(url->isVisible());
    serverBtn->click();
    QCOMPARE(stack->currentIndex(), 0);
}

QTEST_MAIN(TstWsPageLayout)
#include "tst_ws_page_layout.moc"
