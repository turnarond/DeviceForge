#pragma once
#include "ui/IFileSource.h"
#include "framework/DeviceInfo.h"
#include <QMutex>
#include <memory>

class IProtocolAdapter;

// 远程文件源 — 包装协议适配器（FTP/FTPS → FtpAdapter，SFTP → SshAdapter）。
// 通过 ProtocolRegistry 按协议 id 创建适配器，方法映射到适配器同名操作，
// 所有失败均传递 lastError（成功时清空）。
class RemoteFileSource : public IFileSource {
public:
    explicit RemoteFileSource(const QString& protocol, const QString& displayName);
    QString sourceId() const override;                 // "ftp"/"ssh"（ProtocolRegistry 注册表键）
    QString displayName() const override;              // "192.168.1.100 (FTP)"
    std::vector<FtpFileInfo> list(const QString& path) override;
    bool mkdir(const QString& path) override;
    bool rename(const QString& oldPath, const QString& newPath) override;
    bool remove(const QString& path, bool isDir) override;
    bool clearDirectory(const QString& path) override;
    bool upload(const QString& localPath, const QString& remotePath) override;
    bool download(const QString& remotePath, const QString& localPath) override;
    bool connect(const DeviceInfo& device, const AuthInfo& auth) override;
    bool reconnect() override;              // disconnect + connect(上次缓存凭证)
    bool isConnected() const override;
    QString lastError() const override;
    void setProgressCallback(std::function<void(int)> cb) override;
    void setCancelFlag(std::atomic<bool>* flag) override;

    void setUseFtps(bool useFtps);   // FTP 源启用 FTPS 加密（connect 时生效）
private:
    // 无锁私有体：调用方必须已持有 m_mutex（connect/reconnect 的公共入口加锁后复用，
    // 避免 QMutex 非递归死锁）
    bool connectLocked(const DeviceInfo& device, const AuthInfo& auth);

    mutable QMutex m_mutex;   // 串行化 adapter 操作（异步 list/reconnect 并发安全）
    std::shared_ptr<IProtocolAdapter> m_adapter;
    QString m_protocol;     // "ftp"/"ssh"（ProtocolRegistry 注册表键；SFTP 能力由 SshAdapter 提供）
    QString m_displayName;
    QString m_lastError;
    bool m_useFtps = false;
    DeviceInfo m_device;    // 上次 connect 缓存（reconnect 用）
    AuthInfo m_auth;        // 上次 connect 缓存（reconnect 用）
    std::function<void(int)> m_progressCb;   // 暂存：setter 可能先于 connect 调用
    std::atomic<bool>* m_cancelFlag = nullptr;
};
