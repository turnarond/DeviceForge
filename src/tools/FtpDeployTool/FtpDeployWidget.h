/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: FtpDeployWidget.h
 * Date: 2026-07-25
 * Author: turnarond
 *
 * Description: FTP 部署 Tool 前端（双面板宿主重构） — 组装两个 FileBrowserPanel：
 *              左侧本地（LocalFileSource），右侧远程（RemoteFileSource），
 *              工具栏保留工具级批量部署配置（协议/设备/FTPS/端口/清空/重启），
 *              文件浏览/操作能力全部下沉到 FileBrowserPanel（Task 2/3）。
 *              批量部署链路零改动（FtpDeployBackend 不变）。
 */

#pragma once
#include "framework/ToolWidget.h"
#include <memory>
#include <atomic>
#include <vector>
#include <string>
#include <QString>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QSplitter>
#include <QLabel>
#include <QVBoxLayout>

class FtpDeployBackend;
class DeviceBusWidget;
class MultiProgressWidget;
class FileBrowserPanel;
class RemoteFileSource;
class QTimer;

class FtpDeployWidget : public ToolWidget {
    Q_OBJECT

public:
    explicit FtpDeployWidget(QWidget* parent = nullptr);
    ~FtpDeployWidget() override = default;

    // --- ToolWidget 实现 ---
    QString toolId() const override { return "com.deviceforge.ftp.deploy"; }
    QString toolName() const override { return "文件部署"; }
    void onToolStart() override;
    void onToolStop() override;

    void setBackend(FtpDeployBackend* backend);
    void setDeviceBusWidget(DeviceBusWidget* deviceBus);

    // 菜单联动入口（主窗口「部署」菜单调用）
    void startDeployFromMenu();
    void cancelDeployFromMenu();

signals:
    // 状态栏方向指示（Task 5）：左右面板路径/选中变化时组合发射「左路径 → 右路径」
    void directionChanged(const QString& text);

private slots:
    void onDeployClicked();
    void onRefreshRemote();

private:
    // 连接状态点状态（connStatusDot：灰=未连接/未配置，灰闪=连接中，青绿=连接成功，红=连接失败）
    enum class RemoteConnState { Unknown, Connecting, Connected, Failed };

    void setupUi();
    void setupToolbar(QVBoxLayout* mainLayout);
    void setupBottomBar(QVBoxLayout* mainLayout);
    void appendLog(const QString& msg);
    std::vector<std::string> collectLocalFiles() const;
    void connectBackendSignals();
    std::string currentProtocol() const;
    void setConnState(RemoteConnState state);
    void updateDeployBtnText();
    // 连接失败/重连失败/无设备统一 detach：面板置无源（清空列表+路径），远程源缓存一并失效
    void detachRemotePanel();

    FtpDeployBackend*  m_backend = nullptr;
    DeviceBusWidget*   m_deviceBus = nullptr;

    // 工具栏（工具级批量部署配置）
    QComboBox*   m_protocolCombo = nullptr;
    QComboBox*   m_deviceCombo = nullptr;
    QSpinBox*    m_portSpin = nullptr;
    QCheckBox*   m_ftpsCheck = nullptr;
    QCheckBox*   m_clearCheck = nullptr;
    QCheckBox*   m_rebootCheck = nullptr;
    QLabel*      m_connStatusDot = nullptr;   // 连接状态点（灰/灰闪/青绿/红，底色代码动态设置）
    QTimer*      m_connFlashTimer = nullptr;  // Connecting 态灰闪定时器（亮灰 ↔ 灰，500ms）
    bool         m_connFlashOn = false;       // 灰闪当前相位（定时器 tick 取反）
    QPushButton* m_refreshBtn = nullptr;      // 「⟳ 刷新」按钮（重新连接 + 刷新远程列表）

    // 双栏面板（FileBrowserPanel 宿主）
    FileBrowserPanel* m_leftPanel  = nullptr;   // 本地（LocalFileSource，固定）
    FileBrowserPanel* m_rightPanel = nullptr;   // 远程（RemoteFileSource，协议/设备变化时重建）

    // 远程源缓存（协议/设备/端口/FTPS 变化时重建；断线时原位重连保留面板路径）
    std::shared_ptr<RemoteFileSource> m_remoteSource;
    QString    m_remoteSrcDevice;
    QString    m_remoteSrcProto;
    int        m_remoteSrcPort = 0;
    bool       m_remoteSrcUseFtps = false;
    std::atomic<bool> m_remoteBusy{false}; // 防止并发刷新（远程源适配器非线程安全）
    quint64 m_connGeneration = 0; // 连接代际令牌：onRefreshRemote 入口 ++，worker 捕获、
                                  // 回调比对——连接在途时配置变更（切换设备/删除全部设备）
                                  // 作废在途连接的陈旧回调，防过期源挂载（与面板 loadGeneration 同模式）

    // 部署
    QPushButton*          m_deployBtn = nullptr;
    MultiProgressWidget*  m_multiProgress = nullptr;

    // 分割器
    QSplitter* m_splitter = nullptr;
};
