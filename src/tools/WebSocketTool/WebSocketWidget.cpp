/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: WebSocketWidget.cpp
 *
 * Date: 2026-10-08（v2.12 页面重构：控制台 + 消息事件流双栏）
 *
 * Author: turnarond
 *
 * Description: WebSocket Tool 前端实现。左栏控制台（模式/配置/状态/启停/发布订阅），
 *              右栏消息事件流表（时间|方向|主题|字节|内容预览，Interactive 列宽，
 *              2000 行 FIFO 上限）。右栏为唯一垂直扩展主体，窗口拉伸不再散架。
 *              正文预览仅进本页面；全局日志只记方向+字节数（仓规脱敏约束）。
 */

#include "WebSocketWidget.h"
#include "WebSocketBackend.h"
#include "config/ConfigStore.h"

#include <QButtonGroup>
#include <QDateTime>
#include <QGroupBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QSplitter>
#include <QStyle>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

constexpr int kConsoleMinWidth = 320;
constexpr int kConsoleDefaultWidth = 360;

// 事件流列定义
enum EventColumn { ColTime = 0, ColDirection, ColTopic, ColBytes, ColPreview, ColCount };

QString formatEventTime(qint64 epochMs)
{
    return QDateTime::fromMSecsSinceEpoch(epochMs).toString(QStringLiteral("hh:mm:ss.zzz"));
}

} // namespace

WebSocketWidget::WebSocketWidget(QWidget* parent)
    : ToolWidget(parent)
{
    setupUi();

    m_statusTimer.setInterval(1000);
    m_statusTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_statusTimer, &QTimer::timeout, this, &WebSocketWidget::onRefreshStatusTick);
}

void WebSocketWidget::setupUi()
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(8, 8, 8, 8);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName(QStringLiteral("wsSplitter"));
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(6);

    // ── 左控制台 ──
    auto* console = new QWidget(splitter);
    console->setObjectName(QStringLiteral("wsConsole"));
    console->setMinimumWidth(kConsoleMinWidth);
    auto* consoleLayout = new QVBoxLayout(console);
    consoleLayout->setContentsMargins(0, 0, 0, 0);
    consoleLayout->setSpacing(8);

    // 模式分段按钮
    auto* modeGroup = new QGroupBox(QStringLiteral("模式"), console);
    auto* modeLayout = new QHBoxLayout(modeGroup);
    m_btnModeServer = new QPushButton(QStringLiteral("Server"), modeGroup);
    m_btnModeServer->setObjectName(QStringLiteral("wsModeServer"));
    m_btnModeClient = new QPushButton(QStringLiteral("Client"), modeGroup);
    m_btnModeClient->setObjectName(QStringLiteral("wsModeClient"));
    for (QPushButton* b : {m_btnModeServer, m_btnModeClient}) {
        b->setCheckable(true);
        b->setMinimumHeight(30);
    }
    m_btnModeServer->setChecked(true);
    m_modeGroup = new QButtonGroup(this);
    m_modeGroup->addButton(m_btnModeServer);
    m_modeGroup->addButton(m_btnModeClient);
    m_modeGroup->setExclusive(true);
    modeLayout->addWidget(m_btnModeServer);
    modeLayout->addWidget(m_btnModeClient);
    consoleLayout->addWidget(modeGroup);

    // 情境配置（只显当前模式）
    m_configStack = new QStackedWidget(console);
    m_configStack->setObjectName(QStringLiteral("wsConfigStack"));
    {
        auto* serverPage = new QWidget(m_configStack);
        auto* form = new QFormLayout(serverPage);
        m_editBindAddr = new QLineEdit(QStringLiteral("127.0.0.1"), serverPage);
        m_editBindAddr->setObjectName(QStringLiteral("wsBindAddr"));
        m_spinPort = new QSpinBox(serverPage);
        m_spinPort->setObjectName(QStringLiteral("wsPort"));
        m_spinPort->setRange(1, 65535);
        m_spinPort->setValue(9001);
        m_chkWss = new QCheckBox(QStringLiteral("WSS（安全模式，使用内置自签名测试证书）"), serverPage);
        m_chkWss->setObjectName(QStringLiteral("wsWss"));
        auto* tokenRow = new QHBoxLayout();
        m_editToken = new QLineEdit(serverPage);
        m_editToken->setObjectName(QStringLiteral("wsToken"));
        m_editToken->setEchoMode(QLineEdit::Password);
        m_editToken->setPlaceholderText(QStringLiteral("留空 = 不校验 Token"));
        m_btnTokenEye = new QPushButton(QStringLiteral("显示"), serverPage);
        m_btnTokenEye->setObjectName(QStringLiteral("wsTokenEye"));
        m_btnTokenEye->setCheckable(true);
        m_btnTokenEye->setMaximumWidth(56);
        tokenRow->addWidget(m_editToken, 1);
        tokenRow->addWidget(m_btnTokenEye);
        form->addRow(QStringLiteral("绑定地址"), m_editBindAddr);
        form->addRow(QStringLiteral("端口"), m_spinPort);
        form->addRow(QString(), m_chkWss);
        form->addRow(QStringLiteral("Token"), tokenRow);
        m_configStack->addWidget(serverPage);

        auto* clientPage = new QWidget(m_configStack);
        auto* cform = new QFormLayout(clientPage);
        m_editUrl = new QLineEdit(clientPage);
        m_editUrl->setObjectName(QStringLiteral("wsUrl"));
        m_editUrl->setPlaceholderText(QStringLiteral("ws://127.0.0.1:9001 或 wss://…"));
        m_chkTrustCert = new QCheckBox(QStringLiteral("信任自签名证书（仅测试）"), clientPage);
        m_chkTrustCert->setObjectName(QStringLiteral("wsTrustCert"));
        cform->addRow(QStringLiteral("服务器 URL"), m_editUrl);
        cform->addRow(QString(), m_chkTrustCert);
        m_configStack->addWidget(clientPage);
    }
    consoleLayout->addWidget(m_configStack);

    // 状态卡
    auto* statusGroup = new QGroupBox(QStringLiteral("运行状态"), console);
    auto* statusLayout = new QVBoxLayout(statusGroup);
    auto* statusRow = new QHBoxLayout();
    m_statusDot = new QLabel(QStringLiteral("●"), statusGroup);
    m_statusDot->setObjectName(QStringLiteral("wsStatusDot"));
    m_statusDot->setProperty("running", false);
    m_statusText = new QLabel(QStringLiteral("未运行"), statusGroup);
    m_statusText->setObjectName(QStringLiteral("wsStatusText"));
    statusRow->addWidget(m_statusDot);
    statusRow->addWidget(m_statusText);
    statusRow->addStretch();
    statusLayout->addLayout(statusRow);
    m_targetText = new QLabel(QStringLiteral("—"), statusGroup);
    m_targetText->setObjectName(QStringLiteral("wsTargetText"));
    m_targetText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusLayout->addWidget(m_targetText);
    m_clientList = new QListWidget(statusGroup);
    m_clientList->setObjectName(QStringLiteral("wsClientList"));
    m_clientList->setMaximumHeight(110);
    m_clientList->setToolTip(QStringLiteral("Server 在线客户端"));
    m_topicList = new QListWidget(statusGroup);
    m_topicList->setObjectName(QStringLiteral("wsTopicList"));
    m_topicList->setMaximumHeight(110);
    m_topicList->setToolTip(QStringLiteral("Client 已订阅主题"));
    statusLayout->addWidget(m_clientList);
    statusLayout->addWidget(m_topicList);
    consoleLayout->addWidget(statusGroup);

    // 启停
    auto* ctrlRow = new QHBoxLayout();
    m_btnStart = new QPushButton(QStringLiteral("启动"), console);
    m_btnStart->setObjectName(QStringLiteral("wsStartButton"));
    m_btnStop = new QPushButton(QStringLiteral("停止"), console);
    m_btnStop->setObjectName(QStringLiteral("wsStopButton"));
    m_btnStop->setEnabled(false);
    ctrlRow->addWidget(m_btnStart);
    ctrlRow->addWidget(m_btnStop);
    consoleLayout->addLayout(ctrlRow);

    // 发布 / 订阅
    auto* pubGroup = new QGroupBox(QStringLiteral("发布 / 订阅"), console);
    auto* pubLayout = new QVBoxLayout(pubGroup);
    m_editTopic = new QLineEdit(pubGroup);
    m_editTopic->setObjectName(QStringLiteral("wsTopicInput"));
    m_editTopic->setPlaceholderText(QStringLiteral("主题"));
    m_editMessage = new QLineEdit(pubGroup);
    m_editMessage->setObjectName(QStringLiteral("wsMessageInput"));
    m_editMessage->setPlaceholderText(QStringLiteral("消息内容"));
    pubLayout->addWidget(m_editTopic);
    pubLayout->addWidget(m_editMessage);
    auto* pubBtnRow = new QHBoxLayout();
    m_btnSubscribe = new QPushButton(QStringLiteral("订阅"), pubGroup);
    m_btnSubscribe->setObjectName(QStringLiteral("wsSubscribeButton"));
    m_btnUnsubscribe = new QPushButton(QStringLiteral("退订"), pubGroup);
    m_btnUnsubscribe->setObjectName(QStringLiteral("wsUnsubscribeButton"));
    m_btnPublish = new QPushButton(QStringLiteral("发布"), pubGroup);
    m_btnPublish->setObjectName(QStringLiteral("wsPublishButton"));
    pubBtnRow->addWidget(m_btnSubscribe);
    pubBtnRow->addWidget(m_btnUnsubscribe);
    pubBtnRow->addWidget(m_btnPublish);
    pubLayout->addLayout(pubBtnRow);
    consoleLayout->addWidget(pubGroup);

    consoleLayout->addStretch(); // 左栏不参与垂直拉伸分配

    // ── 右事件流 ──
    auto* eventPanel = new QWidget(splitter);
    eventPanel->setObjectName(QStringLiteral("wsEventPanel"));
    auto* eventLayout = new QVBoxLayout(eventPanel);
    eventLayout->setContentsMargins(0, 0, 0, 0);
    auto* toolRow = new QHBoxLayout();
    m_chkAutoScroll = new QCheckBox(QStringLiteral("自动滚动"), eventPanel);
    m_chkAutoScroll->setObjectName(QStringLiteral("wsAutoScroll"));
    m_chkAutoScroll->setChecked(true);
    m_btnClearEvents = new QPushButton(QStringLiteral("清除"), eventPanel);
    m_btnClearEvents->setObjectName(QStringLiteral("wsClearEvents"));
    m_droppedLabel = new QLabel(eventPanel);
    m_droppedLabel->setObjectName(QStringLiteral("wsDroppedLabel"));
    m_droppedLabel->setVisible(false);
    toolRow->addWidget(m_chkAutoScroll);
    toolRow->addWidget(m_btnClearEvents);
    toolRow->addStretch();
    toolRow->addWidget(m_droppedLabel);
    eventLayout->addLayout(toolRow);

    m_eventTable = new QTableWidget(0, ColCount, eventPanel);
    m_eventTable->setObjectName(QStringLiteral("wsEventTable"));
    m_eventTable->setHorizontalHeaderLabels(
        {QStringLiteral("时间"), QStringLiteral("方向"), QStringLiteral("主题"),
         QStringLiteral("字节"), QStringLiteral("内容预览")});
    m_eventTable->verticalHeader()->setVisible(false);
    m_eventTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_eventTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_eventTable->setAlternatingRowColors(true);
    m_eventTable->setWordWrap(false);
    // Interactive 列宽（吸取 v2.11.1 批量命令表教训）：可拖拽，末列默认铺满剩余
    for (int col = 0; col < ColPreview; ++col)
        m_eventTable->horizontalHeader()->setSectionResizeMode(col, QHeaderView::Interactive);
    m_eventTable->horizontalHeader()->setSectionResizeMode(ColPreview, QHeaderView::Stretch);
    m_eventTable->setColumnWidth(ColTime, 100);
    m_eventTable->setColumnWidth(ColDirection, 44);
    m_eventTable->setColumnWidth(ColTopic, 130);
    m_eventTable->setColumnWidth(ColBytes, 56);
    m_eventTable->horizontalHeader()->setSectionsMovable(false);
    eventLayout->addWidget(m_eventTable, 1);

    splitter->addWidget(console);
    splitter->addWidget(eventPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setCollapsible(0, false);
    splitter->setSizes(QList<int>() << kConsoleDefaultWidth << 640);
    rootLayout->addWidget(splitter);

    // ── 信号连接 ──
    connect(m_btnModeServer, &QPushButton::clicked, this, &WebSocketWidget::onServerModeToggled);
    connect(m_btnModeClient, &QPushButton::clicked, this, &WebSocketWidget::onClientModeToggled);
    connect(m_btnStart, &QPushButton::clicked, this, &WebSocketWidget::onStartClicked);
    connect(m_btnStop, &QPushButton::clicked, this, &WebSocketWidget::onStopClicked);
    connect(m_btnSubscribe, &QPushButton::clicked, this, &WebSocketWidget::onSubscribeClicked);
    connect(m_btnUnsubscribe, &QPushButton::clicked, this, &WebSocketWidget::onUnsubscribeClicked);
    connect(m_btnPublish, &QPushButton::clicked, this, &WebSocketWidget::onPublishClicked);
    connect(m_btnClearEvents, &QPushButton::clicked, this, &WebSocketWidget::onClearEventsClicked);
    connect(m_btnTokenEye, &QPushButton::toggled, this, &WebSocketWidget::onTokenVisibilityToggled);
    onServerModeToggled();
}

// ============ 后端回调接线 ============

void WebSocketWidget::setBackend(WebSocketBackend* backend)
{
    m_backend = backend;
    if (!m_backend) return;

    // 所有回调经 QueuedConnection 回主线程；invokeMethod 以 this 为 context，
    // 对象析构后排队调用自动作废（防 UAF，仓规长稳约束）
    m_backend->setLogCallback([this](const std::string& msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            appendLog(QString::fromStdString(msg));
        }, Qt::QueuedConnection);
    });

    m_backend->setMessageCallback([this](const wsproto::WsEvent& event) {
        QMetaObject::invokeMethod(this, [this, event]() {
            pushEvent(event);
            // 全局日志行：方向+主题+字节数，不含正文
            QString line = QStringLiteral("[ws] %1 %2")
                               .arg(event.direction, event.action);
            if (!event.topic.isEmpty())
                line += QStringLiteral("(%1)").arg(event.topic);
            line += QStringLiteral(" %2B").arg(event.byteCount);
            appendLog(line);
        }, Qt::QueuedConnection);
    });

    m_backend->setClientConnectCallback([this](const std::string& clientInfo) {
        QMetaObject::invokeMethod(this, [this, clientInfo]() {
            appendLog(QString::fromStdString("[连接] " + clientInfo));
            refreshStatusCard();
        }, Qt::QueuedConnection);
    });

    m_backend->setClientDisconnectCallback([this](const std::string& clientInfo) {
        QMetaObject::invokeMethod(this, [this, clientInfo]() {
            appendLog(QString::fromStdString("[断开] " + clientInfo));
            refreshStatusCard();
        }, Qt::QueuedConnection);
    });

    m_backend->setErrorCallback([this](const std::string& err) {
        QMetaObject::invokeMethod(this, [this, err]() {
            appendLog(QString::fromStdString("[错误] " + err));
            applyRunningUi(false);
            m_statusText->setText(QStringLiteral("错误，已停止"));
        }, Qt::QueuedConnection);
    });
}

// ============ ToolWidget 生命周期 ============

void WebSocketWidget::onToolStart()
{
    appendLog(QStringLiteral("WebSocket 工具已就绪"));
    emit toolStatusChanged(QStringLiteral("就绪"));
}

void WebSocketWidget::onToolStop()
{
    m_statusTimer.stop();
    refreshStatusCard();
    appendLog(QStringLiteral("WebSocket 工具已停止"));
    emit toolStatusChanged(QStringLiteral("已停止"));
}

// ============ 事件流 ============

void WebSocketWidget::pushEvent(const wsproto::WsEvent& event)
{
    if (m_eventTable->rowCount() >= kMaxEventRows) {
        m_eventTable->removeRow(0);
        ++m_droppedCount;
        m_droppedLabel->setText(QStringLiteral("已丢弃 %1 行").arg(m_droppedCount));
        m_droppedLabel->setVisible(true);
    }
    const int target = m_eventTable->rowCount();
    m_eventTable->insertRow(target);

    auto setCell = [this, target](int col, const QString& text) {
        auto* item = new QTableWidgetItem(text);
        item->setTextAlignment(col == ColBytes ? Qt::AlignRight | Qt::AlignVCenter
                                               : Qt::AlignLeft | Qt::AlignVCenter);
        m_eventTable->setItem(target, col, item);
    };
    setCell(ColTime, formatEventTime(event.timestampMs));
    setCell(ColDirection, event.direction);
    setCell(ColTopic, event.topic);
    setCell(ColBytes, QString::number(event.byteCount));
    auto* previewItem = new QTableWidgetItem(event.preview);
    previewItem->setToolTip(event.body.isEmpty() ? event.preview : event.body);
    m_eventTable->setItem(target, ColPreview, previewItem);

    if (m_chkAutoScroll->isChecked())
        m_eventTable->scrollToBottom();
    refreshStatusCard();
}

void WebSocketWidget::onClearEventsClicked()
{
    m_eventTable->setRowCount(0);
    m_droppedCount = 0;
    m_droppedLabel->setVisible(false);
}

void WebSocketWidget::onTokenVisibilityToggled(bool show)
{
    m_editToken->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
    m_btnTokenEye->setText(show ? QStringLiteral("隐藏") : QStringLiteral("显示"));
}

// ============ 模式切换 ============

void WebSocketWidget::onServerModeToggled()
{
    m_configStack->setCurrentIndex(0);
    // Server 模式：订阅/退订按钮不适用（订阅表在 Server 侧被动维护）
    m_btnSubscribe->setEnabled(false);
    m_btnUnsubscribe->setEnabled(false);
    refreshStatusCard();
}

void WebSocketWidget::onClientModeToggled()
{
    m_configStack->setCurrentIndex(1);
    m_btnSubscribe->setEnabled(m_backend && m_backend->isRunning());
    m_btnUnsubscribe->setEnabled(m_backend && m_backend->isRunning());
    refreshStatusCard();
}

// ============ 状态卡 ============

void WebSocketWidget::applyRunningUi(bool running)
{
    m_btnStart->setEnabled(!running);
    m_btnStop->setEnabled(running);
    m_btnModeServer->setEnabled(!running);
    m_btnModeClient->setEnabled(!running);
    m_statusDot->setProperty("running", running);
    m_statusDot->style()->unpolish(m_statusDot);
    m_statusDot->style()->polish(m_statusDot);
    if (running)
        m_statusTimer.start();
    else
        m_statusTimer.stop();
}

void WebSocketWidget::refreshStatusCard()
{
    const bool serverMode = !m_backend || m_backend->isServerMode();
    m_clientList->setVisible(m_backend && m_backend->isRunning() && serverMode);
    m_topicList->setVisible(m_backend && m_backend->isRunning() && !serverMode);
    if (!m_backend || !m_backend->isRunning()) {
        if (!m_statusDot->property("running").toBool())
            m_statusText->setText(QStringLiteral("未运行"));
        m_targetText->setText(QStringLiteral("—"));
        m_clientList->clear();
        m_topicList->clear();
        return;
    }
    if (serverMode) {
        const QStringList peers = m_backend->serverClientPeers();
        m_statusText->setText(QStringLiteral("Server 运行中 · 客户端 %1").arg(peers.size()));
        m_targetText->setText(QStringLiteral("%1:%2%3")
                                  .arg(m_editBindAddr->text().trimmed(),
                                       QString::number(m_backend->serverListenPort()),
                                       m_chkWss->isChecked() ? QStringLiteral(" (WSS)")
                                                             : QString()));
        m_clientList->clear();
        m_clientList->addItems(peers);
    } else {
        const QStringList topics = m_backend->clientSubscriptions();
        m_statusText->setText(QStringLiteral("Client 已连接 · 订阅 %1").arg(topics.size()));
        m_targetText->setText(m_editUrl->text().trimmed());
        m_topicList->clear();
        m_topicList->addItems(topics);
    }
}

void WebSocketWidget::onRefreshStatusTick()
{
    refreshStatusCard();
}

// ============ 启动/停止 ============

void WebSocketWidget::onStartClicked()
{
    if (!m_backend) {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("Backend 未就绪"));
        return;
    }
    if (m_backend->isRunning()) {
        QMessageBox::warning(this, QStringLiteral("警告"),
                             QStringLiteral("WebSocket 服务已在运行中"));
        return;
    }

    const bool serverMode = m_btnModeServer->isChecked();
    m_droppedCount = 0;
    m_droppedLabel->setVisible(false);

    if (serverMode) {
        const QString bind = m_editBindAddr->text().trimmed();
        if (bind.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请输入绑定地址"));
            return;
        }
        const int port = m_spinPort->value();
        const bool ssl = m_chkWss->isChecked();
        const QString token = m_editToken->text().trimmed();
        ConfigStore::instance().save(QStringLiteral("websocket.endpoint"),
                                     QStringLiteral("server:%1").arg(port),
                                     QVariantMap{
                                         {QStringLiteral("mode"), QStringLiteral("server")},
                                         {QStringLiteral("bind"), bind},
                                         {QStringLiteral("port"), port},
                                         {QStringLiteral("ssl"), ssl},
                                         {QStringLiteral("token_set"), !token.isEmpty()},
                                         {QStringLiteral("updated_at"),
                                          QDateTime::currentMSecsSinceEpoch()}});
        m_backend->setBindAddress(bind);
        m_backend->setAuthToken(token.toStdString());
        m_backend->startServer(port, ssl);
        emit toolStatusChanged(QStringLiteral("Server 启动中..."));
    } else {
        const QString url = m_editUrl->text().trimmed();
        if (url.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请输入服务器 URL"));
            return;
        }
        ConfigStore::instance().save(QStringLiteral("websocket.endpoint"), url,
                                     QVariantMap{
                                         {QStringLiteral("mode"), QStringLiteral("client")},
                                         {QStringLiteral("url"), url},
                                         {QStringLiteral("trustCert"),
                                          m_chkTrustCert->isChecked()},
                                         {QStringLiteral("updated_at"),
                                          QDateTime::currentMSecsSinceEpoch()}});
        m_backend->setIgnoreSslErrors(m_chkTrustCert->isChecked());
        m_backend->startClient(url.toStdString());
        emit toolStatusChanged(QStringLiteral("Client 连接中..."));
    }
    applyRunningUi(true);
    if (!m_backend->isRunning()) {
        // 同步失败路径（如端口占用/地址非法）立即回落 UI
        applyRunningUi(false);
        m_statusText->setText(QStringLiteral("启动失败"));
    }
    refreshStatusCard();
}

void WebSocketWidget::onStopClicked()
{
    if (!m_backend) return;
    if (!m_backend->isRunning()) {
        QMessageBox::warning(this, QStringLiteral("警告"),
                             QStringLiteral("WebSocket 服务未运行"));
        return;
    }
    if (m_backend->isServerMode())
        m_backend->stopServer();
    else
        m_backend->stopClient();
    applyRunningUi(false);
    m_statusText->setText(QStringLiteral("未运行"));
    m_btnSubscribe->setEnabled(false);
    m_btnUnsubscribe->setEnabled(false);
    refreshStatusCard();
    emit toolStatusChanged(QStringLiteral("已停止"));
}

// ============ 发布/订阅 ============

void WebSocketWidget::onSubscribeClicked()
{
    if (!m_backend) return;
    const QString topic = m_editTopic->text().trimmed();
    if (topic.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请输入主题"));
        return;
    }
    if (!m_backend->isRunning()) {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请先启动 WebSocket 服务"));
        return;
    }
    m_backend->subscribe(topic.toStdString());
}

void WebSocketWidget::onUnsubscribeClicked()
{
    if (!m_backend) return;
    const QString topic = m_editTopic->text().trimmed();
    if (topic.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请输入主题"));
        return;
    }
    if (!m_backend->isRunning()) {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请先启动 WebSocket 服务"));
        return;
    }
    m_backend->unsubscribe(topic.toStdString());
}

void WebSocketWidget::onPublishClicked()
{
    if (!m_backend) return;
    const QString topic = m_editTopic->text().trimmed();
    const QString message = m_editMessage->text().trimmed();
    if (topic.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请输入主题"));
        return;
    }
    if (message.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请输入要发布的消息"));
        return;
    }
    if (!m_backend->isRunning()) {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请先启动 WebSocket 服务"));
        return;
    }
    m_backend->publish(topic.toStdString(), message.toStdString());
}

// ============ 日志 ============

void WebSocketWidget::appendLog(const QString& msg)
{
    if (m_globalLogCb) {
        const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss"));
        m_globalLogCb(QStringLiteral("[").append(ts).append("] ").append(msg));
    }
}
