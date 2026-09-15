// TransferExecutor.cpp — prepare → transfer → verify → commit 与重试收口

#include "transfer/TransferExecutor.h"

#include <QCryptographicHash>
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
constexpr qint64 kHashBlockSize = 64 * 1024;

#ifdef Q_OS_WIN
constexpr bool kLocalCommitNonAtomic = false;
#else
// POSIX rename 本身具备同文件系统原子替换语义，但当前实现没有 Windows
// MOVEFILE_WRITE_THROUGH 对应的持久化保证，因此按现有结果契约显式标记降级。
constexpr bool kLocalCommitNonAtomic = true;
#endif

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

struct LocalFileSnapshot
{
    bool exists = false;
    bool readable = true;
    quint64 size = 0;
    qint64 modifiedMs = 0;
    QByteArray sha256;
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

TransferItemResult cancelledResult(TransferError error,
                                   int attempts,
                                   bool nonAtomic = false)
{
    return {TransferState::Cancelled, std::move(error), attempts, nonAtomic, 0};
}

TransferItemResult cancelledResult(int attempts, bool nonAtomic = false)
{
    return cancelledResult(
        makeError(TransferErrorCode::Cancelled, QStringLiteral("传输已取消")),
        attempts,
        nonAtomic);
}

TransferItemResult needsAttentionResult(TransferError error,
                                        int attempts,
                                        bool nonAtomic = false)
{
    return {TransferState::NeedsAttention, std::move(error), attempts, nonAtomic, 0};
}

TransferError targetChangedError(const QString& subject)
{
    return makeError(TransferErrorCode::TargetChanged,
                     QStringLiteral("%1在传输期间发生变化，已停止提交").arg(subject));
}

LocalFileSnapshot localSnapshot(const QString& path)
{
    const QFileInfo before(path);
    if (!before.exists())
        return {};
    if (!before.isFile())
        return {true, false, static_cast<quint64>(before.size()),
                before.lastModified().toMSecsSinceEpoch(), {}};

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {true, false, static_cast<quint64>(before.size()),
                before.lastModified().toMSecsSinceEpoch(), {}};

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray block = file.read(kHashBlockSize);
        if (block.isEmpty() && file.error() != QFileDevice::NoError)
            return {true, false, static_cast<quint64>(before.size()),
                    before.lastModified().toMSecsSinceEpoch(), {}};
        hash.addData(block);
    }

    const QFileInfo after(path);
    if (!after.exists() || !after.isFile() || after.size() != before.size()
        || after.lastModified() != before.lastModified())
        return {after.exists(), false, static_cast<quint64>(std::max<qint64>(after.size(), 0)),
                after.lastModified().toMSecsSinceEpoch(), {}};

    return {true,
            true,
            static_cast<quint64>(after.size()),
            after.lastModified().toMSecsSinceEpoch(),
            hash.result()};
}

bool sameLocalFile(const LocalFileSnapshot& baseline, const LocalFileSnapshot& current)
{
    if (baseline.exists != current.exists)
        return false;
    if (!baseline.exists)
        return true;
    return baseline.readable && current.readable && baseline.size == current.size
        && baseline.modifiedMs == current.modifiedMs && baseline.sha256 == current.sha256;
}

bool sameRemoteTarget(const TransferFileStat& baseline, const TransferFileStat& current)
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

bool cleanupRemoteTemporary(ITransferChannel& channel,
                            const TransferCapabilities& capabilities,
                            const QString& temporaryPath,
                            TransferError& rootError)
{
    if (!capabilities.remove) {
        appendCleanupFailure(rootError, QStringLiteral("通道不支持删除"));
        return false;
    }
    if (!channel.remove(temporaryPath)) {
        const TransferError cleanupError = channel.lastError();
        appendCleanupFailure(rootError,
                             cleanupError.detail.isEmpty() ? cleanupError.message
                                                           : cleanupError.detail);
        return false;
    }
    return true;
}

bool cleanupLocalTemporary(const QString& temporaryPath, TransferError& rootError)
{
    if (!QFileInfo::exists(temporaryPath))
        return true;
    if (!QFile::remove(temporaryPath)) {
        appendCleanupFailure(rootError, QStringLiteral("无法删除本地临时文件"));
        return false;
    }
    return true;
}

TransferItemResult cancelWithRemoteCleanup(ITransferChannel& channel,
                                           const TransferCapabilities& capabilities,
                                           const QString& temporaryPath,
                                           int attempts,
                                           bool atomicCommit)
{
    TransferError error =
        makeError(TransferErrorCode::Cancelled, QStringLiteral("传输已取消"));
    if (atomicCommit)
        cleanupRemoteTemporary(channel, capabilities, temporaryPath, error);
    return cancelledResult(std::move(error), attempts, !atomicCommit);
}

TransferItemResult cancelWithLocalCleanup(const QString& temporaryPath, int attempts)
{
    TransferError error =
        makeError(TransferErrorCode::Cancelled, QStringLiteral("传输已取消"));
    cleanupLocalTemporary(temporaryPath, error);
    return cancelledResult(std::move(error), attempts);
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
            if (cancel.load())
                return cancelledResult(attemptIndex);

            const bool connectionReady = attemptIndex == 0
                ? m_channel.connect(m_device, *m_credentials)
                : m_channel.reconnect();
            if (cancel.load())
                return cancelledResult(attemptIndex + 1);
            if (connectionReady) {
                firstAttemptIndex = attemptIndex;
                connected = true;
                break;
            }

            TransferError connectionError =
                channelErrorOrFallback(m_channel, QStringLiteral("连接失败"));
            if (!connectionError.retryable || attemptIndex == delays.size())
                return failedResult(std::move(connectionError), attemptIndex + 1);
        }
        if (!connected)
            return failedResult(makeError(TransferErrorCode::RemoteIo,
                                          QStringLiteral("连接重试次数已耗尽")),
                                retryDelaysMs().size() + 1);
    }

    if (cancel.load())
        return cancelledResult(firstAttemptIndex + (m_credentials ? 1 : 0));

    const TransferCapabilities capabilities = m_channel.capabilities();
    return request.direction == TransferDirection::Upload
        ? executeUpload(request, capabilities, cancel, firstAttemptIndex)
        : executeDownload(request, capabilities, cancel, firstAttemptIndex);
}

bool TransferExecutor::prepareRemoteStat(const QString& path,
                                         TransferFileStat& stat,
                                         std::atomic_bool& cancel,
                                         int& attemptIndex,
                                         TransferItemResult& terminalResult)
{
    const QVector<int> delays = retryDelaysMs();
    const int firstAttemptIndex = attemptIndex;

    for (int currentAttempt = firstAttemptIndex; currentAttempt <= delays.size();
         ++currentAttempt) {
        if (currentAttempt > firstAttemptIndex) {
            if (!waitBeforeRetry(delays.at(currentAttempt - 1), cancel)) {
                terminalResult = cancelledResult(currentAttempt);
                return false;
            }
            if (cancel.load()) {
                terminalResult = cancelledResult(currentAttempt);
                return false;
            }
            const bool reconnected = m_channel.reconnect();
            if (cancel.load()) {
                terminalResult = cancelledResult(currentAttempt + 1);
                return false;
            }
            if (!reconnected) {
                TransferError reconnectError =
                    channelErrorOrFallback(m_channel, QStringLiteral("重连失败"));
                if (!reconnectError.retryable || currentAttempt == delays.size()) {
                    terminalResult = failedResult(std::move(reconnectError),
                                                  currentAttempt + 1);
                    return false;
                }
                continue;
            }
        }

        if (cancel.load()) {
            terminalResult = cancelledResult(currentAttempt);
            return false;
        }
        const bool statSucceeded = m_channel.stat(path, stat);
        TransferError statError;
        if (!statSucceeded)
            statError = channelErrorOrFallback(m_channel, QStringLiteral("读取远端元数据失败"));
        if (cancel.load()) {
            terminalResult = cancelledResult(currentAttempt + 1);
            return false;
        }
        if (statSucceeded) {
            attemptIndex = currentAttempt;
            return true;
        }
        if (!statError.retryable || currentAttempt == delays.size()) {
            terminalResult = failedResult(std::move(statError), currentAttempt + 1);
            return false;
        }
    }

    terminalResult = failedResult(makeError(TransferErrorCode::RemoteIo,
                                            QStringLiteral("远端元数据重试次数已耗尽")),
                                  delays.size() + 1);
    return false;
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

    if (cancel.load())
        return cancelledResult(firstAttemptIndex);
    const LocalFileSnapshot sourceBaseline = localSnapshot(request.localPath);
    if (cancel.load())
        return cancelledResult(firstAttemptIndex);
    if (!sourceBaseline.exists || !sourceBaseline.readable)
        return failedResult(makeError(TransferErrorCode::LocalIo,
                                      QStringLiteral("本地源文件不存在、不是普通文件或无法读取")),
                            firstAttemptIndex);

    int preparedAttemptIndex = firstAttemptIndex;
    TransferFileStat targetBaseline;
    TransferItemResult prepareFailure;
    if (!prepareRemoteStat(request.remotePath,
                           targetBaseline,
                           cancel,
                           preparedAttemptIndex,
                           prepareFailure))
        return prepareFailure;
    if (request.overwrite == OverwritePolicy::Skip && targetBaseline.exists)
        return {TransferState::Succeeded, {}, 0, false, 0, false, true};

    const bool atomicCommit = capabilities.rename;
    const QString transferPath = atomicCommit ? temporaryPathFor(request.remotePath)
                                              : request.remotePath;

    if (!atomicCommit) {
        TransferFileStat currentTarget;
        if (!prepareRemoteStat(request.remotePath,
                               currentTarget,
                               cancel,
                               preparedAttemptIndex,
                               prepareFailure))
            return prepareFailure;
        if (!sameRemoteTarget(targetBaseline, currentTarget))
            return needsAttentionResult(targetChangedError(QStringLiteral("远端目标")),
                                        preparedAttemptIndex,
                                        true);
    }

    const QVector<int> delays = retryDelaysMs();
    const int firstTransferAttempt = preparedAttemptIndex;
    for (int attemptIndex = firstTransferAttempt; attemptIndex <= delays.size(); ++attemptIndex) {
        if (attemptIndex > firstTransferAttempt) {
            if (!waitBeforeRetry(delays.at(attemptIndex - 1), cancel))
                return cancelledResult(attemptIndex, !atomicCommit);
            if (cancel.load())
                return cancelledResult(attemptIndex, !atomicCommit);
            const bool reconnected = m_channel.reconnect();
            TransferError reconnectError;
            if (!reconnected)
                reconnectError = channelErrorOrFallback(m_channel, QStringLiteral("重连失败"));
            if (cancel.load())
                return cancelledResult(attemptIndex + 1, !atomicCommit);
            if (!reconnected) {
                if (!reconnectError.retryable || attemptIndex == delays.size())
                    return failedResult(std::move(reconnectError), attemptIndex + 1,
                                        !atomicCommit);
                continue;
            }
        }

        if (cancel.load())
            return cancelledResult(attemptIndex, !atomicCommit);

        const int attempts = attemptIndex + 1;
        const bool uploadSucceeded = m_channel.upload(request.localPath, transferPath);
        TransferError uploadError;
        if (!uploadSucceeded)
            uploadError = channelErrorOrFallback(m_channel, QStringLiteral("上传失败"));
        if (cancel.load())
            return cancelWithRemoteCleanup(m_channel, capabilities, transferPath,
                                           attempts, atomicCommit);
        if (!uploadSucceeded) {
            bool cleanupSucceeded = true;
            if (atomicCommit)
                cleanupSucceeded = cleanupRemoteTemporary(
                    m_channel, capabilities, transferPath, uploadError);
            if (!cleanupSucceeded)
                return failedResult(std::move(uploadError), attempts, !atomicCommit);
            if (uploadError.retryable && attemptIndex < delays.size())
                continue;
            return failedResult(std::move(uploadError), attempts, !atomicCommit);
        }

        if (cancel.load())
            return cancelWithRemoteCleanup(m_channel, capabilities, transferPath,
                                           attempts, atomicCommit);
        TransferFileStat transferred;
        const bool transferStatSucceeded = m_channel.stat(transferPath, transferred);
        TransferError verifyError;
        if (!transferStatSucceeded)
            verifyError = channelErrorOrFallback(m_channel, QStringLiteral("读取上传结果失败"));
        if (cancel.load())
            return cancelWithRemoteCleanup(m_channel, capabilities, transferPath,
                                           attempts, atomicCommit);
        if (!transferStatSucceeded) {
            bool cleanupSucceeded = true;
            if (atomicCommit)
                cleanupSucceeded = cleanupRemoteTemporary(
                    m_channel, capabilities, transferPath, verifyError);
            if (!cleanupSucceeded)
                return failedResult(std::move(verifyError), attempts, !atomicCommit);
            if (verifyError.retryable && attemptIndex < delays.size())
                continue;
            return failedResult(std::move(verifyError), attempts, !atomicCommit);
        }
        if (!transferred.exists || transferred.size != sourceBaseline.size) {
            verifyError = makeError(TransferErrorCode::RemoteIo,
                                    QStringLiteral("上传字节数校验失败"));
            if (atomicCommit)
                cleanupRemoteTemporary(m_channel, capabilities, transferPath, verifyError);
            return failedResult(std::move(verifyError), attempts, !atomicCommit);
        }

        if (!atomicCommit) {
            if (cancel.load())
                return cancelWithRemoteCleanup(m_channel, capabilities, transferPath,
                                               attempts, false);
            const LocalFileSnapshot currentSource = localSnapshot(request.localPath);
            if (cancel.load())
                return cancelWithRemoteCleanup(m_channel, capabilities, transferPath,
                                               attempts, false);
            if (!sameLocalFile(sourceBaseline, currentSource)) {
                return needsAttentionResult(
                    targetChangedError(QStringLiteral("本地源文件")), attempts, true);
            }
            return {TransferState::Succeeded, {}, attempts, true,
                    static_cast<qint64>(sourceBaseline.size)};
        }

        if (cancel.load())
            return cancelWithRemoteCleanup(m_channel, capabilities, transferPath,
                                           attempts, true);
        TransferFileStat currentTarget;
        const bool targetStatSucceeded = m_channel.stat(request.remotePath, currentTarget);
        TransferError targetStatError;
        if (!targetStatSucceeded)
            targetStatError =
                channelErrorOrFallback(m_channel, QStringLiteral("复核目标元数据失败"));
        if (cancel.load())
            return cancelWithRemoteCleanup(m_channel, capabilities, transferPath,
                                           attempts, true);
        if (!targetStatSucceeded) {
            const bool cleanupSucceeded = cleanupRemoteTemporary(
                m_channel, capabilities, transferPath, targetStatError);
            if (!cleanupSucceeded)
                return failedResult(std::move(targetStatError), attempts);
            if (targetStatError.retryable && attemptIndex < delays.size())
                continue;
            return failedResult(std::move(targetStatError), attempts);
        }
        if (!sameRemoteTarget(targetBaseline, currentTarget)) {
            TransferError changed = targetChangedError(QStringLiteral("远端目标"));
            cleanupRemoteTemporary(m_channel, capabilities, transferPath, changed);
            return needsAttentionResult(std::move(changed), attempts);
        }

        if (cancel.load())
            return cancelWithRemoteCleanup(m_channel, capabilities, transferPath,
                                           attempts, true);
        const LocalFileSnapshot currentSource = localSnapshot(request.localPath);
        if (cancel.load())
            return cancelWithRemoteCleanup(m_channel, capabilities, transferPath,
                                           attempts, true);
        if (!sameLocalFile(sourceBaseline, currentSource)) {
            TransferError changed = targetChangedError(QStringLiteral("本地源文件"));
            cleanupRemoteTemporary(m_channel, capabilities, transferPath, changed);
            return needsAttentionResult(std::move(changed), attempts);
        }

        if (cancel.load())
            return cancelWithRemoteCleanup(m_channel, capabilities, transferPath,
                                           attempts, true);
        const bool renameSucceeded = m_channel.rename(transferPath, request.remotePath);
        TransferError renameError;
        if (!renameSucceeded)
            renameError = channelErrorOrFallback(m_channel, QStringLiteral("原子提交失败"));
        if (!renameSucceeded && cancel.load())
            return cancelWithRemoteCleanup(m_channel, capabilities, transferPath,
                                           attempts, true);
        if (!renameSucceeded) {
            const bool cleanupSucceeded = cleanupRemoteTemporary(
                m_channel, capabilities, transferPath, renameError);
            if (!cleanupSucceeded)
                return failedResult(std::move(renameError), attempts);
            if (renameError.retryable && attemptIndex < delays.size())
                continue;
            return failedResult(std::move(renameError), attempts);
        }

        // rename 已成功时提交不可回退；即使此检查点刚观察到取消，也必须如实返回成功。
        (void)cancel.load();
        return {TransferState::Succeeded, {}, attempts, false,
                static_cast<qint64>(sourceBaseline.size), true};
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

    int preparedAttemptIndex = firstAttemptIndex;
    TransferFileStat sourceStat;
    TransferItemResult prepareFailure;
    if (!prepareRemoteStat(request.remotePath,
                           sourceStat,
                           cancel,
                           preparedAttemptIndex,
                           prepareFailure))
        return prepareFailure;
    if (!sourceStat.exists)
        return failedResult(makeError(TransferErrorCode::InvalidPath,
                                      QStringLiteral("远端源文件不存在")),
                            preparedAttemptIndex + 1);

    if (cancel.load())
        return cancelledResult(preparedAttemptIndex + 1);
    const LocalFileSnapshot targetBaseline = localSnapshot(request.localPath);
    if (cancel.load())
        return cancelledResult(preparedAttemptIndex + 1);
    if (targetBaseline.exists && !targetBaseline.readable)
        return failedResult(makeError(TransferErrorCode::LocalIo,
                                      QStringLiteral("本地目标不是普通文件或无法读取")),
                            preparedAttemptIndex + 1);
    if (request.overwrite == OverwritePolicy::Skip && targetBaseline.exists)
        return {TransferState::Succeeded, {}, 0, false, 0, false, true};

    const QString temporaryPath = temporaryPathFor(request.localPath);
    const QVector<int> delays = retryDelaysMs();
    const int firstTransferAttempt = preparedAttemptIndex;

    for (int attemptIndex = firstTransferAttempt; attemptIndex <= delays.size();
         ++attemptIndex) {
        if (attemptIndex > firstTransferAttempt) {
            if (!waitBeforeRetry(delays.at(attemptIndex - 1), cancel))
                return cancelledResult(attemptIndex);
            if (cancel.load())
                return cancelledResult(attemptIndex);
            const bool reconnected = m_channel.reconnect();
            TransferError reconnectError;
            if (!reconnected)
                reconnectError = channelErrorOrFallback(m_channel, QStringLiteral("重连失败"));
            if (cancel.load())
                return cancelledResult(attemptIndex + 1);
            if (!reconnected) {
                if (!reconnectError.retryable || attemptIndex == delays.size())
                    return failedResult(std::move(reconnectError), attemptIndex + 1);
                continue;
            }
        }

        if (cancel.load())
            return cancelledResult(attemptIndex);

        if (QFileInfo::exists(temporaryPath) && !QFile::remove(temporaryPath)) {
            TransferError cleanupError = makeError(TransferErrorCode::LocalIo,
                                                   QStringLiteral("无法清理旧的本地临时文件"));
            appendCleanupFailure(cleanupError, QStringLiteral("临时路径仍被占用"));
            return failedResult(std::move(cleanupError), attemptIndex);
        }

        const int attempts = attemptIndex + 1;
        const bool downloadSucceeded = m_channel.download(request.remotePath, temporaryPath);
        TransferError downloadError;
        if (!downloadSucceeded)
            downloadError = channelErrorOrFallback(m_channel, QStringLiteral("下载失败"));
        if (cancel.load())
            return cancelWithLocalCleanup(temporaryPath, attempts);
        if (!downloadSucceeded) {
            const bool cleanupSucceeded = cleanupLocalTemporary(temporaryPath, downloadError);
            if (!cleanupSucceeded)
                return failedResult(std::move(downloadError), attempts);
            if (downloadError.retryable && attemptIndex < delays.size())
                continue;
            return failedResult(std::move(downloadError), attempts);
        }

        if (cancel.load())
            return cancelWithLocalCleanup(temporaryPath, attempts);
        const QFileInfo downloaded(temporaryPath);
        if (!downloaded.exists()
            || static_cast<quint64>(downloaded.size()) != sourceStat.size) {
            TransferError verifyError = makeError(TransferErrorCode::LocalIo,
                                                  QStringLiteral("下载字节数校验失败"));
            cleanupLocalTemporary(temporaryPath, verifyError);
            return failedResult(std::move(verifyError), attempts);
        }

        if (cancel.load())
            return cancelWithLocalCleanup(temporaryPath, attempts);
        const LocalFileSnapshot currentTarget = localSnapshot(request.localPath);
        if (cancel.load())
            return cancelWithLocalCleanup(temporaryPath, attempts);
        if (!sameLocalFile(targetBaseline, currentTarget)) {
            TransferError changed = targetChangedError(QStringLiteral("本地目标"));
            cleanupLocalTemporary(temporaryPath, changed);
            return needsAttentionResult(std::move(changed), attempts);
        }

        if (cancel.load())
            return cancelWithLocalCleanup(temporaryPath, attempts);
        const bool commitSucceeded = replaceLocalFile(temporaryPath, request.localPath);
        if (!commitSucceeded) {
            TransferError commitError = makeError(TransferErrorCode::LocalIo,
                                                  QStringLiteral("本地原子提交失败"));
            cleanupLocalTemporary(temporaryPath, commitError);
            return failedResult(std::move(commitError), attempts, kLocalCommitNonAtomic);
        }

        // 提交成功后不可回退；检查取消只作为协议检查点，结果仍必须如实为成功。
        (void)cancel.load();
        return {TransferState::Succeeded,
                {},
                attempts,
                kLocalCommitNonAtomic,
                static_cast<qint64>(sourceStat.size),
                !kLocalCommitNonAtomic};
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
