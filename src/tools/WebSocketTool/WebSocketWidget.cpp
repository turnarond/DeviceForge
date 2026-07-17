/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: WebSocketWidget.cpp
 *
 * Date: 2026-07-05
 *
 * Author: turnarond
 *
 * Description: WebSocket Tool 前端实现 — 纯代码构建 UI，
 *              匹配原 tab_websocket.ui 布局：模式切换 → Server/Client 设置 → 发布/订阅 → 消息日志。
 */

#include "WebSocketWidget.h"
#include "WebSocketBackend.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDateTime>
#include <QMessageBox>
#include <QMetaObject>

WebSocketWidget::WebSocketWidget(QWidget* parent)
    : ToolWidget(parent)
{
    setupUi();
}

void WebSocketWidget::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    // ========== 模式选择 ==========
    auto* modeGroup = new QGroupBox("模式", this);
    auto* modeLayout = new QHBoxLayout(modeGroup);

    m_radioServer = new QRadioButton("Server", this);
    m_radioClient = new QRadioButton("Client", this);
    m_radioServer->setChecked(true);
    m_radioClient->setChecked(false);

    connect(m_radioServer, &QRadioButton::toggled, this, &WebSocketWidget::onServerToggled);
    connect(m_radioClient, &QRadioButton::toggled, this, &WebSocketWidget::onClientToggled);

    modeLayout->addWidget(m_radioServer);
    modeLayout->addWidget(m_radioClient);
    modeLayout->addStretch();
    mainLayout->addWidget(modeGroup);

    // ========== Server 设置 ==========
    m_groupServer = new QGroupBox("Server 设置", this);
    auto* serverLayout = new QHBoxLayout(m_groupServer);

    serverLayout->addWidget(new QLabel("端口:", this));
    m_spinPort = new QSpinBox(this);
    m_spinPort->setRange(1, 65535);
    m_spinPort->setValue(12345);
    serverLayout->addWidget(m_spinPort);

    m_chkWss = new QCheckBox("WSS（安全模式）", this);
    m_chkWss->setToolTip("启用 WebSocket Secure，需配置 SSL 证书");
    serverLayout->addWidget(m_chkWss);
    serverLayout->addStretch();
    mainLayout->addWidget(m_groupServer);

    // ========== Client 设置 ==========
    m_groupClient = new QGroupBox("Client 设置", this);
    auto* clientLayout = new QHBoxLayout(m_groupClient);

    clientLayout->addWidget(new QLabel("服务器 URL:", this));
    m_editUrl = new QLineEdit(this);
    m_editUrl->setPlaceholderText("ws://localhost:12345  或  wss://host:port");
    m_editUrl->setText("ws://localhost:12345");
    clientLayout->addWidget(m_editUrl, 1);
    mainLayout->addWidget(m_groupClient);

    // 初始状态：Server 模式
    m_groupServer->setEnabled(true);
    m_groupClient->setEnabled(false);

    // ========== 启动/停止按钮 ==========
    auto* ctrlLayout = new QHBoxLayout();
    m_btnStart = new QPushButton("启动", this);
    m_btnStart->setObjectName("btnPrimary");
    m_btnStop  = new QPushButton("停止", this);
    m_btnStop->setEnabled(false);

    connect(m_btnStart, &QPushButton::clicked, this, &WebSocketWidget::onStartClicked);
    connect(m_btnStop,  &QPushButton::clicked, this, &WebSocketWidget::onStopClicked);

    ctrlLayout->addStretch();
    ctrlLayout->addWidget(m_btnStart);
    ctrlLayout->addWidget(m_btnStop);
    mainLayout->addLayout(ctrlLayout);

    // ========== 发布/订阅 ==========
    auto* pubGroup = new QGroupBox("发布 / 订阅", this);
    auto* pubLayout = new QVBoxLayout(pubGroup);

    auto* topicLayout = new QHBoxLayout();
    topicLayout->addWidget(new QLabel("主题:", this));
    m_editTopic = new QLineEdit(this);
    m_editTopic->setPlaceholderText("输入主题 URL，例如 /sensors/temperature");
    topicLayout->addWidget(m_editTopic, 1);
    pubLayout->addLayout(topicLayout);

    auto* msgLayout = new QHBoxLayout();
    msgLayout->addWidget(new QLabel("消息:", this));
    m_editMessage = new QLineEdit(this);
    m_editMessage->setPlaceholderText("输入要发布的消息内容");
    msgLayout->addWidget(m_editMessage, 1);
    pubLayout->addLayout(msgLayout);

    auto* pubBtnLayout = new QHBoxLayout();
    m_btnSubscribe = new QPushButton("订阅", this);
    m_btnPublish   = new QPushButton("发布", this);

    connect(m_btnSubscribe, &QPushButton::clicked, this, &WebSocketWidget::onSubscribeClicked);
    connect(m_btnPublish,   &QPushButton::clicked, this, &WebSocketWidget::onPublishClicked);

    pubBtnLayout->addStretch();
    pubBtnLayout->addWidget(m_btnSubscribe);
    pubBtnLayout->addWidget(m_btnPublish);
    pubLayout->addLayout(pubBtnLayout);

    mainLayout->addWidget(pubGroup);

    // ========== 消息日志 ==========
    auto* logGroup = new QGroupBox("消息日志", this);
    auto* logLayout = new QVBoxLayout(logGroup);

    m_logView = new QPlainTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setPlaceholderText("WebSocket 消息日志...");
    m_logView->setMaximumBlockCount(5000); // 限制日志行数防止内存溢出
    logLayout->addWidget(m_logView, 1);

    auto* logBtnLayout = new QHBoxLayout();
    m_btnClearLog = new QPushButton("清理日志", this);
    connect(m_btnClearLog, &QPushButton::clicked, this, &WebSocketWidget::onClearLogClicked);
    logBtnLayout->addStretch();
    logBtnLayout->addWidget(m_btnClearLog);
    logLayout->addLayout(logBtnLayout);

    mainLayout->addWidget(logGroup, 1);
}

void WebSocketWidget::setBackend(WebSocketBackend* backend)
{
    m_backend = backend;
    if (!m_backend) return;

    // 绑定 Backend 回调（通过 QMetaObject::invokeMethod + Qt::QueuedConnection 保证跨线程安全）
    m_backend->setLogCallback([this](const std::string& msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            appendLog(QString::fromStdString(msg));
        }, Qt::QueuedConnection);
    });

    m_backend->setMessageCallback([this](const std::string& msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            appendLog(QString::fromStdString("[消息] " + msg));
        }, Qt::QueuedConnection);
    });

    m_backend->setClientConnectCallback([this](const std::string& clientInfo) {
        QMetaObject::invokeMethod(this, [this, clientInfo]() {
            appendLog(QString::fromStdString("[连接] " + clientInfo));
        }, Qt::QueuedConnection);
    });

    m_backend->setClientDisconnectCallback([this](const std::string& clientInfo) {
        QMetaObject::invokeMethod(this, [this, clientInfo]() {
            appendLog(QString::fromStdString("[断开] " + clientInfo));
        }, Qt::QueuedConnection);
    });

    m_backend->setErrorCallback([this](const std::string& err) {
        QMetaObject::invokeMethod(this, [this, err]() {
            appendLog(QString::fromStdString("[错误] " + err));
            // 错误时恢复按钮状态
            m_btnStart->setEnabled(true);
            m_btnStop->setEnabled(false);
            m_radioServer->setEnabled(true);
            m_radioClient->setEnabled(true);
        }, Qt::QueuedConnection);
    });
}

// ============ ToolWidget 生命周期 ============

void WebSocketWidget::onToolStart()
{
    appendLog("WebSocket 工具已就绪");
    emit toolStatusChanged("就绪");
}

void WebSocketWidget::onToolStop()
{
    appendLog("WebSocket 工具已停止");
    emit toolStatusChanged("已停止");
}

// ============ 模式切换 ============

void WebSocketWidget::onServerToggled(bool checked)
{
    if (checked) {
        m_groupServer->setEnabled(true);
        m_groupClient->setEnabled(false);
    }
}

void WebSocketWidget::onClientToggled(bool checked)
{
    if (checked) {
        m_groupServer->setEnabled(false);
        m_groupClient->setEnabled(true);
    }
}

// ============ 启动/停止 ============

void WebSocketWidget::onStartClicked()
{
    if (!m_backend) {
        QMessageBox::warning(this, "错误", "Backend 未就绪");
        return;
    }

    if (m_backend->isRunning()) {
        QMessageBox::warning(this, "警告", "WebSocket 服务已在运行中");
        return;
    }

    m_btnStart->setEnabled(false);
    m_btnStop->setEnabled(true);
    m_radioServer->setEnabled(false);
    m_radioClient->setEnabled(false);

    if (m_radioServer->isChecked()) {
        int port = m_spinPort->value();
        bool ssl = m_chkWss->isChecked();
        m_backend->startServer(port, ssl);
        emit toolStatusChanged("Server 启动中...");
    } else {
        QString url = m_editUrl->text().trimmed();
        if (url.isEmpty()) {
            QMessageBox::warning(this, "警告", "请输入服务器 URL");
            m_btnStart->setEnabled(true);
            m_btnStop->setEnabled(false);
            m_radioServer->setEnabled(true);
            m_radioClient->setEnabled(true);
            return;
        }
        m_backend->startClient(url.toStdString());
        emit toolStatusChanged("Client 连接中...");
    }
}

void WebSocketWidget::onStopClicked()
{
    if (!m_backend) return;

    if (!m_backend->isRunning()) {
        QMessageBox::warning(this, "警告", "WebSocket 服务未运行");
        return;
    }

    if (m_backend->isServerMode()) {
        m_backend->stopServer();
    } else {
        m_backend->stopClient();
    }

    m_btnStart->setEnabled(true);
    m_btnStop->setEnabled(false);
    m_radioServer->setEnabled(true);
    m_radioClient->setEnabled(true);
    emit toolStatusChanged("已停止");
}

// ============ 发布/订阅 ============

void WebSocketWidget::onSubscribeClicked()
{
    if (!m_backend) return;

    QString topic = m_editTopic->text().trimmed();
    if (topic.isEmpty()) {
        QMessageBox::warning(this, "警告", "请输入主题");
        return;
    }

    if (!m_backend->isRunning()) {
        QMessageBox::warning(this, "警告", "请先启动 WebSocket 服务");
        return;
    }

    m_backend->subscribe(topic.toStdString());
}

void WebSocketWidget::onPublishClicked()
{
    if (!m_backend) return;

    QString topic = m_editTopic->text().trimmed();
    QString message = m_editMessage->text().trimmed();

    if (topic.isEmpty()) {
        QMessageBox::warning(this, "警告", "请输入主题");
        return;
    }
    if (message.isEmpty()) {
        QMessageBox::warning(this, "警告", "请输入要发布的消息");
        return;
    }
    if (!m_backend->isRunning()) {
        QMessageBox::warning(this, "警告", "请先启动 WebSocket 服务");
        return;
    }

    m_backend->publish(topic.toStdString(), message.toStdString());
}

// ============ 日志 ============

void WebSocketWidget::onClearLogClicked()
{
    m_logView->clear();
    appendLog("日志已清理");
}

void WebSocketWidget::appendLog(const QString& msg)
{
    QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    m_logView->appendPlainText("[" + ts + "] " + msg);
}
