// tst_transfer_executor.cpp — 单文件可靠传输、重试、取消与原子提交契约测试

#include <QtTest>

#include "transfer/TransferExecutor.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QDir>

#include <atomic>
#include <deque>
#include <functional>
#include <utility>

namespace {

TransferError timeoutError()
{
    return {TransferErrorCode::Timeout, QStringLiteral("传输超时"),
            QStringLiteral("operation timed out"), true};
}

TransferError connectionLostError()
{
    return {TransferErrorCode::ConnectionLost, QStringLiteral("连接中断"),
            QStringLiteral("connection reset by peer"), true};
}

TransferError permissionError()
{
    return {TransferErrorCode::Permission, QStringLiteral("权限不足"),
            QStringLiteral("permission denied"), false};
}

TransferError remoteIoError()
{
    return {TransferErrorCode::RemoteIo, QStringLiteral("远端写入失败"),
            QStringLiteral("remote write failed"), false};
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(data) == data.size();
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

bool setModifiedTime(const QString& path, const QDateTime& modified)
{
    QFile file(path);
    return file.open(QIODevice::ReadWrite)
        && file.setFileTime(modified, QFileDevice::FileModificationTime);
}

class MockChannel final : public ITransferChannel
{
public:
    bool connect(const DeviceInfo& device, const AuthInfo& auth) override
    {
        ++connectCalls;
        connectedDevice = device;
        connectedAuth = auth;
        return takeResult(connectResults, true);
    }

    bool reconnect() override
    {
        ++reconnectCalls;
        return takeResult(reconnectResults, true);
    }

    bool upload(const QString& localPath, const QString& remotePath) override
    {
        ++uploadCalls;
        uploadSources.push_back(localPath);
        uploadTargets.push_back(remotePath);
        const bool result = takeResult(uploadResults, true);
        if (result && onUpload)
            onUpload();
        return result;
    }

    bool download(const QString& remotePath, const QString& localPath) override
    {
        ++downloadCalls;
        downloadSources.push_back(remotePath);
        downloadTargets.push_back(localPath);
        const bool result = takeResult(downloadResults, true);
        if (!result && !failedDownloadPayload.isNull())
            writeFile(localPath, failedDownloadPayload);
        if (result && !downloadPayload.isNull() && !writeFile(localPath, downloadPayload)) {
            currentError = {TransferErrorCode::LocalIo, QStringLiteral("测试写入失败"),
                            QStringLiteral("mock local write failed"), false};
            return false;
        }
        if (onDownloadAttempt)
            onDownloadAttempt(localPath, result);
        if (result && onDownload)
            onDownload(localPath);
        return result;
    }

    bool stat(const QString& path, TransferFileStat& out) override
    {
        ++statCalls;
        statPaths.push_back(path);
        if (statHandler)
            return statHandler(path, out);
        out = {};
        currentError = {};
        return true;
    }

    bool rename(const QString& from, const QString& to) override
    {
        ++renameCalls;
        renameFrom = from;
        renameTo = to;
        return takeResult(renameResults, true);
    }

    bool remove(const QString& path) override
    {
        ++removeCalls;
        removedPaths.push_back(path);
        return takeResult(removeResults, true);
    }

    void setProgressCallback(std::function<void(int)> callback) override
    {
        progressCallback = std::move(callback);
    }

    void setCancelFlag(std::atomic_bool* flag) override
    {
        cancelFlag = flag;
        cancelFlagHistory.push_back(flag);
    }

    TransferCapabilities capabilities() const override { return caps; }
    TransferError lastError() const override { return currentError; }

    void clearCredentials() override { ++clearCredentialsCalls; }

    bool takeResult(std::deque<bool>& results, bool fallback)
    {
        const bool result = results.empty() ? fallback : results.front();
        if (!results.empty())
            results.pop_front();
        if (result) {
            currentError = {};
        } else if (!operationErrors.empty()) {
            currentError = operationErrors.front();
            operationErrors.pop_front();
        } else {
            currentError = remoteIoError();
        }
        return result;
    }

    TransferCapabilities caps{true, true, true, true, true};
    std::deque<bool> connectResults;
    std::deque<bool> reconnectResults;
    std::deque<bool> uploadResults;
    std::deque<bool> downloadResults;
    std::deque<bool> renameResults;
    std::deque<bool> removeResults;
    std::deque<TransferError> operationErrors;
    std::function<bool(const QString&, TransferFileStat&)> statHandler;
    std::function<void()> onUpload;
    std::function<void(const QString&)> onDownload;
    std::function<void(const QString&, bool)> onDownloadAttempt;
    QByteArray downloadPayload;
    QByteArray failedDownloadPayload;
    TransferError currentError;
    std::function<void(int)> progressCallback;
    std::atomic_bool* cancelFlag = nullptr;
    QVector<std::atomic_bool*> cancelFlagHistory;
    QVector<QString> uploadSources;
    QVector<QString> uploadTargets;
    QVector<QString> downloadSources;
    QVector<QString> downloadTargets;
    QVector<QString> statPaths;
    QVector<QString> removedPaths;
    QString renameFrom;
    QString renameTo;
    DeviceInfo connectedDevice;
    AuthInfo connectedAuth;
    int connectCalls = 0;
    int reconnectCalls = 0;
    int uploadCalls = 0;
    int downloadCalls = 0;
    int statCalls = 0;
    int renameCalls = 0;
    int removeCalls = 0;
    int clearCredentialsCalls = 0;
};

TransferItemRequest uploadRequest(const QString& localPath, const QString& remotePath)
{
    return {localPath, remotePath, TransferDirection::Upload, OverwritePolicy::Overwrite};
}

TransferItemRequest downloadRequest(const QString& remotePath, const QString& localPath)
{
    return {localPath, remotePath, TransferDirection::Download, OverwritePolicy::Overwrite};
}

void makeAtomicUploadStat(MockChannel& channel, qint64 sourceSize)
{
    channel.statHandler = [sourceSize](const QString& path, TransferFileStat& out) {
        if (path.contains(QStringLiteral(".deviceforge-part-")))
            out = {true, static_cast<quint64>(sourceSize), QStringLiteral("temporary")};
        else
            out = {};
        return true;
    };
}

} // namespace

class TstTransferExecutor : public QObject
{
    Q_OBJECT

private slots:
    void init() { m_cancel.store(false); }

    void retriesThenCommits()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("a.bin"));
        QVERIFY(writeFile(localPath, QByteArray("firmware")));

        MockChannel channel;
        channel.uploadResults = {false, false, true};
        channel.operationErrors = {timeoutError(), connectionLostError()};
        makeAtomicUploadStat(channel, 8);
        QVector<int> delays;
        TransferExecutor executor(channel, [&delays](int milliseconds) {
            delays.push_back(milliseconds);
        });

        const auto result = executor.execute(uploadRequest(localPath, QStringLiteral("/opt/a.bin")),
                                             m_cancel);

        QCOMPARE(result.state, TransferState::Succeeded);
        QCOMPARE(result.attempts, 3);
        QCOMPARE(result.bytes, qint64(8));
        QVERIFY(!result.nonAtomic);
        QCOMPARE(channel.uploadCalls, 3);
        QCOMPARE(channel.reconnectCalls, 2);
        QCOMPARE(delays, QVector<int>({1000, 3000}));
        QCOMPARE(channel.renameCalls, 1);
        QVERIFY(channel.uploadTargets.front().contains(QStringLiteral(".deviceforge-part-")));
        QCOMPARE(channel.renameFrom, channel.uploadTargets.front());
        QCOMPARE(channel.renameTo, QStringLiteral("/opt/a.bin"));
        QCOMPARE(channel.clearCredentialsCalls, 1);
        QVERIFY(channel.cancelFlag == nullptr);
    }

    void deterministicFailureDoesNotRetryAndPreservesRootError()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("denied.bin"));
        QVERIFY(writeFile(localPath, QByteArray("blocked")));

        MockChannel channel;
        channel.uploadResults = {false};
        channel.operationErrors = {permissionError()};
        makeAtomicUploadStat(channel, 7);
        QVector<int> delays;
        TransferExecutor executor(channel, [&delays](int milliseconds) {
            delays.push_back(milliseconds);
        });

        const auto result = executor.execute(
            uploadRequest(localPath, QStringLiteral("/secure/denied.bin")), m_cancel);

        QCOMPARE(result.state, TransferState::Failed);
        QCOMPARE(result.error.code, TransferErrorCode::Permission);
        QCOMPARE(result.attempts, 1);
        QCOMPARE(channel.uploadCalls, 1);
        QCOMPARE(channel.reconnectCalls, 0);
        QVERIFY(delays.isEmpty());
        QCOMPARE(channel.removeCalls, 1);
        QCOMPARE(channel.renameCalls, 0);
        QCOMPARE(channel.clearCredentialsCalls, 1);
        QVERIFY(QFileInfo::exists(localPath));
        QCOMPARE(readFile(localPath), QByteArray("blocked"));
    }

    void cancellationBeforeTransferHasOneTerminalResult()
    {
        MockChannel channel;
        TransferExecutor executor(channel, [](int) {});
        m_cancel.store(true);

        const auto result = executor.execute(
            uploadRequest(QStringLiteral("missing.bin"), QStringLiteral("/missing.bin")),
            m_cancel);

        QCOMPARE(result.state, TransferState::Cancelled);
        QCOMPARE(result.error.code, TransferErrorCode::Cancelled);
        QCOMPARE(result.attempts, 0);
        QCOMPARE(channel.uploadCalls, 0);
        QCOMPARE(channel.renameCalls, 0);
        QCOMPARE(channel.clearCredentialsCalls, 1);
        QCOMPARE(channel.cancelFlagHistory.size(), 2);
        QCOMPARE(channel.cancelFlagHistory.front(), &m_cancel);
        QVERIFY(channel.cancelFlagHistory.back() == nullptr);
    }

    void cancellationDuringBackoffStopsBeforeReconnect()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("cancel.bin"));
        QVERIFY(writeFile(localPath, QByteArray("cancel")));

        MockChannel channel;
        channel.uploadResults = {false};
        channel.operationErrors = {timeoutError()};
        makeAtomicUploadStat(channel, 6);
        TransferExecutor executor(channel, [this](int) { m_cancel.store(true); });

        const auto result = executor.execute(
            uploadRequest(localPath, QStringLiteral("/cancel.bin")), m_cancel);

        QCOMPARE(result.state, TransferState::Cancelled);
        QCOMPARE(result.attempts, 1);
        QCOMPARE(channel.uploadCalls, 1);
        QCOMPARE(channel.reconnectCalls, 0);
        QCOMPARE(channel.renameCalls, 0);
        QCOMPARE(channel.clearCredentialsCalls, 1);
    }

    void downloadUsesLocalTemporaryFileAndAtomicallyReplacesTarget()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("firmware.bin"));
        QVERIFY(writeFile(localPath, QByteArray("old")));

        MockChannel channel;
        channel.downloadPayload = QByteArray("new-firmware");
        channel.statHandler = [](const QString&, TransferFileStat& out) {
            out = {true, 12, QStringLiteral("remote")};
            return true;
        };
        TransferExecutor executor(channel, [](int) {});

        const auto result = executor.execute(
            downloadRequest(QStringLiteral("/opt/firmware.bin"), localPath), m_cancel);

        QCOMPARE(result.state, TransferState::Succeeded);
        QCOMPARE(result.bytes, qint64(12));
        QCOMPARE(channel.downloadCalls, 1);
        QCOMPARE(channel.statPaths, QVector<QString>({QStringLiteral("/opt/firmware.bin")}));
        QVERIFY(channel.downloadTargets.front().contains(QStringLiteral(".deviceforge-part-")));
        QVERIFY(!QFileInfo::exists(channel.downloadTargets.front()));
        QCOMPARE(readFile(localPath), QByteArray("new-firmware"));
        QCOMPARE(channel.renameCalls, 0);
        QCOMPARE(channel.removeCalls, 0);
        QCOMPARE(channel.clearCredentialsCalls, 1);
#ifdef Q_OS_WIN
        QVERIFY(!result.nonAtomic);
#else
        QVERIFY(result.nonAtomic);
#endif
    }

    void transientPrepareStatRetriesWithinTotalAttemptBudget()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("stat-retry.bin"));
        QVERIFY(writeFile(localPath, QByteArray("stat")));

        MockChannel channel;
        int statAttempt = 0;
        channel.statHandler = [&channel, &statAttempt](const QString& path,
                                                       TransferFileStat& out) {
            if (statAttempt++ == 0) {
                channel.currentError = timeoutError();
                return false;
            }
            if (path.contains(QStringLiteral(".deviceforge-part-")))
                out = {true, 4, QStringLiteral("temporary")};
            else
                out = {};
            channel.currentError = {};
            return true;
        };
        QVector<int> delays;
        TransferExecutor executor(channel, [&delays](int milliseconds) {
            delays.push_back(milliseconds);
        });

        const auto result = executor.execute(
            uploadRequest(localPath, QStringLiteral("/stat-retry.bin")), m_cancel);

        QCOMPARE(result.state, TransferState::Succeeded);
        QCOMPARE(result.attempts, 2);
        QCOMPARE(channel.reconnectCalls, 1);
        QCOMPARE(channel.uploadCalls, 1);
        QCOMPARE(channel.renameCalls, 1);
        QCOMPARE(delays, QVector<int>({1000}));
    }

    void cancellationAfterTargetStatNeverRenames()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("cancel-commit.bin"));
        QVERIFY(writeFile(localPath, QByteArray("data")));

        MockChannel channel;
        int targetStats = 0;
        channel.statHandler = [this, &targetStats](const QString& path,
                                                   TransferFileStat& out) {
            if (path.contains(QStringLiteral(".deviceforge-part-"))) {
                out = {true, 4, QStringLiteral("temporary")};
            } else {
                out = {};
                if (targetStats++ == 1)
                    m_cancel.store(true);
            }
            return true;
        };
        TransferExecutor executor(channel, [](int) {});

        const auto result = executor.execute(
            uploadRequest(localPath, QStringLiteral("/cancel-commit.bin")), m_cancel);

        QCOMPARE(result.state, TransferState::Cancelled);
        QCOMPARE(result.error.code, TransferErrorCode::Cancelled);
        QCOMPARE(channel.renameCalls, 0);
        QCOMPARE(channel.removeCalls, 1);
    }

    void renameFailureAndCleanupFailureBothRemainVisible()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("rename-fail.bin"));
        QVERIFY(writeFile(localPath, QByteArray("data")));

        MockChannel channel;
        channel.renameResults = {false};
        channel.removeResults = {false};
        channel.operationErrors = {remoteIoError(), permissionError()};
        makeAtomicUploadStat(channel, 4);
        TransferExecutor executor(channel, [](int) {});

        const auto result = executor.execute(
            uploadRequest(localPath, QStringLiteral("/rename-fail.bin")), m_cancel);

        QCOMPARE(result.state, TransferState::Failed);
        QCOMPARE(result.error.code, TransferErrorCode::RemoteIo);
        QVERIFY(result.error.detail.contains(QStringLiteral("remote write failed")));
        QVERIFY(result.error.detail.contains(QStringLiteral("permission denied")));
        QCOMPARE(channel.renameCalls, 1);
        QCOMPARE(channel.removeCalls, 1);
        QVERIFY(QFileInfo::exists(localPath));
    }

    void cleanupFailureStopsRetrySoRemoteResidualIsVisible()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("residual.bin"));
        QVERIFY(writeFile(localPath, QByteArray("data")));

        MockChannel channel;
        channel.renameResults = {false};
        channel.removeResults = {false};
        channel.operationErrors = {timeoutError(), permissionError()};
        makeAtomicUploadStat(channel, 4);
        TransferExecutor executor(channel, [](int) {});

        const auto result = executor.execute(
            uploadRequest(localPath, QStringLiteral("/residual.bin")), m_cancel);

        QCOMPARE(result.state, TransferState::Failed);
        QCOMPARE(result.error.code, TransferErrorCode::Timeout);
        QVERIFY(result.error.detail.contains(QStringLiteral("清理")));
        QCOMPARE(result.attempts, 1);
        QCOMPARE(channel.reconnectCalls, 0);
        QCOMPARE(channel.uploadCalls, 1);
        QCOMPARE(channel.renameCalls, 1);
        QCOMPARE(channel.removeCalls, 1);
    }

    void uploadSourceSameSizeSameMtimeChangeBlocksCommit()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("source-hash.bin"));
        QVERIFY(writeFile(localPath, QByteArray("AAAA")));
        const QDateTime baselineTime = QFileInfo(localPath).lastModified();

        MockChannel channel;
        makeAtomicUploadStat(channel, 4);
        channel.onUpload = [localPath, baselineTime] {
            QVERIFY(writeFile(localPath, QByteArray("BBBB")));
            QVERIFY(setModifiedTime(localPath, baselineTime));
        };
        TransferExecutor executor(channel, [](int) {});

        const auto result = executor.execute(
            uploadRequest(localPath, QStringLiteral("/source-hash.bin")), m_cancel);

        QCOMPARE(result.state, TransferState::NeedsAttention);
        QCOMPARE(result.error.code, TransferErrorCode::TargetChanged);
        QCOMPARE(channel.renameCalls, 0);
        QCOMPARE(channel.removeCalls, 1);
        QCOMPARE(readFile(localPath), QByteArray("BBBB"));
    }

    void uploadSourceChangeDuringFinalTargetStatBlocksCommit()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("source-last-check.bin"));
        QVERIFY(writeFile(localPath, QByteArray("AAAA")));
        const QDateTime baselineTime = QFileInfo(localPath).lastModified();

        MockChannel channel;
        int targetStatCalls = 0;
        bool mutationSucceeded = false;
        channel.statHandler = [localPath, baselineTime, &targetStatCalls,
                               &mutationSucceeded](
                                  const QString& path, TransferFileStat& out) {
            if (path.contains(QStringLiteral(".deviceforge-part-"))) {
                out = {true, 4, QStringLiteral("temporary")};
                return true;
            }
            ++targetStatCalls;
            if (targetStatCalls == 2) {
                mutationSucceeded = writeFile(localPath, QByteArray("BBBB"))
                    && setModifiedTime(localPath, baselineTime);
            }
            out = {};
            return true;
        };
        TransferExecutor executor(channel, [](int) {});

        const auto result = executor.execute(
            uploadRequest(localPath, QStringLiteral("/source-last-check.bin")), m_cancel);

        QCOMPARE(targetStatCalls, 2);
        QVERIFY(mutationSucceeded);
        QCOMPARE(result.state, TransferState::NeedsAttention);
        QCOMPARE(result.error.code, TransferErrorCode::TargetChanged);
        QCOMPARE(channel.renameCalls, 0);
        QCOMPARE(channel.removeCalls, 1);
        QCOMPARE(readFile(localPath), QByteArray("BBBB"));
    }

    void downloadTargetSameSizeSameMtimeChangeBlocksCommit()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("target-hash.bin"));
        QVERIFY(writeFile(localPath, QByteArray("AAAA")));
        const QDateTime baselineTime = QFileInfo(localPath).lastModified();

        MockChannel channel;
        channel.downloadPayload = QByteArray("DOWN");
        channel.statHandler = [](const QString&, TransferFileStat& out) {
            out = {true, 4, QStringLiteral("remote")};
            return true;
        };
        channel.onDownload = [localPath, baselineTime](const QString&) {
            QVERIFY(writeFile(localPath, QByteArray("BBBB")));
            QVERIFY(setModifiedTime(localPath, baselineTime));
        };
        TransferExecutor executor(channel, [](int) {});

        const auto result = executor.execute(
            downloadRequest(QStringLiteral("/target-hash.bin"), localPath), m_cancel);

        QCOMPARE(result.state, TransferState::NeedsAttention);
        QCOMPARE(result.error.code, TransferErrorCode::TargetChanged);
        QCOMPARE(readFile(localPath), QByteArray("BBBB"));
        QVERIFY(!QFileInfo::exists(channel.downloadTargets.front()));
    }

    void failedDownloadRemovesPartialTemporaryFile()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("partial.bin"));

        MockChannel channel;
        channel.downloadResults = {false};
        channel.failedDownloadPayload = QByteArray("partial");
        channel.operationErrors = {remoteIoError()};
        channel.statHandler = [](const QString&, TransferFileStat& out) {
            out = {true, 10, QStringLiteral("remote")};
            return true;
        };
        TransferExecutor executor(channel, [](int) {});

        const auto result = executor.execute(
            downloadRequest(QStringLiteral("/partial.bin"), localPath), m_cancel);

        QCOMPARE(result.state, TransferState::Failed);
        QCOMPARE(result.error.code, TransferErrorCode::RemoteIo);
        QCOMPARE(channel.downloadCalls, 1);
        QVERIFY(!QFileInfo::exists(channel.downloadTargets.front()));
        QVERIFY(!QFileInfo::exists(localPath));
    }

    void cancelledDownloadReportsLocalTemporaryCleanupFailure()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("cancel-cleanup.bin"));

        MockChannel channel;
        channel.statHandler = [](const QString&, TransferFileStat& out) {
            out = {true, 1, QStringLiteral("remote")};
            return true;
        };
        channel.onDownload = [this](const QString& temporaryPath) {
            QVERIFY(QDir().mkpath(temporaryPath));
            m_cancel.store(true);
        };
        TransferExecutor executor(channel, [](int) {});

        const auto result = executor.execute(
            downloadRequest(QStringLiteral("/cancel-cleanup.bin"), localPath), m_cancel);

        QCOMPARE(result.state, TransferState::Cancelled);
        QCOMPARE(result.error.code, TransferErrorCode::Cancelled);
        QVERIFY(result.error.detail.contains(QStringLiteral("清理")));
        const QString temporaryPath = channel.downloadTargets.front();
        QVERIFY(QFileInfo(temporaryPath).isDir());
        QVERIFY(QDir().rmdir(temporaryPath));
    }

    void uploadRefusesCommitWhenRemoteTargetChanged()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("changed.bin"));
        QVERIFY(writeFile(localPath, QByteArray("firmware")));

        MockChannel channel;
        int targetStats = 0;
        channel.statHandler = [&targetStats](const QString& path, TransferFileStat& out) {
            if (path.contains(QStringLiteral(".deviceforge-part-"))) {
                out = {true, 8, QStringLiteral("temporary")};
            } else if (targetStats++ == 0) {
                out = {true, 10, QStringLiteral("baseline")};
            } else {
                out = {true, 11, QStringLiteral("changed")};
            }
            return true;
        };
        TransferExecutor executor(channel, [](int) {});

        const auto result = executor.execute(
            uploadRequest(localPath, QStringLiteral("/opt/changed.bin")), m_cancel);

        QCOMPARE(result.state, TransferState::NeedsAttention);
        QCOMPARE(result.error.code, TransferErrorCode::TargetChanged);
        QCOMPARE(channel.renameCalls, 0);
        QCOMPARE(channel.removeCalls, 1);
        QVERIFY(channel.removedPaths.front().contains(QStringLiteral(".deviceforge-part-")));
        QCOMPARE(channel.clearCredentialsCalls, 1);
    }

    void downloadRefusesCommitWhenLocalTargetChanged()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("changed-local.bin"));
        QVERIFY(writeFile(localPath, QByteArray("old")));

        MockChannel channel;
        channel.downloadPayload = QByteArray("new-firmware");
        channel.statHandler = [](const QString&, TransferFileStat& out) {
            out = {true, 12, QStringLiteral("remote")};
            return true;
        };
        channel.onDownload = [localPath](const QString&) {
            QVERIFY(writeFile(localPath, QByteArray("changed")));
        };
        TransferExecutor executor(channel, [](int) {});

        const auto result = executor.execute(
            downloadRequest(QStringLiteral("/opt/changed-local.bin"), localPath), m_cancel);

        QCOMPARE(result.state, TransferState::NeedsAttention);
        QCOMPARE(result.error.code, TransferErrorCode::TargetChanged);
        QCOMPARE(readFile(localPath), QByteArray("changed"));
        QCOMPARE(channel.downloadCalls, 1);
        QVERIFY(!QFileInfo::exists(channel.downloadTargets.front()));
        QCOMPARE(channel.clearCredentialsCalls, 1);
    }

    void cleanupFailureDoesNotHideTransferFailure()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("io.bin"));
        QVERIFY(writeFile(localPath, QByteArray("io")));

        MockChannel channel;
        channel.uploadResults = {false};
        channel.removeResults = {false};
        channel.operationErrors = {remoteIoError(), permissionError()};
        makeAtomicUploadStat(channel, 2);
        TransferExecutor executor(channel, [](int) {});

        const auto result = executor.execute(
            uploadRequest(localPath, QStringLiteral("/opt/io.bin")), m_cancel);

        QCOMPARE(result.state, TransferState::Failed);
        QCOMPARE(result.error.code, TransferErrorCode::RemoteIo);
        QVERIFY(result.error.detail.contains(QStringLiteral("清理")));
        QCOMPARE(channel.removeCalls, 1);
        QCOMPARE(channel.clearCredentialsCalls, 1);
    }

    void missingRenameCapabilityFallsBackToVisibleNonAtomicResult()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("legacy.bin"));
        QVERIFY(writeFile(localPath, QByteArray("legacy")));

        MockChannel channel;
        channel.caps.rename = false;
        int targetStats = 0;
        channel.statHandler = [&targetStats](const QString&, TransferFileStat& out) {
            if (targetStats++ < 2)
                out = {};
            else
                out = {true, 6, QStringLiteral("written")};
            return true;
        };
        TransferExecutor executor(channel, [](int) {});

        const auto result = executor.execute(
            uploadRequest(localPath, QStringLiteral("/legacy.bin")), m_cancel);

        QCOMPARE(result.state, TransferState::Succeeded);
        QVERIFY(result.nonAtomic);
        QCOMPARE(result.bytes, qint64(6));
        QCOMPARE(channel.uploadTargets, QVector<QString>({QStringLiteral("/legacy.bin")}));
        QCOMPARE(channel.renameCalls, 0);
        QCOMPARE(channel.removeCalls, 0);
        QCOMPARE(channel.clearCredentialsCalls, 1);
    }

    void successfulUploadDoesNotDeleteMoveSource()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("move-source.bin"));
        QVERIFY(writeFile(localPath, QByteArray("source")));

        MockChannel channel;
        makeAtomicUploadStat(channel, 6);
        TransferExecutor executor(channel, [](int) {});

        const auto result = executor.execute(
            uploadRequest(localPath, QStringLiteral("/move-target.bin")), m_cancel);

        QCOMPARE(result.state, TransferState::Succeeded);
        QVERIFY(QFileInfo::exists(localPath));
        QVERIFY(channel.removedPaths.isEmpty());
        QCOMPARE(channel.renameCalls, 1);
    }

    void skipPolicyLeavesExistingTargetUntouched()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("skip.bin"));
        QVERIFY(writeFile(localPath, QByteArray("source")));

        MockChannel channel;
        channel.statHandler = [](const QString&, TransferFileStat& out) {
            out = {true, 42, QStringLiteral("existing")};
            return true;
        };
        TransferExecutor executor(channel, [](int) {});
        TransferItemRequest request = uploadRequest(localPath, QStringLiteral("/skip.bin"));
        request.overwrite = OverwritePolicy::Skip;

        const auto result = executor.execute(request, m_cancel);

        QCOMPARE(result.state, TransferState::Succeeded);
        QCOMPARE(result.attempts, 0);
        QVERIFY(result.skipped);
        QCOMPARE(result.bytes, qint64(0));
        QCOMPARE(channel.uploadCalls, 0);
        QCOMPARE(channel.renameCalls, 0);
        QCOMPARE(channel.removeCalls, 0);
        QCOMPARE(channel.clearCredentialsCalls, 1);
    }

    void downloadSkipPolicyLeavesExistingLocalTargetUntouched()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("existing.bin"));
        QVERIFY(writeFile(localPath, QByteArray("existing")));

        MockChannel channel;
        channel.statHandler = [](const QString&, TransferFileStat& out) {
            out = {true, 42, QStringLiteral("remote")};
            return true;
        };
        TransferExecutor executor(channel, [](int) {});
        TransferItemRequest request =
            downloadRequest(QStringLiteral("/existing.bin"), localPath);
        request.overwrite = OverwritePolicy::Skip;

        const auto result = executor.execute(request, m_cancel);

        QCOMPARE(result.state, TransferState::Succeeded);
        QCOMPARE(result.attempts, 0);
        QVERIFY(result.skipped);
        QCOMPARE(result.bytes, qint64(0));
        QCOMPARE(readFile(localPath), QByteArray("existing"));
        QCOMPARE(channel.downloadCalls, 0);
        QCOMPARE(channel.clearCredentialsCalls, 1);
    }

    void emptyPathFailsBeforeAnyChannelIo()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("source.bin"));
        QVERIFY(writeFile(localPath, QByteArray("source")));

        MockChannel channel;
        TransferExecutor executor(channel, [](int) {});

        const auto result = executor.execute(uploadRequest(localPath, QString()), m_cancel);

        QCOMPARE(result.state, TransferState::Failed);
        QCOMPARE(result.error.code, TransferErrorCode::InvalidPath);
        QCOMPARE(result.attempts, 0);
        QVERIFY(!result.skipped);
        QCOMPARE(channel.statCalls, 0);
        QCOMPARE(channel.uploadCalls, 0);
        QCOMPARE(channel.clearCredentialsCalls, 1);
    }

    void missingUploadSourceFailsWithoutBeingMarkedSkipped()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString missingPath = directory.filePath(QStringLiteral("missing.bin"));

        MockChannel channel;
        TransferExecutor executor(channel, [](int) {});

        const auto result = executor.execute(
            uploadRequest(missingPath, QStringLiteral("/missing.bin")), m_cancel);

        QCOMPARE(result.state, TransferState::Failed);
        QCOMPARE(result.error.code, TransferErrorCode::LocalIo);
        QCOMPARE(result.attempts, 0);
        QVERIFY(!result.skipped);
        QCOMPARE(channel.statCalls, 0);
        QCOMPARE(channel.uploadCalls, 0);
        QCOMPARE(channel.clearCredentialsCalls, 1);
    }

    void connectsAndClearsTaskPrivateCredentials()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("secret.bin"));
        QVERIFY(writeFile(localPath, QByteArray("secret")));

        MockChannel channel;
        makeAtomicUploadStat(channel, 6);
        const DeviceInfo device{"10.0.0.8", 21, "ftp", "PLC-8", ""};
        AuthInfo credentials{"operator", "secret-password"};
        TransferExecutor executor(channel, device, credentials, [](int) {});

        const auto result = executor.execute(
            uploadRequest(localPath, QStringLiteral("/secret.bin")), m_cancel);

        QCOMPARE(result.state, TransferState::Succeeded);
        QCOMPARE(channel.connectCalls, 1);
        QCOMPARE(channel.connectedDevice.ip, device.ip);
        QCOMPARE(channel.connectedAuth.user, std::string("operator"));
        QCOMPARE(channel.connectedAuth.password, std::string("secret-password"));
        QVERIFY(credentials.user.empty());
        QVERIFY(credentials.password.empty());
        QCOMPARE(channel.clearCredentialsCalls, 1);
    }

    void transientConnectFailureConsumesOneRetryAttempt()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("reconnect.bin"));
        QVERIFY(writeFile(localPath, QByteArray("retry")));

        MockChannel channel;
        channel.connectResults = {false};
        channel.reconnectResults = {true};
        channel.operationErrors = {timeoutError()};
        makeAtomicUploadStat(channel, 5);
        const DeviceInfo device{"10.0.0.9", 21, "ftp", "PLC-9", ""};
        AuthInfo credentials{"operator", "secret-password"};
        QVector<int> delays;
        TransferExecutor executor(channel, device, credentials, [&delays](int milliseconds) {
            delays.push_back(milliseconds);
        });

        const auto result = executor.execute(
            uploadRequest(localPath, QStringLiteral("/reconnect.bin")), m_cancel);

        QCOMPARE(result.state, TransferState::Succeeded);
        QCOMPARE(result.attempts, 2);
        QCOMPARE(channel.connectCalls, 1);
        QCOMPARE(channel.reconnectCalls, 1);
        QCOMPARE(channel.uploadCalls, 1);
        QCOMPARE(delays, QVector<int>({1000}));
        QVERIFY(credentials.user.empty());
        QVERIFY(credentials.password.empty());
        QCOMPARE(channel.clearCredentialsCalls, 1);
    }

private:
    std::atomic_bool m_cancel{false};
};

QTEST_MAIN(TstTransferExecutor)
#include "tst_transfer_executor.moc"
