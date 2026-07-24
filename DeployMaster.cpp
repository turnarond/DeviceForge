#include "DeployMaster.h"
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QDesktopServices>
#include <QStandardPaths>
#include <QUrl>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <algorithm>
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
#include <QKeySequence>

#include "OpcUaClient.h" // add header
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
#include "src/utils/FormatUtils.h"      // formatFileSize()
#include "config/SettingsDialog.h"      // 配置管理面板

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
    m_remotePathEdit = new QLineEdit("/", this);
    ui.verticalLayout_remote->insertWidget(1, m_remotePathEdit);
    // 清除日志按钮移到底部日志区
    ui.groupBox_log->layout()->addWidget(ui.btn_clearLog);
    ui.tabWidget->setTabVisible(ui.tabWidget->indexOf(ui.tab_deploy), false);

    connect(ui.btn_clearLog, &QPushButton::clicked, this, &DeployMaster::onClearLogClicked);

    // 隐藏旧远端预览控件（将被 m_protocolCombo + m_deviceCombo 替代）
    ui.btn_refreshRemote->hide();

    // 初始化远端预览功能
    setupRemotePreview();

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

// OPC UA 客户端 Tool（open62541 真实实现，替代旧演示 Tab）
void DeployMaster::setupOpcUaClientTab()
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
    widget->onToolStart();
    m_opcUaClientBackend = backend;
    m_opcUaClientWidget = widget;
    ui.tabWidget->addTab(m_opcUaClientWidget, tr("OPC UA 客户端"));
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
    // 初始化远程文件模型，启用多选批量下载
    remoteFileModel = new QStandardItemModel(this);
    ui.tree_remoteFiles->setModel(remoteFileModel);
    ui.tree_remoteFiles->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui.tree_remoteFiles->setEditTriggers(QAbstractItemView::NoEditTriggers); // 禁用双击重命名
    ui.tree_remoteFiles->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui.tree_remoteFiles, &QTreeView::customContextMenuRequested,
            this, &DeployMaster::onRemoteFileContextMenu);

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

    // 确保 currentRemoteIP 与 combo box 同步（setCurrentIndex(idx) 时信号被屏蔽）
    currentRemoteIP = ui.cmb_targetIPs->currentText();

    // 如果之前无选中但列表不空，自动选中第一个并刷新
    if (ui.cmb_targetIPs->currentIndex() < 0 && ui.cmb_targetIPs->count() > 0) {
        ui.cmb_targetIPs->setCurrentIndex(0);
        currentRemoteIP = ui.cmb_targetIPs->currentText();
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
    
    // 异步刷新远程文件列表（按值捕获避免数据竞争）
    QString ip = currentRemoteIP;
    QString path = currentRemotePath;
    QtConcurrent::run([=]() {
        try {
            FtpManager ftpm(ip, 21);
            ftpm.setCredentials(user, pass);
            QList<FtpFileInfo> files = ftpm.listFtpDirectoryDetailed(path);
            
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
    
    // 排序：目录在前，文件在后，各自按名称字母排序
    QList<FtpFileInfo> sorted = files;
    std::sort(sorted.begin(), sorted.end(), [](const FtpFileInfo& a, const FtpFileInfo& b) {
        if (a.isDirectory != b.isDirectory)
            return a.isDirectory > b.isDirectory; // 目录排在文件前
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });

    // 添加文件和文件夹
    for (const FtpFileInfo& file : sorted) {
        QStandardItem* item;
        if (file.isDirectory) {
            item = new QStandardItem(file.name);
            item->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));
            item->setData(currentRemotePath + file.name + "/", Qt::UserRole);
            item->setData(true, Qt::UserRole + 1); // 标记为目录
            item->setData(false, Qt::UserRole + 2); // 标记为非根目录项
            item->setData(file.name, Qt::UserRole + 3); // 存储原始目录名
        } else {
            QString sizeStr = formatFileSize(file.size);
            item = new QStandardItem(QString("%1 (%2)").arg(file.name, sizeStr));
            item->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileIcon));
            item->setData(currentRemotePath + file.name, Qt::UserRole);
            item->setData(false, Qt::UserRole + 1); // 标记为文件
            item->setData(false, Qt::UserRole + 2); // 标记为非根目录项
            item->setData(file.name, Qt::UserRole + 3); // 存储原始文件名
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
    } else {
        // 双击文件 → 选中并下载
        ui.tree_remoteFiles->selectionModel()->select(index,
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        onDownloadRemoteFile();
    }
}

void DeployMaster::onDownloadRemoteFile()
{
    // 获取所有选中项（支持多选 + 目录递归下载）
    QModelIndexList selected = ui.tree_remoteFiles->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        appendGlobalLog("请先选择要下载的文件或文件夹");
        return;
    }

    // 去重（QTreeView 可能返回同一行多个列）
    QList<QModelIndex> uniqueRows;
    for (const auto& idx : selected) {
        if (idx.column() == 0) uniqueRows.append(idx);
    }
    if (uniqueRows.isEmpty()) return;

    // 收集用户选中的条目信息 {remotePath, isDir, localName}
    struct SelectedEntry { QString remotePath; bool isDir; QString localName; };
    QList<SelectedEntry> entries;

    for (const auto& idx : uniqueRows) {
        QStandardItem* item = remoteFileModel->itemFromIndex(idx);
        if (!item) continue;
        bool isDir = item->data(Qt::UserRole + 1).toBool();
        bool isRoot = item->data(Qt::UserRole + 2).toBool();
        if (isRoot) continue;

        QString remotePath = item->data(Qt::UserRole).toString();
        QString name = item->data(Qt::UserRole + 3).toString();

        SelectedEntry e;
        e.remotePath = remotePath;
        e.isDir = isDir;
        e.localName = name;
        entries.append(e);
    }

    if (entries.isEmpty()) return;

    QString localDir = QFileDialog::getExistingDirectory(this, "选择保存目录");
    if (localDir.isEmpty()) return;

    QString user = m_deviceBusWidget->user();
    QString pass = m_deviceBusWidget->password();

    appendGlobalLog(QString("⬇ 开始下载 %1 个条目 → %2").arg(entries.size()).arg(localDir));

    // 全部在后台线程执行：先递归扫描目录，再逐文件下载
    QString ip = currentRemoteIP;
    QPointer<DeployMaster> self(this);
    QtConcurrent::run([=]() {
        if (!self) return;
        auto log = [self](const QString& msg) {
            if (self) QMetaObject::invokeMethod(self, "appendGlobalLog",
                Qt::QueuedConnection, Q_ARG(QString, msg));
        };

        // 递归收集目录下所有文件
        struct FlatItem { QString remotePath; QString localName; };
        std::function<void(const QString&, const QString&, QList<FlatItem>&)> scanDir;
        scanDir = [&](const QString& dirPath, const QString& prefix, QList<FlatItem>& out) {
            try {
                FtpManager ftm(ip, 21);
                ftm.setCredentials(user, pass);
                QList<FtpFileInfo> children = ftm.listFtpDirectoryDetailed(dirPath);
                for (const auto& c : children) {
                    QString childRemote = dirPath;
                    if (!childRemote.endsWith('/')) childRemote += '/';
                    childRemote += c.name;
                    if (c.isDirectory) {
                        scanDir(childRemote + "/", prefix + c.name + "/", out);
                    } else {
                        out.append({childRemote, prefix + c.name});
                    }
                }
            } catch (const std::exception& e) {
                log(QString("⚠ 扫描失败 %1: %2").arg(dirPath).arg(QString::fromStdString(e.what()).left(80)));
            }
        };

        QList<FlatItem> allFiles;
        for (const auto& e : entries) {
            if (e.isDir) {
                log(QString("📁 扫描目录: %1").arg(e.localName));
                scanDir(e.remotePath, e.localName + "/", allFiles);
            } else {
                allFiles.append({e.remotePath, e.localName});
            }
        }

        int total = allFiles.size();
        if (total == 0) {
            log("没有可下载的文件（所选条目中无文件）");
            return;
        }
        log(QString("共 %1 个文件待下载").arg(total));

        int success = 0, failed = 0;
        FtpManager ftm(ip, 21);
        ftm.setCredentials(user, pass);
        for (int i = 0; i < total; ++i) {
            const auto& f = allFiles[i];
            QString localPath = localDir + "/" + f.localName;
            QDir().mkpath(QFileInfo(localPath).absolutePath());
            try {
                ftm.downloadFile(f.remotePath, localPath);
                ++success;
                log(QString("  [%1/%2] ✅ %3").arg(i + 1).arg(total).arg(f.localName));
            } catch (const std::exception& ex) {
                ++failed;
                log(QString("  [%1/%2] ❌ %3: %4").arg(i + 1).arg(total).arg(f.localName)
                    .arg(QString::fromStdString(ex.what()).left(80)));
            }
        }
        log(QString("✅ 下载完成: %1 成功, %2 失败").arg(success).arg(failed));
    });
}

// ============================================================
// 右键上下文菜单 — 下载 / 查看 / 重命名 / 删除
// ============================================================

void DeployMaster::onRemoteFileContextMenu(const QPoint& pos)
{
    QModelIndex index = ui.tree_remoteFiles->indexAt(pos);
    bool isDir = index.isValid() ? index.data(Qt::UserRole + 1).toBool() : false;
    bool isRoot = index.isValid() ? index.data(Qt::UserRole + 2).toBool() : false;

    QMenu menu(ui.tree_remoteFiles);

    if (index.isValid() && !isRoot) {
        menu.addAction("下载", this, &DeployMaster::onDownloadRemoteFile);
        if (!isDir) {
            menu.addAction("查看", this, &DeployMaster::onViewRemoteFile);
        }
        menu.addSeparator();
        menu.addAction("重命名", this, &DeployMaster::onRenameRemoteFile);
        menu.addAction(isDir ? "删除目录" : "删除文件", this, &DeployMaster::onDeleteRemoteFile);
    } else if (isRoot) {
        menu.addAction("编辑路径...", this, [this]() {
            bool ok;
            QString newPath = QInputDialog::getText(this, "编辑远程路径",
                "请输入新的远程路径:", QLineEdit::Normal, currentRemotePath, &ok);
            if (ok && !newPath.isEmpty()) {
                if (!newPath.endsWith('/')) newPath += '/';
                if (!newPath.startsWith('/')) newPath = '/' + newPath;
                currentRemotePath = newPath;
                appendGlobalLog(QString("📝 路径已更改为: %1").arg(currentRemotePath));
                refreshRemoteFiles();
            }
        });
    }
    menu.addSeparator();
    menu.addAction("刷新", this, &DeployMaster::refreshRemoteFiles);

    menu.exec(ui.tree_remoteFiles->viewport()->mapToGlobal(pos));
}

// 查看文件：下载到临时目录 + 系统默认程序打开（≤5MB）
void DeployMaster::onViewRemoteFile()
{
    QModelIndex idx = ui.tree_remoteFiles->currentIndex();
    if (!idx.isValid()) return;
    QStandardItem* item = remoteFileModel->itemFromIndex(idx);
    if (!item) return;
    bool isDir = item->data(Qt::UserRole + 1).toBool();
    if (isDir) return;

    QString remotePath = item->data(Qt::UserRole).toString();
    QString fileName = item->data(Qt::UserRole + 3).toString();

    QString user = m_deviceBusWidget->user();
    QString pass = m_deviceBusWidget->password();

    appendGlobalLog("🔍 正在查看: " + fileName);
    QString ip = currentRemoteIP;
    QPointer<DeployMaster> self(this);
    QtConcurrent::run([=]() {
        if (!self) return;
        try {
            // 先列出目录获取文件大小
            FtpManager ftm(ip, 21);
            ftm.setCredentials(user, pass);
            QString parentDir = remotePath.left(remotePath.lastIndexOf('/'));
            if (parentDir.isEmpty()) parentDir = "/";
            QList<FtpFileInfo> files = ftm.listFtpDirectoryDetailed(parentDir);
            qint64 actualSize = -1;
            for (const auto& f : files) {
                if (f.name == fileName) { actualSize = f.size; break; }
            }
            if (actualSize > 5 * 1024 * 1024) {
                QMetaObject::invokeMethod(this, [this, fileName]() {
                    appendGlobalLog(QString("⚠ 文件 %1 超过 5MB，请使用下载功能").arg(fileName));
                }, Qt::QueuedConnection);
                return;
            }

            QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                + "/DeviceForge/view";
            QDir().mkpath(tempDir);
            QString localPath = tempDir + "/" + fileName;

            FtpManager ftm2(ip, 21);
            ftm2.setCredentials(user, pass);
            ftm2.downloadFile(remotePath, localPath);

            QMetaObject::invokeMethod(this, [this, localPath, fileName]() {
                QDesktopServices::openUrl(QUrl::fromLocalFile(localPath));
                appendGlobalLog("✅ 已打开: " + fileName);
            }, Qt::QueuedConnection);
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = QString::fromStdString(e.what())]() {
                appendGlobalLog("❌ 查看失败: " + msg);
            }, Qt::QueuedConnection);
        }
    });
}

// 删除选中文件/文件夹
void DeployMaster::onDeleteRemoteFile()
{
    QModelIndexList selected = ui.tree_remoteFiles->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    // 收集要删除的条目
    struct DelItem { QString parentDir; QString name; bool isDir; };
    QList<DelItem> items;
    for (const auto& idx : selected) {
        if (idx.column() != 0) continue;
        QStandardItem* item = remoteFileModel->itemFromIndex(idx);
        if (!item) continue;
        bool isDir = item->data(Qt::UserRole + 1).toBool();
        bool isRoot = item->data(Qt::UserRole + 2).toBool();
        if (isRoot) continue;
        QString remotePath = item->data(Qt::UserRole).toString();
        QString name = item->data(Qt::UserRole + 3).toString();
        // 从 remotePath 提取 parentDir
        QString parentDir = currentRemotePath;
        items.append({parentDir, name, isDir});
    }
    if (items.isEmpty()) return;

    QStringList names;
    for (const auto& di : items) names << di.name;
    int ret = QMessageBox::warning(this, "确认删除",
        QString("确定要删除以下 %1 个条目吗？\n\n%2\n\n此操作不可撤销！")
            .arg(items.size())
            .arg(names.join("\n")),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    QString user = m_deviceBusWidget->user();
    QString pass = m_deviceBusWidget->password();

    appendGlobalLog(QString("🗑 正在删除 %1 个条目...").arg(items.size()));
    QString ip = currentRemoteIP;
    QPointer<DeployMaster> self(this);
    QtConcurrent::run([=]() {
        if (!self) return;
        int ok = 0, fail = 0;
        for (const auto& di : items) {
            try {
                FtpManager ftm(ip, 21);
                ftm.setCredentials(user, pass);
                bool result = di.isDir
                    ? ftm.deleteFtpDirectory(di.parentDir, di.name)
                    : ftm.deleteFtpFile(di.parentDir, di.name);
                if (result) ++ok; else ++fail;
            } catch (...) { ++fail; }
        }
        QMetaObject::invokeMethod(this, [this, ok, fail]() {
            appendGlobalLog(QString("✅ 删除完成: %1 成功, %2 失败").arg(ok).arg(fail));
            if (ok > 0) refreshRemoteFiles();
        }, Qt::QueuedConnection);
    });
}

// 重命名选中文件/文件夹
void DeployMaster::onRenameRemoteFile()
{
    QModelIndex idx = ui.tree_remoteFiles->currentIndex();
    if (!idx.isValid()) return;
    QStandardItem* item = remoteFileModel->itemFromIndex(idx);
    if (!item) return;
    bool isRoot = item->data(Qt::UserRole + 2).toBool();
    if (isRoot) return;

    QString oldName = item->data(Qt::UserRole + 3).toString();
    bool ok;
    QString newName = QInputDialog::getText(this, "重命名",
        QString("新名称（当前: %1）:").arg(oldName),
        QLineEdit::Normal, oldName, &ok);
    if (!ok || newName.isEmpty() || newName == oldName) return;

    QString user = m_deviceBusWidget->user();
    QString pass = m_deviceBusWidget->password();

    appendGlobalLog(QString("✏ 重命名: %1 → %2").arg(oldName, newName));
    QString ip = currentRemoteIP;
    QString path = currentRemotePath;
    QPointer<DeployMaster> self(this);
    QtConcurrent::run([=]() {
        if (!self) return;
        try {
            FtpManager ftm(ip, 21);
            ftm.setCredentials(user, pass);
            bool result = ftm.renameFtpFile(path, oldName, newName);
            QMetaObject::invokeMethod(this, [this, oldName, newName, result]() {
                if (result) {
                    appendGlobalLog(QString("✅ 重命名成功: %1 → %2").arg(oldName, newName));
                    refreshRemoteFiles();
                } else {
                    appendGlobalLog("❌ 重命名失败（服务器拒绝）");
                }
            }, Qt::QueuedConnection);
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = QString::fromStdString(e.what())]() {
                appendGlobalLog("❌ 重命名失败: " + msg);
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

    // 复用 .ui 已有的"帮助"菜单,添加"检查更新"项
    m_checkUpdateAction = ui.menu_help->addAction("检查更新...");
    connect(m_checkUpdateAction, &QAction::triggered,
            this, &DeployMaster::onCheckUpdateTriggered);

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
void DeployMaster::onCheckUpdateTriggered(bool isAuto)
{
    if (!m_updateChecker) return;
    m_autoCheck = isAuto;
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