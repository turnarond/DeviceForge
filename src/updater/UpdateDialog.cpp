/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: UpdateDialog.cpp
 * Date: 2026-07-15
 * Author: turnarond
 * Description: UpdateDialog 实现 — 非模态更新对话框
 */
#include "UpdateDialog.h"
#include "UpdateChecker.h"
#include "utils/FormatUtils.h"
#include <QCloseEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>

UpdateDialog::UpdateDialog(UpdateChecker* checker, QWidget* parent)
    : QDialog(parent), m_checker(checker)
{
    setupUi();
    // 非模态对话框 — 不阻塞主界面
    setModal(false);
    setWindowTitle("DeviceForge 在线更新");
    resize(520, 480);
}

void UpdateDialog::setupUi() {
    auto* main = new QVBoxLayout(this);
    main->setSpacing(10);

    // 版本信息组
    auto* infoGroup = new QGroupBox("版本信息", this);
    auto* infoLayout = new QVBoxLayout(infoGroup);
    m_currentVersion = new QLabel(this);
    m_newVersion = new QLabel(this);
    m_fileSize = new QLabel(this);

    // 当前版本: 取自 CMake 定义的版本宏
    QString cur = QString("v%1.%2.%3")
        .arg(DEVICEFORGE_VERSION_MAJOR)
        .arg(DEVICEFORGE_VERSION_MINOR)
        .arg(DEVICEFORGE_VERSION_PATCH);
    m_currentVersion->setText("当前版本: " + cur);
    infoLayout->addWidget(m_currentVersion);
    infoLayout->addWidget(m_newVersion);
    infoLayout->addWidget(m_fileSize);

    // 下载地址（可点击外链）
    m_downloadUrl = new QLabel(this);
    m_downloadUrl->setTextFormat(Qt::RichText);
    m_downloadUrl->setOpenExternalLinks(true);
    m_downloadUrl->setVisible(false);  // 仅在有 URL 时显示
    infoLayout->addWidget(m_downloadUrl);

    main->addWidget(infoGroup);

    // 更新内容组
    auto* notesGroup = new QGroupBox("更新内容", this);
    auto* notesLayout = new QVBoxLayout(notesGroup);
    m_releaseNotes = new QTextBrowser(this);
    m_releaseNotes->setMaximumHeight(200);
    notesLayout->addWidget(m_releaseNotes);
    main->addWidget(notesGroup);

    // 进度区(默认隐藏,下载时显示)
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setVisible(false);
    m_progressLabel = new QLabel(this);
    m_progressLabel->setVisible(false);
    main->addWidget(m_progress);
    main->addWidget(m_progressLabel);

    // 按钮区 — 取消 + 主操作(琴色按钮,btnPrimary)
    auto* btnLayout = new QHBoxLayout();
    m_cancelBtn = new QPushButton("稍后再说", this);
    m_actionBtn = new QPushButton("下载并更新", this);
    m_actionBtn->setObjectName("btnPrimary");

    connect(m_cancelBtn, &QPushButton::clicked, this, &UpdateDialog::onCancelClicked);
    connect(m_actionBtn, &QPushButton::clicked, this, &UpdateDialog::onActionClicked);

    btnLayout->addStretch();
    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addWidget(m_actionBtn);
    main->addLayout(btnLayout);
}

void UpdateDialog::setReleaseInfo(const ReleaseInfo& info) {
    m_info = info;
    m_newVersion->setText(QString("最新版本: %1").arg(QString::fromStdString(info.tagName)));
    m_fileSize->setText(QString("文件大小: %1").arg(formatFileSize(info.assetSize)));
    // 下载直链（可点击，浏览器打开）
    if (!info.downloadUrl.empty()) {
        QString url = QString::fromStdString(info.downloadUrl);
        m_downloadUrl->setText(QString("<a href='%1' style='color:#F0A030;'>%1</a>").arg(url));
        m_downloadUrl->setToolTip("在浏览器中打开下载 (GitHub)");
        m_downloadUrl->setVisible(true);
    }
    m_releaseNotes->setMarkdown(QString::fromStdString(info.body));
}

void UpdateDialog::setState(UpdateState state) {
    switch (state) {
    case UpdateState::Ready:
        // 有新版本:启用主按钮,进度区隐藏
        m_actionBtn->setText("下载并更新");
        m_actionBtn->setEnabled(true);
        m_progress->setVisible(false);
        m_progressLabel->setVisible(false);
        break;
    case UpdateState::Downloading:
        // 下载中:主按钮禁用,进度区可见
        m_actionBtn->setText("正在下载...");
        m_actionBtn->setEnabled(false);
        m_progress->setVisible(true);
        m_progress->setValue(0);
        m_progressLabel->setVisible(true);
        break;
    case UpdateState::Installed:
        // 下载+校验完成,等待用户点击重启
        m_actionBtn->setText("安装并重启");
        m_actionBtn->setEnabled(true);
        m_progress->setValue(100);
        m_progressLabel->setText("下载完成,点击安装并重启");
        break;
    case UpdateState::Error:
        // 失败可重试(触发重新查询)
        m_actionBtn->setText("重试");
        m_actionBtn->setEnabled(true);
        break;
    case UpdateState::Idle:
        // 已是最新版本
        m_actionBtn->setText("已是最新版本");
        m_actionBtn->setEnabled(false);
        break;
    case UpdateState::Checking:
    default:
        // Checking 中:不显示/不修改 UI(由 onCancelClicked 允许取消)
        break;
    }
}

void UpdateDialog::setProgress(int pct, int64_t downloaded, int64_t total) {
    m_progress->setValue(pct);
    m_progressLabel->setText(
        QString("已下载: %1 / %2").arg(formatFileSize(downloaded)).arg(formatFileSize(total)));
}

void UpdateDialog::onActionClicked() {
    // 根据当前状态分发动作
    auto state = m_checker->state();
    if (state == UpdateState::Ready) {
        m_checker->downloadUpdate();
    } else if (state == UpdateState::Installed) {
        m_checker->installUpdate();
    } else if (state == UpdateState::Error) {
        m_checker->checkForUpdate();
    }
}

void UpdateDialog::onCancelClicked() {
    // 下载中点击取消 -> 先通知 checker 终止下载,再关闭对话框
    if (m_checker->state() == UpdateState::Downloading) {
        m_checker->cancelDownload();
    }
    close();
}

// 点击窗口标题栏 X 关闭 = 取消当前操作,状态回 idle
void UpdateDialog::closeEvent(QCloseEvent* event) {
    if (m_checker && m_checker->state() == UpdateState::Downloading) {
        m_checker->cancelDownload();
    }
    QDialog::closeEvent(event);
}
