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