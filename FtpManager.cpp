// FtpManager.cpp
#include "FtpManager.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDateTime>
#include <QDebug>
#include <QUrl>

struct FtpContext {
    QFile* file = nullptr;
    qint64 total = 0;
    FtpManager::ProgressCallback progress;
};

static qint64 parseListSize(const QString& line)
{
    QStringList parts = line.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (parts.size() >= 5) {
        bool ok = false;
        qint64 size = parts[4].toLongLong(&ok); // Unix LIST: ... size ...
        return ok ? size : -1;
    }
    return -1;
}

static QString parseListFilename(const QString& line)
{
    QStringList parts = line.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (parts.size() >= 9) {
        return parts.mid(8).join(" ");
    }
    return QString();
}

// 辅助：从 LIST 行解析时间和文件名（Unix 风格）
static QDateTime parseListTime(const QString& line)
{
    // Unix LIST 示例: -rw-r--r-- 1 user group 1234 Jan 10 12:34 app.log
    // 注意：年份可能省略，需智能判断
    QRegularExpression re(R"(\S+\s+\d+\s+\S+\s+\S+\s+\d+\s+(\w{3})\s+(\d{1,2})\s+([\d:]+)\s+(.+))");
    QRegularExpressionMatch match = re.match(line.trimmed());
    if (!match.hasMatch()) return QDateTime();

    QString monthStr = match.captured(1);
    int day = match.captured(2).toInt();
    QString timeOrYear = match.captured(3); // 可能是 "12:34" 或 "2025"

    // 月份映射
    QStringList months = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    int month = months.indexOf(monthStr) + 1;
    if (month == 0) return QDateTime();

    QDate date;
    QTime time;

    if (timeOrYear.contains(':')) {
        // 格式: HH:mm → 属于今年或去年
        time = QTime::fromString(timeOrYear, "hh:mm");
        int currentYear = QDate::currentDate().year();
        date = QDate(currentYear, month, day);
        // 如果日期在未来（如 12月查1月），说明是去年
        if (date > QDate::currentDate()) {
            date = QDate(currentYear - 1, month, day);
        }
    }
    else {
        // 格式: YYYY
        int year = timeOrYear.toInt();
        date = QDate(year, month, day);
        time = QTime(0, 0);
    }

    return QDateTime(date, time);
}

// 回调：收集 LIST 输出
static size_t listWriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t total = size * nmemb;
    QByteArray* buffer = static_cast<QByteArray*>(userp);
    buffer->append(static_cast<char*>(contents), total);
    return total;
}

QList<FtpFileInfo> FtpManager::listFtpDirectoryDetailed(const QString& remoteDir)
{
    QList<FtpFileInfo> result;
    QByteArray output;

    // 确保路径以 '/' 结尾（表示目录）
    QString dirPath = remoteDir;
    if (!dirPath.endsWith('/')) {
        dirPath += '/';
    }

    curl_easy_setopt(curl_, CURLOPT_URL,
        QString("ftp://%1:%2%3").arg(host_).arg(port_).arg(dirPath).toUtf8().constData());
    curl_easy_setopt(curl_, CURLOPT_USERNAME, user_.toUtf8().constData());
    curl_easy_setopt(curl_, CURLOPT_PASSWORD, pass_.toUtf8().constData());
    curl_easy_setopt(curl_, CURLOPT_DIRLISTONLY, 0L); // 关键：获取完整 LIST
    curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "LIST"); // 显式使用 LIST
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, listWriteCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &output);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 30L); // 添加超时设置，防止无限等待

    CURLcode res = curl_easy_perform(curl_);
    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("LIST failed: ") + curl_easy_strerror(res));
    }

    // 解析 LIST 输出
    QStringList lines = QString::fromUtf8(output).split('\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        if (line.trimmed().isEmpty()) continue;

        QString filename = parseListFilename(line);
        if (filename.isEmpty() || filename == "." || filename == "..") continue;

        qint64 size = parseListSize(line);
        QDateTime modTime = parseListTime(line);

        FtpFileInfo info;
        info.remotePath = dirPath;
        info.name = filename;
        info.size = size;
        info.lastModified = modTime;
        result << info;
    }

    return result;
}
size_t readCallback(void* ptr, size_t size, size_t nmemb, void* userdata) 
{
    FtpContext* ctx = static_cast<FtpContext*>(userdata);
    size_t toRead = size * nmemb;
    qint64 read = ctx->file->read(static_cast<char*>(ptr), toRead);
    return static_cast<size_t>(read);
}

size_t writeCallbackString(void* contents, size_t size, size_t nmemb, void* userp)
{
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


// 回调函数：收集 MLSD 输出行
static size_t mlsdWriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t total = size * nmemb;
    QByteArray* buffer = static_cast<QByteArray*>(userp);
    buffer->append(static_cast<char*>(contents), total);
    return total;
}

FtpManager::FtpManager(const QString& host, quint16 port, const QString& user, const QString& pass)
    : host_(host), port_(port), user_(user), pass_(pass) 
{
    curl_ = curl_easy_init();
    if (!curl_) {
        throw std::runtime_error("curl_easy_init() 失败");
    }
    curl_easy_setopt(curl_, CURLOPT_TCP_KEEPALIVE, 1L);		// 设置长连接
    curl_easy_setopt(curl_, CURLOPT_USERPWD, (user + ":" + pass).toStdString().c_str());
}

void FtpManager::uploadFile(const QString& localPath, const QString& remoteDirectory,
    const ProgressCallback& progress) 
{
    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("无法打开本地文件: " + localPath.toStdString());
    }

    QString remoteFilePath = buildRemoteFilePath(remoteDirectory, localPath);

    // 使用 QUrl 安全构建 URL（自动处理编码）
    QUrl url;
    url.setScheme("ftp");
    url.setHost(host_);
    url.setPort(port_);
    url.setPath("/" + remoteFilePath); // 必须以 / 开头

    QString urlStr = url.toString(QUrl::FullyEncoded);
    QByteArray urlBytes = urlStr.toUtf8(); // 保持生命周期

    FtpContext ctx;
    ctx.file = &file;
    ctx.total = file.size();
    ctx.progress = progress;

    curl_easy_setopt(curl_, CURLOPT_URL, urlBytes.constData());
    curl_easy_setopt(curl_, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl_, CURLOPT_READFUNCTION, readCallback);
    curl_easy_setopt(curl_, CURLOPT_READDATA, &ctx);
    curl_easy_setopt(curl_, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(file.size()));
    curl_easy_setopt(curl_, CURLOPT_FTP_CREATE_MISSING_DIRS, 1L);
    curl_easy_setopt(curl_, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl_, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(curl_, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 300L);

    CURLcode res = curl_easy_perform(curl_);
    file.close();

    if (res != CURLE_OK) {
        std::string errorMsg = "libcurl 上传失败 (";
        errorMsg += std::to_string(res);
        errorMsg += "): ";
        errorMsg += curl_easy_strerror(res);
        errorMsg += " | URL: ";
        errorMsg += urlStr.toStdString();
        throw std::runtime_error(errorMsg);
    }
}

void FtpManager::uploadFolder(const QString& localPath, const QString& remoteBasePath) {
    QDir localDir(localPath);
    if (!localDir.exists()) {
        std::string errorMsg = "本地文件夹不存在: " + localPath.toStdString();
        throw std::runtime_error(errorMsg);
    }

    QDirIterator it(localPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QFileInfo fi = it.fileInfo();
        if (fi.isFile()) {
            QString filePath = fi.absolutePath();
            QString relPath = localDir.relativeFilePath(filePath);
            QString remoteFile = remoteBasePath + "/"  + localDir.dirName() + "/" + relPath;
            uploadFile(fi.absoluteFilePath(), remoteFile);
        }
    }
}
void FtpManager::clearRemoteDirectory(const QString& remoteDir)
{
    // 规范化路径：去掉首尾斜杠，避免 /app/ -> //app//
    QString cleanDir = remoteDir;
    if (cleanDir.startsWith('/')) cleanDir = cleanDir.mid(1);
    if (cleanDir.endsWith('/')) cleanDir.chop(1);
    if (cleanDir.isEmpty()) {
        throw std::runtime_error("不能清空根目录 '/'");
    }

    // 第一步：列出当前目录内容
    QStringList entries;
    try {
        entries = listFtpDirectory(cleanDir);
    }
    catch (const std::exception& ex) {
        // 如果目录不存在，视为已“清空”
        if (std::string(ex.what()).find("550") != std::string::npos) {
            return; // 550 = File unavailable (e.g., file not found)
        }
        throw; // 其他错误继续抛出
    }

    // 第二步：先删除所有文件
    for (const QString& entry : entries) {
        if (entry.isEmpty()) continue;

        // 构造完整路径用于判断（但 libcurl 的 DELE 不需要完整路径，只需相对名）
        QString fullPath = cleanDir + "/" + entry;

        // 尝试删除文件（如果失败，可能是目录）
        bool isFile = deleteFtpFile(cleanDir, entry);
        if (!isFile) {
            // 如果不是文件，就当作目录处理（递归清空）
            clearRemoteDirectory(fullPath); // 递归清空子目录
            // 然后删除空目录
            deleteFtpDirectory(cleanDir, entry);
        }
    }
}

bool FtpManager::deleteFtpFile(const QString& parentDir, const QString& filename)
{
    QUrl url;
    url.setScheme("ftp");
    url.setHost(host_);
    url.setPort(port_);
    url.setPath("/" + parentDir.trimmed() + "/" + filename);

    QString urlStr = url.toString(QUrl::FullyEncoded);
    QByteArray urlBytes = urlStr.toUtf8();

    // 使用 QUOTE 发送 DELE 命令（更可靠）
    struct curl_slist* quote = nullptr;
    quote = curl_slist_append(quote, ("DELE " + filename).toUtf8().constData());

    curl_easy_setopt(curl_, CURLOPT_URL, urlBytes.constData());
    curl_easy_setopt(curl_, CURLOPT_QUOTE, quote);
    curl_easy_setopt(curl_, CURLOPT_NOBODY, 1L); // 不下载内容
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl_);
    curl_slist_free_all(quote);

    return (res == CURLE_OK);
}

bool FtpManager::deleteFtpDirectory(const QString& parentDir, const QString& dirname)
{
    struct curl_slist* quote = nullptr;
    quote = curl_slist_append(quote, ("RMD " + dirname).toUtf8().constData());

    // 构造父目录 URL
    QString parentPath = parentDir;
    if (parentPath.startsWith('/')) parentPath = parentPath.mid(1);
    QUrl url;
    url.setScheme("ftp");
    url.setHost(host_);
    url.setPort(port_);
    url.setPath("/" + parentPath);

    QString urlStr = url.toString(QUrl::FullyEncoded);
    QByteArray urlBytes = urlStr.toUtf8();

    curl_easy_setopt(curl_, CURLOPT_URL, urlBytes.constData());
    curl_easy_setopt(curl_, CURLOPT_QUOTE, quote);
    curl_easy_setopt(curl_, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl_);
    curl_slist_free_all(quote);

    return (res == CURLE_OK);
}

QStringList FtpManager::listFtpDirectory(const QString& remoteDir)
{
    QUrl url;
    url.setScheme("ftp");
    url.setHost(host_);
    url.setPort(port_);
    url.setPath("/" + remoteDir.trimmed()); // 确保路径格式正确

    // 如果 remoteDir 为空或 "/"，则列出根目录
    if (url.path().isEmpty() || url.path() == "/") {
        url.setPath("/");
    }

    QString urlStr = url.toString(QUrl::FullyEncoded);
    QByteArray urlBytes = urlStr.toUtf8();

    QString listOutput;
    curl_easy_setopt(curl_, CURLOPT_URL, urlBytes.constData());
    curl_easy_setopt(curl_, CURLOPT_DIRLISTONLY, 1L); // 只返回文件名列表
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writeCallbackString);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &listOutput);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 30L); // 添加超时设置

    CURLcode res = curl_easy_perform(curl_);
    if (res != CURLE_OK) {
        throw std::runtime_error("无法列出远程目录: " + std::string(curl_easy_strerror(res)));
    }

    // 按行分割，过滤空行
    QStringList entries = listOutput.split('\n', Qt::SkipEmptyParts);
    for (QString& entry : entries) {
        entry = entry.trimmed();
    }
    return entries;
}

QString FtpManager::buildRemoteFilePath(const QString& remoteDir, const QString& localFilePath) 
{
    QFileInfo fi(localFilePath);
    QString fileName = fi.fileName();

    QString cleanDir = remoteDir;
    if (cleanDir.startsWith('/')) cleanDir = cleanDir.mid(1);
    if (cleanDir.endsWith('/')) cleanDir.chop(1);

    return cleanDir.isEmpty() ? fileName : cleanDir + "/" + fileName;
}

size_t writeData(void* ptr, size_t size, size_t nmemb, QFile* stream)
{
    stream->write(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

size_t FtpManager::progressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    auto callback = static_cast<ProgressCallback*>(clientp);
    if (*callback && dltotal > 0) {
        (*callback)(static_cast<qint64>(dlnow), static_cast<qint64>(dltotal));
    }
    return 0;
}

void FtpManager::downloadFile(const QString& remotePath, const QString& localPath,
    const ProgressCallback& progress)
{
    QFileInfo fileInfo(localPath);
    QDir().mkpath(fileInfo.absolutePath());

    QFile localFile(localPath);
    if (!localFile.open(QIODevice::WriteOnly))
        throw std::runtime_error("无法创建本地文件: " + localPath.toStdString());

    QString url = QString("ftp://%1:%2%3").arg(host_).arg(port_).arg(remotePath);
    curl_easy_setopt(curl_, CURLOPT_URL, url.toUtf8().constData());
    curl_easy_setopt(curl_, CURLOPT_USERNAME, user_.toUtf8().constData());
    curl_easy_setopt(curl_, CURLOPT_PASSWORD, pass_.toUtf8().constData());
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writeData);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &localFile);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 300L); // 添加超时设置，与上传保持一致

    if (progress) {
        curl_easy_setopt(curl_, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl_, CURLOPT_XFERINFOFUNCTION, progressCallback);
        curl_easy_setopt(curl_, CURLOPT_XFERINFODATA, &progress);
    }
    else {
        curl_easy_setopt(curl_, CURLOPT_NOPROGRESS, 1L);
    }

    CURLcode res = curl_easy_perform(curl_);
    localFile.close();

    if (res != CURLE_OK) {
        localFile.remove(); // 删除失败文件
        throw std::runtime_error(std::string("下载失败: ") + curl_easy_strerror(res));
    }
}

FtpManager::~FtpManager() {
    if (curl_) {
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }
}
