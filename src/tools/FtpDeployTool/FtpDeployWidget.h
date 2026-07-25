/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: FtpDeployWidget.h
 * Date: 2026-07-25
 * Author: turnarond
 *
 * Description: FTP 部署 Tool 前端（v2.4 双栏重构） — QSplitter 双栏文件管理器，
 *              左侧本地目录树（QFileSystemModel），右侧远程 FTP 目录表格（RemoteFileModel），
 *              拖拽上传 + 多设备批量部署。
 */

#pragma once
#include "framework/ToolWidget.h"
#include <QTreeView>
#include <QTableView>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>
#include <QSplitter>
#include <QLabel>
#include <QFileSystemModel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>

class FtpDeployBackend;
class DeviceBusWidget;
class MultiProgressWidget;
class RemoteFileModel;

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

    void setBackend(FtpDeployBackend* backend);
    void setDeviceBusWidget(DeviceBusWidget* deviceBus);

private slots:
    void onDeployClicked();
    void onRefreshRemote();
    void onRemoteDirChanged(const QModelIndex& index);
    void onRemoteContextMenu(const QPoint& pos);
    void onDeleteRemote();
    void onRenameRemote();
    void onDownloadRemote();
    void onNewRemoteDir();

private:
    // 拖拽辅助方法 — 供 dropEvent 和 eventFilter 共用
    void handleDropOnRemote(const QList<QUrl>& urls);
    void handleDropOnLocal(const QList<QUrl>& urls);

protected:
    // 拖拽支持 — 从系统资源管理器拖入文件到远程面板直接上传
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupUi();
    void setupToolbar(QVBoxLayout* mainLayout);
    void setupLocalPanel();
    void setupRemotePanel();
    void setupBottomBar(QVBoxLayout* mainLayout);
    void navigateToRemoteDir(const QString& path);
    void refreshBreadcrumb(const QString& path);
    void appendLog(const QString& msg);
    std::vector<std::string> collectLocalFiles() const;
    void connectBackendSignals();

    FtpDeployBackend*  m_backend = nullptr;
    DeviceBusWidget*    m_deviceBus = nullptr;

    // 工具栏
    QComboBox*   m_deviceCombo = nullptr;
    QLineEdit*   m_remotePathEdit = nullptr;
    QSpinBox*    m_portSpin = nullptr;
    QCheckBox*   m_ftpsCheck = nullptr;
    QCheckBox*   m_clearCheck = nullptr;
    QCheckBox*   m_rebootCheck = nullptr;

    // 本地面板
    QFileSystemModel* m_localFsModel = nullptr;
    QTreeView*        m_localTree = nullptr;
    QLineEdit*        m_localPathEdit = nullptr;

    // 远程面板
    RemoteFileModel* m_remoteModel = nullptr;
    QTableView*      m_remoteTable = nullptr;
    QPushButton*     m_refreshBtn = nullptr;
    QWidget*         m_breadcrumbWidget = nullptr;
    QHBoxLayout*     m_breadcrumbLayout = nullptr;
    QString          m_currentRemotePath;

    // 部署
    QPushButton*          m_deployBtn = nullptr;
    MultiProgressWidget*  m_multiProgress = nullptr;
    QTextEdit*            m_logView = nullptr;

    // 容器面板（替代 setProperty 传递指针）
    QWidget* m_localPanel  = nullptr;
    QWidget* m_remotePanel = nullptr;

    // 分割器
    QSplitter* m_splitter = nullptr;
};
