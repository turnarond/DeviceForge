// LocalTransferChannel.h — Task 5 内部本地文件可靠传输通道

#pragma once

#include "transfer/ITransferChannel.h"

class LocalTransferChannel final : public ITransferChannel
{
public:
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

private:
    bool copyFile(const QString& sourcePath, const QString& targetPath);
    void setError(TransferErrorCode code, const QString& message);

    std::function<void(int)> m_progress;
    std::atomic_bool* m_cancel = nullptr;
    TransferError m_error;
};
