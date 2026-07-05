/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * File: FtpDeployWidget.h
 *
 * Date: 2026-07-04
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: FTP 部署 Tool 前端 — 继承 ToolWidget，提供标准三段式布局：
 *              [配置] → [操作] → [文件列表 + 进度 + 日志]
 */

#pragma once
#include "framework/ToolWidget.h"
#include <QLineEdit>
#include <QTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QCheckBox>
#include <QListWidget>
#include <QSpinBox>
#include <QGridLayout>

class FtpDeployBackend;

class FtpDeployWidget : public ToolWidget {
    Q_OBJECT

public:
    explicit FtpDeployWidget(QWidget* parent = nullptr);
    ~FtpDeployWidget() override = default;

    // --- ToolWidget 实现 ---
    QString toolId() const override { return "com.deploymaster.ftp.deploy"; }
    QString toolName() const override { return "文件部署"; }
    void onToolStart() override;
    void onToolStop() override;

    // 绑定后端（由 ToolHost 工厂创建后调用）
    void setBackend(FtpDeployBackend* backend);

private slots:
    void onAddFilesClicked();
    void onAddFolderClicked();
    void onClearFilesClicked();
    void onDeployClicked();

private:
    void setupUi();
    void appendLog(const QString& msg);

    FtpDeployBackend* m_backend = nullptr;

    // UI 控件
    QLineEdit*    m_remotePathEdit = nullptr;
    QSpinBox*     m_timeoutSpin    = nullptr;
    QCheckBox*    m_clearCheck     = nullptr;
    QCheckBox*    m_rebootCheck    = nullptr;
    QCheckBox*    m_ftpsCheck      = nullptr;
    QListWidget*  m_fileList       = nullptr;
    QPushButton*  m_deployBtn      = nullptr;
    QPushButton*  m_cancelBtn      = nullptr;
    QProgressBar* m_progressBar    = nullptr;
    QTextEdit*    m_logView        = nullptr;
};
