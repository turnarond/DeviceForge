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
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>

#include "tools/WebSocketTool/WebSocketWidget.h"
#include "tools/WebSocketTool/WsEventTypes.h"

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
    void eventTableIsTheOnlyVerticalExpander();
    void eventColumnsAreUserResizable();
    void eventStreamCapsAtCapacityWithDroppedCount();
    void tokenFieldMaskedWithVisibilityToggle();
    void modeSwitchSelectsConfigPageAndSubscriptionAvailability();
};

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
