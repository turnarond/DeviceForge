#pragma once

#include <QWidget>
#include <QDateTime>
#include <QMap>
#include <QTextEdit>
#include <QStringList>
#include <QVector>
#include <QTreeWidgetItem>
#include "src/model/FtpManager.h"

namespace Ui {
    class TabLogQuery;
}

class DeployMaster;

class LogQueryTab : public QWidget
{
    Q_OBJECT

public:
    explicit LogQueryTab(DeployMaster* parentWindow, QWidget* parent = nullptr);
    ~LogQueryTab();

    void setMainWindow(DeployMaster* mainWindow);
    QStringList getTargetIPs();

public slots:
    void on_btn_queryRequested();
    void on_btn_downloadSelected();
    void on_btn_downloadAll();
    void onTreeItemDoubleClicked();
    void onLogQueryResultReady(const QString& ip, const QList<FtpFileInfo>& files);
    void appendLog(const QString& log);
    void updateDownloadProgress(const QString& ip, const QString& filename,
        qint64 bytesReceived, qint64 totalBytes);

private:
    Ui::TabLogQuery* ui;
    DeployMaster* m_mainWindow;
    QMap<QString, QList<FtpFileInfo>> m_logFilesCache;
    QTextEdit* txt_globalLog;

    void startDownloadTask(const QString& ip, const QString& remotePath,
        const QString& localPath, const QString& filename);
    void downloadALogForIP(const QString& ip, const QString& remoteFile, const QString& saveDir);

    QString logPath() const;
    QDateTime startTime() const;
    QDateTime endTime() const;
    QString formatFileSize(qint64 bytes);
    void appendLogResult(const QString& ip, const QList<FtpFileInfo>& files);
    void clearResults();
};