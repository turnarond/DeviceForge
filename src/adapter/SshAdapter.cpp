#include "SshAdapter.h"
#include <lwlog/lwlog.h>

// MSVC 未定义 POSIX S_ISDIR 宏，libssh2 权限字段使用 POSIX 值
#ifndef S_ISDIR
#define S_ISDIR(m)  (((m) & 0170000) == 0040000)
#endif
#include <QHostAddress>
#include <QCryptographicHash>
#include <QDateTime>
#include <thread>

// TOFU 已接受主机指纹集合 — 进程级静态存储，跨适配器实例共享
QSet<QString> SshAdapter::s_knownHosts;

// ============================================================
// 构造 / 析构
// ============================================================

SshAdapter::SshAdapter()
{
    // libssh2_init(0) 在 main.cpp 进程级调用一次，此处不再重复初始化
}

SshAdapter::~SshAdapter()
{
    disconnect();
    // 不调用 libssh2_exit()：会拆除进程级全局状态，影响其他适配器实例
}

// ============================================================
// IProtocolAdapter — 连接生命周期
// ============================================================

bool SshAdapter::connect(const DeviceInfo& device, const AuthInfo& auth)
{
    disconnect();

    // 1. TCP 连接
    m_socket = new QTcpSocket();
    m_socket->connectToHost(QString::fromStdString(device.ip),
                            device.port ? device.port : 22);
    if (!m_socket->waitForConnected(10000)) {
        m_lastError = "SSH 连接失败: " + m_socket->errorString().toStdString();
        delete m_socket;
        m_socket = nullptr;
        return false;
    }

    // 2. 创建 libssh2 session
    m_session = libssh2_session_init();
    if (!m_session) {
        m_lastError = "libssh2 session 初始化失败";
        disconnect();
        return false;
    }

    // 3. 握手（阻塞模式 — 由调用方放入后台线程）
    libssh2_session_set_blocking(m_session, 1);
    // I5: 设置阻塞操作超时，防止握手/读写永久阻塞不可中断（默认 10s）
    libssh2_session_set_timeout(m_session, 10000);
    if (libssh2_session_handshake(m_session, static_cast<libssh2_socket_t>(
            m_socket->socketDescriptor())) != 0) {
        m_lastError = "SSH 握手失败";
        disconnect();
        return false;
    }

    // 4. 主机密钥校验 (TOFU)
    if (!verifyHostKey()) {
        // m_lastError 已由 verifyHostKey 设置
        disconnect();
        return false;
    }

    // 5. 密码认证
    if (libssh2_userauth_password(m_session, auth.user.c_str(),
                                   auth.password.c_str()) != 0) {
        m_lastError = "SSH 认证失败: 用户名或密码错误";
        disconnect();
        return false;
    }

    // C1: 连接成功。disconnect() 曾将 m_cancelled 置 true，此处复位，
    //     否则 request() 的 while(!m_cancelled) 读循环会立即退出导致 0 字节输出
    m_cancelled = false;

    // SFTP 子系统可选初始化 — 服务器可能未启用，不影响 exec channel 使用
    if (!sftpInit()) {
        // 仅记录日志，不阻断连接
    }
    return true;
}

void SshAdapter::disconnect()
{
    m_cancelled = true;

    if (m_sftpSession) {
        libssh2_sftp_shutdown(m_sftpSession);
        m_sftpSession = nullptr;
    }

    if (m_subscribeActive) {
        unsubscribe();
    }

    closeChannel(m_channel);

    if (m_session) {
        libssh2_session_disconnect(m_session, "bye");
        libssh2_session_free(m_session);
        m_session = nullptr;
    }

    if (m_socket) {
        m_socket->close();
        // I8: 在 QtConcurrent::run 线程中无事件循环，deleteLater 事件永不派发→泄漏
        delete m_socket;
        m_socket = nullptr;
    }
}

bool SshAdapter::isConnected() const
{
    return m_session != nullptr;
}

std::string SshAdapter::lastError() const
{
    return m_lastError;
}

// ============================================================
// IProtocolAdapter — 传输模式
// ============================================================

std::future<Response> SshAdapter::request(const Request& req)
{
    return std::async(std::launch::async, [this, req]() -> Response {
        Response r;

        if (!m_session) {
            r.success = false;
            r.errorMessage = "SSH 未连接";
            return r;
        }

        // I5: 按请求设置阻塞操作超时，防止 libssh2_channel_read 永久阻塞
        int timeoutMs = req.timeoutMs > 0 ? req.timeoutMs : 10000;
        libssh2_session_set_timeout(m_session, timeoutMs);

        LIBSSH2_CHANNEL* ch = libssh2_channel_open_session(m_session);
        if (!ch) {
            r.success = false;
            r.errorMessage = "打开 SSH channel 失败";
            return r;
        }

        // I4: 合并 stderr 到普通读流，使 libssh2_channel_read 同时返回 stdout+stderr
        libssh2_channel_handle_extended_data2(ch,
            LIBSSH2_CHANNEL_EXTENDED_DATA_MERGE);

        int rc = libssh2_channel_exec(ch, req.path.c_str());
        if (rc != 0) {
            char* errMsg = nullptr;
            int errLen = 0;
            libssh2_session_last_error(m_session, &errMsg, &errLen, 0);
            r.success = false;
            r.errorMessage = std::string("命令执行失败: ")
                           + std::string(errMsg ? errMsg : "", errLen);
            libssh2_channel_free(ch);
            return r;
        }

        char buf[4096];
        std::string output;
        r.success = true;  // I3: 默认成功，读错误分支置为 false，循环结束不再无条件覆盖
        while (!m_cancelled) {
            ssize_t n = libssh2_channel_read(ch, buf, sizeof(buf));
            if (n > 0) {
                output.append(buf, static_cast<size_t>(n));
            } else if (n == 0) {
                // 阻塞模式：0 表示读到 EOF/无更多数据
                break;
            } else {
                // n < 0: 错误
                r.success = false;
                r.errorMessage = "读取 SSH 命令输出失败";
                break;
            }
        }

        libssh2_channel_send_eof(ch);
        // I3: 不再无条件 r.success = true；成功/失败由读循环决定
        r.statusCode = libssh2_channel_get_exit_status(ch);
        libssh2_channel_wait_closed(ch);
        libssh2_channel_free(ch);

        r.data = output;
        return r;
    });
}

void SshAdapter::subscribe(const Request& /*req*/, StreamCallback /*onData*/)
{
    m_lastError = "SSH subscribe 模式暂未实现";
}

void SshAdapter::unsubscribe()
{
    m_subscribeActive = false;
    closeChannel(m_channel);
}

ProtocolCapability SshAdapter::capability() const
{
    ProtocolCapability c;
    c.requestResponse  = true;
    c.streaming        = false;
    c.broadcast        = false;
    c.publishSubscribe = false;
    c.maxConnections   = 1;
    return c;
}

// ============================================================
// 主机密钥校验 — TOFU (Trust On First Use)
// ============================================================

std::string SshAdapter::collectHostFingerprint()
{
    size_t len = 0;
    int type = 0;
    const char* key = libssh2_session_hostkey(m_session, &len, &type);
    if (!key || len == 0) {
        return "";
    }

    // SHA256 hash of raw host key → hex string
    QByteArray rawKey(key, static_cast<int>(len));
    QByteArray hex = QCryptographicHash::hash(rawKey,
                                              QCryptographicHash::Sha256).toHex();
    return hex.toStdString();
}

bool SshAdapter::verifyHostKey()
{
    std::string fp = collectHostFingerprint();
    if (fp.empty()) {
        m_lastError = "无法获取 SSH 主机密钥";
        return false;
    }

    QString qfp = QString::fromStdString(fp);

    // I7: 指纹已在进程级 TOFU 集合中 → 与历史一致，通过
    if (s_knownHosts.contains(qfp)) {
        return true;
    }

    // I7: 集合非空但当前指纹不在其中 → 与首次连接不符，拒绝（防中间人攻击）
    if (!s_knownHosts.isEmpty()) {
        m_lastError = "SSH 主机密钥与首次连接时不符，可能存在中间人攻击";
        LWLOG_W("SSH TOFU: host key mismatch, rejecting connection");
        return false;
    }

    // I7: 集合为空（进程内首次 SSH 连接）→ 记录并接受
    s_knownHosts.insert(qfp);
    LWLOG_I("SSH TOFU: accepted host key " + fp);
    return true;
}

// ============================================================
// SFTP 文件操作
// ============================================================

bool SshAdapter::sftpInit()
{
    if (!m_session) return false;
    m_sftpSession = libssh2_sftp_init(m_session);
    if (!m_sftpSession) {
        m_lastError = "SFTP 初始化失败";
        return false;
    }
    return true;
}

std::vector<FtpFileInfo> SshAdapter::sftpListDirectory(const std::string& remotePath)
{
    std::vector<FtpFileInfo> result;
    if (!m_sftpSession) { m_lastError = "SFTP 未初始化"; return result; }

    LIBSSH2_SFTP_HANDLE* dir = libssh2_sftp_opendir(m_sftpSession, remotePath.c_str());
    if (!dir) {
        m_lastError = "SFTP 打开目录失败: " + remotePath;
        return result;
    }

    char filename[512];
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    while (libssh2_sftp_readdir(dir, filename, sizeof(filename), &attrs) > 0) {
        std::string name(filename);
        if (name == "." || name == "..") continue;

        FtpFileInfo fi;
        fi.name = name;
        fi.isDir = (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS)
            ? S_ISDIR(attrs.permissions) : false;
        fi.size = (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) ? attrs.filesize : 0;
        fi.dateTime = (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME)
            ? QDateTime::fromSecsSinceEpoch(attrs.mtime).toString("yyyy-MM-dd HH:mm:ss").toStdString()
            : "";

        // 权限：八进制 → rwx 字符串
        if (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) {
            unsigned int p = attrs.permissions & 0777;
            fi.permissions = std::string()
                + ((p & 0400) ? "r" : "-") + ((p & 0200) ? "w" : "-") + ((p & 0100) ? "x" : "-")
                + ((p & 0040) ? "r" : "-") + ((p & 0020) ? "w" : "-") + ((p & 0010) ? "x" : "-")
                + ((p & 0004) ? "r" : "-") + ((p & 0002) ? "w" : "-") + ((p & 0001) ? "x" : "-");
        }

        result.push_back(fi);
    }
    libssh2_sftp_closedir(dir);
    return result;
}

bool SshAdapter::sftpUploadFile(const std::string& localPath, const std::string& remotePath)
{
    if (!m_sftpSession) { m_lastError = "SFTP 未初始化"; return false; }

    // 打开本地文件读取
    FILE* localFile = fopen(localPath.c_str(), "rb");
    if (!localFile) { m_lastError = "无法打开本地文件: " + localPath; return false; }
    fseek(localFile, 0, SEEK_END);
    uint64_t fileSize = ftell(localFile);
    fseek(localFile, 0, SEEK_SET);

    // 创建远程文件
    LIBSSH2_SFTP_HANDLE* remoteFile = libssh2_sftp_open(m_sftpSession, remotePath.c_str(),
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
        LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR |
        LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);
    if (!remoteFile) { fclose(localFile); m_lastError = "SFTP 创建文件失败"; return false; }

    // 分块上传（8KB 缓冲区）
    char buf[8192];
    uint64_t sent = 0;
    bool ok = true;
    while (sent < fileSize) {
        size_t n = fread(buf, 1, sizeof(buf), localFile);
        if (n == 0) break;
        ssize_t rc = libssh2_sftp_write(remoteFile, buf, n);
        if (rc < 0) { ok = false; break; }
        sent += rc;
        if (m_sftpProgressCb && fileSize > 0) {
            int pct = static_cast<int>((sent * 100) / fileSize);
            m_sftpProgressCb(pct);
        }
    }

    fclose(localFile);
    libssh2_sftp_close(remoteFile);
    if (!ok) m_lastError = "SFTP 上传写入失败";
    return ok;
}

bool SshAdapter::sftpDownloadFile(const std::string& remotePath, const std::string& localPath)
{
    if (!m_sftpSession) { m_lastError = "SFTP 未初始化"; return false; }

    LIBSSH2_SFTP_HANDLE* remoteFile = libssh2_sftp_open(m_sftpSession, remotePath.c_str(),
        LIBSSH2_FXF_READ, 0);
    if (!remoteFile) { m_lastError = "SFTP 打开文件失败"; return false; }

    // 获取文件大小
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    libssh2_sftp_fstat(remoteFile, &attrs);
    uint64_t fileSize = attrs.filesize;

    FILE* localFile = fopen(localPath.c_str(), "wb");
    if (!localFile) { libssh2_sftp_close(remoteFile); m_lastError = "无法创建本地文件"; return false; }

    char buf[8192];
    uint64_t received = 0;
    bool ok = true;
    while (true) {
        ssize_t n = libssh2_sftp_read(remoteFile, buf, sizeof(buf));
        if (n < 0) { ok = false; break; }
        if (n == 0) break;
        fwrite(buf, 1, n, localFile);
        received += n;
        if (m_sftpProgressCb && fileSize > 0) {
            int pct = static_cast<int>((received * 100) / fileSize);
            m_sftpProgressCb(pct);
        }
    }

    fclose(localFile);
    libssh2_sftp_close(remoteFile);
    if (!ok) m_lastError = "SFTP 下载读取失败";
    return ok;
}

bool SshAdapter::sftpDeleteFile(const std::string& remotePath)
{
    if (!m_sftpSession) { m_lastError = "SFTP 未初始化"; return false; }
    int rc = libssh2_sftp_unlink(m_sftpSession, remotePath.c_str());
    if (rc != 0) { m_lastError = "SFTP 删除文件失败"; return false; }
    return true;
}

bool SshAdapter::sftpDeleteDirectory(const std::string& remotePath)
{
    if (!m_sftpSession) { m_lastError = "SFTP 未初始化"; return false; }

    // 先尝试直接删除（空目录），失败则递归删除
    if (libssh2_sftp_rmdir(m_sftpSession, remotePath.c_str()) == 0) return true;

    // 目录非空：列出内容，递归删除
    auto entries = sftpListDirectory(remotePath);
    for (const auto& e : entries) {
        std::string fullPath = remotePath + "/" + e.name;
        if (e.isDir) sftpDeleteDirectory(fullPath);
        else sftpDeleteFile(fullPath);
    }
    int rc = libssh2_sftp_rmdir(m_sftpSession, remotePath.c_str());
    if (rc != 0) { m_lastError = "SFTP 删除目录失败"; return false; }
    return true;
}

bool SshAdapter::sftpRename(const std::string& oldPath, const std::string& newPath)
{
    if (!m_sftpSession) { m_lastError = "SFTP 未初始化"; return false; }
    int rc = libssh2_sftp_rename(m_sftpSession, oldPath.c_str(), newPath.c_str());
    if (rc != 0) { m_lastError = "SFTP 重命名失败"; return false; }
    return true;
}

bool SshAdapter::sftpMakeDirectory(const std::string& remotePath)
{
    if (!m_sftpSession) { m_lastError = "SFTP 未初始化"; return false; }
    int rc = libssh2_sftp_mkdir(m_sftpSession, remotePath.c_str(),
        LIBSSH2_SFTP_S_IRWXU | LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IXGRP | LIBSSH2_SFTP_S_IROTH | LIBSSH2_SFTP_S_IXOTH);
    if (rc != 0) { m_lastError = "SFTP 新建目录失败"; return false; }
    return true;
}

void SshAdapter::sftpSetProgressCallback(std::function<void(int)> cb)
{
    m_sftpProgressCb = std::move(cb);
}

// ============================================================
// 辅助方法
// ============================================================

void SshAdapter::closeChannel(LIBSSH2_CHANNEL*& ch)
{
    if (ch) {
        libssh2_channel_send_eof(ch);
        libssh2_channel_wait_closed(ch);
        libssh2_channel_free(ch);
        ch = nullptr;
    }
}
