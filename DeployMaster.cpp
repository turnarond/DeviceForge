#include "DeployMaster.h"
#include "TelnetClient.h"

#include <QFileInfo>
#include <QFileDialog>
#include <QString>
#include <QtConcurrent/QtConcurrent>
#include <QStyleFactory>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QInputDialog>
#include <QVBoxLayout>

#include "OpcUaClient.h" // add header
#include "WebSocketClient.h" // add header
//#include "DiagnosticClient.h" // add header

#include "src/framework/ToolHost.h"
#include "src/ui/DeviceBusWidget.h"

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

    setupLogQueryTab();
    setupTelnetDeployTab();
    setupModbusClusterTab();
    setupOpcUaClientTab();
    setupWebSocketClientTab();

    // 设置 splitter 初始大小比例：工作区占75%，日志区占25%
    ui.splitter_log->setSizes(QList<int>() << 375 << 125);
    // 设置水平 splitter 初始大小比例：左侧工作区占75%，右侧预览占25%
    ui.splitter_main->setSizes(QList<int>() << 600 << 200);

    QApplication::setStyle(QStyleFactory::create("Fusion"));

    connect(ui.btn_addFiles, &QPushButton::clicked, this, &DeployMaster::onAddFilesClicked);
    connect(ui.btn_addFolders, &QPushButton::clicked, this, &DeployMaster::onAddFolderClicked);
    connect(ui.btn_clearUploadList, &QPushButton::clicked, this, &DeployMaster::onFileItemCleanClicked);
    connect(ui.list_uploadedItems, &DropListWidget::filesDropped, this, &DeployMaster::onFilesDropped);
    connect(ui.btn_deploy, &QPushButton::clicked, this, &DeployMaster::onDeployClicked);
    connect(ui.btn_clearLog, &QPushButton::clicked, this, &DeployMaster::onClearLogClicked);

    // 监听应用状态变化
    connect(AppState::instance(), &AppState::isBusyChanged, this, [this](bool busy) {
        ui.btn_deploy->setEnabled(!busy);
    });
    connect(AppState::instance(), &AppState::taskProgressChanged, this, [this](int progress) {
        // 更新进度条（如果有）
    });

    // 初始化远端预览功能
    setupRemotePreview();
}

// 在初始化函数中（如 setupUi 后）
void DeployMaster::setupLogQueryTab()
{
    m_logQueryTab = new LogQueryTab(this, this);
    // 添加到主窗口的 tabWidget
    ui.tabWidget->addTab(m_logQueryTab, tr("日志查询"));

}

void DeployMaster::setupTelnetDeployTab()
{
    m_telnetDeployTab = new TelnetDeploy(this);
    ui.tabWidget->addTab(m_telnetDeployTab, tr("批量命令"));
}

void DeployMaster::setupModbusClusterTab()
{
    m_modbusCluster = new ModbusCluster(this);
    ui.tabWidget->addTab(m_modbusCluster, tr("MODBUS测试"));
}

// new: setup for opc ua
void DeployMaster::setupOpcUaClientTab()
{
    // OpcUaClientTab is optional and uses placeholder implementation
    OpcUaClientTab* opcTab = new OpcUaClientTab(this);
    ui.tabWidget->addTab(opcTab, tr("OPC UA 客户端"));
}

// new: setup for websocket
void DeployMaster::setupWebSocketClientTab()
{
    m_webSocketClient = new WebSocketClient(this);
    ui.tabWidget->addTab(m_webSocketClient, tr("WebSocket"));
}

// new: setup for diagnostic
void DeployMaster::setupDiagnosticClientTab()
{
    //m_diagnosticClient = new DiagnosticClient(this);
    //ui.tabWidget->addTab(m_diagnosticClient, tr("诊断工具"));
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
    QString user = m_deviceBusWidget ? m_deviceBusWidget->user() : QString();
    QString pass = m_deviceBusWidget ? m_deviceBusWidget->password() : QString();
    QString remotePath = ui.txt_remotePath->text().trimmed();
    if (!remotePath.endsWith('/')) remotePath += '/';

    if (uploadItems.isEmpty()) {
        appendFtpLog("❌ 错误：请至少添加一个要上传的文件或文件夹！");
        return;
    }
    if (user.isEmpty() || pass.isEmpty()) {
        appendFtpLog("❌ 错误：请输入用户名和密码！");
        return;
    }

    QStringList ips = getTargetIPs();
    if (ips.isEmpty()) {
        appendFtpLog("❌ 错误：请至少输入一个目标 IP！");
        return;
    }

    bool shouldReboot = ui.chk_rebootAfterDeploy->isChecked();
    bool shouldClearRemote = ui.chk_clearRemoteBeforeUpload->isChecked();

    appendFtpLog(QString("🚀 开始部署 %1 个内容到 %2 台设备...")
        .arg(uploadItems.size()).arg(ips.size()));

    // 构建要上传的文件列表
    QStringList items;
    for (const UploadItem& item : uploadItems) {
        items << item.fullPath;
    }

    // TODO: 通过 ToolHost + FtpDeployBackend 触发部署（替换旧 EventBus 路径）
    appendFtpLog("⚠ 部署功能已迁移至「文件部署」Tool，请使用新的 Tool 界面。");
}

void DeployMaster::onFtpUploadFinished(const QStringList& deploySuccesses,
    const QStringList& deployFailures,
    bool shouldReboot,
    const QString& user,
    const QString& pass)
{
    QStringList rebootSuccess, rebootFailure;
    if (shouldReboot && !deploySuccesses.isEmpty()) {
        appendFtpLog("🔄 开始发送重启命令...");

        // 为每个成功设备创建 TelnetClient（在主线程！）
        for (const QString& ip : deploySuccesses) {
            TelnetClient tc;

            if (!tc.syncConnectToHost(ip, 23, 3000)) {
                rebootFailure << ip;
                appendFtpLog(QString("❌ 连接失败: %1").arg(ip));
                continue;
            }

            if (!tc.syncLogin(user, pass, 5000)) {
                rebootFailure << ip;
                appendFtpLog(QString("❌ 登录失败: %1").arg(ip));
                continue;
            }

            tc.syncSendCommand("sync", 1000);
            QThread::msleep(100);
            if (tc.syncSendCommand("reboot -f", 1000)) {
                rebootSuccess << ip;
                appendFtpLog(QString("✅ 重启命令已发送: %1").arg(ip));
            }
            else {
                rebootFailure << ip;
                appendFtpLog(QString("⚠️ 命令发送异常: %1").arg(ip));
            }
        }

        appendFtpLog("🎉 批量重启完成！");
        appendFtpLog(QString("📊 重启成功: %1 | 重启失败: %2")
            .arg(rebootSuccess.size()).arg(rebootFailure.size()));
        if (!rebootFailure.isEmpty())
            appendFtpLog("❌ 失败列表: " + rebootFailure.join(", "));
    }

    ui.btn_deploy->setEnabled(true);
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
    uploadItems.clear();
}

void DeployMaster::onClearLogClicked()
{
    ui.txt_globalLog->clear();
}

void DeployMaster::addFileToList(const QString& filePath)
{
    // 检查文件是否已经在列表中，避免重复添加
    QList<QListWidgetItem*> existingItems = ui.list_uploadedItems->findItems(filePath, Qt::MatchExactly);
    if (existingItems.isEmpty()) {
        ui.list_uploadedItems->addItem(filePath);
        UploadItem item;
        item.isFolder = false;
        item.fullPath = filePath;
        uploadItems.push_back(item);
    }
}

void DeployMaster::addFolderToList(const QString& folderPath)
{
    // 检查文件夹是否已经在列表中，避免重复添加
    QList<QListWidgetItem*> existingItems = ui.list_uploadedItems->findItems(folderPath, Qt::MatchExactly);
    if (existingItems.isEmpty()) {
        ui.list_uploadedItems->addItem(folderPath);
        UploadItem item;
        item.isFolder = true;;
        item.fullPath = folderPath;
        uploadItems.push_back(item);
    }
}

void DeployMaster::appendFtpLog(const QString& log)
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
    
    // 设置初始路径
    currentRemotePath = ui.txt_remotePath->text().trimmed();
    // 确保路径格式正确
    if (!currentRemotePath.endsWith('/')) {
        currentRemotePath += '/';
    }
    if (!currentRemotePath.startsWith('/')) {
        currentRemotePath = '/' + currentRemotePath;
    }
    
    // 连接信号槽
    connect(ui.cmb_targetIPs, &QComboBox::currentTextChanged, this, &DeployMaster::onIPSelectionChanged);
    connect(ui.btn_refreshRemote, &QPushButton::clicked, this, &DeployMaster::refreshRemoteFiles);
    connect(ui.tree_remoteFiles, &QTreeView::doubleClicked, this, &DeployMaster::onRemoteFileDoubleClicked);
    
    // 初始化IP下拉框
    QStringList ips = getTargetIPs();
    ui.cmb_targetIPs->clear();
    ui.cmb_targetIPs->addItems(ips);
    
    if (!ips.isEmpty()) {
        currentRemoteIP = ips.first();
        refreshRemoteFiles();
    }
}

void DeployMaster::onIPSelectionChanged()
{
    currentRemoteIP = ui.cmb_targetIPs->currentText();
    if (!currentRemoteIP.isEmpty()) {
        refreshRemoteFiles();
    }
}

void DeployMaster::refreshRemoteFiles()
{
    if (currentRemoteIP.isEmpty()) {
        appendFtpLog("❌ 错误：请选择目标 IP！");
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
        appendFtpLog("❌ 错误：请输入用户名和密码！");
        return;
    }
    
    appendFtpLog(QString("🔄 正在刷新远程文件列表: %1%2").arg(currentRemoteIP).arg(currentRemotePath));
    
    // 异步刷新远程文件列表
    QtConcurrent::run([=]() {
        try {
            FtpManager ftpm(currentRemoteIP, 21);
            ftpm.setCredentials(user, pass);
            QList<FtpFileInfo> files = ftpm.listFtpDirectoryDetailed(currentRemotePath);
            
            // 在主线程更新UI
            QMetaObject::invokeMethod(this, "buildRemoteFileTree", Qt::QueuedConnection, Q_ARG(QList<FtpFileInfo>, files));
            QMetaObject::invokeMethod(this, "appendFtpLog", Qt::QueuedConnection, Q_ARG(QString, "✅ 远程文件列表刷新成功"));
        } catch (const std::exception& ex) {
            QMetaObject::invokeMethod(this, "appendFtpLog", Qt::QueuedConnection, Q_ARG(QString, QString("❌ 刷新失败: %1").arg(QString::fromStdString(ex.what()).left(100))));
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
            appendFtpLog(QString("📝 路径已更改为: %1").arg(currentRemotePath));
            refreshRemoteFiles();
        }
    } else if (isDirectory) {
        // 双击普通目录项，进入该目录
        QString path = item->data(Qt::UserRole).toString();
        currentRemotePath = path;
        appendFtpLog(QString("📁 进入目录: %1").arg(path));
        refreshRemoteFiles();
    }
}