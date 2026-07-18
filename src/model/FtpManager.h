#pragma once

#include <QString>
#include <QFile>
#include <QList>
#include <QDateTime>
#include <functional>
#include <curl/curl.h>

struct FtpFileInfo {
    QString remotePath;
    QString name;
    QDateTime lastModified;
    qint64 size = -1;
    bool isDirectory = false;
    QString permissions; // FTP LIST 权限字符串（首字符 d=目录, -=文件）
};

class FtpManager {
public:
    using ProgressCallback = std::function<void(qint64 bytesSent, qint64 total)>;

    explicit FtpManager(const QString& host = "", quint16 port = 21);
    ~FtpManager();

    void setCredentials(const QString& user, const QString& pass);
    void clearCredentials();
    void setHost(const QString& host, quint16 port);

    void uploadFiles(const QStringList& items, const QStringList& targets, const QString& remotePath,
        std::function<void(int)> onProgress = nullptr,
        std::function<void(bool, const QStringList&, const QStringList&)> onFinished = nullptr);

    void uploadFile(const QString& localPath, const QString& remotePath,
        const ProgressCallback& progress = {});
    void uploadFolder(const QString& localPath, const QString& remoteBasePath);

    void clearRemoteDirectory(const QString& remoteDir);
    bool deleteFtpFile(const QString& parentDir, const QString& filename);
    bool deleteFtpDirectory(const QString& parentDir, const QString& dirname);
    bool renameFtpFile(const QString& parentDir, const QString& oldName,
                       const QString& newName);
    QStringList listFtpDirectory(const QString& remoteDir);
    QList<FtpFileInfo> listFtpDirectoryDetailed(const QString& remoteDir);

    void downloadFile(const QString& remotePath, const QString& localPath,
        const ProgressCallback& progress = {});
    static size_t progressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);

private:
    QString buildRemoteFilePath(const QString& remoteDir, const QString& localFilePath);
    bool uploadToTarget(const QString& target, const QStringList& items, const QString& remotePath,
        const QString& user, const QString& pass);

private:
    QString m_host;
    quint16 m_port;
    QString m_user;
    QString m_pass;
};
