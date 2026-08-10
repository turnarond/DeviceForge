#include "SshAdapter.h"
#include <lwlog/lwlog.h>
#include <filesystem>  // planFolderUpload 递归遍历（std::filesystem）

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
    // 设置阻塞操作超时：死连接上 opendir/readdir 的等待减半（5s），
    // 配合 FtpDeployWidget 的自动重连重试，用户感知从"卡 10 秒"变为"快速失败 + 自动恢复"
    libssh2_session_set_timeout(m_session, 5000);
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
        LWLOG_W(std::string("SFTP 子系统初始化失败（服务器可能未启用）：") + m_lastError);
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
    m_lastError.clear();  // 成功路径清残留错误（对齐 FtpAdapter 惯例；防 sftpClearDirectory 误判）
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
        // . 和 .. 保留，由 RemoteFileModel 处理导航

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
    _fseeki64(localFile, 0, SEEK_END);
    uint64_t fileSize = static_cast<uint64_t>(_ftelli64(localFile));
    _fseeki64(localFile, 0, SEEK_SET);

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
        if (m_sftpCancelFlag && *m_sftpCancelFlag) { ok = false; m_lastError = "上传已取消"; break; }
        size_t n = fread(buf, 1, sizeof(buf), localFile);
        if (n == 0) {
            if (ferror(localFile)) { ok = false; m_lastError = "SFTP 上传读取本地文件失败"; }
            break; // EOF 或错误
        }
        // libssh2_sftp_write 可能部分写入，循环直到本块全部写出
        size_t written = 0;
        while (written < n) {
            ssize_t rc = libssh2_sftp_write(remoteFile, buf + written, n - written);
            if (rc < 0) { ok = false; break; }
            written += static_cast<size_t>(rc);
        }
        if (!ok) break;
        sent += written;
        if (m_sftpProgressCb && fileSize > 0) {
            int pct = static_cast<int>((sent * 100) / fileSize);
            m_sftpProgressCb(pct);
        }
    }

    fclose(localFile);
    libssh2_sftp_close(remoteFile);
    if (!ok && m_lastError.empty()) m_lastError = "SFTP 上传写入失败";
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
        if (fwrite(buf, 1, n, localFile) != static_cast<size_t>(n)) {
            ok = false;
            m_lastError = "SFTP 下载写入失败（磁盘空间不足？）";
            break;
        }
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
    return sftpDeleteDirectoryRecursive(remotePath, 0);
}

bool SshAdapter::sftpDeleteDirectoryRecursive(const std::string& remotePath, int depth)
{
    if (!m_sftpSession) { m_lastError = "SFTP 未初始化"; return false; }
    if (depth > 64) { m_lastError = "SFTP 删除目录超过最大深度"; return false; }

    // 先尝试直接删除（空目录），失败则递归删除
    if (libssh2_sftp_rmdir(m_sftpSession, remotePath.c_str()) == 0) return true;

    // 目录非空：列出内容，递归删除（跳过 . 和 ..，防止无限递归）
    auto entries = sftpListDirectory(remotePath);
    for (const auto& e : entries) {
        if (e.name == "." || e.name == "..") continue;
        std::string fullPath = remotePath + "/" + e.name;
        bool ok = e.isDir
            ? sftpDeleteDirectoryRecursive(fullPath, depth + 1)
            : sftpDeleteFile(fullPath);
        if (!ok) return false; // 子项失败即中止
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
// SFTP 部署能力（IDeployable）
// ============================================================

// 本地递归展开 → 远程路径映射（纯逻辑，可单测）
std::vector<SftpPlanItem> SshAdapter::planFolderUpload(const std::string& localRoot,
                                                       const std::string& remoteRoot)
{
    std::vector<SftpPlanItem> items;
    namespace fs = std::filesystem;
    std::error_code ec;

    // 目录项在前（保证上传前 mkdir 顺序），文件项在后
    std::vector<SftpPlanItem> dirs, files;
    // 非抛异常迭代：遍历中途出错（权限/悬空链接等）时 MSVC 将迭代器置为 end 且
    // increment(ec) 仅置 ec 不抛异常 → 已展开条目保留，不会整体失败/跳过该条
    fs::recursive_directory_iterator it(localRoot, ec);
    const fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        std::string rel = it->path().lexically_relative(localRoot).generic_string();
        std::string remote = remoteRoot;
        if (!remote.empty() && remote.back() != '/') remote += '/';
        remote += rel;
        if (it->is_directory(ec)) {
            dirs.push_back({it->path().string(), remote, true});
        } else if (!ec) {
            files.push_back({it->path().string(), remote, false});
        }
    }
    // 遍历异常终止（MSVC 出错即置 end）：已展开条目保留，但计划可能不完整 → 告警
    if (ec) {
        LWLOG_W(std::string("本地目录遍历中断，计划可能不完整: ") + localRoot);
    }
    items.insert(items.end(), dirs.begin(), dirs.end());
    items.insert(items.end(), files.begin(), files.end());
    return items;
}

// 逐项执行：目录 → mkdir（已存在忽略），文件 → sftpUploadFile（取消检查）
bool SshAdapter::sftpUploadFolder(const std::string& localPath, const std::string& remotePath)
{
    auto items = planFolderUpload(localPath, remotePath);
    // localRoot 不存在/不可读：planFolderUpload 返回空 → 显式报错而非静默成功
    if (items.empty() && !std::filesystem::exists(localPath)) {
        m_lastError = "本地目录不存在或不可读: " + localPath;
        return false;
    }
    for (const auto& item : items) {
        if (m_sftpCancelFlag && *m_sftpCancelFlag) { m_lastError = "操作已取消"; return false; }
        if (item.isDirectory) {
            // mkdir 失败不中止：目录已存在（EEXIST）属正常，真失败由后续文件上传暴露；
            // 清除 sftpMakeDirectory 设置的误导性错误（成功路径下调用方不应读到失败信息）
            if (!sftpMakeDirectory(item.remotePath)) {
                LWLOG_W(std::string("SFTP 目录已存在或创建失败（继续）: ") + item.remotePath);
                m_lastError.clear();
            }
        } else {
            if (!sftpUploadFile(item.localPath, item.remotePath)) return false;
        }
    }
    return true;
}

// 清空目录内容但保留目录本身：LIST → 逐项删除（文件删、子目录递归删）
bool SshAdapter::sftpClearDirectory(const std::string& remotePath)
{
    // 根目录守卫：禁止清空根（与 FtpAdapter::clearRemoteDirectory 对齐）。
    // 空路径/SFTP 登录 cwd/根目录均拒绝——否则会递归删除根目录全部内容
    // （DeviceForge 目标设备为 SylixOS 嵌入式设备，SFTP 常为 root 账号）
    std::string cleanPath = remotePath;
    while (!cleanPath.empty() && cleanPath.front() == '/') cleanPath.erase(0, 1);
    while (!cleanPath.empty() && cleanPath.back() == '/') cleanPath.pop_back();
    if (cleanPath.empty()) {
        m_lastError = "不能清空根目录 '/'";
        return false;
    }
    if (!m_sftpSession) { m_lastError = "SFTP 未初始化"; return false; }
    auto entries = sftpListDirectory(remotePath);
    if (!m_lastError.empty()) return false;   // LIST 失败：报错而非静默假成功
    for (const auto& e : entries) {
        if (e.name == "." || e.name == "..") continue;   // sftpListDirectory 保留 . / ..
        if (m_sftpCancelFlag && *m_sftpCancelFlag) { m_lastError = "操作已取消"; return false; }
        std::string full = remotePath;
        if (!full.empty() && full.back() != '/') full += '/';
        full += e.name;
        if (e.isDir) {                                   // FtpFileInfo::isDir（见 FtpFileInfo.h）
            if (!sftpDeleteDirectory(full)) return false;  // 递归删除（已有实现）
        } else {
            if (!sftpDeleteFile(full)) return false;
        }
    }
    return true;
}

// 部署前设置、部署期间不得修改（指针写入非原子）
void SshAdapter::sftpSetCancelFlag(std::atomic<bool>* flag) { m_sftpCancelFlag = flag; }

// --- IDeployable 映射 ---
bool SshAdapter::uploadFile(const std::string& p, const std::string& r) { return sftpUploadFile(p, r); }
bool SshAdapter::uploadFolder(const std::string& p, const std::string& r) { return sftpUploadFolder(p, r); }
bool SshAdapter::clearRemoteDirectory(const std::string& p) { return sftpClearDirectory(p); }
void SshAdapter::setProgressCallback(std::function<void(int)> cb) { sftpSetProgressCallback(std::move(cb)); }
void SshAdapter::setCancelFlag(std::atomic<bool>* flag) { sftpSetCancelFlag(flag); }

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
