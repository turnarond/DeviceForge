/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: OpcUaClientWidget.h
 *
 * Date: 2026-07-11
 *
 * Author: turnarond
 *
 * Description: OPC UA 客户端 Tool 前端 — 继承 ToolWidget，四面板布局：
 *              连接配置 / 地址空间浏览 / 读写 / 订阅。
 *              通过 setBackend 注入 OpcUaClientBackend，回调统一
 *              QMetaObject::invokeMethod 编组到 GUI 线程（Task 6 订阅
 *              回调将由 svc() 后台线程派发）。
 */

#pragma once
#include "framework/ToolWidget.h"
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QTreeWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QLabel>
#include <QTimer>

class OpcUaClientBackend;

class OpcUaClientWidget : public ToolWidget {
    Q_OBJECT
public:
    explicit OpcUaClientWidget(QWidget* parent = nullptr);
    ~OpcUaClientWidget() override = default;

    QString toolId() const override { return "com.deviceforge.opcua.client"; }
    QString toolName() const override { return "OPC UA 客户端"; }
    void onToolStart() override;
    void onToolStop() override;

    void setBackend(OpcUaClientBackend* backend);

private slots:
    void onConnectClicked();
    void onBrowseClicked();
    void onReadClicked();
    void onWriteClicked();
    void onSubscribeClicked();
    void onRefreshTimer();
    void onTreeItemDoubleClicked(QTreeWidgetItem* item, int column);

private:
    void setupUi();
    void appendLog(const QString& msg);
    void updateConnectionStatus(bool connected);
    void populateBrowseTree(const QString& json);
    void upsertSubscriptionRow(const QString& nodeId);

    OpcUaClientBackend* m_backend = nullptr;
    QTimer* m_refreshTimer = nullptr;
    bool    m_connected    = false;

    // 连接配置
    QLineEdit*   m_endpointEdit        = nullptr;
    QPushButton* m_connectBtn          = nullptr;
    QComboBox*   m_securityPolicyCombo = nullptr;
    QComboBox*   m_authCombo           = nullptr;
    QLabel*      m_statusLabel         = nullptr;

    // 地址空间浏览
    QTreeWidget* m_browseTree = nullptr;
    QPushButton* m_browseBtn  = nullptr;

    // 读 / 写面板
    QLineEdit*    m_nodeIdEdit = nullptr;
    QLineEdit*    m_valueEdit  = nullptr;
    QPushButton*  m_readBtn    = nullptr;
    QPushButton*  m_writeBtn   = nullptr;
    QTableWidget* m_batchTable = nullptr;

    // 订阅面板
    QTableWidget* m_subscriptionTable = nullptr;
    QPushButton*  m_subscribeBtn       = nullptr;

    // 日志
    QTextEdit* m_logView = nullptr;
};
