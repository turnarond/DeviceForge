/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * File: NetRelayWidget.h
 *
 * Date: 2026-07-08
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: 网络中继调试 Tool 前端 — 继承 ToolWidget，纯代码构建 UI。
 *              提供协议选择、地址配置、会话列表、Hex+ASCII 实时十六进制视图、导出功能。
 */

#pragma once
#include "framework/ToolWidget.h"
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QGroupBox>
#include <QSplitter>
#include <QLabel>
#include <QTreeWidget>
#include <QProgressBar>
#include <QByteArray>

class NetRelayBackend;
enum class RelayDirection;
enum class RelayProtocol;

class NetRelayWidget : public ToolWidget {
    Q_OBJECT

public:
    explicit NetRelayWidget(QWidget* parent = nullptr);
    ~NetRelayWidget() override = default;

    // --- ToolWidget 实现 ---
    QString toolId() const override { return "com.deviceforge.net.relay"; }
    QString toolName() const override { return "网络中继"; }
    void onToolStart() override;
    void onToolStop() override;

    void setBackend(NetRelayBackend* backend);

private slots:
    void onStartClicked();
    void onStopClicked();
    void onExportClicked();
    void onClearClicked();
    void onRecordBrowse();
    void onReplayBrowse();
    void onReplayStart();
    void onReplayPause();
    void onReplayStop();

private:
    void setupUi();
    void appendLog(const QString& msg);
    void appendHexView(RelayDirection dir, const QString& peer, int sessionId, const QByteArray& data);
    void updateSession(const struct RelaySession& session);
    QString formatHexDump(const QByteArray& data);
    void setRelayControlsEnabled(bool enabled);   // 回放时禁用中继控件，反之亦然

    NetRelayBackend* m_backend = nullptr;

    // 配置区
    QComboBox*  m_comboProtocol  = nullptr;
    QLineEdit*  m_editListenAddr = nullptr;
    QSpinBox*   m_spinListenPort = nullptr;
    QLineEdit*  m_editUpstreamHost = nullptr;
    QSpinBox*   m_spinUpstreamPort = nullptr;

    // 控制按钮
    QPushButton* m_btnStart  = nullptr;
    QPushButton* m_btnStop   = nullptr;
    QPushButton* m_btnExport = nullptr;
    QPushButton* m_btnClear  = nullptr;

    // 录制
    QCheckBox*   m_chkRecord    = nullptr;
    QLineEdit*   m_editRecPath  = nullptr;
    QPushButton* m_btnRecBrowse = nullptr;

    // 回放
    QLineEdit*    m_editReplayFile  = nullptr;
    QPushButton*  m_btnReplayBrowse = nullptr;
    QLineEdit*    m_editReplayHost  = nullptr;
    QSpinBox*     m_spinReplayPort  = nullptr;
    QComboBox*    m_comboSpeed      = nullptr;
    QPushButton*  m_btnReplayStart  = nullptr;
    QPushButton*  m_btnReplayPause  = nullptr;
    QPushButton*  m_btnReplayStop   = nullptr;
    QProgressBar* m_replayProgress  = nullptr;

    // 会话列表
    QTreeWidget*     m_sessionTree = nullptr;

    // Hex 视图
    QPlainTextEdit*  m_hexView = nullptr;

    // 日志区
    QPlainTextEdit*  m_logView = nullptr;

    // 导出缓冲区（完整 hex dump 文本，不受 m_hexView maxBlockCount 限制）
    QString          m_exportBuffer;
    static constexpr int kMaxExportChars = 5 * 1024 * 1024; // 5 MB 上限
    static constexpr int kMaxHexRenderBytes = 64 * 1024;    // 单块最多渲染 64 KB，防 UI 尖峰
};
