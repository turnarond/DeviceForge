/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: OpcUaClientWidget.cpp
 *
 * Date: 2026-07-11
 *
 * Author: turnarond
 *
 * Description: OpcUaClientWidget 实现 — 四面板布局 + Backend 回调编组。
 *              布局参照 ModbusWidget（程序化构建，不用 .ui 文件）。
 */

#include "OpcUaClientWidget.h"
#include "OpcUaClientBackend.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QHeaderView>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

OpcUaClientWidget::OpcUaClientWidget(QWidget* parent) : ToolWidget(parent) { setupUi(); }

// ============================================================
// UI 构建
// ============================================================

void OpcUaClientWidget::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    // ---------- 面板 1：连接配置 ----------
    auto* connGroup = new QGroupBox("连接配置", this);
    auto* connLayout = new QVBoxLayout(connGroup);

    auto* row1 = new QHBoxLayout();
    row1->addWidget(new QLabel("Endpoint:", this));
    m_endpointEdit = new QLineEdit("opc.tcp://192.168.1.10:4840", this);
    row1->addWidget(m_endpointEdit, 1);
    m_connectBtn = new QPushButton("连接", this);
    row1->addWidget(m_connectBtn);
    connLayout->addLayout(row1);

    auto* row2 = new QHBoxLayout();
    row2->addWidget(new QLabel("安全策略:", this));
    m_securityPolicyCombo = new QComboBox(this);
    m_securityPolicyCombo->addItem("None");
    row2->addWidget(m_securityPolicyCombo);
    row2->addSpacing(16);
    row2->addWidget(new QLabel("认证:", this));
    m_authCombo = new QComboBox(this);
    m_authCombo->addItem("匿名");
    row2->addWidget(m_authCombo);
    row2->addSpacing(16);
    row2->addWidget(new QLabel("状态:", this));
    m_statusLabel = new QLabel("○ 未连接", this);
    row2->addWidget(m_statusLabel);
    row2->addStretch();
    connLayout->addLayout(row2);
    mainLayout->addWidget(connGroup);

    // ---------- 面板 2 + 3：地址空间浏览 | 读/写（左右并排） ----------
    auto* midSplitter = new QSplitter(Qt::Horizontal, this);

    // 面板 2：地址空间浏览
    auto* browseGroup = new QGroupBox("地址空间浏览", this);
    auto* browseLayout = new QVBoxLayout(browseGroup);
    m_browseBtn = new QPushButton("浏览", this);
    browseLayout->addWidget(m_browseBtn);
    m_browseTree = new QTreeWidget(this);
    m_browseTree->setHeaderLabels({"显示名", "NodeId"});
    m_browseTree->setColumnWidth(0, 160);
    browseLayout->addWidget(m_browseTree, 1);
    midSplitter->addWidget(browseGroup);

    // 面板 3：读 / 写
    auto* rwGroup = new QGroupBox("读 / 写", this);
    auto* rwLayout = new QVBoxLayout(rwGroup);

    auto* nodeRow = new QHBoxLayout();
    nodeRow->addWidget(new QLabel("NodeId:", this));
    m_nodeIdEdit = new QLineEdit(this);
    m_nodeIdEdit->setPlaceholderText("ns=2;s=Demo.Static.Scalar.Double");
    nodeRow->addWidget(m_nodeIdEdit, 1);
    rwLayout->addLayout(nodeRow);

    auto* valRow = new QHBoxLayout();
    valRow->addWidget(new QLabel("值:", this));
    m_valueEdit = new QLineEdit(this);
    valRow->addWidget(m_valueEdit, 1);
    rwLayout->addLayout(valRow);

    auto* btnRow = new QHBoxLayout();
    m_readBtn = new QPushButton("读", this);
    m_writeBtn = new QPushButton("写", this);
    btnRow->addWidget(m_readBtn);
    btnRow->addWidget(m_writeBtn);
    btnRow->addStretch();
    rwLayout->addLayout(btnRow);

    m_batchTable = new QTableWidget(0, 3, this);
    m_batchTable->setHorizontalHeaderLabels({"NodeId", "值", "质量"});
    m_batchTable->horizontalHeader()->setStretchLastSection(true);
    m_batchTable->setAlternatingRowColors(true);
    m_batchTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    rwLayout->addWidget(m_batchTable, 1);
    midSplitter->addWidget(rwGroup);

    midSplitter->setStretchFactor(0, 1);
    midSplitter->setStretchFactor(1, 1);
    mainLayout->addWidget(midSplitter, 2);

    // ---------- 面板 4：订阅 ----------
    auto* subGroup = new QGroupBox("订阅", this);
    auto* subLayout = new QVBoxLayout(subGroup);
    auto* subBtnRow = new QHBoxLayout();
    m_subscribeBtn = new QPushButton("订阅", this);
    m_subscribeBtn->setCheckable(true);
    subBtnRow->addWidget(m_subscribeBtn);
    subBtnRow->addStretch();
    subLayout->addLayout(subBtnRow);

    m_subscriptionTable = new QTableWidget(0, 3, this);
    m_subscriptionTable->setHorizontalHeaderLabels({"NodeId", "最新值", "时间戳"});
    m_subscriptionTable->horizontalHeader()->setStretchLastSection(true);
    m_subscriptionTable->setAlternatingRowColors(true);
    m_subscriptionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    subLayout->addWidget(m_subscriptionTable, 1);
    mainLayout->addWidget(subGroup, 1);

    // ---------- 日志 ----------
    m_logView = new QTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setMaximumHeight(100);
    mainLayout->addWidget(m_logView);

    // ---------- 定时器 + 信号连接 ----------
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(1000);

    connect(m_connectBtn,   &QPushButton::clicked, this, &OpcUaClientWidget::onConnectClicked);
    connect(m_browseBtn,    &QPushButton::clicked, this, &OpcUaClientWidget::onBrowseClicked);
    connect(m_readBtn,      &QPushButton::clicked, this, &OpcUaClientWidget::onReadClicked);
    connect(m_writeBtn,     &QPushButton::clicked, this, &OpcUaClientWidget::onWriteClicked);
    connect(m_subscribeBtn, &QPushButton::clicked, this, &OpcUaClientWidget::onSubscribeClicked);
    connect(m_refreshTimer, &QTimer::timeout,      this, &OpcUaClientWidget::onRefreshTimer);
    connect(m_browseTree,   &QTreeWidget::itemDoubleClicked, this, &OpcUaClientWidget::onTreeItemDoubleClicked);

    updateConnectionStatus(false);
}

// ============================================================
// Backend 注入 + 回调编组（统一 QueuedConnection 编组到 GUI 线程）
// ============================================================

void OpcUaClientWidget::setBackend(OpcUaClientBackend* backend)
{
    m_backend = backend;
    if (!m_backend) return;

    m_backend->setLogCallback([this](const std::string& msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            appendLog(QString::fromStdString(msg));
        }, Qt::QueuedConnection);
    });

    m_backend->setConnectionCallback([this](bool connected) {
        QMetaObject::invokeMethod(this, [this, connected]() {
            updateConnectionStatus(connected);
        }, Qt::QueuedConnection);
    });

    m_backend->setDataChangeCallback([this](const QString& nodeId, const QVariant& value,
                                            quint64 ts, const QString& quality) {
        QMetaObject::invokeMethod(this, [this, nodeId, value, ts, quality]() {
            const QString valStr = value.toString();
            const QString tsStr  = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(ts))
                                       .toString("hh:mm:ss");

            // 更新读写批量表（NodeId / 值 / 质量）
            bool found = false;
            for (int r = 0; r < m_batchTable->rowCount(); ++r) {
                if (m_batchTable->item(r, 0) && m_batchTable->item(r, 0)->text() == nodeId) {
                    m_batchTable->item(r, 1)->setText(valStr);
                    m_batchTable->item(r, 2)->setText(quality);
                    found = true;
                    break;
                }
            }
            if (!found) {
                int row = m_batchTable->rowCount();
                m_batchTable->insertRow(row);
                m_batchTable->setItem(row, 0, new QTableWidgetItem(nodeId));
                m_batchTable->setItem(row, 1, new QTableWidgetItem(valStr));
                m_batchTable->setItem(row, 2, new QTableWidgetItem(quality));
            }

            // 若该节点在订阅表中，更新其最新值 + 时间戳
            for (int r = 0; r < m_subscriptionTable->rowCount(); ++r) {
                if (m_subscriptionTable->item(r, 0) &&
                    m_subscriptionTable->item(r, 0)->text() == nodeId) {
                    m_subscriptionTable->item(r, 1)->setText(valStr);
                    m_subscriptionTable->item(r, 2)->setText(tsStr);
                    break;
                }
            }
        }, Qt::QueuedConnection);
    });
}

// ============================================================
// 生命周期
// ============================================================

void OpcUaClientWidget::onToolStart() { appendLog("OPC UA 客户端已就绪"); }

void OpcUaClientWidget::onToolStop()
{
    if (m_refreshTimer->isActive()) m_refreshTimer->stop();
    appendLog("OPC UA 客户端已停止");
}

// ============================================================
// 槽函数
// ============================================================

void OpcUaClientWidget::onConnectClicked()
{
    if (!m_backend) return;
    if (m_connected) {
        m_backend->disconnectFromServer();
    } else {
        const QString endpoint = m_endpointEdit->text().trimmed();
        if (endpoint.isEmpty()) {
            appendLog("请填写 Endpoint 地址");
            return;
        }
        m_backend->connectToServer(endpoint);
    }
}

void OpcUaClientWidget::onBrowseClicked()
{
    if (!m_backend) return;
    const QString json = m_backend->browseAddressSpace();
    populateBrowseTree(json);
}

void OpcUaClientWidget::onReadClicked()
{
    if (!m_backend) return;
    const QString nodeId = m_nodeIdEdit->text().trimmed();
    if (nodeId.isEmpty()) {
        appendLog("请填写要读取的 NodeId");
        return;
    }
    m_backend->readNodes(QStringList{nodeId});
}

void OpcUaClientWidget::onWriteClicked()
{
    if (!m_backend) return;
    const QString nodeId = m_nodeIdEdit->text().trimmed();
    if (nodeId.isEmpty()) {
        appendLog("请填写要写入的 NodeId");
        return;
    }
    m_backend->writeNodes(QStringList{nodeId}, QVariantList{m_valueEdit->text()});
}

void OpcUaClientWidget::onSubscribeClicked()
{
    if (!m_backend) return;

    if (m_subscribeBtn->isChecked()) {
        const QString nodeId = m_nodeIdEdit->text().trimmed();
        if (nodeId.isEmpty()) {
            appendLog("请先填写要订阅的 NodeId");
            m_subscribeBtn->setChecked(false);
            return;
        }
        upsertSubscriptionRow(nodeId);
        m_backend->subscribeNodes(QStringList{nodeId});
        m_refreshTimer->start();
        m_subscribeBtn->setText("停止");
        appendLog("已开始订阅（轮询模式，间隔 1000ms）");
    } else {
        m_refreshTimer->stop();
        m_backend->unsubscribeAll();
        m_subscribeBtn->setText("订阅");
        appendLog("已停止订阅");
    }
}

void OpcUaClientWidget::onRefreshTimer()
{
    if (!m_backend) return;
    QStringList nodeIds;
    for (int r = 0; r < m_subscriptionTable->rowCount(); ++r) {
        if (m_subscriptionTable->item(r, 0)) {
            nodeIds << m_subscriptionTable->item(r, 0)->text();
        }
    }
    if (!nodeIds.isEmpty()) m_backend->readNodes(nodeIds);
}

void OpcUaClientWidget::onTreeItemDoubleClicked(QTreeWidgetItem* item, int /*column*/)
{
    if (!item) return;
    const QString nodeId = item->data(0, Qt::UserRole).toString();
    if (!nodeId.isEmpty()) {
        m_nodeIdEdit->setText(nodeId);
        appendLog("已选择节点: " + nodeId);
    }
}

// ============================================================
// 辅助方法
// ============================================================

void OpcUaClientWidget::appendLog(const QString& msg)
{
    const QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");
    m_logView->append("[" + ts + "] " + msg);
}

void OpcUaClientWidget::updateConnectionStatus(bool connected)
{
    m_connected = connected;
    if (connected) {
        m_statusLabel->setText("● 已连接");
        m_connectBtn->setText("断开");
    } else {
        m_statusLabel->setText("○ 未连接");
        m_connectBtn->setText("连接");
    }
}

void OpcUaClientWidget::populateBrowseTree(const QString& json)
{
    m_browseTree->clear();
    if (json.isEmpty()) return;

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        appendLog("地址空间 JSON 解析失败: " + err.errorString());
        return;
    }

    const QJsonArray arr = doc.array();
    for (const QJsonValue& v : arr) {
        const QJsonObject obj = v.toObject();
        const QString nodeId      = obj.value("nodeId").toString();
        const QString displayName = obj.value("displayName").toString();
        const QString browseName  = obj.value("browseName").toString();
        const QString label = displayName.isEmpty() ? browseName : displayName;

        auto* item = new QTreeWidgetItem(m_browseTree);
        item->setText(0, label.isEmpty() ? nodeId : label);
        item->setText(1, nodeId);
        item->setData(0, Qt::UserRole, nodeId);   // 供双击时提取
        item->setToolTip(1, nodeId);
    }
    appendLog(QString("地址空间浏览完成，共 %1 个节点").arg(arr.size()));
}

void OpcUaClientWidget::upsertSubscriptionRow(const QString& nodeId)
{
    for (int r = 0; r < m_subscriptionTable->rowCount(); ++r) {
        if (m_subscriptionTable->item(r, 0) &&
            m_subscriptionTable->item(r, 0)->text() == nodeId) {
            return;   // 已存在
        }
    }
    int row = m_subscriptionTable->rowCount();
    m_subscriptionTable->insertRow(row);
    m_subscriptionTable->setItem(row, 0, new QTableWidgetItem(nodeId));
    m_subscriptionTable->setItem(row, 1, new QTableWidgetItem("-"));
    m_subscriptionTable->setItem(row, 2, new QTableWidgetItem("-"));
}
