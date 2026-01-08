#include "DeployMaster.h"
#include "TelnetClient.h"

#include <QFileInfo>
#include <QFileDialog>
#include <QString>
#include <QtConcurrent/QtConcurrent>
#include <QStyleFactory>

DeployMaster::DeployMaster(QWidget* parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    QApplication::setStyle(QStyleFactory::create("Fusion"));

    connect(ui.btn_addFiles, &QPushButton::clicked, this, &DeployMaster::onAddFilesClicked);
    connect(ui.btn_addFolders, &QPushButton::clicked, this, &DeployMaster::onAddFolderClicked);
    connect(ui.btn_clearUploadList, &QPushButton::clicked, this, &DeployMaster::onFileItemCleanClicked);
    connect(ui.list_uploadedItems, &DropListWidget::filesDropped, this, &DeployMaster::onFilesDropped);

    connect(ui.btn_deploy, &QPushButton::clicked, this, &DeployMaster::onDeployClicked);
    connect(ui.btn_clearLog, &QPushButton::clicked, this, &DeployMaster::onClearLogClicked);
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
    QString user = ui.txt_user->text().trimmed();
    QString pass = ui.txt_pass->text().trimmed(); // 注意：Qt 无 PasswordBox，需用 QLineEdit + setEchoMode
    bool shouldReboot = ui.chk_rebootAfterDeploy->isChecked();
    bool shouldClearRemote = ui.chk_clearRemoteBeforeUpload->isChecked();
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

    ui.btn_deploy->setEnabled(false);
    appendFtpLog(QString("🚀 开始部署 %1 个内容到 %2 台设备...")
        .arg(uploadItems.size()).arg(ips.size()));

    // 启动异步 FTP 上传（仅上传，不处理 Telnet）
    QtConcurrent::run([=]() {
        QStringList deploySuccesses, deployFailures;

        for (const QString& ip : ips) {
            QString targetIp = ip.trimmed();
            QString port = targetIp.contains(':') ? "" : "21";

            FtpManager ftpm(targetIp, port.toUShort(), user, pass);
            // 注意：appendFtpLog 不是线程安全的！需用 invokeMethod
            QMetaObject::invokeMethod(this, "appendFtpLog", Qt::QueuedConnection,
                Q_ARG(QString, QString("➡️ 正在部署到设备：%1").arg(targetIp)));

            // 🔥 新增：清空远程路径（如果勾选）
            if (shouldClearRemote) {
                try {
                    QMetaObject::invokeMethod(this, "appendFtpLog", Qt::QueuedConnection,
                        Q_ARG(QString, QString("🧹 正在清空远程路径: %1").arg(remotePath)));
                    ftpm.clearRemoteDirectory(remotePath);
                    QMetaObject::invokeMethod(this, "appendFtpLog", Qt::QueuedConnection,
                        Q_ARG(QString, "✅ 远程路径已清空"));
                }
                catch (const std::exception& ex) {
                    QMetaObject::invokeMethod(this, "appendFtpLog", Qt::QueuedConnection,
                        Q_ARG(QString, QString("❌ 清空失败: %1").arg(QString::fromStdString(ex.what()).left(100))));
                    // 可选择：跳过该设备，或终止部署？
                    // 这里建议跳过，记录失败
                    deployFailures << targetIp;
                    continue; // 跳过上传
                }
            }

            bool allSuccess = true;
            for (const UploadItem& item : uploadItems) {
                try {
                    if (item.isFolder) {
                        ftpm.uploadFolder(item.fullPath, remotePath);
                    }
                    else {
                        ftpm.uploadFile(item.fullPath, remotePath);
                    }
                    QMetaObject::invokeMethod(this, "appendFtpLog", Qt::QueuedConnection,
                        Q_ARG(QString, QString("✅ 上传成功: %1").arg(item.displayName())));
                }
                catch (const std::exception& ex) {
                    QMetaObject::invokeMethod(this, "appendFtpLog", Qt::QueuedConnection,
                        Q_ARG(QString, QString("❌ 上传失败: %1 -> %2").arg(item.displayName(), QString::fromStdString(ex.what()).left(100))));
                    allSuccess = false;
                }
                QThread::msleep(50);
            }

            if (allSuccess) {
                deploySuccesses << targetIp;
            }
            else {
                deployFailures << targetIp;
            }
            QThread::msleep(100);
        }
        QMetaObject::invokeMethod(this, "appendFtpLog", Qt::QueuedConnection,
            Q_ARG(QString, QString("🎉 批量部署完成！")));
        QMetaObject::invokeMethod(this, "appendFtpLog", Qt::QueuedConnection,
            Q_ARG(QString, QString("📊 部署成功: %1 | 部署失败: %2").arg(deploySuccesses.size()).arg(deployFailures.size())));
        if (deployFailures.size() > 0) {
            QMetaObject::invokeMethod(this, "appendFtpLog", Qt::QueuedConnection,
                Q_ARG(QString, QString("❌ 失败列表:").arg(deployFailures.join(", "))));
        }
        // 上传完成后，回到主线程：先汇报结果，再决定是否重启
        QMetaObject::invokeMethod(this, "onFtpUploadFinished", Qt::QueuedConnection,
            Q_ARG(QStringList, deploySuccesses),
            Q_ARG(QStringList, deployFailures),
            Q_ARG(bool, shouldReboot),
            Q_ARG(QString, user),
            Q_ARG(QString, pass)
        );
        });
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
    QString text = ui.txt_ipList->toPlainText(); // 适用于 QTextEdit 和 QPlainTextEdit

    // 按行分割
    QStringList lines = text.split('\n', Qt::SkipEmptyParts);

    for (QString line : lines) {
        line = line.trimmed(); // 去除首尾空格
        if (!line.isEmpty()) {
            // 可选：简单验证是否像 IP（增强健壮性）
            // 例如：跳过明显无效的行（如包含字母、多个冒号等）
            // 这里先只做基本过滤
            ips << line;
        }
    }

    return ips;
}

DeployMaster::~DeployMaster()
{
}