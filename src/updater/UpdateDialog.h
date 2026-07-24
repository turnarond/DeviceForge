/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: UpdateDialog.h
 * Date: 2026-07-15
 * Author: turnarond
 * Description: 在线更新对话框 — 非模态 QDialog，展示版本信息/Release Notes/下载进度
 */
#pragma once
#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTextBrowser>
#include "UpdateTypes.h"

class UpdateChecker;

// UpdateDialog — 在线更新提示框
// 非模态，不阻塞主界面;通过回调驱动 UI 状态切换
class UpdateDialog : public QDialog {
    Q_OBJECT
public:
    explicit UpdateDialog(UpdateChecker* checker, QWidget* parent = nullptr);

    // 刷新远端 Release 信息(版本号/文件大小/Release Notes)
    void setReleaseInfo(const ReleaseInfo& info);
    // 切换 UI 状态机(根据 UpdateState 决定按钮文字、可用性、进度条可见性)
    void setState(UpdateState state);
    // 进度条更新(百分比/已下载/总数)
    void setProgress(int pct, int64_t downloaded, int64_t total);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onActionClicked();
    void onCancelClicked();

private:
    void setupUi();

    UpdateChecker* m_checker;
    ReleaseInfo m_info;

    QLabel*       m_currentVersion = nullptr;
    QLabel*       m_newVersion     = nullptr;
    QLabel*       m_fileSize       = nullptr;
    QLabel*       m_downloadUrl    = nullptr;
    QTextBrowser* m_releaseNotes   = nullptr;
    QProgressBar* m_progress       = nullptr;
    QLabel*       m_progressLabel  = nullptr;
    QPushButton*  m_actionBtn      = nullptr;
    QPushButton*  m_cancelBtn      = nullptr;
};
