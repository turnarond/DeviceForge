#pragma once
#include "IProtocolAdapter.h"
#include "adapter/IDeployable.h"
#include <QTcpSocket>
#include <libssh2/libssh2.h>
#include <libssh2/libssh2_sftp.h>  // SFTP subsystem
#include "tools/FtpDeployTool/FtpFileInfo.h"  // 复用 FtpFileInfo
#include <functional>
#include <QSet>
#include <QFuture>
#include <atomic>

// --- SFTP 部署能力（IDeployable）---
struct SftpPlanItem {
    std::string localPath;
    std::string remotePath;
    bool isDirectory = false;
};

// SSH 协议适配器 — 基于 libssh2 实现 IProtocolAdapter
// 支持密码认证 + TOFU (Trust On First Use) 主机密钥校验
// 阻塞模式，由调用方放入 QtConcurrent::run 线程中执行
class SshAdapter : public IProtocolAdapter, public IDeployable {
public:
    SshAdapter();
    ~SshAdapter() override;

    // --- IProtocolAdapter 实现 ---
    std::string protocolId() const override { return "ssh"; }
    bool connect(const DeviceInfo& device, const AuthInfo& auth) override;
    void disconnect() override;
    bool isConnected() const override;
    std::string lastError() const override;
    std::future<Response> request(const Request& req) override;
    void subscribe(const Request& req, StreamCallback onData) override;
    void unsubscribe() override;
    ProtocolCapability capability() const override;

    // --- SFTP 文件操作（SSH 文件传输子系统）---
    std::vector<FtpFileInfo> sftpListDirectory(const std::string& remotePath);
    bool sftpUploadFile(const std::string& localPath, const std::string& remotePath);
    bool sftpDownloadFile(const std::string& remotePath, const std::string& localPath);
    bool sftpDeleteFile(const std::string& remotePath);
    bool sftpDeleteDirectory(const std::string& remotePath);
    bool sftpRename(const std::string& oldPath, const std::string& newPath);
    bool sftpMakeDirectory(const std::string& remotePath);
    void sftpSetProgressCallback(std::function<void(int)> cb);

    // --- SFTP 部署能力（IDeployable）---
    static std::vector<SftpPlanItem> planFolderUpload(const std::string& localRoot,
                                                      const std::string& remoteRoot);
    bool sftpUploadFolder(const std::string& localPath, const std::string& remotePath);
    bool sftpClearDirectory(const std::string& remotePath);
    void sftpSetCancelFlag(std::atomic<bool>* flag);

    // --- IDeployable 实现 ---
    bool uploadFile(const std::string& localPath, const std::string& remotePath) override;
    bool uploadFolder(const std::string& localPath, const std::string& remotePath) override;
    bool clearRemoteDirectory(const std::string& remotePath) override;
    void setProgressCallback(std::function<void(int)> cb) override;
    void setCancelFlag(std::atomic<bool>* flag) override;

private:
    QTcpSocket*         m_socket = nullptr;
    LIBSSH2_SESSION*    m_session = nullptr;
    LIBSSH2_CHANNEL*    m_channel = nullptr;  // subscribe 模式用，request 模式每次新建
    std::string         m_lastError;
    std::atomic<bool>   m_cancelled{false};
    static QSet<QString> s_knownHosts;        // TOFU: 已接受的主机指纹集合（进程级共享）
    QFuture<void>       m_subscribeFuture;
    std::atomic<bool>   m_subscribeActive{false};

    std::string collectHostFingerprint();     // 获取当前连接主机指纹（SHA256 Hex）
    bool verifyHostKey();                     // TOFU 校验（首次接受，变化告警）
    void closeChannel(LIBSSH2_CHANNEL*& ch);  // 安全关闭 channel

    // --- SFTP 内部辅助 ---
    LIBSSH2_SFTP* m_sftpSession = nullptr;
    std::function<void(int)> m_sftpProgressCb;
    std::atomic<bool>* m_sftpCancelFlag = nullptr;  // 部署循环取消标志（IDeployable）
    bool sftpInit();
    bool sftpDeleteDirectoryRecursive(const std::string& remotePath, int depth); // 递归删除（带深度上限）
};
