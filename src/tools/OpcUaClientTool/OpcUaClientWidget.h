/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: OpcUaClientWidget.h
 *
 * Date: 2026-07-11 / 2026-07-13 重构
 *
 * Author: turnarond
 *
 * Description: OPC UA 客户端 Tool 前端 — 继承 ToolWidget，重构布局：
 *              ① 连接配置（压一行） ② 地址空间浏览（左主舞台，扁平表格，
 *              显示名/NodeId/类型三列 + 搜索 + 计数 + 排序）
 *              ③ 读/写 + 订阅收进右侧操作栏 ④ 日志缩小。
 *
 *              通过 setBackend 注入 OpcUaClientBackend，回调统一
 *              QMetaObject::invokeMethod 编组到 GUI 线程。
 */

#pragma once
#include "framework/ToolWidget.h"
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QTableWidget>
#include <QTextEdit>
#include <QLabel>
#include <QSplitter>
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
    void onBrowseSearchChanged(const QString& text);
    void onBrowseRowActivated(int row, int column);
    void onReadClicked();
    void onWriteClicked();
    void onSubscribeClicked();
    void onRemoveBatchRow(int row);
    void onRemoveSubscriptionRow(int row);

private:
    void setupUi();
    void appendLog(const QString& msg);
    void updateConnectionStatus(bool connected);
    void populateBrowseTable(const QString& json);
    void upsertSubscriptionRow(const QString& nodeId);

    OpcUaClientBackend* m_backend = nullptr;
    bool    m_connected    = false;

    // 连接配置
    QComboBox*   m_endpointEdit        = nullptr;
    QPushButton* m_connectBtn          = nullptr;
    QComboBox*   m_securityPolicyCombo = nullptr;
    QComboBox*   m_authCombo           = nullptr;
    QLabel*      m_statusLabel         = nullptr;

    // 地址空间浏览（扁平表格 + 搜索）
    QTableWidget* m_browseTable = nullptr;   // 列: 显示名 | NodeId | 类型
    QPushButton*  m_browseBtn   = nullptr;
    QLineEdit*    m_browseSearch = nullptr;   // 搜索框, 即时过滤
    QLabel*       m_browseCount  = nullptr;   // "共 582 项 · 匹配 37"
    int           m_browseTotal  = 0;         // 浏览到的总节点数(不匹配时隐藏行用)

    // 读 / 写面板
    QLineEdit*    m_nodeIdEdit = nullptr;
    QLineEdit*    m_valueEdit  = nullptr;
    QPushButton*  m_readBtn    = nullptr;
    QPushButton*  m_writeBtn   = nullptr;
    QTableWidget* m_batchTable = nullptr; // 列: NodeId | 值 | 质量 | × | ×

    // 订阅面板
    QTableWidget* m_subscriptionTable = nullptr;
    QPushButton*  m_subscribeBtn       = nullptr;

    // 日志
    QTextEdit* m_logView = nullptr;
};
