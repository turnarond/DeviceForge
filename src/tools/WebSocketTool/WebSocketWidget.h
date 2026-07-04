/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * File: WebSocketWidget.h
 *
 * Date: 2026-07-05
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: WebSocket Tool 前端 — 继承 ToolWidget，纯代码构建 UI，
 *              支持 Server/Client 双模式切换、主题订阅/发布、消息日志。
 */

#pragma once
#include "framework/ToolWidget.h"
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QGroupBox>

class WebSocketBackend;

class WebSocketWidget : public ToolWidget {
    Q_OBJECT

public:
    explicit WebSocketWidget(QWidget* parent = nullptr);
    ~WebSocketWidget() override = default;

    // --- ToolWidget 实现 ---
    QString toolId() const override { return "com.deploymaster.websocket.comm"; }
    QString toolName() const override { return "WebSocket 通信"; }
    void onToolStart() override;
    void onToolStop() override;

    // 绑定后端（由 ToolHost 工厂创建后调用）
    void setBackend(WebSocketBackend* backend);

private slots:
    void onStartClicked();
    void onStopClicked();
    void onServerToggled(bool checked);
    void onClientToggled(bool checked);
    void onSubscribeClicked();
    void onPublishClicked();
    void onClearLogClicked();

private:
    void setupUi();
    void appendLog(const QString& msg);

    WebSocketBackend* m_backend = nullptr;

    // UI 控件
    QRadioButton*    m_radioServer    = nullptr;
    QRadioButton*    m_radioClient    = nullptr;
    QGroupBox*       m_groupServer    = nullptr;
    QGroupBox*       m_groupClient    = nullptr;
    QSpinBox*        m_spinPort       = nullptr;
    QCheckBox*       m_chkWss         = nullptr;
    QLineEdit*       m_editUrl        = nullptr;
    QPushButton*     m_btnStart       = nullptr;
    QPushButton*     m_btnStop        = nullptr;
    QLineEdit*       m_editTopic      = nullptr;
    QLineEdit*       m_editMessage    = nullptr;
    QPushButton*     m_btnSubscribe   = nullptr;
    QPushButton*     m_btnPublish     = nullptr;
    QPlainTextEdit*  m_logView        = nullptr;
    QPushButton*     m_btnClearLog    = nullptr;
};
