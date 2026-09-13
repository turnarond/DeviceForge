// ITransferChannel.h — 可靠传输内核使用的协议无关文件通道

#pragma once

#include "framework/DeviceInfo.h"
#include "transfer/TransferTypes.h"

#include <QString>
#include <QtGlobal>

#include <atomic>
#include <functional>

struct TransferCapabilities
{
    bool upload = false;
    bool download = false;
    bool rename = false;
    bool remove = false;
    bool stat = false;
};

struct TransferFileStat
{
    bool exists = false;
    quint64 size = 0;
    QString modifiedText;
};

class ITransferChannel
{
public:
    virtual ~ITransferChannel() = default;

    virtual bool connect(const DeviceInfo& device, const AuthInfo& auth) = 0;
    virtual bool reconnect() = 0;

    virtual bool upload(const QString& localPath, const QString& remotePath) = 0;
    virtual bool download(const QString& remotePath, const QString& localPath) = 0;
    virtual bool stat(const QString& path, TransferFileStat& out) = 0;
    virtual bool rename(const QString& from, const QString& to) = 0;
    virtual bool remove(const QString& path) = 0;

    virtual void setProgressCallback(std::function<void(int)> callback) = 0;
    virtual void setCancelFlag(std::atomic_bool* flag) = 0;

    virtual TransferCapabilities capabilities() const = 0;
    virtual TransferError lastError() const = 0;

    // 清除通道为重连缓存的认证副本。默认实现保持测试/其他内部通道兼容；
    // AdapterTransferChannel 会覆盖并安全擦除实际缓存。
    virtual void clearCredentials() {}
};
