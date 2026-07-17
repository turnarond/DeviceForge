#include "TelnetAdapter.h"
#include "lwconn.h"
#include <sstream>
#include <chrono>
#include <cstring>

// ============================================================
// Pimpl 实现体 — 封装 lwcommunicate::LWTcpClient 操作细节
// ============================================================
struct TelnetAdapter::Impl {
    std::shared_ptr<LWTcpClient> m_client;
    std::string m_host;
    int         m_port = 23;
    std::string m_lastError;

    // 流模式状态
    StreamCallback m_streamCb;
    std::atomic<bool> m_streaming{false};
    std::thread m_readThread;
    std::atomic<bool> m_readThreadRunning{false};

    // 请求-响应模式的响应缓冲区（由读取线程填充，request() 超时后取用）
    std::string m_responseBuffer;
    std::mutex  m_responseMutex;
    std::mutex  m_requestMutex;   // 序列化 request() 调用，防止并发覆盖响应缓冲区

    // 预设超时（毫秒）
    static constexpr int kReadTimeoutMs = 200;
    static constexpr int kSendTimeoutMs = 5000;
    static constexpr int kConnectTimeoutMs = 10000;
    static constexpr int kAuthDelayMs = 500;

    // --- 连接事件回调 ---
    void onConnectionEvent(bool connected, const std::string& extra) {
        if (!connected) {
            m_streaming = false;
        }
    }

    // --- 后台读取线程 ---
    // 持续调用 LWTcpClient::receive()，将数据分发给流回调或存入响应缓冲区
    void readLoop() {
        char buffer[4096];

        while (m_readThreadRunning) {
            if (!m_client || !m_client->isConnected()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kReadTimeoutMs));
                continue;
            }

            size_t received = 0;
            LWConnError err = m_client->receive(buffer, sizeof(buffer) - 1,
                                                 received, kReadTimeoutMs);

            if (err == LWConnError::SUCCESS && received > 0) {
                buffer[received] = '\0';
                std::string chunk(buffer, received);

                if (m_streaming && m_streamCb) {
                    // 流模式：直接回调
                    m_streamCb(chunk, false);
                } else {
                    // 请求-响应模式：存入共享缓冲区
                    std::lock_guard<std::mutex> lock(m_responseMutex);
                    m_responseBuffer += chunk;
                }
            }
            // TIMEOUT / NOT_CONNECTED / RECEIVE_FAILED 都是正常的，继续循环
        }
    }
};

// ============================================================
// 构造 / 析构
// ============================================================

TelnetAdapter::TelnetAdapter() : m_impl(std::make_unique<Impl>()) {}

TelnetAdapter::~TelnetAdapter() {
    disconnect();
}

// ============================================================
// IProtocolAdapter — 连接生命周期
// ============================================================

bool TelnetAdapter::connect(const DeviceInfo& device, const AuthInfo& auth) {
    // 先断开已有连接
    if (m_impl->m_client) {
        disconnect();
    }

    m_impl->m_host = device.ip;
    m_impl->m_port = device.port > 0 ? device.port : 23;

    // 创建 LWTcpClient
    std::string name = "telnet_" + device.ip + ":" + std::to_string(m_impl->m_port);
    m_impl->m_client = std::make_shared<LWTcpClient>(
        name, m_impl->m_host, m_impl->m_port);

    // 设置连接事件回调
    m_impl->m_client->setEventHandler(
        [this](bool connected, const std::string& extra) {
            m_impl->onConnectionEvent(connected, extra);
        });

    // 配置重连策略（启用自动重连）
    ReconnectPolicy policy;
    policy.max_retries = -1;       // 无限重试
    policy.base_delay_ms = 2000;
    policy.max_delay_ms = 30000;
    policy.multiplier = 2.0f;
    m_impl->m_client->setReconnectPolicy(policy);

    // 启动连接（带超时）
    LWConnError err = m_impl->m_client->start(Impl::kConnectTimeoutMs);
    if (err != LWConnError::SUCCESS) {
        m_impl->m_lastError = "Telnet 连接失败: " + m_impl->m_host + ":" +
                              std::to_string(m_impl->m_port);
        m_impl->m_client.reset();
        return false;
    }

    // Telnet 认证：发送用户名 + 密码
    // 等待服务端就绪（发送登录提示）
    std::this_thread::sleep_for(std::chrono::milliseconds(Impl::kAuthDelayMs));

    std::string loginCmd = auth.user + "\r\n";
    LWConnError sendErr = m_impl->m_client->send(loginCmd.c_str(),
                                                   loginCmd.size(), Impl::kSendTimeoutMs);
    if (sendErr != LWConnError::SUCCESS) {
        m_impl->m_lastError = "Telnet 发送用户名失败，连接已断开";
        m_impl->m_client->stop();
        m_impl->m_client.reset();
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(Impl::kAuthDelayMs));

    std::string passCmd = auth.password + "\r\n";
    sendErr = m_impl->m_client->send(passCmd.c_str(), passCmd.size(), Impl::kSendTimeoutMs);
    if (sendErr != LWConnError::SUCCESS) {
        m_impl->m_lastError = "Telnet 发送密码失败，连接已断开";
        m_impl->m_client->stop();
        m_impl->m_client.reset();
        return false;
    }

    // 再等待服务端处理认证
    std::this_thread::sleep_for(std::chrono::milliseconds(Impl::kAuthDelayMs));

    // 启动后台读取线程
    m_impl->m_readThreadRunning = true;
    m_impl->m_readThread = std::thread(&Impl::readLoop, m_impl.get());

    m_impl->m_lastError.clear();
    return true;
}

void TelnetAdapter::disconnect() {
    // 停止流模式
    unsubscribe();

    // 停止读取线程
    m_impl->m_readThreadRunning = false;
    if (m_impl->m_readThread.joinable()) {
        m_impl->m_readThread.join();
    }

    // 断开 TCP 连接
    if (m_impl->m_client) {
        m_impl->m_client->disconnect();
        m_impl->m_client->stop();
        m_impl->m_client.reset();
    }
}

bool TelnetAdapter::isConnected() const {
    return m_impl->m_client && m_impl->m_client->isConnected();
}

std::string TelnetAdapter::lastError() const {
    return m_impl->m_lastError;
}

// ============================================================
// IProtocolAdapter — 传输模式
// ============================================================

std::future<Response> TelnetAdapter::request(const Request& req) {
    return std::async(std::launch::async, [this, req]() -> Response {
        Response resp;

        // 序列化并发请求，防止共享 m_responseBuffer 数据串扰
        std::lock_guard<std::mutex> lock(m_impl->m_requestMutex);

        if (!m_impl->m_client || !m_impl->m_client->isConnected()) {
            resp.success = false;
            resp.errorMessage = "Telnet 未连接";
            return resp;
        }

        // 清空响应缓冲区
        {
            std::lock_guard<std::mutex> lock(m_impl->m_responseMutex);
            m_impl->m_responseBuffer.clear();
        }

        // 发送命令（path 为命令文本，追加 \r\n）
        std::string cmd = req.path;
        if (!cmd.empty() && cmd.back() != '\n') {
            cmd += "\r\n";
        }

        LWConnError sendErr = m_impl->m_client->send(cmd.c_str(), cmd.size(),
                                                        Impl::kSendTimeoutMs);
        if (sendErr != LWConnError::SUCCESS) {
            resp.success = false;
            resp.errorMessage = "Telnet 命令发送失败";
            return resp;
        }

        // 等待响应（由后台读取线程填充 m_responseBuffer）
        int waited = 0;
        int timeoutMs = req.timeoutMs > 0 ? req.timeoutMs : 5000;
        int pollInterval = 100; // 每 100ms 检查一次

        while (waited < timeoutMs) {
            std::this_thread::sleep_for(std::chrono::milliseconds(pollInterval));
            waited += pollInterval;

            // 检查是否已收到数据
            {
                std::lock_guard<std::mutex> lock(m_impl->m_responseMutex);
                if (!m_impl->m_responseBuffer.empty()) {
                    // 等待再多一点数据（可能分片到达）
                    if (waited < 500) {
                        continue; // 至少等待 500ms 聚合响应
                    }
                    break;
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_impl->m_responseMutex);
            resp.success = true;
            resp.data = m_impl->m_responseBuffer;
            m_impl->m_responseBuffer.clear();
        }

        return resp;
    });
}

void TelnetAdapter::subscribe(const Request& req, StreamCallback onData) {
    if (!m_impl->m_client || !m_impl->m_client->isConnected()) {
        m_impl->m_lastError = "Telnet 未连接,无法订阅";
        return;
    }

    m_impl->m_streamCb = std::move(onData);
    m_impl->m_streaming = true;

    // 发送订阅命令（流模式下首先执行一次命令触发数据流）
    std::string cmd = req.path;
    if (!cmd.empty() && cmd.back() != '\n') {
        cmd += "\r\n";
    }

    if (!cmd.empty() && cmd != "\r\n") {
        LWConnError sendErr = m_impl->m_client->send(cmd.c_str(), cmd.size(),
                                                        Impl::kSendTimeoutMs);
        if (sendErr != LWConnError::SUCCESS) {
            m_impl->m_lastError = "Telnet 订阅命令发送失败";
            m_impl->m_streaming = false;
            return;
        }
    }
}

void TelnetAdapter::unsubscribe() {
    m_impl->m_streaming = false;
    m_impl->m_streamCb = nullptr;
}

ProtocolCapability TelnetAdapter::capability() const {
    ProtocolCapability cap;
    cap.requestResponse = true;
    cap.streaming = true;
    cap.broadcast = false;
    cap.publishSubscribe = false;
    cap.maxConnections = 50;
    return cap;
}
