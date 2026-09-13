// AdapterTransferChannel.h — 现有 FTP/SFTP Adapter 的内部可靠传输包装

#pragma once

#include "adapter/IProtocolAdapter.h"
#include "transfer/ITransferChannel.h"

#include <QVector>

#include <memory>

// 通道内部统一目录项；不向上层泄漏 FtpFileInfo 或 libssh2 类型。
struct TransferChannelEntry
{
    QString name;
    quint64 size = 0;
    QString modifiedText;
};

// AdapterTransferChannel 的内部操作表，也是无网络契约测试的注入缝。
// 它不属于 IProtocolAdapter/IDeployable 公共 ABI。
struct TransferChannelOps
{
    std::function<bool(const QString&, const QString&)> upload;
    std::function<bool(const QString&, const QString&)> download;
    std::function<bool(const QString&, const QString&)> rename;
    std::function<bool(const QString&)> remove;
    std::function<bool(const QString&, QVector<TransferChannelEntry>&)> list;
    std::function<void(std::function<void(int)>)> setProgressCallback;
    std::function<void(std::atomic_bool*)> setCancelFlag;
    std::function<bool()> isReady;
};

class AdapterTransferChannel final : public ITransferChannel
{
public:
    AdapterTransferChannel(QString protocol, std::shared_ptr<IProtocolAdapter> adapter);
    AdapterTransferChannel(QString protocol,
                           std::shared_ptr<IProtocolAdapter> adapter,
                           TransferChannelOps operations);
    ~AdapterTransferChannel() override;

    bool connect(const DeviceInfo& device, const AuthInfo& auth) override;
    bool reconnect() override;

    bool upload(const QString& localPath, const QString& remotePath) override;
    bool download(const QString& remotePath, const QString& localPath) override;
    bool stat(const QString& path, TransferFileStat& out) override;
    bool rename(const QString& from, const QString& to) override;
    bool remove(const QString& path) override;

    void setProgressCallback(std::function<void(int)> callback) override;
    void setCancelFlag(std::atomic_bool* flag) override;

    TransferCapabilities capabilities() const override;
    TransferError lastError() const override;
    void clearCredentials() override;

private:
    static TransferChannelOps bindOperations(const QString& protocol,
                                             const std::shared_ptr<IProtocolAdapter>& adapter);
    bool finishConnect(bool connected, const QString& fallbackError);
    bool finishOperation(bool success, const QString& fallbackError);
    bool failUnsupported(const QString& operation);

    QString m_protocol;
    std::shared_ptr<IProtocolAdapter> m_adapter;
    TransferChannelOps m_operations;
    DeviceInfo m_device;
    AuthInfo m_auth;
    bool m_hasConnectionParameters = false;
    TransferError m_lastError;
};
