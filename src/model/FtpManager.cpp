#include "FtpManager.h"
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDateTime>
#include <QDebug>
#include <QUrl>
#include <QtConcurrent/QtConcurrent>

struct FtpContext {
    QFile* file = nullptr;
    qint64 total = 0;
    FtpManager::ProgressCallback progress;
};

static qint64 parseListSize(const QString& line) {
    QStringList parts = line.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (parts.size() >= 5) {
        bool ok = false;
        qint64 size = parts[4].toLongLong(&ok);
        return ok ? size : -1;
    }
    return -1;
}

// 检测 FTP LIST 输出是否目录项（权限首字符 'd'）
static bool parseListIsDir(const QString& line) {
    QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) return false;
    // Unix-style: "drwxr-xr-x ..." → 首字符 'd' 表示目录
    // Windows/IIS: 首字符可能是数字（日期），不是标准权限
    QChar first = trimmed[0];
    return first == QLatin1Char('d');
}

// 解析权限字符串（FTP LIST 第一列）
static QString parseListPermissions(const QString& line) {
    QStringList parts = line.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (parts.size() >= 1) {
        QString first = parts[0];
        // Unix-style: 10-character permission string
        if (first.size() >= 10 && (first[0] == 'd' || first[0] == '-'))
            return first;
    }
    return QString();
}

static QString parseListFilename(const QString& line) {
    QStringList parts = line.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (parts.size() >= 9) {
        return parts.mid(8).join(" ");
    }
    return QString();
}

static QDateTime parseListTime(const QString& line) {
    QRegularExpression re(R"(\S+\s+\d+\s+\S+\s+\S+\s+\d+\s+(\w{3})\s+(\d{1,2})\s+([\d:]+)\s+(.+))");
    QRegularExpressionMatch match = re.match(line.trimmed());
    if (!match.hasMatch()) return QDateTime();

    QString monthStr = match.captured(1);
    int day = match.captured(2).toInt();
    QString timeOrYear = match.captured(3);

    QStringList months = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    int month = months.indexOf(monthStr) + 1;
    if (month == 0) return QDateTime();

    QDate date;
    QTime time;

    if (timeOrYear.contains(':')) {
        time = QTime::fromString(timeOrYear, "hh:mm");
        int currentYear = QDate::currentDate().year();
        date = QDate(currentYear, month, day);
        if (date > QDate::currentDate()) {
            date = QDate(currentYear - 1, month, day);
        }
    } else {
        int year = timeOrYear.toInt();
        date = QDate(year, month, day);
        time = QTime(0, 0);
    }

    return QDateTime(date, time);
}

static size_t listWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    QByteArray* buffer = static_cast<QByteArray*>(userp);
    buffer->append(static_cast<char*>(contents), total);
    return total;
}

size_t readCallback(void* ptr, size_t size, size_t nmemb, void* userdata) {
    FtpContext* ctx = static_cast<FtpContext*>(userdata);
    size_t toRead = size * nmemb;
    qint64 read = ctx->file->read(static_cast<char*>(ptr), toRead);
    return static_cast<size_t>(read);
}

size_t writeCallbackString(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realSize = size * nmemb;
    QString* output = static_cast<QString*>(userp);
    output->append(QString::fromUtf8(static_cast<char*>(contents), static_cast<int>(realSize)));
    return realSize;
}

int progressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    Q_UNUSED(dltotal); Q_UNUSED(dlnow);
    FtpContext* ctx = static_cast<FtpContext*>(clientp);
    if (ctx->progress) {
        ctx->progress(ulnow, ultotal > 0 ? ultotal : ctx->total);
    }
    return 0;
}

size_t writeData(void* ptr, size_t size, size_t nmemb, QFile* stream) {
    stream->write(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

FtpManager::FtpManager(const QString& host, quint16 port) 
    : m_host(host), m_port(port) {}

FtpManager::~FtpManager() { clearCredentials(); }

void FtpManager::setCredentials(const QString& user, const QString& pass) {
    m_user = user;
    m_pass = pass;
}

void FtpManager::clearCredentials() {
    if (!m_pass.isEmpty()) { memset(m_pass.data(), 0, m_pass.size() * sizeof(QChar)); }
    if (!m_user.isEmpty()) { memset(m_user.data(), 0, m_user.size() * sizeof(QChar)); }
    m_pass.clear(); m_user.clear();
}

void FtpManager::setHost(const QString& host, quint16 port) {
    m_host = host;
    m_port = port;
}

void FtpManager::uploadFiles(const QStringList& items, const QStringList& targets, const QString& remotePath,
    std::function<void(int)> onProgress,
    std::function<void(bool, const QStringList&, const QStringList&)> onFinished) {

    // 复制成员变量到局部，避免通过 this 跨线程访问导致数据竞争
    QString user = m_user;
    QString pass = m_pass;

    QtConcurrent::run([=]() {
        QStringList successes, failures;
        int totalSteps = items.size() * targets.size();
        int currentStep = 0;

        // 临时设置凭据供 uploadToTarget 使用
        // 注意：此 lambda 运行在线程池，不能通过 this 访问成员
        for (const QString& target : targets) {
            bool deviceSuccess = uploadToTarget(target, items, remotePath, user, pass);

            for (int i = 0; i < items.size(); ++i) {
                ++currentStep;
                if (onProgress) {
                    onProgress((currentStep * 100) / totalSteps);
                }
            }

            if (deviceSuccess) {
                successes << target;
            } else {
                failures << target;
            }
        }

        if (onFinished) {
            onFinished(failures.isEmpty(), successes, failures);
        }
    });
}

bool FtpManager::uploadToTarget(const QString& target, const QStringList& items, const QString& remotePath,
    const QString& user, const QString& pass) {
    QString host = target.contains(':') ? target.split(':').first() : target;
    quint16 port = target.contains(':') ? target.split(':').last().toUInt() : 21;

    CURL* curl = curl_easy_init();
    if (!curl) {
        return false;
    }

    std::string userPass = (user + ":" + pass).toStdString();
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_USERPWD, userPass.c_str());
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "ftp,ftps");

    bool allSuccess = true;

    for (const QString& item : items) {
        QFileInfo fi(item);
        if (fi.isFile()) {
            try {
                QFile file(item);
                if (!file.open(QIODevice::ReadOnly)) {
                    allSuccess = false;
                    continue;
                }

                QString remoteFilePath = remotePath;
                if (!remoteFilePath.endsWith('/')) remoteFilePath += '/';
                remoteFilePath += fi.fileName();

                QUrl url;
                url.setScheme("ftp");
                url.setHost(host);
                url.setPort(port);
                url.setPath("/" + remoteFilePath);

                QString urlStr = url.toString(QUrl::FullyEncoded);

                FtpContext ctx;
                ctx.file = &file;
                ctx.total = file.size();

                curl_easy_setopt(curl, CURLOPT_URL, urlStr.toUtf8().constData());
                curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
                curl_easy_setopt(curl, CURLOPT_READFUNCTION, readCallback);
                curl_easy_setopt(curl, CURLOPT_READDATA, &ctx);
                curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(file.size()));
                curl_easy_setopt(curl, CURLOPT_FTP_CREATE_MISSING_DIRS, 1L);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);

                CURLcode res = curl_easy_perform(curl);
                file.close();

                if (res != CURLE_OK) {
                    allSuccess = false;
                }
            } catch (const std::exception& e) {
                allSuccess = false;
                qWarning() << "FtpManager::uploadToTarget file exception:" << e.what()
                           << "file:" << item << "target:" << target;
            } catch (...) {
                allSuccess = false;
                qWarning() << "FtpManager::uploadToTarget unknown exception, file:" << item << "target:" << target;
            }
        } else if (fi.isDir()) {
            QDirIterator it(item, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                QFileInfo subFi = it.fileInfo();
                if (subFi.isFile()) {
                    try {
                        QFile file(subFi.absoluteFilePath());
                        if (!file.open(QIODevice::ReadOnly)) {
                            allSuccess = false;
                            continue;
                        }

                        QString relPath = QDir(item).relativeFilePath(subFi.absoluteFilePath());
                        QString remoteFilePath = remotePath;
                        if (!remoteFilePath.endsWith('/')) remoteFilePath += '/';
                        remoteFilePath += QDir(item).dirName() + "/" + relPath;

                        QUrl url;
                        url.setScheme("ftp");
                        url.setHost(host);
                        url.setPort(port);
                        url.setPath("/" + remoteFilePath);

                        QString urlStr = url.toString(QUrl::FullyEncoded);

                        FtpContext ctx;
                        ctx.file = &file;
                        ctx.total = file.size();

                        curl_easy_setopt(curl, CURLOPT_URL, urlStr.toUtf8().constData());
                        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
                        curl_easy_setopt(curl, CURLOPT_READFUNCTION, readCallback);
                        curl_easy_setopt(curl, CURLOPT_READDATA, &ctx);
                        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(file.size()));
                        curl_easy_setopt(curl, CURLOPT_FTP_CREATE_MISSING_DIRS, 1L);
                        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);

                        CURLcode res = curl_easy_perform(curl);
                        file.close();

                        if (res != CURLE_OK) {
                            allSuccess = false;
                        }
                    } catch (const std::exception& e) {
                        allSuccess = false;
                        qWarning() << "FtpManager::uploadToTarget dir file exception:" << e.what()
                                   << "file:" << subFi.absoluteFilePath() << "target:" << target;
                    } catch (...) {
                        allSuccess = false;
                        qWarning() << "FtpManager::uploadToTarget unknown dir exception, file:"
                                   << subFi.absoluteFilePath() << "target:" << target;
                    }
                }
            }
        }
    }

    curl_easy_cleanup(curl);
    return allSuccess;
}

void FtpManager::uploadFile(const QString& localPath, const QString& remoteDirectory,
    const ProgressCallback& progress) {
    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("无法打开本地文件: " + localPath.toStdString());
    }

    QString remoteFilePath = buildRemoteFilePath(remoteDirectory, localPath);

    QUrl url;
    url.setScheme("ftp");
    url.setHost(m_host);
    url.setPort(m_port);
    url.setPath("/" + remoteFilePath);

    QString urlStr = url.toString(QUrl::FullyEncoded);
    QByteArray urlBytes = urlStr.toUtf8();

    FtpContext ctx;
    ctx.file = &file;
    ctx.total = file.size();
    ctx.progress = progress;

    CURL* curl = curl_easy_init();
    if (!curl) {
        file.close();
        throw std::runtime_error("curl_easy_init() 失败");
    }

    curl_easy_setopt(curl, CURLOPT_URL, urlBytes.constData());
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, readCallback);
    curl_easy_setopt(curl, CURLOPT_READDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(file.size()));
    curl_easy_setopt(curl, CURLOPT_FTP_CREATE_MISSING_DIRS, 1L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_USERPWD, (m_user + ":" + m_pass).toStdString().c_str());
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "ftp,ftps");

    CURLcode res = curl_easy_perform(curl);
    file.close();
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::string errorMsg = "libcurl 上传失败 (";
        errorMsg += std::to_string(res);
        errorMsg += "): ";
        errorMsg += curl_easy_strerror(res);
        throw std::runtime_error(errorMsg);
    }
}

void FtpManager::uploadFolder(const QString& localPath, const QString& remoteBasePath) {
    QDir localDir(localPath);
    if (!localDir.exists()) {
        throw std::runtime_error("本地文件夹不存在: " + localPath.toStdString());
    }

    QDirIterator it(localPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QFileInfo fi = it.fileInfo();
        if (fi.isFile()) {
            QString filePath = fi.absolutePath();
            QString relPath = localDir.relativeFilePath(filePath);
            QString remoteFile = remoteBasePath + "/" + localDir.dirName() + "/" + relPath;
            uploadFile(fi.absoluteFilePath(), remoteFile);
        }
    }
}

void FtpManager::clearRemoteDirectory(const QString& remoteDir) {
    QString cleanDir = remoteDir;
    if (cleanDir.startsWith('/')) cleanDir = cleanDir.mid(1);
    if (cleanDir.endsWith('/')) cleanDir.chop(1);
    if (cleanDir.isEmpty()) {
        throw std::runtime_error("不能清空根目录 '/'");
    }

    // 列出目录内容并逐一删除
    QStringList entries = listFtpDirectory(cleanDir);
    for (const QString& entry : entries) {
        // 跳过 . 和 ..
        if (entry == "." || entry == "..") continue;

        // 尝试作为文件删除，失败则尝试作为目录删除
        if (!deleteFtpFile(cleanDir, entry)) {
            deleteFtpDirectory(cleanDir, entry);
        }
    }
}

bool FtpManager::deleteFtpFile(const QString& parentDir, const QString& filename) {
    // 构建完整的 FTP URL
    QString cleanDir = parentDir;
    if (!cleanDir.startsWith('/')) cleanDir.prepend('/');
    if (!cleanDir.endsWith('/')) cleanDir += '/';
    QString fullPath = cleanDir + filename;
    
    QUrl url;
    url.setScheme("ftp");
    url.setHost(m_host);
    url.setPort(m_port);
    url.setPath(fullPath);
    QString urlStr = url.toString(QUrl::FullyEncoded);
    QByteArray urlBytes = urlStr.toUtf8();

    CURL* curl = curl_easy_init();
    if (!curl) {
        return false;
    }

    // 设置删除文件选项
    curl_easy_setopt(curl, CURLOPT_URL, urlBytes.constData());
    curl_easy_setopt(curl, CURLOPT_USERPWD, (m_user + ":" + m_pass).toStdString().c_str());
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "ftp,ftps");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    // 构建删除命令
    QString deleteCmd = QString("DELE %1").arg(fullPath);
    struct curl_slist* commands = nullptr;
    commands = curl_slist_append(commands, deleteCmd.toUtf8().constData());
    curl_easy_setopt(curl, CURLOPT_QUOTE, commands);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(commands);
    curl_easy_cleanup(curl);

    return res == CURLE_OK;
}

bool FtpManager::deleteFtpDirectory(const QString& parentDir, const QString& dirname) {
    // 构建完整的 FTP URL
    QString cleanDir = parentDir;
    if (!cleanDir.startsWith('/')) cleanDir.prepend('/');
    if (!cleanDir.endsWith('/')) cleanDir += '/';
    QString fullPath = cleanDir + dirname;
    
    QUrl url;
    url.setScheme("ftp");
    url.setHost(m_host);
    url.setPort(m_port);
    url.setPath(fullPath);
    QString urlStr = url.toString(QUrl::FullyEncoded);
    QByteArray urlBytes = urlStr.toUtf8();

    CURL* curl = curl_easy_init();
    if (!curl) {
        return false;
    }

    // 设置删除目录选项
    curl_easy_setopt(curl, CURLOPT_URL, urlBytes.constData());
    curl_easy_setopt(curl, CURLOPT_USERPWD, (m_user + ":" + m_pass).toStdString().c_str());
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "ftp,ftps");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    // 构建删除命令
    QString deleteCmd = QString("RMD %1").arg(fullPath);
    struct curl_slist* commands = nullptr;
    commands = curl_slist_append(commands, deleteCmd.toUtf8().constData());
    curl_easy_setopt(curl, CURLOPT_QUOTE, commands);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(commands);
    curl_easy_cleanup(curl);

    return res == CURLE_OK;
}

QStringList FtpManager::listFtpDirectory(const QString& remoteDir) {
    QList<FtpFileInfo> detailed = listFtpDirectoryDetailed(remoteDir);
    QStringList names;
    for (const auto& info : detailed) {
        names << info.name;
    }
    return names;
}

QList<FtpFileInfo> FtpManager::listFtpDirectoryDetailed(const QString& remoteDir) {
    // 构建完整的 FTP URL
    QString cleanDir = remoteDir;
    if (!cleanDir.startsWith('/')) cleanDir.prepend('/');
    
    QUrl url;
    url.setScheme("ftp");
    url.setHost(m_host);
    url.setPort(m_port);
    url.setPath(cleanDir);
    QString urlStr = url.toString(QUrl::FullyEncoded);
    QByteArray urlBytes = urlStr.toUtf8();

    // 用于接收目录列表数据的缓冲区
    QByteArray buffer;

    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("curl_easy_init() 失败");
    }

    // 设置 FTP LIST 选项
    curl_easy_setopt(curl, CURLOPT_URL, urlBytes.constData());
    curl_easy_setopt(curl, CURLOPT_USERPWD, (m_user + ":" + m_pass).toStdString().c_str());
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "ftp,ftps");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, listWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_DIRLISTONLY, 0L);  // 获取详细列表
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::string errorMsg = "libcurl 列目录失败 (";
        errorMsg += std::to_string(res);
        errorMsg += "): ";
        errorMsg += curl_easy_strerror(res);
        throw std::runtime_error(errorMsg);
    }

    // 解析目录列表
    QList<FtpFileInfo> result;
    QString listData = QString::fromUtf8(buffer);
    QStringList lines = listData.split('\n', Qt::SkipEmptyParts);

    for (const QString& line : lines) {
        FtpFileInfo info;
        info.remotePath = cleanDir;
        info.name = parseListFilename(line);
        info.size = parseListSize(line);
        info.lastModified = parseListTime(line);
        info.isDirectory = parseListIsDir(line);
        info.permissions = parseListPermissions(line);

        if (!info.name.isEmpty()) {
            result.append(info);
        }
    }

    return result;
}

void FtpManager::downloadFile(const QString& remotePath, const QString& localPath,
    const ProgressCallback& progress) {
    
    // 构建完整的 FTP URL
    QUrl url;
    url.setScheme("ftp");
    url.setHost(m_host);
    url.setPort(m_port);
    url.setPath("/" + remotePath);
    QString urlStr = url.toString(QUrl::FullyEncoded);
    QByteArray urlBytes = urlStr.toUtf8();

    // 打开本地文件用于写入
    QFile file(localPath);
    if (!file.open(QIODevice::WriteOnly)) {
        throw std::runtime_error("无法打开本地文件: " + localPath.toStdString());
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        file.close();
        throw std::runtime_error("curl_easy_init() 失败");
    }

    // 设置下载选项
    curl_easy_setopt(curl, CURLOPT_URL, urlBytes.constData());
    curl_easy_setopt(curl, CURLOPT_USERPWD, (m_user + ":" + m_pass).toStdString().c_str());
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "ftp,ftps");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);

    // 设置进度回调
    if (progress) {
        // 创建回调包装器
        struct ProgressWrapper {
            ProgressCallback callback;
        };
        ProgressWrapper* wrapper = new ProgressWrapper{progress};
        
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, [](void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) -> int {
            auto* w = static_cast<ProgressWrapper*>(clientp);
            if (w->callback && dltotal > 0) {
                w->callback(static_cast<qint64>(dlnow), static_cast<qint64>(dltotal));
            }
            return 0;
        });
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, wrapper);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        
        // 执行下载
        CURLcode res = curl_easy_perform(curl);
        
        delete wrapper;
        file.close();
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            std::string errorMsg = "libcurl 下载失败 (";
            errorMsg += std::to_string(res);
            errorMsg += "): ";
            errorMsg += curl_easy_strerror(res);
            throw std::runtime_error(errorMsg);
        }
    } else {
        // 无进度回调
        CURLcode res = curl_easy_perform(curl);
        file.close();
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            std::string errorMsg = "libcurl 下载失败 (";
            errorMsg += std::to_string(res);
            errorMsg += "): ";
            errorMsg += curl_easy_strerror(res);
            throw std::runtime_error(errorMsg);
        }
    }
}

size_t FtpManager::progressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    auto callback = static_cast<ProgressCallback*>(clientp);
    if (*callback && dltotal > 0) {
        (*callback)(static_cast<qint64>(dlnow), static_cast<qint64>(dltotal));
    }
    return 0;
}

QString FtpManager::buildRemoteFilePath(const QString& remoteDir, const QString& localFilePath) {
    QFileInfo fi(localFilePath);
    QString fileName = fi.fileName();

    QString cleanDir = remoteDir;
    if (cleanDir.startsWith('/')) cleanDir = cleanDir.mid(1);
    if (cleanDir.endsWith('/')) cleanDir.chop(1);

    return cleanDir.isEmpty() ? fileName : cleanDir + "/" + fileName;
}
