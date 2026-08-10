/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: FtpDeployWidget.cpp（双面板宿主重构 — Task 4）
 * Date: 2026-07-25
 * Author: turnarond
 *
 * Description: FTP 部署双面板宿主实现。批量部署链路零改动（FtpDeployBackend 不变）；
 *              文件浏览/导航/右键操作/拖拽全部下沉 FileBrowserPanel（Task 2/3），
 *              本类仅保留：工具级批量部署配置（协议/设备/FTPS/端口/清空/重启）+
 *              部署入口（校验 → collectLocalFiles → startUpload）+ 远程源按
 *              协议/设备变化重建（RemoteFileSource 注入右侧面板）。
 */

#include "FtpDeployWidget.h"
#include "FtpDeployBackend.h"
#include "MultiProgressWidget.h"
#include "framework/DeviceInfo.h"
#include "ui/DeviceBusWidget.h"
#include "ui/FileBrowserPanel.h"
#include "ui/IFileSource.h"
#include "ui/LocalFileSource.h"
#include "ui/RemoteFileSource.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
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
    mainLayout->setSpacing(6);

    setupToolbar(mainLayout);

    // === 双栏区域 (QSplitter)：FileBrowserPanel 组装 ===
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(2);

    m_leftPanel = new FileBrowserPanel(this);
    m_leftPanel->setObjectName("localPanel");
    m_leftPanel->setSource(std::make_shared<LocalFileSource>());

    m_rightPanel = new FileBrowserPanel(this);
    m_rightPanel->setObjectName("remotePanel");
    // 远程源延迟到协议/设备确定后 setSource（onRefreshRemote 首次调用时注入）

    // 面板互指（F5 复制/F6 移动/Tab 切换/拖拽方向语义依赖 peer）
    m_leftPanel->setPeerPanel(m_rightPanel);
    m_rightPanel->setPeerPanel(m_leftPanel);

    m_splitter->addWidget(m_leftPanel);
    m_splitter->addWidget(m_rightPanel);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 1);
    mainLayout->addWidget(m_splitter, 1);

    setupBottomBar(mainLayout);
    connectBackendSignals();
}

void FtpDeployWidget::setupToolbar(QVBoxLayout* mainLayout)
{
    auto* toolbarWidget = new QWidget(this);
    auto* toolbar = new QGridLayout(toolbarWidget);
    toolbar->setSpacing(6);

    // 行 0: 协议 | 目标设备（浏览设备选择；批量部署目标为设备总线选中设备）
    toolbar->addWidget(new QLabel("协议:", this), 0, 0);
    m_protocolCombo = new QComboBox(this);
    m_protocolCombo->addItem("FTP");
    m_protocolCombo->addItem("SFTP");
    m_protocolCombo->setFixedWidth(80);
    connect(m_protocolCombo, &QComboBox::currentTextChanged, this, [this](const QString& proto) {
        if (proto == "SFTP") {
            m_portSpin->setValue(22);
            m_ftpsCheck->setEnabled(false);
        } else {
            m_portSpin->setValue(21);
            m_ftpsCheck->setEnabled(true);
        }
        // 协议变化 → 远程源重建 + 刷新（setSource 导航到根目录）
        onRefreshRemote();
    });
    toolbar->addWidget(m_protocolCombo, 0, 1);

    toolbar->addWidget(new QLabel("目标设备:", this), 0, 2);
    m_deviceCombo = new QComboBox(this);
    m_deviceCombo->setMinimumWidth(160);
    m_deviceCombo->setToolTip("选择目标设备（多选支持——从设备总线同步）");
    // 设备切换 → 远程源重建 + 刷新（程序化填充期间 blockSignals，见 setDeviceBusWidget）
    connect(m_deviceCombo, &QComboBox::currentTextChanged, this,
            [this](const QString&) { onRefreshRemote(); });
    toolbar->addWidget(m_deviceCombo, 0, 3);

    // 行 1: 端口 | FTPS | 清空 | 重启
    toolbar->addWidget(new QLabel("端口:", this), 1, 0);
    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(21);
    toolbar->addWidget(m_portSpin, 1, 1);

    m_ftpsCheck = new QCheckBox("FTPS 加密", this);
    toolbar->addWidget(m_ftpsCheck, 1, 2);

    m_clearCheck = new QCheckBox("部署前清空远程", this);
    toolbar->addWidget(m_clearCheck, 1, 3);

    m_rebootCheck = new QCheckBox("部署后重启", this);
    toolbar->addWidget(m_rebootCheck, 1, 4);

    mainLayout->addWidget(toolbarWidget);
}

void FtpDeployWidget::setupBottomBar(QVBoxLayout* mainLayout)
{
    // 部署按钮栏
    auto* deployRow = new QHBoxLayout();

    m_deployBtn = new QPushButton("▶ 部署", this);
    m_deployBtn->setObjectName("btnPrimary");
    m_deployBtn->setMinimumHeight(32);
    connect(m_deployBtn, &QPushButton::clicked, this, &FtpDeployWidget::onDeployClicked);
    deployRow->addWidget(m_deployBtn);

    deployRow->addStretch();
    mainLayout->addLayout(deployRow);

    // 多设备进度
    m_multiProgress = new MultiProgressWidget(this);
    connect(m_multiProgress, &MultiProgressWidget::cancelRequested, [this]() {
        if (m_backend) m_backend->cancelUpload();
    });
    mainLayout->addWidget(m_multiProgress);
}

void FtpDeployWidget::connectBackendSignals()
{
    if (!m_backend) return;
    m_backend->setProgressCallback([this](int pct) {
        QMetaObject::invokeMethod(this, [this, pct]() {
            m_multiProgress->setOverallProgress(pct);
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
                // 更新每个设备的状态
                for (const auto& key : successes)
                    m_multiProgress->setDeviceStatusByKey(
                        QString::fromStdString(key), true);
                for (const auto& key : failures)
                    m_multiProgress->setDeviceStatusByKey(
                        QString::fromStdString(key), false);
                int total = static_cast<int>(successes.size() + failures.size());
                int done = static_cast<int>(successes.size());
                m_multiProgress->setOverallProgress(ok ? 100 : 0);
                m_multiProgress->setFinishedSummary(done, total);
                if (ok && !successes.empty()) {
                    appendLog(QString("✅ 部署完成 — 成功: %1, 失败: %2")
                        .arg(successes.size()).arg(failures.size()));
                    // 自动刷新远程面板
                    onRefreshRemote();
                } else {
                    appendLog("❌ 部署失败");
                }
            }, Qt::QueuedConnection);
        });
}

void FtpDeployWidget::onDeployClicked()
{
    if (!m_backend) { appendLog("Backend 未就绪"); return; }
    if (!m_deviceBus) { appendLog("设备总线未关联"); return; }

    auto devices = m_deviceBus->selectedDevices();
    if (devices.empty()) devices = m_deviceBus->allDevices(); // 未选中时部署到全部
    if (devices.empty()) { appendLog("错误：设备总线中没有目标设备"); return; }
    auto files = collectLocalFiles();
    if (files.empty()) { appendLog("请先在左侧本地面板中选中要部署的文件"); return; }

    AuthInfo auth;
    auth.user = m_deviceBus->user().toStdString();
    auth.password = m_deviceBus->password().toStdString();
    m_backend->bindCredentials(auth);
    m_backend->bindDevices(devices);

    m_deployBtn->setEnabled(false);
    m_multiProgress->setDeviceCount(static_cast<int>(devices.size()));
    // 设置设备标签为 IP:port
    for (size_t i = 0; i < devices.size(); ++i) {
        QString devKey = QString::fromStdString(devices[i].ip);
        if (devices[i].port > 0 && devices[i].port != 21)
            devKey += ":" + QString::number(devices[i].port);
        m_multiProgress->setDeviceInfo(static_cast<int>(i), devKey);
    }

    appendLog(QString("开始部署到 %1 台设备...").arg(devices.size()));

    // 部署目标目录 = 右侧远程面板当前目录（远程路径栏已在面板内置）；
    // 空路径回退根目录（远程源延迟注入时 currentPath() 为空——SFTP 空路径会
    // 上传到登录 cwd 而非根目录，必须显式回退 "/"）
    QString remotePath = m_rightPanel ? m_rightPanel->currentPath() : QStringLiteral("/");
    if (remotePath.isEmpty()) remotePath = QStringLiteral("/");
    m_backend->startUpload(files,
        remotePath.toStdString(),
        m_clearCheck->isChecked(),
        m_rebootCheck->isChecked(),
        currentProtocol(),
        m_ftpsCheck->isChecked(),
        m_portSpin->value()
    );
}

void FtpDeployWidget::startDeployFromMenu()
{
    if (!m_deployBtn->isEnabled()) {          // 部署进行中（按钮已禁用）
        appendLog("部署正在进行中，请先取消或等待完成");
        return;
    }
    onDeployClicked();
}

void FtpDeployWidget::cancelDeployFromMenu()
{
    if (m_backend) m_backend->cancelUpload();
    appendLog("已从菜单取消部署");
}

std::vector<std::string> FtpDeployWidget::collectLocalFiles() const
{
    std::vector<std::string> files;
    if (!m_leftPanel) return files;
    // 面板 selectedFiles 返回 FtpFileInfo（name 需拼面板当前路径）；
    // 跳过 ".." 导航条目；文件与文件夹均纳入部署（backend 的 uploadFolder 递归处理文件夹）
    const QString base = m_leftPanel->currentPath();
    for (const auto& f : m_leftPanel->selectedFiles()) {
        if (f.name == ".." || f.name.empty()) continue;
        QString full = base;
        if (!full.endsWith('/')) full += '/';
        full += QString::fromStdString(f.name);
        files.push_back(full.toStdString());
    }
    return files;
}

void FtpDeployWidget::onRefreshRemote()
{
    if (!m_deviceBus || m_deviceBus->allDevices().empty()) {
        appendLog("错误：设备总线中没有目标设备");
        return;
    }
    if (!m_rightPanel) return;

    // 防止并发刷新（远程源适配器非线程安全，多次触发会同时操作同一连接）
    if (m_remoteBusy.load()) {
        appendLog("正在刷新中，请稍候...");
        return;
    }
    m_remoteBusy = true;

    // 从设备下拉框获取当前选中设备
    QString deviceIp = m_deviceCombo->currentText();
    if (deviceIp.isEmpty()) {
        deviceIp = QString::fromStdString(m_deviceBus->allDevices()[0].ip);
    }

    const int port = m_portSpin->value();
    const bool useFtps = m_ftpsCheck->isChecked();
    const QString proto = QString::fromStdString(currentProtocol());

    // 协议/设备/端口/FTPS 变化时重建远程源（重建后 setSource 导航到根目录）
    const bool configChanged = !m_remoteSource
        || proto != m_remoteSrcProto
        || deviceIp != m_remoteSrcDevice
        || port != m_remoteSrcPort
        || useFtps != m_remoteSrcUseFtps;

    DeviceInfo dev;
    dev.ip = deviceIp.toStdString();
    dev.port = port;
    AuthInfo auth;
    auth.user = m_deviceBus->user().toStdString();
    auth.password = m_deviceBus->password().toStdString();

    if (configChanged) {
        auto source = std::make_shared<RemoteFileSource>(
            proto, deviceIp + " (" + m_protocolCombo->currentText() + ")");
        source->setUseFtps(useFtps);
        if (!source->connect(dev, auth)) {
            appendLog("远程连接失败: " + source->lastError());
            m_remoteBusy = false;
            return;
        }
        m_remoteSource = source;
        m_remoteSrcDevice = deviceIp;
        m_remoteSrcProto = proto;
        m_remoteSrcPort = port;
        m_remoteSrcUseFtps = useFtps;
        m_rightPanel->setSource(source);
    } else if (!m_remoteSource->isConnected()) {
        // 连接失效原位重连（保留当前面板路径，复用旧连接缓存语义）
        if (!m_remoteSource->connect(dev, auth)) {
            appendLog("远程重连失败: " + m_remoteSource->lastError());
            m_remoteBusy = false;
            return;
        }
    }

    m_remoteBusy = false;
    m_rightPanel->refresh();
}

void FtpDeployWidget::setBackend(FtpDeployBackend* backend)
{
    m_backend = backend;
    connectBackendSignals();
}

void FtpDeployWidget::setDeviceBusWidget(DeviceBusWidget* deviceBus)
{
    m_deviceBus = deviceBus;

    // 同步设备下拉框（填充期间 blockSignals，避免 currentTextChanged 触发远程连接）
    if (m_deviceBus) {
        m_deviceCombo->blockSignals(true);
        m_deviceCombo->clear();
        for (const auto& d : m_deviceBus->allDevices()) {
            m_deviceCombo->addItem(QString::fromStdString(d.ip));
        }
        m_deviceCombo->blockSignals(false);

        // 设备总线变更时同步下拉框（同样屏蔽信号，不触发远程源重建）
        connect(m_deviceBus, &DeviceBusWidget::deviceSelectionChanged, this, [this]() {
            m_deviceCombo->blockSignals(true);
            m_deviceCombo->clear();
            for (const auto& d : m_deviceBus->allDevices()) {
                m_deviceCombo->addItem(QString::fromStdString(d.ip));
            }
            m_deviceCombo->blockSignals(false);
        });
    }
}

void FtpDeployWidget::onToolStart()
{
    appendLog("文件部署工具已就绪 — 左侧选择本地文件，点击「部署」上传到右侧当前远程目录");
    emit toolStatusChanged("就绪");
}

void FtpDeployWidget::onToolStop()
{
    appendLog("文件部署工具已停止");
    emit toolStatusChanged("已停止");
}

void FtpDeployWidget::appendLog(const QString& msg)
{
    if (m_globalLogCb) {
        QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");
        m_globalLogCb("[" + ts + "] " + msg);
    }
}

std::string FtpDeployWidget::currentProtocol() const
{
    return m_protocolCombo->currentText() == "SFTP" ? "ssh" : "ftp";
}
