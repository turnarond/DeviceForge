// TransferExecutor.cpp — prepare → transfer → verify → commit 与重试收口

#include "transfer/TransferExecutor.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QUuid>

#include <algorithm>
#include <cstdio>
#include <utility>

#ifdef Q_OS_WIN
#  define NOMINMAX
#  include <windows.h>
#endif

namespace {

constexpr int kCancellationPollMs = 50;

class ExecutionCleanup
{
public:
    ExecutionCleanup(ITransferChannel& channel, AuthInfo* credentials)
        : m_channel(channel), m_credentials(credentials)
    {
    }

    ~ExecutionCleanup()
    {
        m_channel.setCancelFlag(nullptr);
        m_channel.clearCredentials();
        if (m_credentials)
            m_credentials->clear();
    }

private:
    ITransferChannel& m_channel;
    AuthInfo* m_credentials;
};

QString temporaryPathFor(const QString& targetPath)
{
    return targetPath + QStringLiteral(".deviceforge-part-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

TransferError makeError(TransferErrorCode code, const QString& message)
{
    return {code, message, message, false};
}

TransferItemResult failedResult(TransferError error, int attempts, bool nonAtomic = false)
{
    return {TransferState::Failed, std::move(error), attempts, nonAtomic, 0};
}

TransferItemResult cancelledResult(int attempts, bool nonAtomic = false)
{
    return {TransferState::Cancelled,
            makeError(TransferErrorCode::Cancelled, QStringLiteral("传输已取消")),
            attempts,
            nonAtomic,
            0};
}

TransferItemResult targetChangedResult(int attempts, bool nonAtomic = false)
{
    return {TransferState::NeedsAttention,
            makeError(TransferErrorCode::TargetChanged,
                      QStringLiteral("目标在传输期间发生变化，已停止提交")),
            attempts,
            nonAtomic,
            0};
}

TransferFileStat localStat(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return {};
    return {true,
            static_cast<quint64>(info.size()),
            QString::number(info.lastModified().toMSecsSinceEpoch())};
}

bool sameTarget(const TransferFileStat& baseline, const TransferFileStat& current)
{
    if (baseline.exists != current.exists)
        return false;
    if (!baseline.exists)
        return true;
    if (baseline.size != current.size)
        return false;
    return baseline.modifiedText.isEmpty() || current.modifiedText.isEmpty()
        || baseline.modifiedText == current.modifiedText;
}

void appendCleanupFailure(TransferError& rootError, const QString& cleanupDetail)
{
    if (cleanupDetail.isEmpty())
        return;
    if (!rootError.detail.isEmpty())
        rootError.detail += u'\n';
    rootError.detail += QStringLiteral("临时文件清理失败：") + cleanupDetail;
}

void cleanupRemoteTemporary(ITransferChannel& channel,
                            const TransferCapabilities& capabilities,
                            const QString& temporaryPath,
                            TransferError& rootError)
{
    if (!capabilities.remove) {
        appendCleanupFailure(rootError, QStringLiteral("通道不支持删除"));
        return;
    }
    if (!channel.remove(temporaryPath)) {
        const TransferError cleanupError = channel.lastError();
        appendCleanupFailure(rootError,
                             cleanupError.detail.isEmpty() ? cleanupError.message
                                                           : cleanupError.detail);
    }
}

void cleanupLocalTemporary(const QString& temporaryPath, TransferError& rootError)
{
    if (!QFileInfo::exists(temporaryPath))
        return;
    if (!QFile::remove(temporaryPath))
        appendCleanupFailure(rootError, QStringLiteral("无法删除本地临时文件"));
}

bool replaceLocalFile(const QString& temporaryPath, const QString& targetPath)
{
#ifdef Q_OS_WIN
    return MoveFileExW(reinterpret_cast<LPCWSTR>(temporaryPath.utf16()),
                       reinterpret_cast<LPCWSTR>(targetPath.utf16()),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
        != FALSE;
#else
    const QByteArray temporaryName = QFile::encodeName(temporaryPath);
    const QByteArray targetName = QFile::encodeName(targetPath);
    return std::rename(temporaryName.constData(), targetName.constData()) == 0;
#endif
}

TransferError channelErrorOrFallback(ITransferChannel& channel, const QString& fallback)
{
    TransferError error = channel.lastError();
    if (error.code == TransferErrorCode::None)
        error = makeError(TransferErrorCode::RemoteIo, fallback);
    return error;
}

} // namespace

TransferExecutor::TransferExecutor(ITransferChannel& channel, Sleeper sleeper)
    : m_channel(channel), m_sleeper(std::move(sleeper))
{
}

TransferExecutor::TransferExecutor(ITransferChannel& channel,
                                   DeviceInfo device,
                                   AuthInfo& credentials,
                                   Sleeper sleeper)
    : m_channel(channel)
    , m_sleeper(std::move(sleeper))
    , m_device(std::move(device))
    , m_credentials(&credentials)
{
}

TransferItemResult TransferExecutor::execute(const TransferItemRequest& request,
                                             std::atomic_bool& cancel)
{
    m_channel.setCancelFlag(&cancel);
    ExecutionCleanup cleanup(m_channel, m_credentials);

    if (cancel.load())
        return cancelledResult(0);

    if (request.localPath.isEmpty() || request.remotePath.isEmpty())
        return failedResult(makeError(TransferErrorCode::InvalidPath,
                                      QStringLiteral("传输源路径和目标路径不能为空")), 0);

    int firstAttemptIndex = 0;
    if (m_credentials) {
        const QVector<int> delays = retryDelaysMs();
        bool connected = false;
        for (int attemptIndex = 0; attemptIndex <= delays.size(); ++attemptIndex) {
            if (attemptIndex > 0
                && !waitBeforeRetry(delays.at(attemptIndex - 1), cancel))
                return cancelledResult(attemptIndex);

            const bool connectionReady = attemptIndex == 0
                ? m_channel.connect(m_device, *m_credentials)
                : m_channel.reconnect();
            if (connectionReady) {
                firstAttemptIndex = attemptIndex;
                connected = true;
                break;
            }

            TransferError connectionError =
                channelErrorOrFallback(m_channel, QStringLiteral("连接失败"));
            if (cancel.load())
                return cancelledResult(attemptIndex + 1);
            if (!connectionError.retryable || attemptIndex == delays.size())
                return failedResult(std::move(connectionError), attemptIndex + 1);
        }
        if (!connected)
            return failedResult(makeError(TransferErrorCode::RemoteIo,
                                          QStringLiteral("连接重试次数已耗尽")),
                                retryDelaysMs().size() + 1);
    }

    if (cancel.load())
        return cancelledResult(0);

    const TransferCapabilities capabilities = m_channel.capabilities();
    return request.direction == TransferDirection::Upload
        ? executeUpload(request, capabilities, cancel, firstAttemptIndex)
        : executeDownload(request, capabilities, cancel, firstAttemptIndex);
}

TransferItemResult TransferExecutor::executeUpload(const TransferItemRequest& request,
                                                   const TransferCapabilities& capabilities,
                                                   std::atomic_bool& cancel,
                                                   int firstAttemptIndex)
{
    if (!capabilities.upload)
        return failedResult(makeError(TransferErrorCode::Unsupported,
                                      QStringLiteral("通道不支持上传")), 0);
    if (!capabilities.stat)
        return failedResult(makeError(TransferErrorCode::Unsupported,
                                      QStringLiteral("通道不支持上传校验")), 0);

    const QFileInfo sourceInfo(request.localPath);
    if (!sourceInfo.exists() || !sourceInfo.isFile())
        return failedResult(makeError(TransferErrorCode::LocalIo,
                                      QStringLiteral("本地源文件不存在或不是普通文件")), 0);
    const qint64 sourceSize = sourceInfo.size();

    TransferFileStat targetBaseline;
    if (!m_channel.stat(request.remotePath, targetBaseline))
        return failedResult(channelErrorOrFallback(m_channel, QStringLiteral("读取目标元数据失败")),
                            m_credentials ? firstAttemptIndex + 1 : 0);
    if (request.overwrite == OverwritePolicy::Skip && targetBaseline.exists)
        return {TransferState::Succeeded, {}, 0, false, 0};

    const bool atomicCommit = capabilities.rename;
    const QString transferPath = atomicCommit ? temporaryPathFor(request.remotePath)
                                              : request.remotePath;
    const QVector<int> delays = retryDelaysMs();

    for (int attemptIndex = firstAttemptIndex; attemptIndex <= delays.size(); ++attemptIndex) {
        if (attemptIndex > firstAttemptIndex) {
            if (!waitBeforeRetry(delays.at(attemptIndex - 1), cancel)) {
                TransferError cancellation =
                    makeError(TransferErrorCode::Cancelled, QStringLiteral("传输已取消"));
                if (atomicCommit)
                    cleanupRemoteTemporary(m_channel, capabilities, transferPath, cancellation);
                return cancelledResult(attemptIndex, !atomicCommit);
            }
            if (!m_channel.reconnect()) {
                TransferError reconnectError =
                    channelErrorOrFallback(m_channel, QStringLiteral("重连失败"));
                if (!reconnectError.retryable || attemptIndex == delays.size())
                    return failedResult(std::move(reconnectError), attemptIndex, !atomicCommit);
                continue;
            }
        }

        if (cancel.load()) {
            TransferError cancellation =
                makeError(TransferErrorCode::Cancelled, QStringLiteral("传输已取消"));
            if (atomicCommit)
                cleanupRemoteTemporary(m_channel, capabilities, transferPath, cancellation);
            return cancelledResult(attemptIndex, !atomicCommit);
        }

        if (!atomicCommit && attemptIndex == 0) {
            TransferFileStat currentTarget;
            if (!m_channel.stat(request.remotePath, currentTarget))
                return failedResult(
                    channelErrorOrFallback(m_channel, QStringLiteral("复核目标元数据失败")),
                    0,
                    true);
            if (!sameTarget(targetBaseline, currentTarget))
                return targetChangedResult(0, true);
        }

        const int attempts = attemptIndex + 1;
        if (!m_channel.upload(request.localPath, transferPath)) {
            TransferError transferError =
                channelErrorOrFallback(m_channel, QStringLiteral("上传失败"));
            if (atomicCommit)
                cleanupRemoteTemporary(m_channel, capabilities, transferPath, transferError);
            if (cancel.load())
                return cancelledResult(attempts, !atomicCommit);
            if (transferError.retryable && attemptIndex < delays.size())
                continue;
            return failedResult(std::move(transferError), attempts, !atomicCommit);
        }

        if (cancel.load()) {
            TransferError cancellation =
                makeError(TransferErrorCode::Cancelled, QStringLiteral("传输已取消"));
            if (atomicCommit)
                cleanupRemoteTemporary(m_channel, capabilities, transferPath, cancellation);
            return cancelledResult(attempts, !atomicCommit);
        }

        TransferFileStat transferred;
        if (!m_channel.stat(transferPath, transferred)) {
            TransferError verifyError =
                channelErrorOrFallback(m_channel, QStringLiteral("读取上传结果失败"));
            if (atomicCommit)
                cleanupRemoteTemporary(m_channel, capabilities, transferPath, verifyError);
            if (verifyError.retryable && attemptIndex < delays.size())
                continue;
            return failedResult(std::move(verifyError), attempts, !atomicCommit);
        }
        if (!transferred.exists || transferred.size != static_cast<quint64>(sourceSize)) {
            TransferError verifyError = makeError(TransferErrorCode::RemoteIo,
                                                  QStringLiteral("上传字节数校验失败"));
            if (atomicCommit)
                cleanupRemoteTemporary(m_channel, capabilities, transferPath, verifyError);
            return failedResult(std::move(verifyError), attempts, !atomicCommit);
        }

        if (!atomicCommit)
            return {TransferState::Succeeded, {}, attempts, true, sourceSize};

        TransferFileStat currentTarget;
        if (!m_channel.stat(request.remotePath, currentTarget)) {
            TransferError verifyError =
                channelErrorOrFallback(m_channel, QStringLiteral("复核目标元数据失败"));
            cleanupRemoteTemporary(m_channel, capabilities, transferPath, verifyError);
            if (verifyError.retryable && attemptIndex < delays.size())
                continue;
            return failedResult(std::move(verifyError), attempts);
        }
        if (!sameTarget(targetBaseline, currentTarget)) {
            TransferError changed = makeError(TransferErrorCode::TargetChanged,
                                              QStringLiteral("目标在传输期间发生变化"));
            cleanupRemoteTemporary(m_channel, capabilities, transferPath, changed);
            TransferItemResult result = targetChangedResult(attempts);
            result.error.detail = changed.detail;
            return result;
        }

        if (!m_channel.rename(transferPath, request.remotePath)) {
            TransferError commitError =
                channelErrorOrFallback(m_channel, QStringLiteral("原子提交失败"));
            cleanupRemoteTemporary(m_channel, capabilities, transferPath, commitError);
            if (cancel.load())
                return cancelledResult(attempts);
            if (commitError.retryable && attemptIndex < delays.size())
                continue;
            return failedResult(std::move(commitError), attempts);
        }

        return {TransferState::Succeeded, {}, attempts, false, sourceSize};
    }

    return failedResult(makeError(TransferErrorCode::RemoteIo,
                                  QStringLiteral("上传重试次数已耗尽")),
                        delays.size() + 1,
                        !atomicCommit);
}

TransferItemResult TransferExecutor::executeDownload(const TransferItemRequest& request,
                                                     const TransferCapabilities& capabilities,
                                                     std::atomic_bool& cancel,
                                                     int firstAttemptIndex)
{
    if (!capabilities.download)
        return failedResult(makeError(TransferErrorCode::Unsupported,
                                      QStringLiteral("通道不支持下载")), 0);
    if (!capabilities.stat)
        return failedResult(makeError(TransferErrorCode::Unsupported,
                                      QStringLiteral("通道不支持下载校验")), 0);

    TransferFileStat sourceStat;
    if (!m_channel.stat(request.remotePath, sourceStat))
        return failedResult(channelErrorOrFallback(m_channel, QStringLiteral("读取远端源元数据失败")),
                            m_credentials ? firstAttemptIndex + 1 : 0);
    if (!sourceStat.exists)
        return failedResult(makeError(TransferErrorCode::InvalidPath,
                                      QStringLiteral("远端源文件不存在")), 0);

    const TransferFileStat targetBaseline = localStat(request.localPath);
    if (request.overwrite == OverwritePolicy::Skip && targetBaseline.exists)
        return {TransferState::Succeeded, {}, 0, false, 0};

    const QString temporaryPath = temporaryPathFor(request.localPath);
    const QVector<int> delays = retryDelaysMs();

    for (int attemptIndex = firstAttemptIndex; attemptIndex <= delays.size(); ++attemptIndex) {
        if (attemptIndex > firstAttemptIndex) {
            if (!waitBeforeRetry(delays.at(attemptIndex - 1), cancel)) {
                TransferError cancellation =
                    makeError(TransferErrorCode::Cancelled, QStringLiteral("传输已取消"));
                cleanupLocalTemporary(temporaryPath, cancellation);
                return cancelledResult(attemptIndex);
            }
            if (!m_channel.reconnect()) {
                TransferError reconnectError =
                    channelErrorOrFallback(m_channel, QStringLiteral("重连失败"));
                if (!reconnectError.retryable || attemptIndex == delays.size())
                    return failedResult(std::move(reconnectError), attemptIndex);
                continue;
            }
        }

        if (cancel.load()) {
            TransferError cancellation =
                makeError(TransferErrorCode::Cancelled, QStringLiteral("传输已取消"));
            cleanupLocalTemporary(temporaryPath, cancellation);
            return cancelledResult(attemptIndex);
        }

        QFile::remove(temporaryPath);
        const int attempts = attemptIndex + 1;
        if (!m_channel.download(request.remotePath, temporaryPath)) {
            TransferError transferError =
                channelErrorOrFallback(m_channel, QStringLiteral("下载失败"));
            cleanupLocalTemporary(temporaryPath, transferError);
            if (cancel.load())
                return cancelledResult(attempts);
            if (transferError.retryable && attemptIndex < delays.size())
                continue;
            return failedResult(std::move(transferError), attempts);
        }

        if (cancel.load()) {
            TransferError cancellation =
                makeError(TransferErrorCode::Cancelled, QStringLiteral("传输已取消"));
            cleanupLocalTemporary(temporaryPath, cancellation);
            return cancelledResult(attempts);
        }

        const QFileInfo downloaded(temporaryPath);
        if (!downloaded.exists()
            || static_cast<quint64>(downloaded.size()) != sourceStat.size) {
            TransferError verifyError = makeError(TransferErrorCode::LocalIo,
                                                  QStringLiteral("下载字节数校验失败"));
            cleanupLocalTemporary(temporaryPath, verifyError);
            return failedResult(std::move(verifyError), attempts);
        }

        if (!sameTarget(targetBaseline, localStat(request.localPath))) {
            TransferError changed = makeError(TransferErrorCode::TargetChanged,
                                              QStringLiteral("目标在传输期间发生变化"));
            cleanupLocalTemporary(temporaryPath, changed);
            TransferItemResult result = targetChangedResult(attempts);
            result.error.detail = changed.detail;
            return result;
        }

        if (!replaceLocalFile(temporaryPath, request.localPath)) {
            TransferError commitError = makeError(TransferErrorCode::LocalIo,
                                                  QStringLiteral("本地原子提交失败"));
            cleanupLocalTemporary(temporaryPath, commitError);
            return failedResult(std::move(commitError), attempts);
        }

        return {TransferState::Succeeded,
                {},
                attempts,
                false,
                static_cast<qint64>(sourceStat.size)};
    }

    return failedResult(makeError(TransferErrorCode::RemoteIo,
                                  QStringLiteral("下载重试次数已耗尽")),
                        delays.size() + 1);
}

bool TransferExecutor::waitBeforeRetry(int milliseconds, std::atomic_bool& cancel) const
{
    if (m_sleeper) {
        m_sleeper(milliseconds);
        return !cancel.load();
    }

    int remaining = milliseconds;
    while (remaining > 0 && !cancel.load()) {
        const int slice = std::min(remaining, kCancellationPollMs);
        QThread::msleep(static_cast<unsigned long>(slice));
        remaining -= slice;
    }
    return !cancel.load();
}
