#include "DeployMaster.h"
#include <QFileInfo>
#include <QFileDialog>
#include <QString>
#include <QtConcurrent/QtConcurrent>
#include <QStyleFactory>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QInputDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QStatusBar>   // Task 5: 状态栏版本标签
#include <QMenuBar>     // Task 5: 帮助菜单(检查更新)
#include <QTimer>
#include <QMouseEvent>       // Task 5: 延迟 5 秒自动检查
#include <QAction>      // Task 5: 菜单项

#include "OpcUaClient.h" // add header
#include "src/tools/WebSocketTool/WebSocketWidget.h"
#include "src/tools/WebSocketTool/WebSocketBackend.h"
#include "src/tools/FtpDeployTool/FtpDeployWidget.h"
#include "src/tools/FtpDeployTool/FtpDeployBackend.h"
#include "src/tools/ModbusTool/ModbusWidget.h"
#include "src/tools/ModbusTool/ModbusBackend.h"

#include "src/framework/ToolHost.h"
#include "src/ui/DeviceBusWidget.h"
#include "src/tools/TelnetTool/TelnetWidget.h"
#include "src/tools/TelnetTool/TelnetBackend.h"
#include "src/tools/NetRelayTool/NetRelayWidget.h"
#include "src/tools/NetRelayTool/NetRelayBackend.h"

#include "src/updater/UpdateChecker.h"  // Task 5: 在线更新服务
#include "src/updater/UpdateDialog.h"   // Task 5: 在线更新对话框

DeployMaster::DeployMaster(QWidget* parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    // 创建设备总线组件（顶部水平胶囊形设备列表）
    m_deviceBusWidget = new DeviceBusWidget(this);
    // 将设备总线插入到主布局顶部（全局配置栏上方）
    if (ui.centralwidget) {
        QVBoxLayout* centralLayout = qobject_cast<QVBoxLayout*>(ui.centralwidget->layout());
        if (centralLayout) {
            centralLayout->insertWidget(0, m_deviceBusWidget);
        }
    }

    // 创建 ToolHost 桥接层（管理 Backend ↔ Widget 配对和生命周期）
    m_toolHost = new ToolHost(this);

    setupFtpDeployTab();
    setupModbusClusterTab();
    setupNetRelayTab();
    setupOpcUaClientTab();

    // 设置 splitter 初始大小比例：工作区占75%，日志区占25%
    ui.splitter_log->setSizes(QList<int>() << 250 << 350);
    // 设置水平 splitter 初始大小比例：左侧工作区占75%，右侧预览占25%
    ui.splitter_main->setSizes(QList<int>() << 600 << 200);

    QApplication::setStyle(QStyleFactory::create("Fusion"));

    // 旧"批量部署" Tab 已完全迁移至「文件部署」Tool Tab
    // 将远端路径输入框移到远端预览面板，隐藏整个旧 Tab
    m_remotePathEdit = new QLineEdit("/apps/m580cn/bin/", this);
    ui.verticalLayout_remote->insertWidget(1, m_remotePathEdit);
    // 清除日志按钮移到底部日志区
    ui.groupBox_log->layout()->addWidget(ui.btn_clearLog);
    ui.tabWidget->setTabVisible(ui.tabWidget->indexOf(ui.tab_deploy), false);

    connect(ui.btn_clearLog, &QPushButton::clicked, this, &DeployMaster::onClearLogClicked);

    // 隐藏旧远端预览控件（将被 m_protocolCombo + m_deviceCombo 替代）
    ui.btn_refreshRemote->hide();

    // 初始化远端预览功能
    setupRemotePreview();

    // 在线更新集成（Task 5）：菜单"帮助-检查更新" + 状态栏版本标签 + 5 秒后自动检查
    setupUpdateChecker();
}

void DeployMaster::initToolTabs()
{
    setupTelnetDeployTab();
    setupWebSocketClientTab();
}

// 在初始化函数中（如 setupUi 后）
void DeployMaster::setupFtpDeployTab()
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
    widget->onToolStart();
    m_ftpBackend = backend;
    m_ftpDeployTab = widget;
    ui.tabWidget->addTab(m_ftpDeployTab, tr("文件部署"));
}

void DeployMaster::setupTelnetDeployTab()
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
    widget->onToolStart();
    m_telnetBackend = backend;
    m_telnetDeployTab = widget;
    ui.tabWidget->addTab(m_telnetDeployTab, tr("批量命令"));
}

void DeployMaster::setupModbusClusterTab()
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
    widget->onToolStart();
    m_modbusBackend = backend;
    m_modbusWidget = widget;
    ui.tabWidget->addTab(m_modbusWidget, tr("MODBUS 测试"));
}

// 网络中继调试 Tool（TCP/UDP 透明代理 + 双向流量捕获）
void DeployMaster::setupNetRelayTab()
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
    widget->onToolStart();
    m_netRelayBackend = backend;
    m_netRelayWidget = widget;
    ui.tabWidget->addTab(m_netRelayWidget, tr("网络调试"));
}

// new: setup for opc ua
void DeployMaster::setupOpcUaClientTab()
{
    // OpcUaClientTab is optional and uses placeholder implementation
    OpcUaClientTab* opcTab = new OpcUaClientTab(this);
    ui.tabWidget->addTab(opcTab, tr("OPC UA 客户端"));
}

// WebSocket 通信 Tab（直接创建 Backend + Widget，不通过 ToolHost）
void DeployMaster::setupWebSocketClientTab()
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
    widget->onToolStart();
    m_webSocketBackend = backend;
    m_webSocketWidget = widget;
    ui.tabWidget->addTab(m_webSocketWidget, tr("WebSocket"));
}

void DeployMaster::onAddFilesClicked()
{
    // 设置文件对话框标题和过滤器
    QString title = tr("选择要部署的文件");
    QString filter = tr("所有文件 (*.*);;可执行文件 (*.exe);;配置文件 (*.ini *.conf);;脚本文件 (*.py *.sh)");

    // 弹出文件选择对话框，允许多选
    QStringList selectedFiles = QFileDialog::getOpenFileNames(this, title, lastUsedDirectory, filter);

    if (!selectedFiles.isEmpty()) {
        // 更新“上次使用的目录”，方便下次打开
        lastUsedDirectory = QFileInfo(selectedFiles.first()).absolutePath();

        // 将选中的文件添加到你的内部数据模型（例如 QStringList, QList<QFileInfo>, 或自定义 Model）
        for (const QString& filePath : selectedFiles) {
            addFileToList(filePath); // 你需要实现这个函数，将文件路径添加到 QListWidget/QTableView 等控件中
        }
    }
}

void DeployMaster::onAddFolderClicked()
{
    QString title = tr("选择要部署的文件夹");
    // 弹出文件夹选择对话框
    QString selectedFolder = QFileDialog::getExistingDirectory(this, title, lastUsedDirectory);

    if (!selectedFolder.isEmpty()) {
        lastUsedDirectory = selectedFolder; // 更新上次目录

        // 将选中的文件夹添加到列表
        addFolderToList(selectedFolder); // 你需要实现这个函数
    }
}

void DeployMaster::onDeployClicked()
{
    appendGlobalLog("部署功能已迁移至「文件部署」Tab");
}

void DeployMaster::onFilesDropped(const QStringList& filePaths)
{
    for (const QString& path : filePaths) {
        QFileInfo fileInfo(path);
        if (fileInfo.isFile()) {
            addFileToList(path); // 复用之前的添加文件逻辑
        }
        else if (fileInfo.isDir()) {
            addFolderToList(path); // 复用之前的添加文件夹逻辑
        }
        // 注意：拖拽可以同时包含文件和文件夹，所以需要判断类型
    }
}

void DeployMaster::onFileItemCleanClicked()
{
    ui.list_uploadedItems->clear();
}

void DeployMaster::onClearLogClicked()
{
    ui.txt_globalLog->clear();
}

void DeployMaster::addFileToList(const QString&) {}

void DeployMaster::addFolderToList(const QString&) {}

void DeployMaster::appendGlobalLog(const QString& log)
{
    ui.txt_globalLog->append(log);
}

QStringList DeployMaster::getTargetIPs()
{
    QStringList ips;
    if (m_deviceBusWidget) {
        for (const auto& dev : m_deviceBusWidget->allDevices()) {
            ips << QString::fromStdString(dev.ip);
        }
    }
    return ips;
}

QStringList DeployMaster::getTargetIPList() const
{
    QStringList ips;
    if (m_deviceBusWidget) {
        for (const auto& dev : m_deviceBusWidget->allDevices()) {
            ips << QString::fromStdString(dev.ip);
        }
    }
    return ips;
}

QString DeployMaster::getFtpUser() const
{
    return m_deviceBusWidget ? m_deviceBusWidget->user() : QString();
}

QString DeployMaster::getFtpPass() const
{
    return m_deviceBusWidget ? m_deviceBusWidget->password() : QString();
}

DeployMaster::~DeployMaster()
{
    // 清理远程文件模型
    if (remoteFileModel) {
        delete remoteFileModel;
        remoteFileModel = nullptr;
    }
}

void DeployMaster::setupRemotePreview()
{
    // 初始化远程文件模型
    remoteFileModel = new QStandardItemModel(this);
    ui.tree_remoteFiles->setModel(remoteFileModel);

    // --- 协议选择行 ---
    auto* protoRow = new QHBoxLayout();
    protoRow->addWidget(new QLabel("协议:", this));
    m_protocolCombo = new QComboBox(this);
    m_protocolCombo->addItem("FTP");
    m_protocolCombo->addItem("SCP (即将推出)");
    connect(m_protocolCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (idx == 1) { // SCP 选中
            appendGlobalLog("SCP 功能即将推出，请使用 FTP 协议");
            m_protocolCombo->blockSignals(true);
            m_protocolCombo->setCurrentIndex(0); // 回弹到 FTP
            m_protocolCombo->blockSignals(false);
            return;
        }
        refreshDeviceCombo();
    });
    protoRow->addWidget(m_protocolCombo);
    protoRow->addStretch();

    // --- 设备选择行 ---
    auto* devRow = new QHBoxLayout();
    devRow->addWidget(new QLabel("设备:", this));
    // 复用 ui.cmb_targetIPs（保留 .ui 中的 QComboBox）
    devRow->addWidget(ui.cmb_targetIPs, 1);
    m_refreshBtn = new QPushButton("刷新", this);
    connect(m_refreshBtn, &QPushButton::clicked, this, &DeployMaster::refreshRemoteFiles);
    devRow->addWidget(m_refreshBtn);
    auto* downloadBtn = new QPushButton("下载", this);
    connect(downloadBtn, &QPushButton::clicked, this, &DeployMaster::onDownloadRemoteFile);
    devRow->addWidget(downloadBtn);

    // --- 路径行 ---
    auto* pathRow = new QHBoxLayout();
    pathRow->addWidget(new QLabel("路径:", this));
    pathRow->addWidget(m_remotePathEdit, 1);

    // --- 插入到远端预览面板 ---
    auto* remoteLayout = qobject_cast<QVBoxLayout*>(ui.groupBox_remotePreview->layout());
    if (remoteLayout) {
        // 清除旧的 cmb_targetIPs + btn_refreshRemote（它们已在 .ui 中，需要先移除）
        // 新的 protoRow、devRow、pathRow 按序插入
        remoteLayout->insertLayout(0, pathRow);
        remoteLayout->insertLayout(0, devRow);
        remoteLayout->insertLayout(0, protoRow);
    }

    // 设置初始路径
    currentRemotePath = m_remotePathEdit->text().trimmed();
    if (!currentRemotePath.endsWith('/')) currentRemotePath += '/';
    if (!currentRemotePath.startsWith('/')) currentRemotePath = '/' + currentRemotePath;

    // 连接信号
    connect(ui.cmb_targetIPs, &QComboBox::currentTextChanged, this, &DeployMaster::onIPSelectionChanged);
    connect(ui.tree_remoteFiles, &QTreeView::doubleClicked, this, &DeployMaster::onRemoteFileDoubleClicked);
    connect(m_remotePathEdit, &QLineEdit::returnPressed, this, &DeployMaster::refreshRemoteFiles);

    // 监听设备总线变化
    connect(m_deviceBusWidget, &DeviceBusWidget::deviceSelectionChanged,
            this, &DeployMaster::refreshDeviceCombo);

    // 初始填充
    refreshDeviceCombo();
}

void DeployMaster::onIPSelectionChanged()
{
    currentRemoteIP = ui.cmb_targetIPs->currentText();
    if (!currentRemoteIP.isEmpty()) {
        refreshRemoteFiles();
    }
}

void DeployMaster::refreshDeviceCombo()
{
    QString currentText = ui.cmb_targetIPs->currentText();
    ui.cmb_targetIPs->blockSignals(true);
    ui.cmb_targetIPs->clear();

    auto devices = m_deviceBusWidget->allDevices();
    for (const auto& dev : devices) {
        QString ip = QString::fromStdString(dev.ip);
        // 协议过滤：FTP 模式显示端口为 21/0 或协议为 ftp 的设备
        bool include = false;
        if (dev.protocol.empty() || dev.protocol == "ftp") {
            include = (dev.port == 21 || dev.port == 0);
        }
        if (include && !ip.isEmpty()) {
            ui.cmb_targetIPs->addItem(ip, ip);
        }
    }

    // 恢复之前选中项
    int idx = ui.cmb_targetIPs->findText(currentText);
    if (idx >= 0) {
        ui.cmb_targetIPs->setCurrentIndex(idx);
    }

    ui.cmb_targetIPs->blockSignals(false);

    // 如果之前无选中但列表不空，自动选中第一个并刷新
    if (ui.cmb_targetIPs->currentIndex() < 0 && ui.cmb_targetIPs->count() > 0) {
        ui.cmb_targetIPs->setCurrentIndex(0);
        refreshRemoteFiles();
    }
}

void DeployMaster::refreshRemoteFiles()
{
    if (currentRemoteIP.isEmpty()) {
        appendGlobalLog("❌ 错误：请选择目标 IP！");
        return;
    }
    
    QString user = m_deviceBusWidget ? m_deviceBusWidget->user() : QString();
    QString pass = m_deviceBusWidget ? m_deviceBusWidget->password() : QString();

    // 确保路径格式正确
    if (!currentRemotePath.endsWith('/')) {
        currentRemotePath += '/';
    }
    if (!currentRemotePath.startsWith('/')) {
        currentRemotePath = '/' + currentRemotePath;
    }
    
    if (user.isEmpty() || pass.isEmpty()) {
        appendGlobalLog("❌ 错误：请输入用户名和密码！");
        return;
    }
    
    appendGlobalLog(QString("🔄 正在刷新远程文件列表: %1%2").arg(currentRemoteIP).arg(currentRemotePath));
    
    // 异步刷新远程文件列表
    QtConcurrent::run([=]() {
        try {
            FtpManager ftpm(currentRemoteIP, 21);
            ftpm.setCredentials(user, pass);
            QList<FtpFileInfo> files = ftpm.listFtpDirectoryDetailed(currentRemotePath);
            
            // 在主线程更新UI
            QMetaObject::invokeMethod(this, "buildRemoteFileTree", Qt::QueuedConnection, Q_ARG(QList<FtpFileInfo>, files));
            QMetaObject::invokeMethod(this, "appendGlobalLog", Qt::QueuedConnection, Q_ARG(QString, "✅ 远程文件列表刷新成功"));
        } catch (const std::exception& ex) {
            QMetaObject::invokeMethod(this, "appendGlobalLog", Qt::QueuedConnection, Q_ARG(QString, QString("❌ 刷新失败: %1").arg(QString::fromStdString(ex.what()).left(100))));
        }
    });
}

void DeployMaster::buildRemoteFileTree(const QList<FtpFileInfo>& files)
{
    // 清空现有模型
    remoteFileModel->clear();
    
    // 添加根目录项
    QStandardItem* rootItem = new QStandardItem(QString("%1 (%2)").arg(currentRemoteIP).arg(currentRemotePath));
    rootItem->setData(currentRemotePath, Qt::UserRole);
    rootItem->setData(true, Qt::UserRole + 1); // 标记为目录
    rootItem->setData(true, Qt::UserRole + 2); // 标记为根目录项
    remoteFileModel->appendRow(rootItem);
    
    // 添加返回上一级文件夹的选项（如果当前路径不是根目录）
    if (currentRemotePath != "/" && !currentRemotePath.isEmpty()) {
        QStandardItem* parentItem = new QStandardItem(".. (返回上一级)");
        // 使用Qt标准目录图标
        parentItem->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));
        QString parentPath = currentRemotePath;
        // 移除末尾的斜杠
        if (parentPath.endsWith('/')) {
            parentPath.chop(1);
        }
        // 找到上一级目录
        int lastSlashIndex = parentPath.lastIndexOf('/');
        if (lastSlashIndex >= 0) {
            parentPath = parentPath.left(lastSlashIndex + 1);
        } else {
            parentPath = "/";
        }
        parentItem->setData(parentPath, Qt::UserRole);
        parentItem->setData(true, Qt::UserRole + 1); // 标记为目录
        parentItem->setData(false, Qt::UserRole + 2); // 标记为非根目录项
        rootItem->appendRow(parentItem);
    }
    
    // 添加文件和文件夹
    for (const FtpFileInfo& file : files) {
        QStandardItem* item;
        if (file.size == -1) { // 目录（size为-1表示可能是目录）
            item = new QStandardItem(file.name);
            // 使用Qt标准目录图标
            item->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));
            item->setData(currentRemotePath + file.name + "/", Qt::UserRole);
            item->setData(true, Qt::UserRole + 1); // 标记为目录
            item->setData(false, Qt::UserRole + 2); // 标记为非根目录项
        } else {
            item = new QStandardItem(QString("%1 (%2 bytes)").arg(file.name).arg(file.size));
            // 使用Qt标准文件图标
            item->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileIcon));
            item->setData(currentRemotePath + file.name, Qt::UserRole);
            item->setData(false, Qt::UserRole + 1); // 标记为文件
            item->setData(false, Qt::UserRole + 2); // 标记为非根目录项
        }
        rootItem->appendRow(item);
    }
    
    // 展开根节点
    ui.tree_remoteFiles->expandAll();
}

void DeployMaster::onRemoteFileDoubleClicked(const QModelIndex& index)
{
    QStandardItem* item = remoteFileModel->itemFromIndex(index);
    if (!item) return;
    
    bool isDirectory = item->data(Qt::UserRole + 1).toBool();
    bool isRootItem = item->data(Qt::UserRole + 2).toBool();
    
    if (isRootItem) {
        // 双击根目录项，允许编辑路径
        bool ok;
        QString newPath = QInputDialog::getText(this, "编辑远程路径", "请输入新的远程路径:", QLineEdit::Normal, currentRemotePath, &ok);
        if (ok && !newPath.isEmpty()) {
            // 确保路径以斜杠结尾
            if (!newPath.endsWith('/')) {
                newPath += '/';
            }
            // 确保路径以斜杠开头
            if (!newPath.startsWith('/')) {
                newPath = '/' + newPath;
            }
            currentRemotePath = newPath;
            appendGlobalLog(QString("📝 路径已更改为: %1").arg(currentRemotePath));
            refreshRemoteFiles();
        }
    } else if (isDirectory) {
        QString path = item->data(Qt::UserRole).toString();
        currentRemotePath = path;
        appendGlobalLog(QString("📁 进入目录: %1").arg(path));
        refreshRemoteFiles();
    }
}

void DeployMaster::onDownloadRemoteFile()
{
    QModelIndex idx = ui.tree_remoteFiles->currentIndex();
    if (!idx.isValid()) {
        appendGlobalLog("请先选择要下载的文件");
        return;
    }
    QStandardItem* item = remoteFileModel->itemFromIndex(idx);
    if (!item) return;
    bool isDir = item->data(Qt::UserRole + 1).toBool();
    if (isDir) {
        appendGlobalLog("暂不支持下载目录，请选择单个文件");
        return;
    }
    QString remotePath = item->data(Qt::UserRole).toString();
    QString fileName = item->text().trimmed();
    QString localDir = QFileDialog::getExistingDirectory(this, "选择保存目录");
    if (localDir.isEmpty()) return;

    appendGlobalLog("下载: " + fileName + " → " + localDir);
    QtConcurrent::run([=]() {
        try {
            FtpManager ftm(currentRemoteIP, 21);
            ftm.setCredentials(
                m_deviceBusWidget->user(),
                m_deviceBusWidget->password());
            ftm.downloadFile(remotePath, localDir + "/" + fileName);
            QMetaObject::invokeMethod(this, [this, fileName]() {
                appendGlobalLog("下载完成: " + fileName);
            }, Qt::QueuedConnection);
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = QString::fromStdString(e.what())]() {
                appendGlobalLog("下载失败: " + msg);
            }, Qt::QueuedConnection);
        }
    });
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
void DeployMaster::setupUpdateChecker()
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
    connect(m_versionLabel, &QLabel::linkActivated, this, &DeployMaster::onVersionLabelClicked);
    m_versionLabel->installEventFilter(this);
    statusBar()->addPermanentWidget(m_versionLabel);

    // 菜单栏: 帮助 -> 检查更新
    QMenu* helpMenu = menuBar()->addMenu("帮助");
    m_checkUpdateAction = helpMenu->addAction("检查更新...");
    connect(m_checkUpdateAction, &QAction::triggered,
            this, &DeployMaster::onCheckUpdateTriggered);

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

    // 错误回调（静默错误,更新 UI 状态到 Error）
    m_updateChecker->setErrorCallback([this](const std::string& msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            Q_UNUSED(msg);
            if (m_updateDialog) {
                m_updateDialog->setState(UpdateState::Error);
            }
            // 状态栏仅显示"检查失败",不暴露错误详情（避免 UI 抖动）
            QString ver = m_versionLabel->text().split(' ').first();
            if (ver.isEmpty()) ver = currentVersionString();
            m_versionLabel->setText(ver + " (检查失败)");
            m_versionLabel->setStyleSheet("color: #7B8494; padding: 0 8px;");
        }, Qt::QueuedConnection);
    });

    // 5 秒后自动触发一次后台检查,避免启动阻塞
    QTimer::singleShot(5000, this, &DeployMaster::onCheckUpdateTriggered);
}

// 用户点击菜单"检查更新"或 5 秒自动触发
void DeployMaster::onCheckUpdateTriggered()
{
    if (!m_updateChecker) return;
    m_checkUpdateAction->setEnabled(false);
    m_updateChecker->checkForUpdate();
}

// 状态机回调（主线程）— 切换状态栏标签 + 自动弹出 UpdateDialog
void DeployMaster::onUpdateStateChanged(UpdateState state)
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
        m_versionLabel->setText(ver + " (检查失败)");
        m_versionLabel->setStyleSheet("color: #7B8494; padding: 0 8px;");
        m_checkUpdateAction->setText("检查更新...");
        break;

    case UpdateState::Downloading:
        m_versionLabel->setText(ver + " (下载中...)");
        m_versionLabel->setStyleSheet("color: #7B8494; padding: 0 8px;");
        break;

    case UpdateState::Installed:
        m_versionLabel->setText(ver + " (已下载,待安装)");
        m_versionLabel->setStyleSheet("color: #7B8494; padding: 0 8px;");
        break;
    }
}

// 点击状态栏版本标签 — 在已有可下载/已下载状态下重新唤起对话框
void DeployMaster::onVersionLabelClicked()
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

 //eventFilter — 处理版本标签鼠标点击（非 RichText 状态下 label 不 emit linkActivated）
bool DeployMaster::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_versionLabel && event->type() == QEvent::MouseButtonRelease) {
        // 仅在非 Ready 状态且当前为纯文本时（富文本的 a href 走 linkActivated）
        if (m_updateChecker && m_updateChecker->state() != UpdateState::Ready) {
            onVersionLabelClicked();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}