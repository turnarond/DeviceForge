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
    // v2.15 波2：单层远端目录创建能力。置于聚合末尾且默认 false——既有 5 项聚合
    // 初始化零改动兼容，未实现该能力的旧通道行为向前不变。
    bool makeDirectory = false;
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

    // 创建**单层**远端目录：仅创建 remotePath 的最后一段，其父目录不存在时返回
    // 失败（逐级编排由 TransferExecutor 上传前的父目录确保步自最浅缺失层起负责）。
    // 默认实现返回 false=不支持；虚函数追加在接口末尾，旧实现类零改动即
    // ABI/行为向前兼容（AGENTS 纪律 6：SDK 场景接口改动向前兼容）。
    virtual bool makeDirectory(const QString& remotePath)
    {
        Q_UNUSED(remotePath);
        return false;
    }
};
