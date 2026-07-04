// LogQueryTab.cpp
#include "LogQueryTab.h"
#include "ui_tab_logquery.h"
#include "DeployMaster.h" // 访问 deviceBusWidget()->allDevices()

#include <QTreeWidgetItem>
#include <QDateTimeEdit>
#include <QtConcurrent/QtConcurrent>
#include <QMessageBox>
#include <QFileDialog>

LogQueryTab::LogQueryTab(DeployMaster* parentWindow, QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::TabLogQuery)
    , m_mainWindow(parentWindow)
{
    ui->setupUi(this);
    txt_globalLog = m_mainWindow->getGlobalLogItem();

    // 连接按钮信号
    connect(ui->btn_queryLogs, &QPushButton::clicked, this, &LogQueryTab::on_btn_queryRequested);
    connect(ui->btn_downloadSelected, &QPushButton::clicked, this, &LogQueryTab::on_btn_downloadSelected);
    connect(ui->btn_downloadAll, &QPushButton::clicked, this, &LogQueryTab::on_btn_downloadAll);

    // 双击日志项预览
    connect(ui->tree_logResults, &QTreeWidget::itemDoubleClicked,
        this, &LogQueryTab::onTreeItemDoubleClicked);

    // 设置默认时间范围（可选）
    ui->dt_end->setDateTime(QDateTime::currentDateTime());
    ui->dt_start->setDateTime(QDateTime::currentDateTime().addDays(-7));
}

LogQueryTab::~LogQueryTab()
{
    delete ui;
}

// 辅助函数：格式化文件大小
QString LogQueryTab::formatFileSize(qint64 bytes)
{
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

QString LogQueryTab::logPath() const
{
    return ui->txt_logPath->text().trimmed();
}


QDateTime LogQueryTab::startTime() const
{
    return ui->dt_start->dateTime();
}

QDateTime LogQueryTab::endTime() const
{
    return ui->dt_end->dateTime();
}

void LogQueryTab::appendLogResult(const QString& ip, const QList<FtpFileInfo>& files)
{
    for (const auto& file : files) {
        auto item = new QTreeWidgetItem(ui->tree_logResults);
        item->setText(0, ip);
        item->setText(1, file.name);

        // 格式化文件大小
        QString sizeStr = "未知";
        if (file.size >= 0) {
            if (file.size < 1024) {
                sizeStr = QString("%1 B").arg(file.size);
            }
            else if (file.size < 1024 * 1024) {
                sizeStr = QString("%1 KB").arg(file.size / 1024.0, 0, 'f', 1);
            }
            else {
                sizeStr = QString("%1 MB").arg(file.size / (1024.0 * 1024.0), 0, 'f', 1);
            }
        }
        item->setText(2, sizeStr);

        item->setText(3, file.lastModified.isValid() ?
            file.lastModified.toString("yyyy-MM-dd HH:mm:ss") : "未知");

        // 初始进度
        item->setText(4, "未下载");

        // 存储完整信息（用于下载时取 size 和路径）
        item->setData(1, Qt::UserRole, QVariant::fromValue(file));
    }
}

void LogQueryTab::clearResults()
{
    ui->tree_logResults->clear();
    m_logFilesCache.clear();
}

void LogQueryTab::onTreeItemDoubleClicked()
{
    QTreeWidgetItem* item = ui->tree_logResults->currentItem();
    if (!item) return;

    QString ip = item->text(0);
    on_btn_downloadSelected();
}

void LogQueryTab::on_btn_queryRequested()
{
    auto ips = getTargetIPs();
    if (ips.isEmpty()) {
        QMessageBox::warning(this, "警告", "请先在\"批量部署\"页填写目标 IP 列表。");
        return;
    }

    QString ftpUser = m_mainWindow->getFtpUser();
    QString ftpPass = m_mainWindow->getFtpPass();

    QString logDir = logPath();
    QDateTime start = startTime();
    QDateTime end = endTime();

    if (logDir.isEmpty()) {
        QMessageBox::information(this, "错误", "日志路径为空");
    }
    // 自动确保是目录路径
    if (!logDir.startsWith('/')) logDir.prepend('/');
    if (!logDir.endsWith('/')) logDir += '/';
    
    // 先清空现有结果
    ui->tree_logResults->clear();

    QtConcurrent::run([=]() {
        for (const QString& ip : ips) {
            QString targetIp = ip.trimmed();
            QMetaObject::invokeMethod(this, "appendLog", Qt::QueuedConnection,
                Q_ARG(QString, QString("🔍 查询 %1 的日志目录: %2").arg(targetIp).arg(logDir)));

            try {
                FtpManager ftp(targetIp, 21);
                ftp.setCredentials(ftpUser, ftpPass);
                auto files = ftp.listFtpDirectoryDetailed(logDir);

                // 过滤：只保留 .log 文件（可选）
                QList<FtpFileInfo> filtered;
                for (const auto& f : files) {
                    //if (f.name.endsWith(".log", Qt::CaseInsensitive) || f.name.contains("log", Qt::CaseInsensitive)) {
                    //    filtered << f;
                    //}
                    if (f.name.endsWith(".log", Qt::CaseInsensitive) || f.name.contains("log", Qt::CaseInsensitive)) {
                        filtered << f;
                    }
                }

                QMetaObject::invokeMethod(this, "onLogQueryResultReady", Qt::QueuedConnection,
                    Q_ARG(QString, targetIp),
                    Q_ARG(QList<FtpFileInfo>, filtered));
            }
            catch (const std::exception& e) {
                QMetaObject::invokeMethod(this, "appendLog", Qt::QueuedConnection,
                    Q_ARG(QString, QString("❌ %1: %2").arg(targetIp).arg(e.what())));
            }
        }
        });
}

void LogQueryTab::startDownloadTask(const QString& ip, const QString& remotePath,
    const QString& localPath, const QString& filename)
{
    QString ftpUser = m_mainWindow->getFtpUser();
    QString ftpPass = m_mainWindow->getFtpPass();
    QtConcurrent::run([=]() {
        try {
            FtpManager ftp(ip, 21);
            ftp.setCredentials(ftpUser, ftpPass);

            // 进度回调：通过信号更新 UI
            FtpManager::ProgressCallback progress = [=](qint64 bytes, qint64 total) {
                QMetaObject::invokeMethod(this, "updateDownloadProgress", Qt::QueuedConnection,
                    Q_ARG(QString, ip),
                    Q_ARG(QString, filename),
                    Q_ARG(qint64, bytes),
                    Q_ARG(qint64, total));
                };

            ftp.downloadFile(remotePath, localPath, progress);

            // 下载完成
            QMetaObject::invokeMethod(this, "appendLog", Qt::QueuedConnection,
                Q_ARG(QString, QString("📥 %1 -> %2 下载成功").arg(filename).arg(localPath)));
            // 可选：标记为"完成"
            QMetaObject::invokeMethod(this, "updateDownloadProgress", Qt::QueuedConnection,
                Q_ARG(QString, ip),
                Q_ARG(QString, filename),
                Q_ARG(qint64, 0),
                Q_ARG(qint64, -1)); // 特殊值表示完成

        }
        catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, "appendLog", Qt::QueuedConnection,
                Q_ARG(QString, QString("❌ %1 下载失败: %2").arg(filename).arg(e.what())));
            QMetaObject::invokeMethod(this, "updateDownloadProgress", Qt::QueuedConnection,
                Q_ARG(QString, ip),
                Q_ARG(QString, filename),
                Q_ARG(qint64, 0),
                Q_ARG(qint64, -2)); // 错误
        }
        });
}

// LogQueryTab::updateDownloadProgress
void LogQueryTab::updateDownloadProgress(const QString& ip, const QString& filename,
    qint64 bytesReceived, qint64 totalBytes)
{
    // 遍历 TreeWidget 找匹配项
    QTreeWidget* tree = ui->tree_logResults;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = tree->topLevelItem(i);
        if (item->text(0) == ip && item->text(1) == filename) {
            QString progressText;
            if (totalBytes == -1) {
                progressText = QString("%1 (%2)")
                    .arg(formatFileSize(bytesReceived))
                    .arg("已完成");
            }
            else if (totalBytes == -2) {
                progressText = QString("%1 (%2)")
                    .arg(formatFileSize(bytesReceived))
                    .arg("下载出错");
            }
            else {
                double percent = (totalBytes > 0) ? (bytesReceived * 100.0 / totalBytes) : 0;
                progressText = QString("%1 / %2 (%3%)")
                    .arg(formatFileSize(bytesReceived))
                    .arg(formatFileSize(totalBytes))
                    .arg(static_cast<int>(percent));
            }
            item->setText(4, progressText);
            break;
        }
    }
}


void LogQueryTab::on_btn_downloadSelected()
{
    QList<QTreeWidgetItem*> selected = ui->tree_logResults->selectedItems();
    QString saveDir = QFileDialog::getExistingDirectory(
        this, "选择保存目录", QDir::homePath()
    );
    if (saveDir.isEmpty()) {
        appendLog("❌ 未选择下载路径");
    }
    for (QTreeWidgetItem* item : selected) {
        FtpFileInfo fileInfo = item->data(1, Qt::UserRole).value<FtpFileInfo>();
        QString ip = item->text(0);
        QString localPath = QString("%1/%2_%3").arg(saveDir).arg(ip).arg(fileInfo.name);
        startDownloadTask(ip, fileInfo.remotePath + fileInfo.name, localPath, fileInfo.name);
    }
}

void LogQueryTab::on_btn_downloadAll()
{
    QString saveDir = QFileDialog::getExistingDirectory(
        this, "选择保存目录", QDir::homePath()
    );
    if (saveDir.isEmpty()) return;

    // 遍历 TreeWidget 收集所有 (ip, remoteFile, time)
    struct DownloadTask {
        QString ip;
        QString remotePath;
        QString localPath;
        QString basename;
    };
    QVector<DownloadTask> tasks;

    QString logDir = logPath().trimmed();
    if (!logDir.endsWith('/')) logDir += '/';

    for (int i = 0; i < ui->tree_logResults->topLevelItemCount(); ++i) {
        auto item = ui->tree_logResults->topLevelItem(i);
        QString ip = item->text(0);
        QString remoteFile = item->text(1);
        QString timeStr = item->text(2);

        QString baseName = QFileInfo(remoteFile).fileName();
        QString timestamp = (timeStr != "未知") ?
            QDateTime::fromString(timeStr, "yyyy-MM-dd HH:mm:ss").toString("yyyyMMdd_HHmmss")
            : "unknown";
        QString localName = QString("%1_%2_%3").arg(ip).arg(baseName).arg(timestamp);
        // 避免重名
        QString localPath = saveDir + "/" + localName;
        int counter = 1;
        while (QFile::exists(localPath)) {
            localPath = saveDir + "/" + localName + QString("_%1").arg(counter++);
        }

        tasks.append({ ip, logDir + remoteFile, localPath, baseName });
    }

    if (tasks.isEmpty()) {
        appendLog("❌ 无可下载的日志文件");
        return;
    }

    // 逐个启动下载（避免并发 FTP 连接过多）
    for (const auto& task : tasks) {
        startDownloadTask(task.ip, task.remotePath, task.localPath, task.basename);
        QThread::msleep(100); // 轻微间隔
    }
}

void LogQueryTab::downloadALogForIP(const QString& ip, const QString& remoteFile, const QString& saveDir)
{
    QString logDir = logPath().trimmed();
    if (!logDir.endsWith('/')) logDir += '/';
    QString remotePath = logDir + remoteFile;

    // 获取文件信息（用于时间命名）
    QDateTime modTime;
    // 从 TreeWidget 中取缓存的时间（如果之前查过）
    QTreeWidgetItem* item = nullptr;
    for (int i = 0; i < ui->tree_logResults->topLevelItemCount(); ++i) {
        auto it = ui->tree_logResults->topLevelItem(i);
        if (it->text(0) == ip && it->text(1) == remoteFile) {
            QString timeStr = it->text(2);
            if (timeStr != "未知") {
                modTime = QDateTime::fromString(timeStr, "yyyy-MM-dd HH:mm:ss");
            }
            break;
        }
    }

    // 构造默认文件名：IP_文件名_时间.log
    QString baseName = QFileInfo(remoteFile).fileName();
    QString timestamp = modTime.isValid() ? modTime.toString("yyyyMMdd_HHmmss") : "unknown";
    QString defaultName = QString("%1_%2_%3").arg(ip).arg(baseName).arg(timestamp);


    startDownloadTask(ip, remotePath, saveDir, baseName);
}


QStringList LogQueryTab::getTargetIPs()
{
    return m_mainWindow->getTargetIPList();
}

void LogQueryTab::onLogQueryResultReady(const QString& ip, const QList<FtpFileInfo>& files)
{
    if (!files.isEmpty()) {
        appendLogResult(ip, files);
        //appendLog(QString("✅ %1: 找到 %2 个日志文件").arg(ip).arg(files.size()));
    }
    else {
        //appendLog(QString("⚠️ %1: 未找到日志文件").arg(ip));
    }
}

void LogQueryTab::appendLog(const QString& log)
{
    txt_globalLog->append(log);
}