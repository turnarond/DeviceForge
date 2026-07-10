/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: TelnetWidget.h
 *
 * Date: 2026-07-05
 *
 * Author: turnarond
 *
 * Description: Telnet 批量命令 Tool 前端 — 继承 ToolWidget，提供标准三段式布局：
 *              [配置] → [命令] → [结果列表 + 详情]
 */

#pragma once
#include "framework/ToolWidget.h"
#include <QSpinBox>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QStringList>

class TelnetBackend;
class DeviceBusWidget;

class TelnetWidget : public ToolWidget {
    Q_OBJECT

public:
    explicit TelnetWidget(QWidget* parent = nullptr);
    ~TelnetWidget() override = default;

    // --- ToolWidget 实现 ---
    QString toolId() const override { return "com.deploymaster.telnet.command"; }
    QString toolName() const override { return "批量命令"; }
    void onToolStart() override;
    void onToolStop() override;

    // 绑定后端（由 ToolHost 工厂创建后调用）
    void setBackend(TelnetBackend* backend);

    // 设置设备总线引用（用于获取 IP 列表和凭证）
    void setDeviceBusWidget(DeviceBusWidget* deviceBus);

private slots:
    void onExecuteClicked();
    void onStopClicked();
    void onTreeItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onCopyOutputClicked();
    void onSaveOutputClicked();
    void onExportAllClicked();

private:
    void setupUi();
    void appendLog(const QString& msg);
    void clearResults();

    TelnetBackend* m_backend = nullptr;
    DeviceBusWidget* m_deviceBus = nullptr;

    // UI 控件
    QSpinBox*        m_timeoutSpin    = nullptr;
    QPlainTextEdit*  m_cmdEdit        = nullptr;
    QPushButton*     m_executeBtn     = nullptr;
    QPushButton*     m_stopBtn        = nullptr;
    QTreeWidget*     m_resultTree     = nullptr;
    QTextEdit*       m_detailEdit     = nullptr;
    QPushButton*     m_copyBtn        = nullptr;
    QPushButton*     m_saveBtn        = nullptr;
    QPushButton*     m_exportBtn      = nullptr;
};
