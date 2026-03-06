#include "DiagnosticClient.h"
#include "ui_tab_diagnostic.h"
#include "DeployMaster.h"

#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QDir>
#include <QDirIterator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

DiagnosticClient::DiagnosticClient(DeployMaster* parentWindow, QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::TabDiagnostic)
    , m_mainWindow(parentWindow)
    , m_refreshTimer(new QTimer(this))
    , m_logTreeModel(new QStandardItemModel(this))
{
    ui->setupUi(this);
    initUI();
    loadDevices();
    updateLocalLogTree();

    // 连接信号槽
    connect(ui->btn_add_device, &QPushButton::clicked, this, &DiagnosticClient::on_btn_add_device_clicked);
    connect(ui->btn_remove_device, &QPushButton::clicked, this, &DiagnosticClient::on_btn_remove_device_clicked);
    connect(ui->btn_refresh_status, &QPushButton::clicked, this, &DiagnosticClient::on_btn_refresh_status_clicked);
    connect(ui->btn_start_diagnostic, &QPushButton::clicked, this, &DiagnosticClient::on_btn_start_diagnostic_clicked);
    connect(ui->btn_open_log, &QPushButton::clicked, this, &DiagnosticClient::on_btn_open_log_clicked);
    connect(ui->btn_clean_logs, &QPushButton::clicked, this, &DiagnosticClient::on_btn_clean_logs_clicked);

    connect(m_refreshTimer, &QTimer::timeout, this, &DiagnosticClient::onRefreshStatusTimer);

    // 启动刷新定时器
    m_refreshTimer->start(5000); // 每5秒刷新一次状态
}

DiagnosticClient::~DiagnosticClient()
{
    // 清理VSOA客户端
    for (DeviceInfo& device : m_devices) {
        if (device.vsoaClient) {
            device.vsoaClient->disconnectFromServer();
            delete device.vsoaClient;
        }
    }
    
    delete ui;
    delete m_refreshTimer;
    delete m_logTreeModel;
}

void DiagnosticClient::initUI()
{
    // 初始化任务表格
    ui->table_tasks->setColumnCount(5);
    ui->table_tasks->setHorizontalHeaderLabels({"设备", "状态", "进度", "文件", "大小"});
    ui->table_tasks->horizontalHeader()->setStretchLastSection(true);

    // 初始化日志树
    ui->tree_logs->setModel(m_logTreeModel);
    m_logTreeModel->setHorizontalHeaderLabels({"名称", "大小", "修改时间"});
}

void DiagnosticClient::loadDevices()
{
    // 从配置文件加载设备列表
    // 这里暂时硬编码一些示例设备
    m_devices.clear();
    ui->list_devices->clear();

    DeviceInfo device1;
    device1.ip = "192.168.1.100";
    device1.name = "PLC Master";
    device1.status = "离线";
    device1.vsoaClient = new VsoaClient(this);
    m_devices.append(device1);

    DeviceInfo device2;
    device2.ip = "192.168.1.101";
    device2.name = "PLC Standby";
    device2.status = "离线";
    device2.vsoaClient = new VsoaClient(this);
    m_devices.append(device2);

    // 添加到列表
    for (const DeviceInfo& device : m_devices) {
        QListWidgetItem* item = new QListWidgetItem(QString("%1 (%2) - %3").arg(device.name).arg(device.ip).arg(device.status));
        item->setData(Qt::UserRole, device.ip);
        ui->list_devices->addItem(item);
    }
}

void DiagnosticClient::saveDevices()
{
    // 保存设备列表到配置文件
    // 这里暂时省略实现
}

void DiagnosticClient::updateDeviceStatus(const QString& deviceIP, const QString& status)
{
    // 更新设备状态
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices[i].ip == deviceIP) {
            m_devices[i].status = status;
            break;
        }
    }

    // 更新列表显示
    for (int i = 0; i < ui->list_devices->count(); ++i) {
        QListWidgetItem* item = ui->list_devices->item(i);
        QString ip = item->data(Qt::UserRole).toString();
        if (ip == deviceIP) {
            for (const DeviceInfo& device : m_devices) {
                if (device.ip == ip) {
                    item->setText(QString("%1 (%2) - %3").arg(device.name).arg(device.ip).arg(device.status));
                    break;
                }
            }
            break;
        }
    }
}

void DiagnosticClient::startDiagnosticForDevice(const QString& deviceIP)
{
    // 开始诊断流程
    collectLogs(deviceIP);
}

void DiagnosticClient::collectLogs(const QString& deviceIP)
{
    // 更新任务状态
    TaskInfo task;
    task.deviceIP = deviceIP;
    task.status = "采集中";
    task.progress = 0;
    m_tasks.append(task);
    updateTasksTable();

    ui->txt_status->append(QString("[%1] 开始采集设备 %2 的日志").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")).arg(deviceIP));

    // 查找设备对应的VSOA客户端
    VsoaClient* client = nullptr;
    for (DeviceInfo& device : m_devices) {
        if (device.ip == deviceIP) {
            client = device.vsoaClient;
            break;
        }
    }

    if (!client) {
        ui->txt_status->append(QString("[%1] 设备 %2 未找到VSOA客户端").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")).arg(deviceIP));
        // 更新任务状态
        for (int i = 0; i < m_tasks.size(); ++i) {
            if (m_tasks[i].deviceIP == deviceIP) {
                m_tasks[i].status = "失败";
                break;
            }
        }
        updateTasksTable();
        return;
    }

    // 连接到VSOA服务器
    if (!client->connectToServer(deviceIP, 8899)) {
        ui->txt_status->append(QString("[%1] 连接设备 %2 失败").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")).arg(deviceIP));
        updateDeviceStatus(deviceIP, "离线");
        // 更新任务状态
        for (int i = 0; i < m_tasks.size(); ++i) {
            if (m_tasks[i].deviceIP == deviceIP) {
                m_tasks[i].status = "失败";
                break;
            }
        }
        updateTasksTable();
        return;
    }

    // 构造请求参数
    QJsonObject params;
    params["log_lines"] = ui->spin_log_lines->value();
    params["include_dump"] = ui->chk_include_dump->isChecked();
    params["timeout_sec"] = ui->spin_timeout->value();

    // 发送RPC请求
    QByteArray responseData;
    int status = client->sendRpcRequest("/api/diag/collect", params, &responseData);

    if (status != VSOA_STATUS_SUCCESS) {
        ui->txt_status->append(QString("[%1] 设备 %2 采集日志失败: 状态码 %3").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")).arg(deviceIP).arg(status));
        updateDeviceStatus(deviceIP, "离线");
        // 更新任务状态
        for (int i = 0; i < m_tasks.size(); ++i) {
            if (m_tasks[i].deviceIP == deviceIP) {
                m_tasks[i].status = "失败";
                break;
            }
        }
        updateTasksTable();
        return;
    }

    // 解析响应
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    QJsonObject obj = doc.object();

    if (obj.contains("status") && obj["status"].toString() == "success") {
        QString fileName = obj["file_name"].toString();
        qint64 fileSize = obj["file_size"].toDouble();

        // 开始下载日志
        downloadLogs(deviceIP, fileName, fileSize);
    } else {
        // 采集失败
        for (int i = 0; i < m_tasks.size(); ++i) {
            if (m_tasks[i].deviceIP == deviceIP) {
                m_tasks[i].status = "失败";
                break;
            }
        }
        updateTasksTable();

        ui->txt_status->append(QString("[%1] 设备 %2 采集日志失败").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")).arg(deviceIP));
    }
}

void DiagnosticClient::downloadLogs(const QString& deviceIP, const QString& fileName, qint64 fileSize)
{
    // 更新任务状态
    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks[i].deviceIP == deviceIP) {
            m_tasks[i].status = "下载中";
            m_tasks[i].fileName = fileName;
            m_tasks[i].fileSize = fileSize;
            break;
        }
    }
    updateTasksTable();

    ui->txt_status->append(QString("[%1] 开始下载设备 %2 的日志文件: %3").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")).arg(deviceIP).arg(fileName));

    // 查找设备对应的VSOA客户端
    VsoaClient* client = nullptr;
    for (DeviceInfo& device : m_devices) {
        if (device.ip == deviceIP) {
            client = device.vsoaClient;
            break;
        }
    }

    if (!client) {
        ui->txt_status->append(QString("[%1] 设备 %2 未找到VSOA客户端").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")).arg(deviceIP));
        // 更新任务状态
        for (int i = 0; i < m_tasks.size(); ++i) {
            if (m_tasks[i].deviceIP == deviceIP) {
                m_tasks[i].status = "失败";
                break;
            }
        }
        updateTasksTable();
        return;
    }

    // 确保连接到VSOA服务器
    if (client->connectToServer(deviceIP, 8899)) {
        // 准备保存文件
        QString savePath = QDir::currentPath() + "/Logs/" + fileName;
        QFile file(savePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            ui->txt_status->append(QString("[%1] 无法创建文件: %2").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")).arg(savePath));
            // 更新任务状态
            for (int i = 0; i < m_tasks.size(); ++i) {
                if (m_tasks[i].deviceIP == deviceIP) {
                    m_tasks[i].status = "失败";
                    break;
                }
            }
            updateTasksTable();
            return;
        }

        // 分块下载文件
        qint64 offset = 0;
        const int chunkSize = 4096; // 每次下载4KB

        while (offset < fileSize) {
            // 构造请求参数
            QJsonObject params;
            params["file_name"] = fileName;
            params["offset"] = offset;
            params["size"] = chunkSize;

            // 发送RPC请求，获取二进制数据
            QByteArray responseData;
            QByteArray responseBinary;
            int status = client->sendRpcRequest("/api/diag/download", params, &responseData, &responseBinary);

            if (status != VSOA_STATUS_SUCCESS) {
                ui->txt_status->append(QString("[%1] 设备 %2 下载日志失败: 状态码 %3").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")).arg(deviceIP).arg(status));
                file.close();
                // 更新任务状态
                for (int i = 0; i < m_tasks.size(); ++i) {
                    if (m_tasks[i].deviceIP == deviceIP) {
                        m_tasks[i].status = "失败";
                        break;
                    }
                }
                updateTasksTable();
                return;
            }

            // 写入文件
            qint64 bytesWritten = file.write(responseBinary);
            if (bytesWritten != responseBinary.size()) {
                ui->txt_status->append(QString("[%1] 写入文件失败").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")));
                file.close();
                // 更新任务状态
                for (int i = 0; i < m_tasks.size(); ++i) {
                    if (m_tasks[i].deviceIP == deviceIP) {
                        m_tasks[i].status = "失败";
                        break;
                    }
                }
                updateTasksTable();
                return;
            }

            // 更新进度
            offset += responseBinary.size();
            int progress = static_cast<int>((offset * 100) / fileSize);

            // 更新任务状态
            for (int i = 0; i < m_tasks.size(); ++i) {
                if (m_tasks[i].deviceIP == deviceIP) {
                    m_tasks[i].progress = progress;
                    break;
                }
            }
            updateTasksTable();

            // 如果已经下载完成，退出循环
            if (offset >= fileSize) {
                break;
            }
        }

        file.close();

        // 下载完成，开始清理远程文件
        cleanupRemoteFiles(deviceIP, fileName);

        // 更新任务状态
        for (int i = 0; i < m_tasks.size(); ++i) {
            if (m_tasks[i].deviceIP == deviceIP) {
                m_tasks[i].status = "完成";
                m_tasks[i].progress = 100;
                break;
            }
        }
        updateTasksTable();

        // 更新本地日志树
        updateLocalLogTree();

        ui->txt_status->append(QString("[%1] 设备 %2 日志下载完成: %3").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")).arg(deviceIP).arg(fileName));
    } else {
        ui->txt_status->append(QString("[%1] 连接设备 %2 失败").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")).arg(deviceIP));
        updateDeviceStatus(deviceIP, "离线");
        // 更新任务状态
        for (int i = 0; i < m_tasks.size(); ++i) {
            if (m_tasks[i].deviceIP == deviceIP) {
                m_tasks[i].status = "失败";
                break;
            }
        }
        updateTasksTable();
    }
}

void DiagnosticClient::cleanupRemoteFiles(const QString& deviceIP, const QString& fileName)
{
    ui->txt_status->append(QString("[%1] 开始清理设备 %2 的远程文件: %3").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")).arg(deviceIP).arg(fileName));

    // 查找设备对应的VSOA客户端
    VsoaClient* client = nullptr;
    for (DeviceInfo& device : m_devices) {
        if (device.ip == deviceIP) {
            client = device.vsoaClient;
            break;
        }
    }

    if (!client) {
        ui->txt_status->append(QString("[%1] 设备 %2 未找到VSOA客户端").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")).arg(deviceIP));
        return;
    }

    // 确保连接到VSOA服务器
    if (client->connectToServer(deviceIP, 8899)) {
        // 构造请求参数
        QJsonObject params;
        params["file_name"] = fileName;

        // 发送RPC请求
        QByteArray responseData;
        int status = client->sendRpcRequest("/api/diag/cleanup", params, &responseData);

        if (status != VSOA_STATUS_SUCCESS) {
            ui->txt_status->append(QString("[%1] 设备 %2 清理远程文件失败: 状态码 %3").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")).arg(deviceIP).arg(status));
        } else {
            ui->txt_status->append(QString("[%1] 设备 %2 远程文件清理完成: %3").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")).arg(deviceIP).arg(fileName));
        }
    } else {
        ui->txt_status->append(QString("[%1] 连接设备 %2 失败").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")).arg(deviceIP));
        updateDeviceStatus(deviceIP, "离线");
    }
}

void DiagnosticClient::updateLocalLogTree()
{
    // 清空模型
    m_logTreeModel->clear();
    m_logTreeModel->setHorizontalHeaderLabels({"名称", "大小", "修改时间"});

    // 获取日志目录
    QDir logDir(QDir::currentPath() + "/Logs");
    if (!logDir.exists()) {
        logDir.mkpath(".");
    }

    // 遍历日志目录
    QDirIterator it(logDir, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString filePath = it.next();
        QFileInfo fileInfo(filePath);

        if (fileInfo.isFile() && fileInfo.suffix() == "diag") {
            // 添加文件到树模型
            QList<QStandardItem*> items;
            items.append(new QStandardItem(fileInfo.fileName()));
            items.append(new QStandardItem(QString::number(fileInfo.size() / 1024) + " KB"));
            items.append(new QStandardItem(fileInfo.lastModified().toString("yyyy-MM-dd HH:mm:ss")));
            m_logTreeModel->appendRow(items);
        }
    }
}

void DiagnosticClient::updateTasksTable()
{
    // 清空表格
    ui->table_tasks->setRowCount(0);

    // 添加任务到表格
    for (const TaskInfo& task : m_tasks) {
        int row = ui->table_tasks->rowCount();
        ui->table_tasks->insertRow(row);

        ui->table_tasks->setItem(row, 0, new QTableWidgetItem(task.deviceIP));
        ui->table_tasks->setItem(row, 1, new QTableWidgetItem(task.status));
        ui->table_tasks->setItem(row, 2, new QTableWidgetItem(QString::number(task.progress) + "%"));
        ui->table_tasks->setItem(row, 3, new QTableWidgetItem(task.fileName));
        ui->table_tasks->setItem(row, 4, new QTableWidgetItem(QString::number(task.fileSize / 1024) + " KB"));
    }
}

void DiagnosticClient::on_btn_add_device_clicked()
{
    // 添加设备
    bool ok;
    QString ip = QInputDialog::getText(this, "添加设备", "请输入设备 IP 地址:", QLineEdit::Normal, "192.168.1.100", &ok);
    if (ok && !ip.isEmpty()) {
        QString name = QInputDialog::getText(this, "添加设备", "请输入设备名称:", QLineEdit::Normal, "PLC Device", &ok);
        if (ok && !name.isEmpty()) {
            // 添加设备到列表
            DeviceInfo device;
            device.ip = ip;
            device.name = name;
            device.status = "离线";
            device.vsoaClient = new VsoaClient(this);
            m_devices.append(device);

            // 添加到UI
            QListWidgetItem* item = new QListWidgetItem(QString("%1 (%2) - %3").arg(device.name).arg(device.ip).arg(device.status));
            item->setData(Qt::UserRole, device.ip);
            ui->list_devices->addItem(item);

            // 保存设备列表
            saveDevices();

            ui->txt_status->append(QString("[%1] 添加设备: %2 (%3)").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")).arg(name).arg(ip));
        }
    }
}

void DiagnosticClient::on_btn_remove_device_clicked()
{
    // 移除设备
    QListWidgetItem* item = ui->list_devices->currentItem();
    if (item) {
        QString ip = item->data(Qt::UserRole).toString();
        QString name = item->text().split(" (").first();

        // 从列表中移除
        for (int i = 0; i < m_devices.size(); ++i) {
            if (m_devices[i].ip == ip) {
                // 清理VSOA客户端
                if (m_devices[i].vsoaClient) {
                    m_devices[i].vsoaClient->disconnectFromServer();
                    delete m_devices[i].vsoaClient;
                }
                m_devices.removeAt(i);
                break;
            }
        }

        // 从UI中移除
        delete item;

        // 保存设备列表
        saveDevices();

        ui->txt_status->append(QString("[%1] 移除设备: %2 (%3)").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")).arg(name).arg(ip));
    } else {
        QMessageBox::warning(this, "警告", "请选择要移除的设备");
    }
}

void DiagnosticClient::on_btn_refresh_status_clicked()
{
    // 刷新设备状态
    for (DeviceInfo& device : m_devices) {
        // 查找设备对应的VSOA客户端
        VsoaClient* client = device.vsoaClient;
        if (client) {
            // 连接到VSOA服务器
            if (client->connectToServer(device.ip, 8899)) {
                // 发送RPC请求
                QByteArray responseData;
                int status = client->sendRpcRequest("/api/diag/status", QJsonObject(), &responseData);

                if (status == VSOA_STATUS_SUCCESS) {
                    // 解析响应
                    QJsonDocument doc = QJsonDocument::fromJson(responseData);
                    QJsonObject obj = doc.object();

                    if (obj.contains("state")) {
                        QString state = obj["state"].toString();
                        updateDeviceStatus(device.ip, state);
                    }
                } else {
                    updateDeviceStatus(device.ip, "离线");
                }
            } else {
                updateDeviceStatus(device.ip, "离线");
            }
        }
    }

    ui->txt_status->append(QString("[%1] 刷新设备状态").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")));
}

void DiagnosticClient::on_btn_start_diagnostic_clicked()
{
    // 开始诊断
    QList<QListWidgetItem*> selectedItems = ui->list_devices->selectedItems();
    if (selectedItems.isEmpty()) {
        QMessageBox::warning(this, "警告", "请选择要诊断的设备");
        return;
    }

    // 对每个选中的设备开始诊断
    for (QListWidgetItem* item : selectedItems) {
        QString deviceIP = item->data(Qt::UserRole).toString();
        startDiagnosticForDevice(deviceIP);
    }
}

void DiagnosticClient::on_btn_open_log_clicked()
{
    // 打开日志文件
    QModelIndex index = ui->tree_logs->currentIndex();
    if (!index.isValid()) {
        QMessageBox::warning(this, "警告", "请选择要打开的日志文件");
        return;
    }

    QString fileName = index.sibling(index.row(), 0).data().toString();
    QString filePath = QDir::currentPath() + "/Logs/" + fileName;

    // 打开文件
    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        QString content = in.readAll();
        file.close();

        // 显示日志内容
        QMessageBox::information(this, "日志内容", content.left(10000) + (content.length() > 10000 ? "... (内容过长，已截断)" : ""));
    } else {
        QMessageBox::warning(this, "错误", "无法打开日志文件");
    }
}

void DiagnosticClient::on_btn_clean_logs_clicked()
{
    // 清理本地日志
    if (QMessageBox::question(this, "确认", "确定要清理所有本地日志文件吗？") == QMessageBox::Yes) {
        QDir logDir(QDir::currentPath() + "/Logs");
        if (logDir.exists()) {
            QStringList files = logDir.entryList(QStringList() << "*.diag", QDir::Files);
            for (const QString& file : files) {
                logDir.remove(file);
            }
        }

        // 更新日志树
        updateLocalLogTree();

        ui->txt_status->append(QString("[%1] 清理本地日志文件").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")));
    }
}

void DiagnosticClient::onRefreshStatusTimer()
{
    // 定时刷新设备状态
    on_btn_refresh_status_clicked();
}