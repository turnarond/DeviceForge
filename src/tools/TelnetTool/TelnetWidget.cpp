/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * File: TelnetWidget.cpp
 *
 * Date: 2026-07-05
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: Telnet 批量命令 Tool 前端实现 — 标准三段式布局
 *              [配置] → [命令] → [结果列表 + 详情]
 */

#include "TelnetWidget.h"
#include "TelnetBackend.h"
#include "ui/DeviceBusWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QHeaderView>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>

TelnetWidget::TelnetWidget(QWidget* parent)
    : ToolWidget(parent)
{
    setupUi();
}

void TelnetWidget::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    // === 配置区 ===
    auto* configGroup = new QGroupBox("配置", this);
    auto* configLayout = new QHBoxLayout(configGroup);

    configLayout->addWidget(new QLabel("命令超时(秒):", this));
    m_timeoutSpin = new QSpinBox(this);
    m_timeoutSpin->setRange(1, 300);
    m_timeoutSpin->setValue(10);
    m_timeoutSpin->setSuffix(" s");
    m_timeoutSpin->setToolTip("每条命令的最大等待超时时间");
    configLayout->addWidget(m_timeoutSpin);
    configLayout->addStretch();

    mainLayout->addWidget(configGroup);

    // === 命令区 ===
    auto* cmdGroup = new QGroupBox("命令", this);
    auto* cmdLayout = new QVBoxLayout(cmdGroup);

    m_cmdEdit = new QPlainTextEdit(this);
    m_cmdEdit->setPlaceholderText("输入要执行的命令，每行一条命令...\n例如:\nuname -a\nifconfig\nps -ef");
    m_cmdEdit->setMinimumHeight(80);
    m_cmdEdit->setMaximumHeight(150);
    cmdLayout->addWidget(m_cmdEdit);

    auto* btnLayout = new QHBoxLayout();
    m_executeBtn = new QPushButton("执行", this);
    m_executeBtn->setObjectName("btnPrimary");
    m_stopBtn = new QPushButton("停止", this);
    m_stopBtn->setEnabled(false);

    connect(m_executeBtn, &QPushButton::clicked, this, &TelnetWidget::onExecuteClicked);
    connect(m_stopBtn, &QPushButton::clicked, this, &TelnetWidget::onStopClicked);

    btnLayout->addStretch();
    btnLayout->addWidget(m_executeBtn);
    btnLayout->addWidget(m_stopBtn);
    cmdLayout->addLayout(btnLayout);

    mainLayout->addWidget(cmdGroup);

    // === 结果区 ===
    auto* resultGroup = new QGroupBox("执行结果", this);
    auto* resultLayout = new QVBoxLayout(resultGroup);

    // 结果树
    m_resultTree = new QTreeWidget(this);
    m_resultTree->setHeaderLabels({"IP", "状态", "耗时(ms)", "最后输出"});
    m_resultTree->setAlternatingRowColors(true);
    m_resultTree->setRootIsDecorated(false);
    m_resultTree->header()->setStretchLastSection(true);
    m_resultTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_resultTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_resultTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    connect(m_resultTree, &QTreeWidget::itemDoubleClicked,
            this, &TelnetWidget::onTreeItemDoubleClicked);
    resultLayout->addWidget(m_resultTree, 1);

    // 详情输出
    m_detailEdit = new QTextEdit(this);
    m_detailEdit->setReadOnly(true);
    m_detailEdit->setPlaceholderText("双击结果行查看详细输出...");
    m_detailEdit->setMinimumHeight(100);
    resultLayout->addWidget(m_detailEdit, 1);

    // 操作按钮
    auto* resultBtnLayout = new QHBoxLayout();
    m_copyBtn  = new QPushButton("复制输出", this);
    m_saveBtn  = new QPushButton("保存输出", this);
    m_exportBtn = new QPushButton("导出全部结果", this);

    connect(m_copyBtn,  &QPushButton::clicked, this, &TelnetWidget::onCopyOutputClicked);
    connect(m_saveBtn,  &QPushButton::clicked, this, &TelnetWidget::onSaveOutputClicked);
    connect(m_exportBtn, &QPushButton::clicked, this, &TelnetWidget::onExportAllClicked);

    resultBtnLayout->addWidget(m_copyBtn);
    resultBtnLayout->addWidget(m_saveBtn);
    resultBtnLayout->addWidget(m_exportBtn);
    resultBtnLayout->addStretch();
    resultLayout->addLayout(resultBtnLayout);

    mainLayout->addWidget(resultGroup, 1);
}

void TelnetWidget::setBackend(TelnetBackend* backend)
{
    m_backend = backend;
    if (!m_backend) return;

    // 绑定 Backend 回调（跨线程安全，通过 Qt::QueuedConnection 回主线程）
    m_backend->setLogCallback([this](const std::string& msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            appendLog(QString::fromStdString(msg));
        }, Qt::QueuedConnection);
    });

    m_backend->setResultCallback(
        [this](const std::string& ip, bool success, int elapsedMs,
               const std::string& output) {
            QMetaObject::invokeMethod(this, [this, ip, success, elapsedMs, output]() {
                // 在结果树中查找或创建对应行
                QString qIp = QString::fromStdString(ip);
                QString status = success ? "成功" : "失败";
                QString outputPreview = QString::fromStdString(output)
                    .left(100).replace('\n', ' ').replace('\r', "");

                // 更新已有行
                bool found = false;
                for (int i = 0; i < m_resultTree->topLevelItemCount(); ++i) {
                    auto* item = m_resultTree->topLevelItem(i);
                    if (item->text(0) == qIp) {
                        item->setText(1, status);
                        item->setText(2, QString::number(elapsedMs));
                        item->setText(3, outputPreview);
                        item->setData(0, Qt::UserRole, QString::fromStdString(output));
                        item->setData(1, Qt::UserRole, success);
                        // 设置行颜色
                        QColor color = success ? QColor("#4caf50") : QColor("#f44336");
                        item->setForeground(1, color);
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    auto* item = new QTreeWidgetItem(m_resultTree);
                    item->setText(0, qIp);
                    item->setText(1, status);
                    item->setText(2, QString::number(elapsedMs));
                    item->setText(3, outputPreview);
                    item->setData(0, Qt::UserRole, QString::fromStdString(output));
                    item->setData(1, Qt::UserRole, success);
                    QColor color = success ? QColor("#4caf50") : QColor("#f44336");
                    item->setForeground(1, color);
                    m_resultTree->addTopLevelItem(item);
                }
            }, Qt::QueuedConnection);
        });

    m_backend->setFinishedCallback(
        [this](int total, int success, int failed) {
            QMetaObject::invokeMethod(this, [this, total, success, failed]() {
                m_executeBtn->setEnabled(true);
                m_stopBtn->setEnabled(false);
                QString summary = QString("执行完毕 — 总计: %1, 成功: %2, 失败: %3")
                    .arg(total).arg(success).arg(failed);
                appendLog(summary);
                emit toolStatusChanged(summary);
            }, Qt::QueuedConnection);
        });
}

void TelnetWidget::setDeviceBusWidget(DeviceBusWidget* deviceBus)
{
    m_deviceBus = deviceBus;
}

void TelnetWidget::onToolStart()
{
    appendLog("批量命令工具已就绪");
    emit toolStatusChanged("就绪");
}

void TelnetWidget::onToolStop()
{
    appendLog("批量命令工具已停止");
    emit toolStatusChanged("已停止");
}

void TelnetWidget::onExecuteClicked()
{
    if (!m_backend) {
        QMessageBox::warning(this, "错误", "Backend 未就绪");
        return;
    }

    // 从设备总线获取目标 IP 列表
    if (!m_deviceBus) {
        QMessageBox::warning(this, "错误", "设备总线未关联");
        return;
    }

    auto devices = m_deviceBus->allDevices();
    if (devices.empty()) {
        QMessageBox::warning(this, "未设置目标", "请在设备总线中添加至少一个目标设备。");
        return;
    }

    // 获取命令
    QString cmdText = m_cmdEdit->toPlainText().trimmed();
    if (cmdText.isEmpty()) {
        QMessageBox::warning(this, "无命令", "请在命令编辑区输入要执行的命令。");
        return;
    }

    // 解析命令列表
    QStringList cmdLines = cmdText.split('\n', Qt::SkipEmptyParts);
    std::vector<std::string> commands;
    for (const auto& line : cmdLines) {
        QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            commands.push_back(trimmed.toStdString());
        }
    }

    if (commands.empty()) {
        QMessageBox::warning(this, "无命令", "命令行为空。");
        return;
    }

    // 获取凭证
    QString user = m_deviceBus->user();
    QString pass = m_deviceBus->password();

    // 提取 IP 列表
    std::vector<std::string> ips;
    for (const auto& dev : devices) {
        ips.push_back(dev.ip);
    }

    // 清除旧结果
    clearResults();

    // UI 状态切换
    m_executeBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);

    int timeoutSec = m_timeoutSpin->value();
    appendLog(QString("开始执行命令，目标: %1 台设备，超时: %2 秒")
        .arg(ips.size()).arg(timeoutSec));
    appendLog(QString("命令列表: %1 条").arg(commands.size()));
    emit toolStatusChanged("执行中...");

    // 绑定设备与凭证到 Backend
    AuthInfo auth;
    auth.user = user.toStdString();
    auth.password = pass.toStdString();
    m_backend->bindCredentials(auth);

    std::vector<DeviceInfo> deviceInfos;
    for (const auto& dev : devices) {
        DeviceInfo di;
        di.ip = dev.ip;
        di.port = 23;
        di.protocol = "telnet";
        deviceInfos.push_back(di);
    }
    m_backend->bindDevices(deviceInfos);

    // 预填充结果树（每台设备一行，初始状态"等待执行"）
    for (const auto& ip : ips) {
        auto* item = new QTreeWidgetItem(m_resultTree);
        item->setText(0, QString::fromStdString(ip));
        item->setText(1, "等待执行");
        item->setText(2, "0");
        item->setText(3, "");
        m_resultTree->addTopLevelItem(item);
    }

    // 启动后端执行
    m_backend->executeCommand(ips, commands, timeoutSec);
}

void TelnetWidget::onStopClicked()
{
    if (m_backend) {
        m_backend->cancel();
    }
    m_stopBtn->setEnabled(false);
    appendLog("用户取消执行");
    emit toolStatusChanged("已取消");
}

void TelnetWidget::onTreeItemDoubleClicked(QTreeWidgetItem* item, int /*column*/)
{
    if (!item) return;
    QString output = item->data(0, Qt::UserRole).toString();
    m_detailEdit->setPlainText(output);
}

void TelnetWidget::onCopyOutputClicked()
{
    QString text = m_detailEdit->toPlainText();
    if (text.isEmpty()) {
        // 尝试复制当前选中行的输出
        auto* item = m_resultTree->currentItem();
        if (item) {
            text = item->data(0, Qt::UserRole).toString();
        }
    }
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text);
        appendLog("输出已复制到剪贴板");
    }
}

void TelnetWidget::onSaveOutputClicked()
{
    QString text = m_detailEdit->toPlainText();
    auto* item = m_resultTree->currentItem();
    if (text.isEmpty() && item) {
        text = item->data(0, Qt::UserRole).toString();
    }

    QString path = QFileDialog::getSaveFileName(this, "保存输出到文件",
        QString(), "Text Files (*.txt);;All Files (*)");
    if (!path.isEmpty()) {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QTextStream ts(&f);
            ts << text;
            f.close();
            appendLog("输出已保存: " + path);
        }
    }
}

void TelnetWidget::onExportAllClicked()
{
    if (m_resultTree->topLevelItemCount() == 0) {
        QMessageBox::information(this, "提示", "没有可导出的结果。");
        return;
    }

    QString path = QFileDialog::getSaveFileName(this, "导出全部结果",
        QString(), "Text Files (*.txt);;All Files (*)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;

    QTextStream ts(&f);
    ts << "===== Telnet 批量命令执行结果 =====\n";
    ts << "导出时间: " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "\n\n";

    for (int i = 0; i < m_resultTree->topLevelItemCount(); ++i) {
        auto* item = m_resultTree->topLevelItem(i);
        QString ip     = item->text(0);
        QString status = item->text(1);
        QString elapsed = item->text(2);
        QString output = item->data(0, Qt::UserRole).toString();

        ts << "==== " << ip << " ====\n";
        ts << "状态: " << status << " | 耗时: " << elapsed << "ms\n";
        ts << "--- 输出 ---\n";
        ts << output << "\n\n";
    }

    f.close();
    appendLog("全部结果已导出: " + path);
}

void TelnetWidget::appendLog(const QString& msg)
{
    // 将日志追加到详情区域（也作为操作日志视图使用）
    QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    m_detailEdit->appendPlainText("[" + ts + "] " + msg);
}

void TelnetWidget::clearResults()
{
    m_resultTree->clear();
    m_detailEdit->clear();
}
