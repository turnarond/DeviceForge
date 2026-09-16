#pragma once

#include "adapter/IDeployable.h"
#include "transfer/AdapterTransferChannel.h"
#include "tools/FtpDeployTool/DeployJob.h"
#include <QFile>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>

// 将 Mock 的实际上传结果保存到内存远端；stat/rename 均读取这份内容。
inline DeployJob::Params::ChannelFactory memoryDeployChannelFactory(
    bool atomic = true, std::function<void()> committed = {})
{
    struct Storage { QMutex mutex; QMap<QString, QByteArray> files; };
    auto storage = std::make_shared<Storage>();
    return [storage, atomic, committed](std::shared_ptr<IProtocolAdapter> adapter) {
        auto* deploy = dynamic_cast<IDeployable*>(adapter.get());
        TransferChannelOps ops;
        ops.upload = [adapter, deploy, storage](const QString& local, const QString& remote) {
            if (!deploy->uploadFile(local.toStdString(), remote.toStdString())) return false;
            QFile source(local);
            if (!source.open(QIODevice::ReadOnly)) return false;
            const auto content = source.readAll();
            QMutexLocker lock(&storage->mutex);
            storage->files[remote] = content;
            return true;
        };
        ops.list = [storage](const QString& parent, QVector<TransferChannelEntry>& out) {
            QMutexLocker lock(&storage->mutex);
            for (auto i = storage->files.cbegin(); i != storage->files.cend(); ++i) {
                const auto slash = i.key().lastIndexOf('/');
                const auto fileParent = slash == 0 ? QStringLiteral("/")
                    : (slash < 0 ? QStringLiteral(".") : i.key().left(slash));
                if (fileParent == parent)
                    out.push_back({i.key().mid(slash + 1), quint64(i.value().size()), {}});
            }
            return true;
        };
        if (atomic) ops.rename = [storage, committed](const QString& from, const QString& to) {
            QMutexLocker lock(&storage->mutex);
            if (!storage->files.contains(from)) return false;
            storage->files[to] = storage->files.take(from);
            if (committed) committed();
            return true;
        };
        ops.remove = [storage](const QString& path) {
            QMutexLocker lock(&storage->mutex);
            storage->files.remove(path);
            return true;
        };
        ops.setProgressCallback = [deploy](auto cb) { deploy->setProgressCallback(std::move(cb)); };
        ops.setCancelFlag = [deploy](auto flag) { deploy->setCancelFlag(flag); };
        return std::make_unique<AdapterTransferChannel>(QStringLiteral("ftp"), adapter, std::move(ops));
    };
}
