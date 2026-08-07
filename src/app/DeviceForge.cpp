#include "DeviceForge.h"
#include <QMessageBox>
#include <QString>
#include <QStyleFactory>
#include <QVBoxLayout>
#include <QLabel>
#include <QStatusBar>   // Task 5: 状态栏版本标签
#include <QMenuBar>     // Task 5: 帮助菜单(检查更新)
#include <QTimer>
#include <QAction>      // Task 5: 菜单项
#include <QKeySequence>

#include <QPushButton>

#include "src/tools/WebSocketTool/WebSocketWidget.h"
#include "src/tools/WebSocketTool/WebSocketBackend.h"
#include "src/tools/FtpDeployTool/FtpDeployWidget.h"
#include "src/tools/FtpDeployTool/FtpDeployBackend.h"
#include "src/tools/ModbusTool/ModbusWidget.h"
#include "src/tools/ModbusTool/ModbusBackend.h"
#include "src/tools/OpcUaClientTool/OpcUaClientWidget.h"
#include "src/tools/OpcUaClientTool/OpcUaClientBackend.h"

#include "src/framework/ToolHost.h"
#include "src/ui/DeviceBusWidget.h"
#include "src/tools/TelnetTool/TelnetWidget.h"
#include "src/tools/TelnetTool/TelnetBackend.h"
#include "src/tools/NetRelayTool/NetRelayWidget.h"
#include "src/tools/NetRelayTool/NetRelayBackend.h"

#include "src/updater/UpdateChecker.h"  // Task 5: 在线更新服务
#include "src/updater/UpdateDialog.h"   // Task 5: 在线更新对话框
#include "config/SettingsDialog.h"      // 配置管理面板

DeviceForge::DeviceForge(QWidget* parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    // 1. 创建导航栏
    m_navBar = new NavBar(this);
    m_navBar->addItem("📁", "文件\n部署", "ftp.deploy");
    m_navBar->addItem("📝", "批量\n命令", "telnet.command");
    m_navBar->addItem("🔌", "Web\nSocket", "websocket.comm");
    m_navBar->addItem("📊", "MOD\nBUS", "modbus.test");
    m_navBar->addItem("🌐", "网络\n调试", "netrelay.proxy");
    m_navBar->addItem("🔧", "OPC\nUA", "opcua.client");
    m_navBar->addItem("⚙", "设置", "settings");

    // 2. 获取 m_toolStack 指针
    m_toolStack = ui.toolStack;

    // 3. 用 QHBoxLayout 组织：导航栏 + 内容区
    if (ui.centralwidget) {
        auto* centralLayout = qobject_cast<QVBoxLayout*>(ui.centralwidget->layout());
        if (centralLayout) {
            centralLayout->removeWidget(ui.splitter_log);
            delete centralLayout;
        }

        auto* mainHBox = new QHBoxLayout(ui.centralwidget);
        mainHBox->setContentsMargins(0, 0, 0, 0);
        mainHBox->setSpacing(0);
        mainHBox->addWidget(m_navBar);

        // 右侧：设备栏 + 工具区 + 日志（垂直布局）
        auto* rightVBox = new QVBoxLayout();
        rightVBox->setContentsMargins(8, 4, 8, 8);
        rightVBox->setSpacing(4);
        // 设备栏（Task 3 重写，这里先保留原 DeviceBusWidget 作为占位）
        m_deviceBusWidget = new DeviceBusWidget(this);
        rightVBox->addWidget(m_deviceBusWidget);
        // 工具区 + 日志
        rightVBox->addWidget(ui.splitter_log, 1);
        mainHBox->addLayout(rightVBox, 1);
    }

    // 4. 连接导航栏
    connect(m_navBar, &NavBar::itemClicked, this, [this](int index) {
        if (index < m_toolStack->count()) {
            m_toolStack->setCurrentIndex(index);
        }
        // 特殊处理：最后一个导航项是设置
        if (index == m_navBar->count() - 1) {
            SettingsDialog dlg(this);
            dlg.exec();
            // 恢复上一活跃项
            m_navBar->setActiveItem(m_toolStack->currentIndex());
        }
    });

    // 创建 ToolHost 桥接层（管理 Backend ↔ Widget 配对和生命周期）
    m_toolHost = new ToolHost(this);

    // 5. 创建所有 Tool（按导航栏顺序）
    setupFtpDeployTab();       // index 0
    setupTelnetDeployTab();    // index 1
    setupWebSocketClientTab(); // index 2
    setupModbusClusterTab();   // index 3
    setupNetRelayTab();        // index 4
    setupOpcUaClientTab();     // index 5

    // 6. 设置初始活跃项
    m_navBar->setActiveItem(0);
    m_toolStack->setCurrentIndex(0);

    // 7. 调整 splitter 比例（日志区默认 250px，约 10 行）
    ui.splitter_log->setSizes(QList<int>() << 600 << 250);

    // 8. 底部日志折叠条（Task 4）
    m_logCollapseBar = new QWidget(this);
    m_logCollapseBar->setObjectName("logCollapseBar");
    m_logCollapseBar->setFixedHeight(4);
    m_logCollapseBar->setCursor(Qt::PointingHandCursor);
    m_logCollapseBar->installEventFilter(this);
    // 将折叠条放在日志组框顶部
    auto* logLayout = qobject_cast<QVBoxLayout*>(ui.groupBox_log->layout());
    if (logLayout) {
        logLayout->insertWidget(0, m_logCollapseBar);
    }

    m_logExpanded = true;

    QApplication::setStyle(QStyleFactory::create("Fusion"));

    // 清除日志按钮
    auto* clearLogBtn = new QPushButton(tr("清除日志"), this);
    clearLogBtn->setMinimumSize(100, 30);
    ui.groupBox_log->layout()->addWidget(clearLogBtn);
    connect(clearLogBtn, &QPushButton::clicked, this, &DeviceForge::onClearLogClicked);

    // 配置管理：文件菜单「设置...」Ctrl+,
    {
        auto* settingsAct = new QAction(QStringLiteral("设置..."), this);
        settingsAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
        connect(settingsAct, &QAction::triggered, this, [this]() {
            SettingsDialog dlg(this);
            dlg.exec();
        });
        if (ui.menu_file)
            ui.menu_file->addAction(settingsAct);
        else
            menuBar()->addAction(settingsAct);
    }

    // 在线更新集成（Task 5）：菜单"帮助-检查更新" + 状态栏版本标签 + 5 秒后自动检查
    setupUpdateChecker();
}

void DeviceForge::initToolTabs()
{
    // 所有 Tool 已在构造函数中创建（按导航栏顺序），此处无需操作
}

// 在初始化函数中（如 setupUi 后）
void DeviceForge::setupFtpDeployTab()
{
    auto backend = std::make_shared<FtpDeployBackend>();
    auto* widget = new FtpDeployWidget(this);

    int rc = backend->OnStart(0, nullptr);
    if (rc != 0) {
        appendGlobalLog("❌ FTP 部署 Tool Backend 启动失败 (rc=" + QString::number(rc) + ")");
        delete widget;
        return;
    }

    widget->setBackend(backend.get());
    widget->setDeviceBusWidget(m_deviceBusWidget);
    widget->setGlobalLogCallback([this](const QString& msg) { appendGlobalLog(msg); });
    widget->onToolStart();
    m_ftpBackend = backend;
    m_ftpDeployTab = widget;
    m_toolStack->addWidget(m_ftpDeployTab);
}

void DeviceForge::setupTelnetDeployTab()
{
    // 直接创建 TelnetBackend + TelnetWidget，不通过 ToolHost（ToolHost 只支持单个活跃 Tool）
    auto backend = std::make_shared<TelnetBackend>();
    auto* widget = new TelnetWidget(this);

    int rc = backend->OnStart(0, nullptr);
    if (rc != 0) {
        appendGlobalLog("❌ TelnetTool Backend 启动失败 (rc=" + QString::number(rc) + ")");
        delete widget;
        return;
    }

    widget->setBackend(backend.get());
    widget->setDeviceBusWidget(m_deviceBusWidget);
    widget->setGlobalLogCallback([this](const QString& msg) { appendGlobalLog(msg); });
    widget->onToolStart();
    m_telnetBackend = backend;
    m_telnetDeployTab = widget;
    m_toolStack->addWidget(m_telnetDeployTab);
}

void DeviceForge::setupModbusClusterTab()
{
    auto backend = std::make_shared<ModbusBackend>();
    auto* widget = new ModbusWidget(this);

    int rc = backend->OnStart(0, nullptr);
    if (rc != 0) {
        appendGlobalLog("❌ ModbusTool Backend 启动失败 (rc=" + QString::number(rc) + ")");
        delete widget;
        return;
    }

    widget->setBackend(backend.get());
    widget->setGlobalLogCallback([this](const QString& msg) { appendGlobalLog(msg); });
    widget->onToolStart();
    m_modbusBackend = backend;
    m_modbusWidget = widget;
    m_toolStack->addWidget(m_modbusWidget);
}

// 网络中继调试 Tool（TCP/UDP 透明代理 + 双向流量捕获）
void DeviceForge::setupNetRelayTab()
{
    auto backend = std::make_shared<NetRelayBackend>();
    auto* widget = new NetRelayWidget(this);

    int rc = backend->OnStart(0, nullptr);
    if (rc != 0) {
        appendGlobalLog("❌ NetRelayTool Backend 启动失败 (rc=" + QString::number(rc) + ")");
        delete widget;
        return;
    }

    widget->setBackend(backend.get());
    widget->setGlobalLogCallback([this](const QString& msg) { appendGlobalLog(msg); });
    widget->onToolStart();
    m_netRelayBackend = backend;
    m_netRelayWidget = widget;
    m_toolStack->addWidget(m_netRelayWidget);
}

// OPC UA 客户端 Tool（open62541 真实实现，替代旧演示 Tab）
void DeviceForge::setupOpcUaClientTab()
{
    auto backend = std::make_shared<OpcUaClientBackend>();
    auto* widget = new OpcUaClientWidget(this);

    int rc = backend->OnStart(0, nullptr);
    if (rc != 0) {
        appendGlobalLog("❌ OPC UA Client Backend 启动失败 (rc=" + QString::number(rc) + ")");
        delete widget;
        return;
    }

    widget->setBackend(backend.get());
    widget->setGlobalLogCallback([this](const QString& msg) { appendGlobalLog(msg); });
    widget->onToolStart();
    m_opcUaClientBackend = backend;
    m_opcUaClientWidget = widget;
    m_toolStack->addWidget(m_opcUaClientWidget);
}

// WebSocket 通信 Tab（直接创建 Backend + Widget，不通过 ToolHost）
void DeviceForge::setupWebSocketClientTab()
{
    auto backend = std::make_shared<WebSocketBackend>();
    auto* widget = new WebSocketWidget(this);

    int rc = backend->OnStart(0, nullptr);
    if (rc != 0) {
        appendGlobalLog("❌ WebSocketTool Backend 启动失败 (rc=" + QString::number(rc) + ")");
        delete widget;
        return;
    }

    widget->setBackend(backend.get());
    widget->setGlobalLogCallback([this](const QString& msg) { appendGlobalLog(msg); });
    widget->onToolStart();
    m_webSocketBackend = backend;
    m_webSocketWidget = widget;
    m_toolStack->addWidget(m_webSocketWidget);
}

void DeviceForge::onClearLogClicked()
{
    ui.txt_globalLog->clear();
}

void DeviceForge::appendGlobalLog(const QString& log)
{
    ui.txt_globalLog->append(log);
    // 折叠态时琴色闪烁提示（Task 4）
    if (!m_logExpanded && m_logCollapseBar) {
        m_logCollapseBar->setStyleSheet("#logCollapseBar { background: #F0A030; }");
        QTimer::singleShot(600, this, [this]() {
            if (m_logCollapseBar) {
                m_logCollapseBar->setStyleSheet(
                    "#logCollapseBar { background: #252A33; }"
                    "#logCollapseBar:hover { background: #333B48; }"
                );
            }
        });
    }
}

QStringList DeviceForge::getTargetIPList() const
{
    QStringList ips;
    if (m_deviceBusWidget) {
        for (const auto& dev : m_deviceBusWidget->allDevices()) {
            ips << QString::fromStdString(dev.ip);
        }
    }
    return ips;
}

QString DeviceForge::getFtpUser() const
{
    return m_deviceBusWidget ? m_deviceBusWidget->user() : QString();
}

QString DeviceForge::getFtpPass() const
{
    return m_deviceBusWidget ? m_deviceBusWidget->password() : QString();
}

DeviceForge::~DeviceForge()
{
}



// ============================================================
// Task 5: 在线更新 UI 集成（菜单 + 状态栏版本标签）
// ============================================================

// 当前版本字串（用于状态栏显示），从 CMake 宏组装
static QString currentVersionString() {
    return QString("v%1.%2.%3")
        .arg(DEVICEFORGE_VERSION_MAJOR)
        .arg(DEVICEFORGE_VERSION_MINOR)
        .arg(DEVICEFORGE_VERSION_PATCH);
}

// 初始化 UpdateChecker、状态栏版本标签、菜单栏"帮助-检查更新"
// 并在 5 秒后自动触发一次检查
void DeviceForge::setupUpdateChecker()
{
    m_updateChecker = std::make_shared<UpdateChecker>();
    // 使用 CMake 编译宏设置当前版本,避免硬编码漂移
    m_updateChecker->setCurrentVersion(
        std::to_string(DEVICEFORGE_VERSION_MAJOR) + "." +
        std::to_string(DEVICEFORGE_VERSION_MINOR) + "." +
        std::to_string(DEVICEFORGE_VERSION_PATCH));
    // UpdateChecker 内部业务用 QtConcurrent 异步执行,无需启动 ServiceTask 工作线程

    // 状态栏版本标签（右侧永久挂件）
    m_versionLabel = new QLabel(this);
    m_versionLabel->setText(currentVersionString() + " (检查中...)");
    m_versionLabel->setStyleSheet("color: #7B8494; padding: 0 8px;");
    m_versionLabel->setCursor(Qt::PointingHandCursor); // 鼠标手型,提示可点击
    // 版本标签可点击: 富文本(有新版本)时通过 linkActivated,纯文本时通过 eventFilter
    connect(m_versionLabel, &QLabel::linkActivated, this, &DeviceForge::onVersionLabelClicked);
    m_versionLabel->installEventFilter(this);
    statusBar()->addPermanentWidget(m_versionLabel);

    // 复用 .ui 已有的"帮助"菜单,添加"检查更新"项
    m_checkUpdateAction = ui.menu_help->addAction("检查更新...");
    connect(m_checkUpdateAction, &QAction::triggered,
            this, &DeviceForge::onCheckUpdateTriggered);

    // 连接 .ui 中已有的"关于"动作
    connect(ui.action_about, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "关于 DeviceForge",
            QString("DeviceForge v%1.%2.%3\n\n"
                    "工业级设备批量运维平台\n\n"
                    "© 2024-2026 turnarond\n"
                    "https://github.com/turnarond/DeviceForge")
                .arg(DEVICEFORGE_VERSION_MAJOR)
                .arg(DEVICEFORGE_VERSION_MINOR)
                .arg(DEVICEFORGE_VERSION_PATCH));
    });

    // 状态切换回调(UpdateChecker 在工作线程触发,通过 QueuedConnection 切回主线程)
    m_updateChecker->setStateChangedCallback([this](UpdateState state) {
        QMetaObject::invokeMethod(this, [this, state]() {
            onUpdateStateChanged(state);
        }, Qt::QueuedConnection);
    });

    // 下载进度回调
    m_updateChecker->setProgressCallback(
        [this](int pct, int64_t downloaded, int64_t total) {
            QMetaObject::invokeMethod(this, [this, pct, downloaded, total]() {
                if (m_updateDialog) {
                    m_updateDialog->setProgress(pct, downloaded, total);
                }
            }, Qt::QueuedConnection);
        });

    // 错误回调（setState(Error) 已通过状态回调处理标签更新）
    m_updateChecker->setErrorCallback([this](const std::string& msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            if (m_updateDialog) {
                m_updateDialog->setState(UpdateState::Error);
            }
            // 日志记录实际错误原因
            appendGlobalLog(QString("检查更新失败: %1").arg(QString::fromStdString(msg)));
        }, Qt::QueuedConnection);
    });

    // 5 秒后自动触发一次后台检查,避免启动阻塞
    QTimer::singleShot(5000, this, [this]() { onCheckUpdateTriggered(true); });
}

// 用户点击菜单"检查更新"或 5 秒自动触发（isAuto: 自动检查静默失败）
void DeviceForge::onCheckUpdateTriggered(bool isAuto)
{
    if (!m_updateChecker) return;
    m_autoCheck = isAuto;
    m_checkUpdateAction->setEnabled(false);
    m_updateChecker->checkForUpdate();
}

// 状态机回调（主线程）— 切换状态栏标签 + 自动弹出 UpdateDialog
void DeviceForge::onUpdateStateChanged(UpdateState state)
{
    if (!m_checkUpdateAction) return;

    // 检查/下载中禁用菜单;其他状态可重新触发
    m_checkUpdateAction->setEnabled(state != UpdateState::Checking &&
                                     state != UpdateState::Downloading);

    const QString ver = currentVersionString();

    switch (state) {
    case UpdateState::Checking:
        m_versionLabel->setText(ver + " (检查中...)");
        m_versionLabel->setStyleSheet("color: #7B8494; padding: 0 8px;");
        m_checkUpdateAction->setText("检查更新...");
        break;

    case UpdateState::Ready: {
        // 有新版本:标签切琴色 + 可点击;同时自动弹窗
        ReleaseInfo info = m_updateChecker->releaseInfo();
        QString tag = QString::fromStdString(info.tagName);
        // 「琴色是动词」 — 仅信号态,使用琴色 #F0A030 提示可点击
        m_versionLabel->setText(
            QString("<a href='#' style='color:#F0A030;text-decoration:none;'>%1 可用 &#9662;</a>").arg(tag));
        m_versionLabel->setTextFormat(Qt::RichText);
        m_checkUpdateAction->setText("下载 " + tag + "...");

        // 自动弹出对话框（首次创建）
        if (!m_updateDialog) {
            m_updateDialog = new UpdateDialog(m_updateChecker.get(), this);
        }
        m_updateDialog->setReleaseInfo(info);
        m_updateDialog->setState(state);
        m_updateDialog->show();
        m_updateDialog->raise();
        m_updateDialog->activateWindow();
        break;
    }

    case UpdateState::Idle:
        m_versionLabel->setText(ver + " (已是最新)");
        m_versionLabel->setStyleSheet("color: #7B8494; padding: 0 8px;");
        m_checkUpdateAction->setText("检查更新...");
        break;

    case UpdateState::Error:
        // 自动检查失败静默,手动检查失败显示 "检查失败"
        if (!m_autoCheck) {
            m_versionLabel->setText(ver + " (检查失败)");
            m_versionLabel->setStyleSheet("color: #7B8494; padding: 0 8px;");
        }
        m_checkUpdateAction->setText("检查更新...");
        if (m_updateDialog) m_updateDialog->setState(state);
        break;

    case UpdateState::Downloading:
        m_versionLabel->setText(ver + " (下载中...)");
        m_versionLabel->setStyleSheet("color: #7B8494; padding: 0 8px;");
        if (m_updateDialog) m_updateDialog->setState(state);
        break;

    case UpdateState::Installed:
        m_versionLabel->setText(ver + " (已下载,待安装)");
        m_versionLabel->setStyleSheet("color: #7B8494; padding: 0 8px;");
        if (m_updateDialog) m_updateDialog->setState(state);
        break;
    }
}

// 点击状态栏版本标签 — 在已有可下载/已下载状态下重新唤起对话框
void DeviceForge::onVersionLabelClicked()
{
    if (!m_updateChecker) return;
    auto state = m_updateChecker->state();
    if (state == UpdateState::Ready || state == UpdateState::Downloading ||
        state == UpdateState::Installed) {
        if (!m_updateDialog) {
            m_updateDialog = new UpdateDialog(m_updateChecker.get(), this);
        }
        if (state == UpdateState::Ready) {
            m_updateDialog->setReleaseInfo(m_updateChecker->releaseInfo());
        }
        m_updateDialog->setState(state);
        m_updateDialog->show();
        m_updateDialog->raise();
        m_updateDialog->activateWindow();
    }
}

 //eventFilter — 处理版本标签鼠标点击 + 日志折叠条点击（Task 4）
bool DeviceForge::eventFilter(QObject* watched, QEvent* event) {
    // 日志折叠条点击（Task 4）
    if (watched == m_logCollapseBar && event->type() == QEvent::MouseButtonPress) {
        m_logExpanded = !m_logExpanded;
        ui.groupBox_log->setVisible(m_logExpanded);
        ui.splitter_log->setSizes(m_logExpanded
            ? QList<int>() << 600 << 250
            : QList<int>() << 600 << 0);
        return true;
    }
    if (watched == m_versionLabel && event->type() == QEvent::MouseButtonRelease) {
        // 仅在非 Ready 状态且当前为纯文本时（富文本的 a href 走 linkActivated）
        if (m_updateChecker && m_updateChecker->state() != UpdateState::Ready) {
            onVersionLabelClicked();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}