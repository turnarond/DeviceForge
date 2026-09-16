// LocalPanelTransfer.h — 双栏本地传输的共享入口
#pragma once

#include "transfer/LocalTransferChannel.h"
#include "transfer/TransferExecutor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <filesystem>

inline std::filesystem::path localFilesystemPath(const QString& path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(QFile::encodeName(path).constData());
#endif
}

// 预检和 F6 清理前共享此检查；不缓存文件身份，便于发现提交期间的路径变化。
inline bool localPathsEquivalent(const QString& source, const QString& target)
{
    const QFileInfo sourceInfo(source);
    const QFileInfo targetInfo(target);
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    if (QString::compare(QDir::cleanPath(sourceInfo.absoluteFilePath()),
                         QDir::cleanPath(targetInfo.absoluteFilePath()), sensitivity) == 0)
        return true;
    const QString canonicalSource = sourceInfo.canonicalFilePath();
    const QString canonicalTarget = targetInfo.canonicalFilePath();
    if (!canonicalSource.isEmpty() && !canonicalTarget.isEmpty()
        && QString::compare(canonicalSource, canonicalTarget, sensitivity) == 0)
        return true;
    std::error_code error;
    return std::filesystem::equivalent(localFilesystemPath(source),
                                       localFilesystemPath(target), error) && !error;
}

inline TransferItemResult executeLocalPanelTransfer(
    const TransferItemRequest& item, std::atomic_bool& cancel,
    const std::function<void(int)>& progress = {})
{
    if (localPathsEquivalent(item.remotePath, item.localPath)) {
        TransferItemResult result;
        result.error = {TransferErrorCode::InvalidPath,
                        QStringLiteral("源和目标指向同一个本地文件，已拒绝复制或移动"),
                        {}, false};
        return result;
    }
    LocalTransferChannel channel;
    channel.setProgressCallback(progress);
    TransferExecutor executor(channel);
    return executor.execute(item, cancel);
}
