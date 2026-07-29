/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: FtpDeployWidget.cpp (v2.4 双栏重构)
 * Date: 2026-07-25
 * Author: turnarond
 *
 * Description: FTP 部署双栏文件管理器实现。
 */

#include "FtpDeployWidget.h"
#include "FtpDeployBackend.h"
#include "RemoteFileModel.h"
#include "MultiProgressWidget.h"
#include "adapter/ProtocolRegistry.h"
#include "adapter/FtpAdapter.h"
#include "adapter/SshAdapter.h"
#include "ui/DeviceBusWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QApplication>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QDateTime>
#include <QtConcurrent/QtConcurrent>

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

    // === 双栏区域 (QSplitter) ===
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(2);

    setupLocalPanel();
    setupRemotePanel();

    // 使用成员变量（替代旧的 setProperty 传递指针）
    m_splitter->addWidget(m_localPanel);
    m_splitter->addWidget(m_remotePanel);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 4);
    mainLayout->addWidget(m_splitter, 1);

    setAcceptDrops(true);

    setupBottomBar(mainLayout);
    connectBackendSignals();
}

void FtpDeployWidget::setupToolbar(QVBoxLayout* mainLayout)
{
    auto* toolbarWidget = new QWidget(this);
    auto* toolbar = new QGridLayout(toolbarWidget);
    toolbar->setSpacing(6);

    // 行 0: 协议 | 目标设备 | 远程路径 | 刷新
    toolbar->addWidget(new QLabel("协议:", this), 0, 0);
    m_protocolCombo = new QComboBox(this);
    m_protocolCombo->addItem("FTP");
    m_protocolCombo->addItem("SFTP");
    m_protocolCombo->setFixedWidth(80);
    connect(m_protocolCombo, &QComboBox::currentTextChanged, this, [this](const QString& proto) {
        m_remoteModel->clear();
        m_currentRemotePath = "/";
        refreshBreadcrumb("/");
        m_remotePathEdit->setText("/");
        if (proto == "SFTP") {
            m_portSpin->setValue(22);
            m_ftpsCheck->setEnabled(false);
        } else {
            m_portSpin->setValue(21);
            m_ftpsCheck->setEnabled(true);
        }
        onRefreshRemote();
    });
    toolbar->addWidget(m_protocolCombo, 0, 1);

    toolbar->addWidget(new QLabel("目标设备:", this), 0, 2);
    m_deviceCombo = new QComboBox(this);
    m_deviceCombo->setMinimumWidth(160);
    m_deviceCombo->setToolTip("选择目标设备（多选支持——从设备总线同步）");
    toolbar->addWidget(m_deviceCombo, 0, 3);

    toolbar->addWidget(new QLabel("远程路径:", this), 0, 4);
    m_remotePathEdit = new QLineEdit("/", this);
    m_remotePathEdit->setPlaceholderText("/");
    connect(m_remotePathEdit, &QLineEdit::returnPressed, this, &FtpDeployWidget::onRefreshRemote);
    toolbar->addWidget(m_remotePathEdit, 0, 5);

    m_refreshBtn = new QPushButton("刷新", this);
    m_refreshBtn->setToolTip("刷新远程目录列表 (F5)");
    connect(m_refreshBtn, &QPushButton::clicked, this, &FtpDeployWidget::onRefreshRemote);
    toolbar->addWidget(m_refreshBtn, 0, 6);

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

void FtpDeployWidget::setupLocalPanel()
{
    auto* localContainer = new QWidget(this);
    auto* localLayout = new QVBoxLayout(localContainer);
    localLayout->setContentsMargins(0, 0, 0, 0);
    localLayout->setSpacing(4);

    auto* header = new QHBoxLayout();
    auto* iconLabel = new QLabel("📁 本地文件", this);
    iconLabel->setStyleSheet("font-weight: bold; color: #C8CCD4;");
    header->addWidget(iconLabel);

    m_localPathEdit = new QLineEdit(this);
    m_localPathEdit->setPlaceholderText("输入路径后回车跳转...");
    connect(m_localPathEdit, &QLineEdit::returnPressed, [this]() {
        QDir dir(m_localPathEdit->text());
        if (dir.exists()) {
            m_localTree->setRootIndex(m_localFsModel->index(dir.absolutePath()));
        }
    });
    header->addWidget(m_localPathEdit, 1);

    auto* openDirBtn = new QPushButton("打开目录", this);
    connect(openDirBtn, &QPushButton::clicked, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "选择本地目录");
        if (!dir.isEmpty()) {
            m_localTree->setRootIndex(m_localFsModel->index(dir));
            m_localPathEdit->setText(dir);
        }
    });
    header->addWidget(openDirBtn);
    localLayout->addLayout(header);

    // QFileSystemModel
    m_localFsModel = new QFileSystemModel(this);
    m_localFsModel->setRootPath(QDir::currentPath());
    m_localFsModel->setFilter(QDir::AllEntries | QDir::Hidden);

    m_localTree = new QTreeView(this);
    m_localTree->setModel(m_localFsModel);
    m_localTree->setRootIndex(m_localFsModel->index(QDir::currentPath()));
    m_localTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_localTree->setDragEnabled(true);
    m_localTree->setAcceptDrops(true);
    m_localTree->setDragDropMode(QAbstractItemView::DragDrop);
    m_localTree->setColumnHidden(1, true);
    m_localTree->viewport()->installEventFilter(this); // 拦截拖拽事件到本地面板
    m_localTree->setColumnHidden(2, true); // 隐藏 type 列
    m_localTree->setColumnHidden(3, true); // 隐藏 date 列
    m_localTree->header()->setStretchLastSection(true);

    // 监听目录切换，更新路径栏
    connect(m_localTree->selectionModel(), &QItemSelectionModel::currentChanged,
        [this](const QModelIndex& current, const QModelIndex&) {
        if (current.isValid()) {
            QFileInfo fi = m_localFsModel->fileInfo(current);
            if (fi.isDir()) {
                m_localPathEdit->setText(fi.absoluteFilePath());
            }
        }
    });

    // 选中变更时触发远程面板精确对比着色
    connect(m_localTree->selectionModel(), &QItemSelectionModel::selectionChanged,
        this, [this]() {
            std::vector<LocalFileInfo> locals;
            QModelIndexList sel = m_localTree->selectionModel()->selectedRows();
            for (const auto& idx : sel) {
                QFileInfo fi = m_localFsModel->fileInfo(idx);
                LocalFileInfo lfi;
                lfi.name = fi.fileName().toStdString();
                lfi.size = fi.size();
                lfi.dateTime = fi.lastModified().toString(Qt::ISODate).toStdString();
                locals.push_back(lfi);
            }
            m_remoteModel->setLocalFilesForCompare(locals);
        });

    localLayout->addWidget(m_localTree, 1);

    m_localPanel = localContainer;
}

void FtpDeployWidget::setupRemotePanel()
{
    auto* remoteContainer = new QWidget(this);
    auto* remoteLayout = new QVBoxLayout(remoteContainer);
    remoteLayout->setContentsMargins(0, 0, 0, 0);
    remoteLayout->setSpacing(4);

    auto* header = new QHBoxLayout();
    auto* iconLabel = new QLabel("📁 远程文件", this);
    iconLabel->setStyleSheet("font-weight: bold; color: #C8CCD4;");
    header->addWidget(iconLabel);

    // 面包屑
    m_breadcrumbWidget = new QWidget(this);
    m_breadcrumbLayout = new QHBoxLayout(m_breadcrumbWidget);
    m_breadcrumbLayout->setContentsMargins(0, 0, 0, 0);
    m_breadcrumbLayout->setSpacing(2);
    header->addWidget(m_breadcrumbWidget, 1);

    remoteLayout->addLayout(header);

    // QTableView + RemoteFileModel
    m_remoteModel = new RemoteFileModel(this);

    m_remoteTable = new QTableView(this);
    m_remoteTable->setModel(m_remoteModel);
    m_remoteTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_remoteTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_remoteTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_remoteTable->setAlternatingRowColors(true);
    m_remoteTable->setContextMenuPolicy(Qt::CustomContextMenu);
    m_remoteTable->setAcceptDrops(true);
    m_remoteTable->viewport()->setAcceptDrops(true);
    m_remoteTable->setDragDropMode(QAbstractItemView::DropOnly);
    m_remoteTable->verticalHeader()->setVisible(false);
    m_remoteTable->viewport()->installEventFilter(this);

    // 列宽
    auto* hdr = m_remoteTable->horizontalHeader();
    hdr->setSectionResizeMode(RemoteFileModel::ColIcon, QHeaderView::Fixed);
    m_remoteTable->setColumnWidth(RemoteFileModel::ColIcon, 32);
    hdr->setSectionResizeMode(RemoteFileModel::ColName, QHeaderView::Stretch);
    hdr->setSectionResizeMode(RemoteFileModel::ColSize, QHeaderView::Fixed);
    m_remoteTable->setColumnWidth(RemoteFileModel::ColSize, 80);
    hdr->setSectionResizeMode(RemoteFileModel::ColDateTime, QHeaderView::Fixed);
    m_remoteTable->setColumnWidth(RemoteFileModel::ColDateTime, 140);
    hdr->setSectionResizeMode(RemoteFileModel::ColPermissions, QHeaderView::Fixed);
    m_remoteTable->setColumnWidth(RemoteFileModel::ColPermissions, 80);

    // 双击进入目录
    connect(m_remoteTable, &QTableView::doubleClicked,
        this, &FtpDeployWidget::onRemoteDirChanged);

    // 右键菜单
    connect(m_remoteTable, &QTableView::customContextMenuRequested,
        this, &FtpDeployWidget::onRemoteContextMenu);

    remoteLayout->addWidget(m_remoteTable, 1);

    m_remotePanel = remoteContainer;
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
                m_multiProgress->setOverallProgress(ok ? 100 : 0);
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

    auto devices = m_deviceBus->allDevices();
    if (devices.empty()) { appendLog("错误：设备总线中没有目标设备"); return; }

    // 收集本地面板中选中的文件路径
    auto files = collectLocalFiles();
    if (files.empty()) { appendLog("请先在左侧本地面板中选中要部署的文件"); return; }

    AuthInfo auth;
    auth.user = m_deviceBus->user().toStdString();
    auth.password = m_deviceBus->password().toStdString();
    m_backend->bindCredentials(auth);
    m_backend->bindDevices(devices);

    m_deployBtn->setEnabled(false);
    m_multiProgress->setDeviceCount(static_cast<int>(devices.size()));
    for (size_t i = 0; i < devices.size(); ++i) {
        m_multiProgress->setDeviceStatus(static_cast<int>(i),
            QString::fromStdString(devices[i].ip), false);
    }

    appendLog(QString("开始部署到 %1 台设备...").arg(devices.size()));

    m_backend->startUpload(files,
        m_remotePathEdit->text().toStdString(),
        m_clearCheck->isChecked(),
        m_rebootCheck->isChecked(),
        m_ftpsCheck->isChecked(),
        m_portSpin->value()
    );
}

std::vector<std::string> FtpDeployWidget::collectLocalFiles() const
{
    std::vector<std::string> files;
    QModelIndexList selection = m_localTree->selectionModel()->selectedRows();
    for (const auto& idx : selection) {
        QString path = m_localFsModel->filePath(idx);
        files.push_back(path.toStdString());
    }
    return files;
}

void FtpDeployWidget::onRefreshRemote()
{
    if (!m_deviceBus || m_deviceBus->allDevices().empty()) {
        appendLog("错误：设备总线中没有目标设备");
        return;
    }

    QString path = m_remotePathEdit->text();
    if (path.isEmpty()) path = "/";

    // 从设备下拉框获取当前选中设备
    QString deviceIp = m_deviceCombo->currentText();
    if (deviceIp.isEmpty()) {
        deviceIp = QString::fromStdString(m_deviceBus->allDevices()[0].ip);
    }

    appendLog(QString("正在加载远程目录: %1:%2 ...").arg(deviceIp, path));

    // 在传入后台线程前复制值，避免数据竞争
    const int port = m_portSpin->value();
    const bool useFtps = m_ftpsCheck->isChecked();
    const std::string user = m_deviceBus ? m_deviceBus->user().toStdString() : "";
    const std::string pass = m_deviceBus ? m_deviceBus->password().toStdString() : "";
    const std::string proto = m_protocolCombo->currentText().toStdString();

    QtConcurrent::run([this, deviceIp, path, port, useFtps, user, pass, proto]() {
        auto adapter = ProtocolRegistry::instance()->create(
            proto == "SFTP" ? "ssh" : "ftp");
        if (!adapter) {
            QMetaObject::invokeMethod(this, [this]() {
                appendLog("适配器不可用");
            }, Qt::QueuedConnection);
            return;
        }

        if (proto == "SFTP") {
            auto* ssh = dynamic_cast<SshAdapter*>(adapter.get());
            DeviceInfo dev;
            dev.ip = deviceIp.toStdString();
            dev.port = port;

            AuthInfo auth;
            auth.user = user;
            auth.password = pass;

            if (!ssh->connect(dev, auth)) {
                QString err = QString::fromStdString(ssh->lastError());
                QMetaObject::invokeMethod(this, [this, err]() {
                    appendLog("SFTP 连接失败: " + err);
                }, Qt::QueuedConnection);
                return;
            }

            auto files = ssh->sftpListDirectory(path.toStdString());
            ssh->disconnect();

            QMetaObject::invokeMethod(this, [this, files]() {
                // 补充 . 和 ..（SylixOS 等嵌入式 FTP 服务器 LIST 不返回）
                auto full = files;
                bool hasDot = false, hasDotDot = false;
                for (const auto& f : full) {
                    if (f.name == ".") hasDot = true;
                    if (f.name == "..") hasDotDot = true;
                }
                if (!hasDotDot) {
                    FtpFileInfo dd; dd.name = ".."; dd.isDir = true;
                    full.insert(full.begin(), dd);
                }
                if (!hasDot) {
                    FtpFileInfo d; d.name = "."; d.isDir = true;
                    full.insert(full.begin(), d);
                }
                m_remoteModel->setFileList(full);
                appendLog(QString("远程目录已加载: %1 项").arg(full.size()));
            }, Qt::QueuedConnection);
        } else {
            auto* ftp = dynamic_cast<FtpAdapter*>(adapter.get());
            DeviceInfo dev;
            dev.ip = deviceIp.toStdString();
            dev.port = port;

            AuthInfo auth;
            auth.user = user;
            auth.password = pass;

            if (useFtps) {
                ftp->setUseFtps(true);
            }

            if (!ftp->connect(dev, auth)) {
                QString err = QString::fromStdString(ftp->lastError());
                QMetaObject::invokeMethod(this, [this, err]() {
                    appendLog("连接失败: " + err);
                }, Qt::QueuedConnection);
                return;
            }

            auto files = ftp->listDirectoryParsed(path.toStdString());
            ftp->disconnect();

            QMetaObject::invokeMethod(this, [this, files]() {
                // 补充 . 和 ..（SylixOS 等嵌入式 FTP 服务器 LIST 不返回）
                auto full = files;
                bool hasDot = false, hasDotDot = false;
                for (const auto& f : full) {
                    if (f.name == ".") hasDot = true;
                    if (f.name == "..") hasDotDot = true;
                }
                if (!hasDotDot) {
                    FtpFileInfo dd; dd.name = ".."; dd.isDir = true;
                    full.insert(full.begin(), dd);
                }
                if (!hasDot) {
                    FtpFileInfo d; d.name = "."; d.isDir = true;
                    full.insert(full.begin(), d);
                }
                m_remoteModel->setFileList(full);
                appendLog(QString("远程目录已加载: %1 项").arg(full.size()));
            }, Qt::QueuedConnection);
        }
    });
}

void FtpDeployWidget::onRemoteDirChanged(const QModelIndex& index)
{
    if (!index.isValid()) return;
    const auto& fi = m_remoteModel->fileAt(index.row());
    if (!fi.isDir) return;

    std::string name = fi.name;
    if (name == ".") {
        // 当前目录 → 刷新
        onRefreshRemote();
        return;
    }
    if (name == "..") {
        // 上级目录
        QString parent = m_currentRemotePath;
        if (parent == "/" || parent.isEmpty()) return; // 已在根目录
        int lastSlash = parent.lastIndexOf('/');
        parent = parent.left(lastSlash);
        if (parent.isEmpty()) parent = "/";
        navigateToRemoteDir(parent);
        return;
    }

    QString newPath = m_currentRemotePath;
    if (!newPath.endsWith('/')) newPath += '/';
    newPath += QString::fromStdString(fi.name);
    navigateToRemoteDir(newPath);
}

void FtpDeployWidget::navigateToRemoteDir(const QString& path)
{
    m_currentRemotePath = path;
    m_remotePathEdit->setText(path);
    refreshBreadcrumb(path);
    onRefreshRemote();
}

void FtpDeployWidget::refreshBreadcrumb(const QString& path)
{
    // 清除旧面包屑
    QLayoutItem* item;
    while ((item = m_breadcrumbLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    QStringList parts = path.split('/', Qt::SkipEmptyParts);
    QString accumulated;

    // 根 "/"
    auto* rootBtn = new QPushButton("/", this);
    rootBtn->setFlat(true);
    rootBtn->setCursor(Qt::PointingHandCursor);
    rootBtn->setStyleSheet("color: #7B8494; padding: 0 4px; border: none;");
    connect(rootBtn, &QPushButton::clicked, [this]() { navigateToRemoteDir("/"); });
    m_breadcrumbLayout->addWidget(rootBtn);

    for (int i = 0; i < parts.size(); ++i) {
        auto* sep = new QLabel(">", this);
        sep->setStyleSheet("color: #333B48; padding: 0 2px;");
        m_breadcrumbLayout->addWidget(sep);

        accumulated += "/" + parts[i];
        QString fullPath = accumulated;

        auto* btn = new QPushButton(parts[i], this);
        btn->setFlat(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            i == parts.size() - 1
                ? "color: #F0A030; font-weight: bold; padding: 0 4px; border: none;"
                : "color: #7B8494; padding: 0 4px; border: none;"
        );
        connect(btn, &QPushButton::clicked, [this, fullPath]() { navigateToRemoteDir(fullPath); });
        m_breadcrumbLayout->addWidget(btn);
    }
    m_breadcrumbLayout->addStretch();
}

void FtpDeployWidget::onRemoteContextMenu(const QPoint& pos)
{
    QModelIndex idx = m_remoteTable->indexAt(pos);
    QMenu menu(this);

    QAction* downloadAct = menu.addAction("下载到本地");
    QAction* deleteAct = menu.addAction("删除");
    menu.addSeparator();
    QAction* renameAct = menu.addAction("重命名");
    QAction* newDirAct = menu.addAction("新建目录");
    menu.addSeparator();
    QAction* copyPathAct = menu.addAction("复制路径");

    QAction* chosen = menu.exec(m_remoteTable->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == downloadAct) onDownloadRemote();
    else if (chosen == deleteAct) onDeleteRemote();
    else if (chosen == renameAct) onRenameRemote();
    else if (chosen == newDirAct) onNewRemoteDir();
    else if (chosen == copyPathAct) {
        if (idx.isValid()) {
            QString fullPath = m_currentRemotePath;
            if (!fullPath.endsWith('/')) fullPath += '/';
            fullPath += QString::fromStdString(m_remoteModel->fileAt(idx.row()).name);
            QApplication::clipboard()->setText(fullPath);
        }
    }
}

void FtpDeployWidget::onDeleteRemote()
{
    QModelIndexList sel = m_remoteTable->selectionModel()->selectedRows();
    if (sel.isEmpty()) return;
    QString name = QString::fromStdString(m_remoteModel->fileAt(sel.first().row()).name);
    auto ans = QMessageBox::question(this, "确认删除",
        QString("确定要删除远程文件 \"%1\" 吗？").arg(name));
    if (ans != QMessageBox::Yes) return;

    // 在传入后台线程前复制值，避免数据竞争
    const int port = m_portSpin->value();
    const std::string deviceIp = m_deviceCombo->currentText().toStdString();
    const std::string user = m_deviceBus ? m_deviceBus->user().toStdString() : "";
    const std::string pass = m_deviceBus ? m_deviceBus->password().toStdString() : "";
    const std::string remotePath = m_currentRemotePath.toStdString();
    const std::string targetName = name.toStdString();
    const std::string proto = m_protocolCombo->currentText().toStdString();

    // 通过适配器删除（异步）
    QtConcurrent::run([this, targetName, port, deviceIp, user, pass, remotePath, proto]() {
        auto adapter = ProtocolRegistry::instance()->create(
            proto == "SFTP" ? "ssh" : "ftp");

        DeviceInfo dev;
        dev.ip = deviceIp;
        dev.port = port;
        AuthInfo auth;
        auth.user = user;
        auth.password = pass;

        std::string fullPath = remotePath;
        if (!fullPath.empty() && fullPath.back() != '/') fullPath += '/';
        fullPath += targetName;
        bool ok = false;

        if (!adapter->connect(dev, auth)) {
            QString err = QString::fromStdString(adapter->lastError());
            QMetaObject::invokeMethod(this, [this, err]() {
                appendLog("删除失败 — 连接错误: " + err);
            }, Qt::QueuedConnection);
            return;
        }

        if (proto == "SFTP") {
            ok = dynamic_cast<SshAdapter*>(adapter.get())->sftpDeleteFile(fullPath);
        } else {
            ok = dynamic_cast<FtpAdapter*>(adapter.get())->deleteFile(fullPath);
        }
        adapter->disconnect();

        QString displayName = QString::fromStdString(targetName);
        QMetaObject::invokeMethod(this, [this, displayName, ok]() {
            appendLog(ok ? QString("已删除: %1").arg(displayName) : "删除失败: " + displayName);
            onRefreshRemote();
        }, Qt::QueuedConnection);
    });
}

void FtpDeployWidget::onDownloadRemote()
{
    QModelIndexList sel = m_remoteTable->selectionModel()->selectedRows();
    if (sel.isEmpty()) return;
    QString name = QString::fromStdString(m_remoteModel->fileAt(sel.first().row()).name);

    QString savePath = QFileDialog::getSaveFileName(this, "保存到", name);
    if (savePath.isEmpty()) return;

    // 在传入后台线程前复制值，避免数据竞争
    const int port = m_portSpin->value();
    const std::string deviceIp = m_deviceCombo->currentText().toStdString();
    const std::string user = m_deviceBus ? m_deviceBus->user().toStdString() : "";
    const std::string pass = m_deviceBus ? m_deviceBus->password().toStdString() : "";
    const std::string remotePath = m_currentRemotePath.toStdString();
    const std::string targetName = name.toStdString();
    const std::string localSavePath = savePath.toStdString();
    const std::string proto = m_protocolCombo->currentText().toStdString();

    QtConcurrent::run([this, targetName, localSavePath, port, deviceIp, user, pass, remotePath, proto]() {
        auto adapter = ProtocolRegistry::instance()->create(
            proto == "SFTP" ? "ssh" : "ftp");

        DeviceInfo dev;
        dev.ip = deviceIp;
        dev.port = port;
        AuthInfo auth;
        auth.user = user;
        auth.password = pass;

        if (!adapter->connect(dev, auth)) {
            QString err = QString::fromStdString(adapter->lastError());
            QMetaObject::invokeMethod(this, [this, err]() {
                appendLog("下载失败 — 连接错误: " + err);
            }, Qt::QueuedConnection);
            return;
        }

        std::string fullPath = remotePath;
        if (!fullPath.empty() && fullPath.back() != '/') fullPath += '/';
        fullPath += targetName;
        bool ok = false;

        if (proto == "SFTP") {
            ok = dynamic_cast<SshAdapter*>(adapter.get())->sftpDownloadFile(fullPath, localSavePath);
        } else {
            ok = dynamic_cast<FtpAdapter*>(adapter.get())->downloadFile(fullPath, localSavePath);
        }
        adapter->disconnect();

        QString displayName = QString::fromStdString(targetName);
        QMetaObject::invokeMethod(this, [this, displayName, ok]() {
            appendLog(ok ? QString("已下载: %1").arg(displayName) : "下载失败: " + displayName);
        }, Qt::QueuedConnection);
    });
}

void FtpDeployWidget::onRenameRemote()
{
    QModelIndexList sel = m_remoteTable->selectionModel()->selectedRows();
    if (sel.isEmpty()) return;
    QString oldName = QString::fromStdString(m_remoteModel->fileAt(sel.first().row()).name);

    bool ok = false;
    QString newName = QInputDialog::getText(this, "重命名",
        QString("将 \"%1\" 重命名为:").arg(oldName), QLineEdit::Normal, oldName, &ok);
    if (!ok || newName.isEmpty() || newName == oldName) return;

    // 异步执行重命名
    const QString proto = m_protocolCombo->currentText();
    const QString old = oldName, nu = newName;
    const QString deviceIp = m_deviceCombo->currentText();
    const int port = m_portSpin->value();
    const std::string user = m_deviceBus ? m_deviceBus->user().toStdString() : "";
    const std::string pass = m_deviceBus ? m_deviceBus->password().toStdString() : "";
    const std::string remotePath = m_currentRemotePath.toStdString();

    QtConcurrent::run([this, proto, old, nu, deviceIp, port, user, pass, remotePath]() {
        auto adapter = ProtocolRegistry::instance()->create(
            proto == "SFTP" ? "ssh" : "ftp");

        DeviceInfo dev;
        dev.ip = deviceIp.toStdString();
        dev.port = port;
        AuthInfo auth;
        auth.user = user;
        auth.password = pass;

        if (!adapter->connect(dev, auth)) {
            QString err = QString::fromStdString(adapter->lastError());
            QMetaObject::invokeMethod(this, [this, err]() {
                appendLog("重命名失败 — 连接错误: " + err);
            }, Qt::QueuedConnection);
            return;
        }

        std::string oldFull = remotePath;
        if (!oldFull.empty() && oldFull.back() != '/') oldFull += '/';
        oldFull += old.toStdString();

        bool renameOk = false;
        if (proto == "SFTP") {
            // sftpRename 需要完整新路径
            std::string newFull = remotePath;
            if (!newFull.empty() && newFull.back() != '/') newFull += '/';
            newFull += nu.toStdString();
            renameOk = dynamic_cast<SshAdapter*>(adapter.get())->sftpRename(oldFull, newFull);
        } else {
            // FtpAdapter::renameFile 只需新文件名（不含路径前缀），内部处理路径拼接
            renameOk = dynamic_cast<FtpAdapter*>(adapter.get())->renameFile(oldFull, nu.toStdString());
        }
        adapter->disconnect();

        QMetaObject::invokeMethod(this, [this, old, nu, renameOk]() {
            appendLog(renameOk ? QString("已重命名: %1 → %2").arg(old, nu)
                               : QString("重命名失败: %1").arg(old));
            if (renameOk) onRefreshRemote();
        }, Qt::QueuedConnection);
    });
}

void FtpDeployWidget::onNewRemoteDir()
{
    bool ok = false;
    QString dirName = QInputDialog::getText(this, "新建目录",
        "目录名:", QLineEdit::Normal, "", &ok);
    if (!ok || dirName.isEmpty()) return;

    // 异步执行新建目录
    const QString proto = m_protocolCombo->currentText();
    const QString dir = dirName;
    const QString deviceIp = m_deviceCombo->currentText();
    const int port = m_portSpin->value();
    const std::string user = m_deviceBus ? m_deviceBus->user().toStdString() : "";
    const std::string pass = m_deviceBus ? m_deviceBus->password().toStdString() : "";
    const std::string remotePath = m_currentRemotePath.toStdString();

    QtConcurrent::run([this, proto, dir, deviceIp, port, user, pass, remotePath]() {
        auto adapter = ProtocolRegistry::instance()->create(
            proto == "SFTP" ? "ssh" : "ftp");

        DeviceInfo dev;
        dev.ip = deviceIp.toStdString();
        dev.port = port;
        AuthInfo auth;
        auth.user = user;
        auth.password = pass;

        if (!adapter->connect(dev, auth)) {
            QString err = QString::fromStdString(adapter->lastError());
            QMetaObject::invokeMethod(this, [this, err]() {
                appendLog("新建目录失败 — 连接错误: " + err);
            }, Qt::QueuedConnection);
            return;
        }

        std::string fullPath = remotePath;
        if (!fullPath.empty() && fullPath.back() != '/') fullPath += '/';
        fullPath += dir.toStdString();

        bool mkdirOk = false;
        if (proto == "SFTP") {
            mkdirOk = dynamic_cast<SshAdapter*>(adapter.get())->sftpMakeDirectory(fullPath);
        } else {
            mkdirOk = dynamic_cast<FtpAdapter*>(adapter.get())->makeDirectory(fullPath);
        }
        adapter->disconnect();

        QMetaObject::invokeMethod(this, [this, dir, mkdirOk]() {
            appendLog(mkdirOk ? QString("已创建目录: %1").arg(dir)
                              : QString("创建目录失败: %1").arg(dir));
            if (mkdirOk) onRefreshRemote();
        }, Qt::QueuedConnection);
    });
}

void FtpDeployWidget::setBackend(FtpDeployBackend* backend)
{
    m_backend = backend;
    connectBackendSignals();
}

void FtpDeployWidget::setDeviceBusWidget(DeviceBusWidget* deviceBus)
{
    m_deviceBus = deviceBus;

    // 同步设备下拉框
    if (m_deviceBus) {
        m_deviceCombo->clear();
        for (const auto& d : m_deviceBus->allDevices()) {
            m_deviceCombo->addItem(QString::fromStdString(d.ip));
        }

        // 设备总线变更时同步下拉框
        connect(m_deviceBus, &DeviceBusWidget::deviceSelectionChanged, this, [this]() {
            m_deviceCombo->clear();
            for (const auto& d : m_deviceBus->allDevices()) {
                m_deviceCombo->addItem(QString::fromStdString(d.ip));
            }
        });
    }
}

void FtpDeployWidget::onToolStart()
{
    appendLog("文件部署工具已就绪 — 拖拽文件到远程面板即可上传");
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

// ────────────────────────────── 拖拽支持 ──────────────────────────────

void FtpDeployWidget::handleDropOnRemote(const QList<QUrl>& urls)
{
    std::vector<std::string> files;
    for (const auto& url : urls) {
        if (url.isLocalFile()) {
            files.push_back(url.toLocalFile().toStdString());
        }
    }
    if (files.empty()) return;

    QString protoLabel = m_protocolCombo->currentText() == "SFTP" ? "SFTP" : "FTP";
    appendLog(QString("📤 [%1] 拖拽上传 %2 个文件到远程目录...").arg(protoLabel).arg(files.size()));

    if (!m_deviceBus || m_deviceBus->allDevices().empty()) {
        appendLog("错误：设备总线中没有目标设备");
        return;
    }
    if (!m_backend) {
        appendLog("错误：Backend 未就绪");
        return;
    }

    auto devices = m_deviceBus->allDevices();
    AuthInfo auth;
    auth.user = m_deviceBus->user().toStdString();
    auth.password = m_deviceBus->password().toStdString();
    m_backend->bindCredentials(auth);
    m_backend->bindDevices(devices);

    m_deployBtn->setEnabled(false);
    m_multiProgress->setDeviceCount(static_cast<int>(devices.size()));

    m_backend->startUpload(files,
        m_remotePathEdit->text().toStdString(),
        m_clearCheck->isChecked(),
        m_rebootCheck->isChecked(),
        m_ftpsCheck->isChecked(),
        m_portSpin->value()
    );
}

void FtpDeployWidget::handleDropOnLocal(const QList<QUrl>& urls)
{
    QString dir = urls.first().toLocalFile();
    QFileInfo fi(dir);
    if (fi.isDir()) {
        m_localTree->setRootIndex(m_localFsModel->index(dir));
        m_localPathEdit->setText(dir);
    }
}

void FtpDeployWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void FtpDeployWidget::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void FtpDeployWidget::dropEvent(QDropEvent* event)
{
    const QMimeData* mime = event->mimeData();
    if (!mime->hasUrls()) return;

    QList<QUrl> urls = mime->urls();
    if (urls.isEmpty()) return;

    // 判断拖放目标位置
    QPoint remotePos = m_remoteTable->mapFrom(this, event->pos());
    bool dropOnRemote = m_remoteTable->rect().contains(remotePos);

    QPoint localPos = m_localTree->mapFrom(this, event->pos());
    bool dropOnLocal = m_localTree->rect().contains(localPos);

    if (dropOnRemote) {
        handleDropOnRemote(urls);
        event->acceptProposedAction();
    } else if (dropOnLocal) {
        handleDropOnLocal(urls);
        event->acceptProposedAction();
    }
}

bool FtpDeployWidget::eventFilter(QObject* watched, QEvent* event)
{
    // 拦截子控件视图口上的拖拽事件（远程表格 / 本地树），使系统文件拖入有效
    switch (event->type()) {
    case QEvent::DragEnter:
        if (auto* de = static_cast<QDragEnterEvent*>(event);
            de->mimeData()->hasUrls()) {
            de->acceptProposedAction();
            return true;
        }
        break;
    case QEvent::DragMove:
        if (auto* dm = static_cast<QDragMoveEvent*>(event);
            dm->mimeData()->hasUrls()) {
            dm->acceptProposedAction();
            return true;
        }
        break;
    case QEvent::Drop:
        if (auto* drop = static_cast<QDropEvent*>(event)) {
            const QMimeData* mime = drop->mimeData();
            if (!mime->hasUrls()) break;

            QList<QUrl> urls = mime->urls();
            if (urls.isEmpty()) break;

            if (watched == m_remoteTable->viewport()) {
                handleDropOnRemote(urls);
                drop->acceptProposedAction();
                return true;

            } else if (watched == m_localTree->viewport()) {
                handleDropOnLocal(urls);
                drop->acceptProposedAction();
                return true;
            }
        }
        break;
    default:
        break;
    }
    return ToolWidget::eventFilter(watched, event);
}
