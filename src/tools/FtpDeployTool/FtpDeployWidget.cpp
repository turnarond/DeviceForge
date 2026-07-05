/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * File: FtpDeployWidget.cpp
 *
 * Date: 2026-07-04
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: FTP 部署 Tool 前端实现 — 标准三段式布局
 *              [配置] → [操作] → [文件列表 + 进度 + 日志]
 */

#include "FtpDeployWidget.h"
#include "FtpDeployBackend.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QFileDialog>
#include <QDateTime>

FtpDeployWidget::FtpDeployWidget(QWidget* parent)
    : ToolWidget(parent)
{
    setupUi();
}

void FtpDeployWidget::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    // === FTP 配置区（增大设置空间） ===
    auto* configGroup = new QGroupBox("FTP 设置", this);
    auto* configLayout = new QGridLayout(configGroup);
    configLayout->setSpacing(6);

    // 行 0: 远程路径
    configLayout->addWidget(new QLabel("远程路径:", this), 0, 0);
    m_remotePathEdit = new QLineEdit("/app", this);
    m_remotePathEdit->setPlaceholderText("/app");
    configLayout->addWidget(m_remotePathEdit, 0, 1, 1, 3);

    // 行 1: 超时设置
    configLayout->addWidget(new QLabel("超时(秒):", this), 1, 0);
    m_timeoutSpin = new QSpinBox(this);
    m_timeoutSpin->setRange(5, 600);
    m_timeoutSpin->setValue(30);
    configLayout->addWidget(m_timeoutSpin, 1, 1);

    // 行 2（原行 1）: 安全选项
    m_ftpsCheck = new QCheckBox("FTPS 加密传输", this);
    m_ftpsCheck->setToolTip("使用 FTP over TLS 加密，防止凭证和文件在网络中被窃听");
    configLayout->addWidget(m_ftpsCheck, 1, 2, 1, 2);

    m_clearCheck = new QCheckBox("部署前清空远程目录", this);
    configLayout->addWidget(m_clearCheck, 2, 0, 1, 2);

    // 行 3（原行 3）: 部署选项
    m_rebootCheck = new QCheckBox("部署完成后重启设备", this);
    configLayout->addWidget(m_rebootCheck, 2, 2, 1, 2);

    mainLayout->addWidget(configGroup);

    // === 操作区 ===
    auto* actionGroup = new QGroupBox("操作", this);
    auto* actionLayout = new QHBoxLayout(actionGroup);

    auto* addFilesBtn = new QPushButton("+ 添加文件", this);
    auto* addFolderBtn = new QPushButton("+ 添加文件夹", this);
    auto* clearFilesBtn = new QPushButton("清空列表", this);

    connect(addFilesBtn, &QPushButton::clicked, this, &FtpDeployWidget::onAddFilesClicked);
    connect(addFolderBtn, &QPushButton::clicked, this, &FtpDeployWidget::onAddFolderClicked);
    connect(clearFilesBtn, &QPushButton::clicked, this, &FtpDeployWidget::onClearFilesClicked);

    actionLayout->addWidget(addFilesBtn);
    actionLayout->addWidget(addFolderBtn);
    actionLayout->addWidget(clearFilesBtn);
    actionLayout->addStretch();

    m_deployBtn = new QPushButton("开始部署", this);
    m_deployBtn->setObjectName("btnPrimary");
    m_cancelBtn = new QPushButton("取消", this);
    m_cancelBtn->setEnabled(false);
    connect(m_deployBtn, &QPushButton::clicked, this, &FtpDeployWidget::onDeployClicked);
    connect(m_cancelBtn, &QPushButton::clicked, [this]() {
        if (m_backend) m_backend->cancelUpload();
        m_cancelBtn->setEnabled(false);
        appendLog("用户取消部署");
    });

    actionLayout->addWidget(m_deployBtn);
    actionLayout->addWidget(m_cancelBtn);

    mainLayout->addWidget(actionGroup);

    // === 文件列表（缩小占比） ===
    m_fileList = new QListWidget(this);
    m_fileList->setMinimumHeight(60);
    m_fileList->setMaximumHeight(140);
    m_fileList->setAlternatingRowColors(true);
    m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    mainLayout->addWidget(m_fileList);

    // === 进度 ===
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_progressBar->setFormat("%p%");
    m_progressBar->setMaximumHeight(22);
    mainLayout->addWidget(m_progressBar);

    // === 日志区 ===
    m_logView = new QTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setMaximumHeight(100);
    m_logView->setPlaceholderText("部署日志...");
    mainLayout->addWidget(m_logView);

    // 底部留弹性空间
    mainLayout->addStretch(1);
}

void FtpDeployWidget::setBackend(FtpDeployBackend* backend)
{
    m_backend = backend;
    if (!m_backend) return;

    // 绑定 Backend 回调（跨线程安全，通过 Qt::QueuedConnection 回主线程）
    m_backend->setProgressCallback([this](int pct) {
        QMetaObject::invokeMethod(this, [this, pct]() {
            m_progressBar->setValue(pct);
        }, Qt::QueuedConnection);
    });

    m_backend->setLogCallback([this](const std::string& msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            appendLog(QString::fromStdString(msg));
        }, Qt::QueuedConnection);
    });

    m_backend->setFinishedCallback(
        [this](bool ok, const std::vector<std::string>& successes,
               const std::vector<std::string>& failures) {
            QMetaObject::invokeMethod(this, [this, ok, successes, failures]() {
                m_deployBtn->setEnabled(true);
                m_cancelBtn->setEnabled(false);
                m_progressBar->setValue(ok ? 100 : 0);
                QString summary = ok
                    ? QString("部署完成 - 成功: %1, 失败: %2")
                          .arg(successes.size()).arg(failures.size())
                    : "部署失败";
                appendLog(summary);
                emit toolStatusChanged(summary);
            }, Qt::QueuedConnection);
        });
}

void FtpDeployWidget::onToolStart()
{
    appendLog("文件部署工具已就绪");
    emit toolStatusChanged("就绪");
}

void FtpDeployWidget::onToolStop()
{
    appendLog("文件部署工具已停止");
    emit toolStatusChanged("已停止");
}

void FtpDeployWidget::onAddFilesClicked()
{
    QStringList files = QFileDialog::getOpenFileNames(this, "选择要部署的文件");
    for (const auto& f : files) {
        m_fileList->addItem(f);
    }
    appendLog(QString("已添加 %1 个文件").arg(files.size()));
}

void FtpDeployWidget::onAddFolderClicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, "选择要部署的文件夹");
    if (!dir.isEmpty()) {
        m_fileList->addItem(dir + "/*");
        appendLog("已添加文件夹: " + dir);
    }
}

void FtpDeployWidget::onClearFilesClicked()
{
    m_fileList->clear();
    appendLog("文件列表已清空");
}

void FtpDeployWidget::onDeployClicked()
{
    if (!m_backend) {
        appendLog("Backend 未就绪");
        return;
    }
    if (m_fileList->count() == 0) {
        appendLog("请先添加要部署的文件");
        return;
    }

    // 从文件列表提取路径
    std::vector<std::string> files;
    for (int i = 0; i < m_fileList->count(); ++i) {
        files.push_back(m_fileList->item(i)->text().toStdString());
    }

    m_deployBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_progressBar->setValue(0);
    m_logView->clear();

    appendLog("开始部署...");
    appendLog(QString("目标路径: %1").arg(m_remotePathEdit->text()));
    appendLog(QString("文件数量: %1").arg(files.size()));
    emit toolStatusChanged("部署中...");

    m_backend->startUpload(
        files,
        m_remotePathEdit->text().toStdString(),
        m_clearCheck->isChecked(),
        m_rebootCheck->isChecked(),
        m_ftpsCheck->isChecked()
    );
}

void FtpDeployWidget::appendLog(const QString& msg)
{
    QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");
    m_logView->append("[" + ts + "] " + msg);
}
