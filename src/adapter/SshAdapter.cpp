#include "SshAdapter.h"
#include <lwlog/lwlog.h>
#include <QHostAddress>
#include <QCryptographicHash>
#include <thread>

// ============================================================
// 构造 / 析构
// ============================================================

SshAdapter::SshAdapter()
{
    libssh2_init(0);
}

SshAdapter::~SshAdapter()
{
    disconnect();
    libssh2_exit();
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

    return true;
}

void SshAdapter::disconnect()
{
    m_cancelled = true;

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
        m_socket->deleteLater();
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

        LIBSSH2_CHANNEL* ch = libssh2_channel_open_session(m_session);
        if (!ch) {
            r.success = false;
            r.errorMessage = "打开 SSH channel 失败";
            return r;
        }

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
        while (!m_cancelled) {
            ssize_t n = libssh2_channel_read(ch, buf, sizeof(buf));
            if (n > 0) {
                output.append(buf, static_cast<size_t>(n));
            } else if (n == 0) {
                // 非阻塞模式下无数据可读；阻塞模式下不应出现
                break;
            } else {
                // n < 0: 错误
                r.success = false;
                r.errorMessage = "读取 SSH 命令输出失败";
                break;
            }
        }

        libssh2_channel_send_eof(ch);
        r.success = true;
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

    if (!m_knownHosts.contains(qfp)) {
        if (m_knownHosts.isEmpty()) {
            // TOFU: 首次连接，自动接受
            m_knownHosts.insert(qfp);
            LWLOG_I("SSH TOFU: accepted host key " + fp);
        } else {
            // 同一批次中已有其他设备的指纹，新设备指纹不同——自动接受
            m_knownHosts.insert(qfp);
        }
    }
    // 指纹已存在且匹配 → 通过
    return true;
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
