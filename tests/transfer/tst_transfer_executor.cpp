// tst_transfer_executor.cpp — 单文件可靠传输、重试、取消与原子提交契约测试

#include <QtTest>

#include "transfer/TransferExecutor.h"
#include "transfer/EnsuredDirCache.h"
#include "transfer/LocalTransferChannel.h"
#include "transfer/LocalPanelTransfer.h"

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

    // v2.15 波2：单层创建远端目录（父缺失即失败），执行器自最浅缺失层起逐级调用。
    bool makeDirectory(const QString& remotePath) override
    {
        ++makeDirectoryCalls;
        makeDirectoryPaths.push_back(remotePath);
        const bool result = takeResult(makeDirectoryResults, true);
        if (result && onMakeDirectory)
            onMakeDirectory();
        return result;
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

    // makeDirectory=true 对齐真实 FTP/SFTP 通道波2 能力口径；关闭臂由专项用例显式置 false。
    TransferCapabilities caps{true, true, true, true, true, true};
    std::deque<bool> connectResults;
    std::deque<bool> reconnectResults;
    std::deque<bool> uploadResults;
    std::deque<bool> downloadResults;
    std::deque<bool> renameResults;
    std::deque<bool> removeResults;
    std::deque<bool> makeDirectoryResults;
    std::deque<TransferError> operationErrors;
    std::function<bool(const QString&, TransferFileStat&)> statHandler;
    std::function<void()> onUpload;
    std::function<void()> onMakeDirectory;
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
    QVector<QString> makeDirectoryPaths;
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
    int makeDirectoryCalls = 0;
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
    void localPanelRejectsSameFile_data()
    {
        QTest::addColumn<QString>("targetName");
        QTest::newRow("same-path") << QStringLiteral("source.bin");
        QTest::newRow("dot-alias") << QStringLiteral("./source.bin");
#ifdef Q_OS_WIN
        QTest::newRow("case-alias") << QStringLiteral("SOURCE.BIN");
#endif
    }

    void localPanelRejectsSameFile()
    {
        QFETCH(QString, targetName);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto source = directory.filePath(QStringLiteral("source.bin"));
        QVERIFY(writeFile(source, QByteArray("only copy")));
        const auto result = executeLocalPanelTransfer(
            downloadRequest(source, directory.filePath(targetName)), m_cancel);
        QCOMPARE(readFile(source), QByteArray("only copy"));
        QCOMPARE(result.state, TransferState::Failed);
        QCOMPARE(result.error.code, TransferErrorCode::InvalidPath);
        QCOMPARE(result.attempts, 0);
        QVERIFY(!result.atomicCommitSucceeded);
        QVERIFY(!result.sourceRemoved);
    }

    void localPanelCopiesDifferentPaths()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto source = directory.filePath(QStringLiteral("source.bin"));
        const auto target = directory.filePath(QStringLiteral("target.bin"));
        QVERIFY(writeFile(source, QByteArray("only copy")));
        const auto result = executeLocalPanelTransfer(downloadRequest(source, target), m_cancel);
        QCOMPARE(result.state, TransferState::Succeeded);
        QCOMPARE(readFile(source), QByteArray("only copy"));
        QCOMPARE(readFile(target), QByteArray("only copy"));
    }

    void localPanelRejectsHardLinkAndRechecksIdentity()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto source = directory.filePath(QStringLiteral("source.bin"));
        const auto target = directory.filePath(QStringLiteral("linked.bin"));
        QVERIFY(writeFile(source, QByteArray("only copy")));
        QVERIFY(!localPathsEquivalent(source, target));
        std::error_code error;
        std::filesystem::create_hard_link(localFilesystemPath(source),
                                           localFilesystemPath(target), error);
        QVERIFY2(!error, error.message().c_str());
        QVERIFY(localPathsEquivalent(source, target));
        const auto result = executeLocalPanelTransfer(downloadRequest(source, target), m_cancel);
        QCOMPARE(result.state, TransferState::Failed);
        QCOMPARE(result.error.code, TransferErrorCode::InvalidPath);
        QCOMPARE(result.attempts, 0);
        QCOMPARE(readFile(source), QByteArray("only copy"));
        QCOMPARE(readFile(target), QByteArray("only copy"));
    }

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

    void localChannelUsesExecutorForFailureCancellationAndResume()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString sourcePath = directory.filePath(QStringLiteral("missing.bin"));
        const QString targetPath = directory.filePath(QStringLiteral("target.bin"));
        TransferItemRequest request =
            downloadRequest(sourcePath, targetPath);
        LocalTransferChannel channel;
        TransferExecutor executor(channel, [](int) {});

        const auto failed = executor.execute(request, m_cancel);
        QCOMPARE(failed.state, TransferState::Failed);
        QVERIFY(!failed.deliverySucceeded);
        QVERIFY(!QFileInfo::exists(targetPath));

        QVERIFY(writeFile(sourcePath, QByteArray("local reliable payload")));
        m_cancel.store(true);
        const auto cancelled = executor.execute(request, m_cancel);
        QCOMPARE(cancelled.state, TransferState::Cancelled);
        QVERIFY(!cancelled.deliverySucceeded);
        QVERIFY(!QFileInfo::exists(targetPath));

        m_cancel.store(false);
        const auto resumed = executor.execute(request, m_cancel);
        QCOMPARE(resumed.state, TransferState::Succeeded);
        QVERIFY(resumed.deliverySucceeded);
        QCOMPARE(readFile(targetPath), QByteArray("local reliable payload"));
    }

    void directoryUploadSourceFailsBeforeAnyChannelCall()
    {
        // 目录源必须在触碰远端之前失败（评审 I-1 钉桩：两条路径口径一致，无
        // 「单发能传文件夹」分叉）。v2.15 任务9 就实：单发/编辑器/保存为模板三入口
        // 已在入口层展开目录（folderexpand::expand），条目级不再有目录源到达本
        // 执行器——本钉桩降为执行器侧纵深防御，断言逐字不变。
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QDir(directory.path()).mkpath(QStringLiteral("bundle")));

        MockChannel channel;
        makeAtomicUploadStat(channel, 0);
        TransferExecutor executor(channel, [](int) {});

        const auto result = executor.execute(
            uploadRequest(directory.filePath(QStringLiteral("bundle")),
                          QStringLiteral("/opt/bundle")),
            m_cancel);

        QCOMPARE(result.state, TransferState::Failed);
        QCOMPARE(result.error.code, TransferErrorCode::LocalIo);
        QCOMPARE(result.attempts, 0);
        QCOMPARE(channel.uploadCalls, 0);
        QCOMPARE(channel.statCalls, 0);
        QCOMPARE(channel.renameCalls, 0);
        QCOMPARE(channel.clearCredentialsCalls, 1);
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

    // ===== v2.15 波2（任务 8）：上传前远端父目录确保 =====

    void parentEnsureCreatesMissingLevelsShallowestFirst()
    {
        // ① 镜像子路径首上传：父缺失且通道支持 → 自最浅缺失层起序贯 makeDirectory
        //（stat 只需探测到第一个缺失层，其后深层直接创建），随后上传照常成功
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("a.bin"));
        QVERIFY(writeFile(localPath, QByteArray("payload")));

        MockChannel channel;
        makeAtomicUploadStat(channel, 7);
        TransferExecutor executor(channel, [](int) {});

        const auto result = executor.execute(
            uploadRequest(localPath, QStringLiteral("/opt/deep/dir/a.bin")), m_cancel);

        QCOMPARE(result.state, TransferState::Succeeded);
        QCOMPARE(channel.makeDirectoryPaths,
                 QVector<QString>({QStringLiteral("/opt"),
                                   QStringLiteral("/opt/deep"),
                                   QStringLiteral("/opt/deep/dir")}));
        QCOMPARE(channel.statPaths.at(0), QStringLiteral("/opt/deep/dir/a.bin"));
        QCOMPARE(channel.statPaths.at(1), QStringLiteral("/opt"));
        QCOMPARE(channel.uploadCalls, 1);
        QCOMPARE(channel.renameCalls, 1);
    }

    void parentEnsureSkippedWhenParentExists()
    {
        // ② stat 判定父目录已存在 → 零建目录调用
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("x.bin"));
        QVERIFY(writeFile(localPath, QByteArray("payload")));

        MockChannel channel;
        channel.statHandler = [](const QString& path, TransferFileStat& out) {
            if (path == QStringLiteral("/opt"))
                out = {true, 0, QStringLiteral("dir")};
            else if (path.contains(QStringLiteral(".deviceforge-part-")))
                out = {true, 7, QStringLiteral("temporary")};
            else
                out = {};
            return true;
        };
        TransferExecutor executor(channel, [](int) {});

        const auto result = executor.execute(
            uploadRequest(localPath, QStringLiteral("/opt/x.bin")), m_cancel);

        QCOMPARE(result.state, TransferState::Succeeded);
        QCOMPARE(channel.makeDirectoryCalls, 0);
        QCOMPARE(channel.uploadCalls, 1);
    }

    void parentEnsureStopsAtExistingAncestorAndCreatesOnlyDeeperLevels()
    {
        // ② ' 变体臂：浅层存在、深层缺失——stat 逐层探测到第一个缺失层后仅创建该层
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("app.log"));
        QVERIFY(writeFile(localPath, QByteArray("payload")));

        MockChannel channel;
        channel.statHandler = [](const QString& path, TransferFileStat& out) {
            if (path == QStringLiteral("/data"))
                out = {true, 0, QStringLiteral("dir")};
            else if (path.contains(QStringLiteral(".deviceforge-part-")))
                out = {true, 7, QStringLiteral("temporary")};
            else
                out = {};
            return true;
        };
        TransferExecutor executor(channel, [](int) {});

        const auto result = executor.execute(
            uploadRequest(localPath, QStringLiteral("/data/logs/app.log")), m_cancel);

        QCOMPARE(result.state, TransferState::Succeeded);
        QCOMPARE(channel.makeDirectoryPaths, QVector<QString>({QStringLiteral("/data/logs")}));
        QCOMPARE(channel.statPaths.at(1), QStringLiteral("/data"));
        QCOMPARE(channel.statPaths.at(2), QStringLiteral("/data/logs"));
    }

    void makeDirectoryFailureIsDeterministicWithoutRetry()
    {
        // ③ 建目录失败=确定性失败（与 deterministicFailure 同族）：中文「远端目录创建失败」
        // 入结果、零上传、零重连、零退避延时
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("blocked.bin"));
        QVERIFY(writeFile(localPath, QByteArray("payload")));

        MockChannel channel;
        channel.makeDirectoryResults = {false};
        channel.operationErrors = {permissionError()};
        makeAtomicUploadStat(channel, 7);
        QVector<int> delays;
        TransferExecutor executor(channel, [&delays](int milliseconds) {
            delays.push_back(milliseconds);
        });

        const auto result = executor.execute(
            uploadRequest(localPath, QStringLiteral("/secure/dir/blocked.bin")), m_cancel);

        QCOMPARE(result.state, TransferState::Failed);
        QCOMPARE(result.error.code, TransferErrorCode::Permission);
        QVERIFY(result.error.message.contains(QStringLiteral("远端目录创建失败")));
        QVERIFY(result.error.message.contains(QStringLiteral("/secure")));
        QVERIFY(result.error.message.contains(QStringLiteral("权限不足")));
        QCOMPARE(result.attempts, 0);
        QCOMPARE(channel.makeDirectoryCalls, 1);
        QCOMPARE(channel.uploadCalls, 0);
        QCOMPARE(channel.reconnectCalls, 0);
        QVERIFY(delays.isEmpty());
    }

    void makeDirectoryFailureRevealsExistingParentAndUploadContinues()
    {
        // 修复轮 1（评审 I-1）：瞬断闭合臂——父目录实际存在、仅 ensure 层 stat 恰逢
        // 瞬断报缺失，随后 MKD 对已存在目录报 550/QUOTE_ERROR 形态失败；执行器必须
        // 复检同一层，确认存在则视为已确保并继续遍历，让上传照常进行（进正常传输环），
        // 而非把瞬断收口成确定性红（makeError retryable 恒 false）。
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("a.bin"));
        QVERIFY(writeFile(localPath, QByteArray("payload")));

        MockChannel channel;
        channel.makeDirectoryResults = {false};
        channel.operationErrors = {connectionLostError()};  // MKD 已存在目录的连接层失败形态
        int optStatCount = 0;
        channel.statHandler = [&channel, &optStatCount](const QString& path, TransferFileStat& out) {
            if (path == QStringLiteral("/opt")) {
                // 首探报缺失（瞬断形态）；建目录失败后的复检报存在
                out = optStatCount++ == 0
                    ? TransferFileStat{}
                    : TransferFileStat{true, 0, QStringLiteral("dir")};
                return true;
            }
            if (path.contains(QStringLiteral(".deviceforge-part-")))
                out = {true, 7, QStringLiteral("temporary")};
            else
                out = {};
            return true;
        };
        QVector<int> delays;
        TransferExecutor executor(channel, [&delays](int milliseconds) {
            delays.push_back(milliseconds);
        });

        const auto result = executor.execute(
            uploadRequest(localPath, QStringLiteral("/opt/a.bin")), m_cancel);

        QCOMPARE(result.state, TransferState::Succeeded);
        QCOMPARE(result.attempts, 1);
        QCOMPARE(channel.makeDirectoryCalls, 1);
        QCOMPARE(channel.makeDirectoryPaths, QVector<QString>({QStringLiteral("/opt")}));
        // stat 序列精确钉桩：目标基线 → /opt 首探（缺失）→ 建失败后复检 /opt（存在）
        // → 临时文件校验 → 目标复核——复检确实发生且就位于建目录失败之后
        QCOMPARE(channel.statPaths,
                 QVector<QString>({QStringLiteral("/opt/a.bin"),
                                   QStringLiteral("/opt"),
                                   QStringLiteral("/opt"),
                                   channel.uploadTargets.front(),
                                   QStringLiteral("/opt/a.bin")}));
        QCOMPARE(channel.uploadCalls, 1);
        QCOMPARE(channel.renameCalls, 1);
        QCOMPARE(channel.reconnectCalls, 0);
        QVERIFY(delays.isEmpty());
    }

    void makeDirectoryFailureWithStillMissingReStatStaysDeterministic()
    {
        // 修复轮 1 互补臂：建目录失败且同层复检仍报缺失 → 维持确定性失败族
        // （真不存在且不可建的硬失败不被软化）；stat 序列钉桩复检发生，收口后零上传，
        // attempts 与既有确定性失败口径一致（makeDirectoryFailureIsDeterministicWithoutRetry 同族）
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("blocked.bin"));
        QVERIFY(writeFile(localPath, QByteArray("payload")));

        MockChannel channel;
        channel.makeDirectoryResults = {false};
        channel.operationErrors = {permissionError()};
        makeAtomicUploadStat(channel, 7);
        QVector<int> delays;
        TransferExecutor executor(channel, [&delays](int milliseconds) {
            delays.push_back(milliseconds);
        });

        const auto result = executor.execute(
            uploadRequest(localPath, QStringLiteral("/secure/a.bin")), m_cancel);

        QCOMPARE(result.state, TransferState::Failed);
        QCOMPARE(result.error.code, TransferErrorCode::Permission);
        QVERIFY(result.error.message.contains(QStringLiteral("远端目录创建失败")));
        QVERIFY(result.error.message.contains(QStringLiteral("/secure")));
        QVERIFY(result.error.message.contains(QStringLiteral("权限不足")));
        QCOMPARE(result.attempts, 0);
        QCOMPARE(channel.makeDirectoryCalls, 1);
        QCOMPARE(channel.uploadCalls, 0);
        QCOMPARE(channel.reconnectCalls, 0);
        QVERIFY(delays.isEmpty());
        // stat 序列：目标基线 → /secure 首探（缺失）→ 建失败后复检（仍缺失）→ 确定性收口
        QCOMPARE(channel.statPaths,
                 QVector<QString>({QStringLiteral("/secure/a.bin"),
                                   QStringLiteral("/secure"),
                                   QStringLiteral("/secure")}));
    }

    void unsupportedChannelNamesCapabilityGapWhenParentMissing()
    {
        // ④ 缺失且通道不支持 → 失败文案点名「通道不支持自动创建」，且绝不触碰 makeDirectory
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("a.bin"));
        QVERIFY(writeFile(localPath, QByteArray("payload")));

        MockChannel channel;
        channel.caps.makeDirectory = false;
        makeAtomicUploadStat(channel, 7);
        TransferExecutor executor(channel, [](int) {});

        const auto result = executor.execute(
            uploadRequest(localPath, QStringLiteral("/brand/newdir/a.bin")), m_cancel);

        QCOMPARE(result.state, TransferState::Failed);
        QCOMPARE(result.error.code, TransferErrorCode::Unsupported);
        QVERIFY(result.error.message.contains(
            QStringLiteral("远端目标父目录不存在且通道不支持自动创建")));
        QCOMPARE(result.attempts, 0);
        QCOMPARE(channel.makeDirectoryCalls, 0);
        QCOMPARE(channel.uploadCalls, 0);
        // stat 序列：目标基线 → 第一个缺失层即止
        QCOMPARE(channel.statPaths,
                 QVector<QString>({QStringLiteral("/brand/newdir/a.bin"),
                                   QStringLiteral("/brand")}));
    }

    void existingTargetSkipsParentEnsureEntirely()
    {
        // 重构钉桩：目标存在即证明父目录存在——父目录检查整体跳过，stat 调用序列
        // 与波1 完全一致（既有计数钉桩契约零漂移的守卫）
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("target.bin"));
        QVERIFY(writeFile(localPath, QByteArray("payload")));

        MockChannel channel;
        channel.statHandler = [](const QString& path, TransferFileStat& out) {
            if (path.contains(QStringLiteral(".deviceforge-part-")))
                out = {true, 7, QStringLiteral("temporary")};
            else
                out = {true, 7, QStringLiteral("baseline")};
            return true;
        };
        TransferExecutor executor(channel, [](int) {});

        const auto result = executor.execute(
            uploadRequest(localPath, QStringLiteral("/deep/dir/target.bin")), m_cancel);

        QCOMPARE(result.state, TransferState::Succeeded);
        QCOMPARE(channel.makeDirectoryCalls, 0);
        QCOMPARE(channel.statPaths,
                 QVector<QString>({QStringLiteral("/deep/dir/target.bin"),
                                   channel.uploadTargets.front(),
                                   QStringLiteral("/deep/dir/target.bin")}));
    }

    void rootLevelAndRelativeTargetsSkipParentEnsure()
    {
        // "/" 父（根恒存在）与无父段（相对文件名）两臂：不 stat 任何父路径、不建目录
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("a.bin"));
        QVERIFY(writeFile(localPath, QByteArray("payload")));

        MockChannel channel;
        makeAtomicUploadStat(channel, 7);
        TransferExecutor executor(channel, [](int) {});

        const auto rootLevel = executor.execute(
            uploadRequest(localPath, QStringLiteral("/root-level.bin")), m_cancel);
        QCOMPARE(rootLevel.state, TransferState::Succeeded);
        QCOMPARE(channel.makeDirectoryCalls, 0);
        QCOMPARE(channel.statPaths,
                 QVector<QString>({QStringLiteral("/root-level.bin"),
                                   channel.uploadTargets.front(),
                                   QStringLiteral("/root-level.bin")}));

        MockChannel relative;
        makeAtomicUploadStat(relative, 7);
        TransferExecutor relativeExecutor(relative, [](int) {});
        const auto relativeTarget = relativeExecutor.execute(
            uploadRequest(localPath, QStringLiteral("relative.bin")), m_cancel);
        QCOMPARE(relativeTarget.state, TransferState::Succeeded);
        QCOMPARE(relative.makeDirectoryCalls, 0);
        QCOMPARE(relative.statPaths.size(), 3);  // 基线/临时/复核，无父探测
    }

    void parentStatErrorStillAttemptsCreationViaMakeDirectory()
    {
        // 真实通道形态：父目录缺失表现为 stat 列表失败而非 exists=false——同臂处理
        // 尝试创建；确保步是确定性的，stat 错误不触发重连/退避
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("a.bin"));
        QVERIFY(writeFile(localPath, QByteArray("payload")));

        MockChannel channel;
        channel.statHandler = [&channel](const QString& path, TransferFileStat& out) {
            if (path == QStringLiteral("/lost")) {
                channel.currentError = permissionError();
                return false;
            }
            if (path.contains(QStringLiteral(".deviceforge-part-")))
                out = {true, 7, QStringLiteral("temporary")};
            else
                out = {};
            return true;
        };
        QVector<int> delays;
        TransferExecutor executor(channel, [&delays](int milliseconds) {
            delays.push_back(milliseconds);
        });

        const auto result = executor.execute(
            uploadRequest(localPath, QStringLiteral("/lost/dir/a.bin")), m_cancel);

        QCOMPARE(result.state, TransferState::Succeeded);
        QCOMPARE(channel.makeDirectoryPaths,
                 QVector<QString>({QStringLiteral("/lost"), QStringLiteral("/lost/dir")}));
        QCOMPARE(channel.reconnectCalls, 0);
        QVERIFY(delays.isEmpty());
        QCOMPARE(channel.uploadCalls, 1);
    }

    void cancellationDuringParentStatPreventsMakeDirectory()
    {
        // stat 期间到达的取消不得再开始建目录（与既有取消检查点口径一致）
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("a.bin"));
        QVERIFY(writeFile(localPath, QByteArray("payload")));

        MockChannel channel;
        channel.statHandler = [this](const QString& path, TransferFileStat& out) {
            if (path == QStringLiteral("/gone")) {
                out = {};
                m_cancel.store(true);
                return true;
            }
            if (path.contains(QStringLiteral(".deviceforge-part-")))
                out = {true, 7, QStringLiteral("temporary")};
            else
                out = {};
            return true;
        };
        TransferExecutor executor(channel, [](int) {});

        const auto result = executor.execute(
            uploadRequest(localPath, QStringLiteral("/gone/dir/a.bin")), m_cancel);

        QCOMPARE(result.state, TransferState::Cancelled);
        QCOMPARE(channel.makeDirectoryCalls, 0);
        QCOMPARE(channel.uploadCalls, 0);
        QCOMPARE(channel.renameCalls, 0);
    }

    void cancellationDuringFirstMakeDirectoryStopsDeeperLevels()
    {
        // 最浅层建目录期间到达的取消不得再向更深层级续建（逐级循环顶检查点）
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localPath = directory.filePath(QStringLiteral("a.bin"));
        QVERIFY(writeFile(localPath, QByteArray("payload")));

        MockChannel channel;
        makeAtomicUploadStat(channel, 7);
        channel.onMakeDirectory = [this] { m_cancel.store(true); };
        TransferExecutor executor(channel, [](int) {});

        const auto result = executor.execute(
            uploadRequest(localPath, QStringLiteral("/gone2/deep/a.bin")), m_cancel);

        QCOMPARE(result.state, TransferState::Cancelled);
        QCOMPARE(channel.makeDirectoryPaths, QVector<QString>({QStringLiteral("/gone2")}));
        QCOMPARE(channel.uploadCalls, 0);
        QCOMPARE(channel.renameCalls, 0);
    }

    // ===== v2.15 波3（任务 11 / 波2 终审 I-3）：运行级已确保远端目录复用 =====

    void ensuredDirCacheContainsOnlyInsertedPaths()
    {
        // 纯头直测：insert 前 contains 为 false；insert 后为 true；不同路径互不串；
        // 大小写敏感（与远端 POSIX 目录名口径一致，不引入 caseFold）
        xfer::EnsuredDirCache cache;
        QVERIFY(!cache.contains(QStringLiteral("/opt")));
        cache.insert(QStringLiteral("/opt"));
        QVERIFY(cache.contains(QStringLiteral("/opt")));
        QVERIFY(!cache.contains(QStringLiteral("/opt/deep")));
        QVERIFY(!cache.contains(QStringLiteral("/OPT")));
        cache.insert(QStringLiteral("/opt/deep"));
        QVERIFY(cache.contains(QStringLiteral("/opt/deep")));
        QVERIFY(cache.contains(QStringLiteral("/opt")));
    }

    void cacheHitSkipsAncestorStatForLaterFiles()
    {
        // 同一缺失父目录下两个文件（DeployJob 形态：每文件新建执行器、共享 job 级
        // 缓存）：首个文件确保步经 mkdir 成功臂确认三层后，第二个文件不得再 stat
        // 这些层，也不得重复 makeDirectory——波2 终审 I-3 的核心收益钉桩。
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localA = directory.filePath(QStringLiteral("a.bin"));
        const QString localB = directory.filePath(QStringLiteral("b.bin"));
        QVERIFY(writeFile(localA, QByteArray("payload")));
        QVERIFY(writeFile(localB, QByteArray("payload")));

        MockChannel channel;
        // 父层恒报缺失 → 第一文件走「mkdir 幂等成功」臂逐层确认
        makeAtomicUploadStat(channel, 7);
        xfer::EnsuredDirCache cache;

        TransferExecutor first(channel, [](int) {}, &cache);
        const auto resultA = first.execute(
            uploadRequest(localA, QStringLiteral("/opt/deep/dir/a.bin")), m_cancel);
        QCOMPARE(resultA.state, TransferState::Succeeded);

        TransferExecutor second(channel, [](int) {}, &cache);
        const auto resultB = second.execute(
            uploadRequest(localB, QStringLiteral("/opt/deep/dir/b.bin")), m_cancel);
        QCOMPARE(resultB.state, TransferState::Succeeded);

        // stat 序列精确钉桩（本用例的 RED 点一：现状第二文件必然重新 stat /opt）：
        // 第二文件的父探测段整体消失，仅剩 基线→临时校验→目标复核
        QCOMPARE(channel.statPaths, QVector<QString>({
            QStringLiteral("/opt/deep/dir/a.bin"),
            QStringLiteral("/opt"),
            channel.uploadTargets.at(0),
            QStringLiteral("/opt/deep/dir/a.bin"),
            QStringLiteral("/opt/deep/dir/b.bin"),
            channel.uploadTargets.at(1),
            QStringLiteral("/opt/deep/dir/b.bin")}));
        // 建目录仅第一文件的三层调用；第二文件零重复创建
        QCOMPARE(channel.makeDirectoryPaths,
                 QVector<QString>({QStringLiteral("/opt"),
                                   QStringLiteral("/opt/deep"),
                                   QStringLiteral("/opt/deep/dir")}));
        // 三层经「mkdir 幂等成功」臂确认后全部记入缓存（记录点断言后置，
        // 保证上方序列断言先命中现状红）
        QVERIFY(cache.contains(QStringLiteral("/opt")));
        QVERIFY(cache.contains(QStringLiteral("/opt/deep")));
        QVERIFY(cache.contains(QStringLiteral("/opt/deep/dir")));
    }

    void statArmExistencePopulatesCacheForLaterFiles()
    {
        // 修复轮 1（评审 I-1）：记录点臂 (a)「该层 stat 报存在」的独立生效钉桩。
        // 夹具与 arm (b)/(c) 可一眼区分：整条祖先链首探即报存在，故 MKD 成功臂与
        // MKD 失败复检臂全程不发生——makeDirectoryPaths 为空即实证，第二文件的
        // 祖先链零 stat 只能来自臂 (a) 的 markEnsured（删该记录点必红，见任务报告）。
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localA = directory.filePath(QStringLiteral("a.bin"));
        const QString localB = directory.filePath(QStringLiteral("b.bin"));
        QVERIFY(writeFile(localA, QByteArray("payload")));
        QVERIFY(writeFile(localB, QByteArray("payload")));

        MockChannel channel;
        int ancestorStats = 0;
        channel.statHandler = [&ancestorStats](const QString& path, TransferFileStat& out) {
            if (path == QStringLiteral("/opt") || path == QStringLiteral("/opt/deep")
                || path == QStringLiteral("/opt/deep/dir")) {
                ++ancestorStats;
                out = {true, 0, QStringLiteral("dir")};  // 臂 (a)：首探即报存在
            } else if (path.contains(QStringLiteral(".deviceforge-part-"))) {
                out = {true, 7, QStringLiteral("temporary")};
            } else {
                out = {};  // 目标基线/复核报缺失：确保步必然进入
            }
            return true;
        };
        xfer::EnsuredDirCache cache;

        TransferExecutor first(channel, [](int) {}, &cache);
        const auto resultA = first.execute(
            uploadRequest(localA, QStringLiteral("/opt/deep/dir/a.bin")), m_cancel);
        QCOMPARE(resultA.state, TransferState::Succeeded);
        // 首文件：三层祖先逐层 stat 报存在，全程零 mkdir（臂 (b)/(c) 未发生）
        QCOMPARE(ancestorStats, 3);
        QCOMPARE(channel.makeDirectoryCalls, 0);
        const int ancestorsAfterFirst = ancestorStats;

        TransferExecutor second(channel, [](int) {}, &cache);
        const auto resultB = second.execute(
            uploadRequest(localB, QStringLiteral("/opt/deep/dir/b.bin")), m_cancel);
        QCOMPARE(resultB.state, TransferState::Succeeded);

        // 第二文件对整条祖先链零 stat：存在性缓存只能来自臂 (a) 的记录点
        QCOMPARE(ancestorStats, ancestorsAfterFirst);
        // 缓存项来自「stat-存在」臂而非创建臂——两文件合计仍为零 mkdir
        QCOMPARE(channel.makeDirectoryCalls, 0);
        QCOMPARE(channel.statPaths, QVector<QString>({
            QStringLiteral("/opt/deep/dir/a.bin"),
            QStringLiteral("/opt"),
            QStringLiteral("/opt/deep"),
            QStringLiteral("/opt/deep/dir"),
            channel.uploadTargets.at(0),
            QStringLiteral("/opt/deep/dir/a.bin"),
            QStringLiteral("/opt/deep/dir/b.bin"),
            channel.uploadTargets.at(1),
            QStringLiteral("/opt/deep/dir/b.bin")}));
        QVERIFY(cache.contains(QStringLiteral("/opt")));
        QVERIFY(cache.contains(QStringLiteral("/opt/deep")));
        QVERIFY(cache.contains(QStringLiteral("/opt/deep/dir")));
    }

    void reStatArmExistencePopulatesCacheForLaterFiles()
    {
        // 修复轮 1（评审 I-1）：记录点臂 (c)「mkdir 报失败后复检同一层报存在」的独立
        // 生效钉桩。夹具与臂 (a)/(b) 可由调用序列一眼区分：单祖先层 /opt——首探报缺失
        // （排除臂 (a)）、紧随其后的 mkdir 失败（排除臂 (b)）、再次的同层复检报存在
        // （臂 (c) 唯一发生点）；statPaths 中 /opt 连续出现两次即该臂的序列指纹。
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localA = directory.filePath(QStringLiteral("a.bin"));
        const QString localB = directory.filePath(QStringLiteral("b.bin"));
        QVERIFY(writeFile(localA, QByteArray("payload")));
        QVERIFY(writeFile(localB, QByteArray("payload")));

        MockChannel channel;
        channel.makeDirectoryResults = {false};
        channel.operationErrors = {connectionLostError()};  // MKD 已存在目录的连接层失败形态
        int optStats = 0;
        channel.statHandler = [&optStats](const QString& path, TransferFileStat& out) {
            if (path == QStringLiteral("/opt")) {
                // 首探报缺失 → MKD 失败 → 复检报存在（臂 (c)）
                out = (optStats == 0)
                    ? TransferFileStat{}
                    : TransferFileStat{true, 0, QStringLiteral("dir")};
                ++optStats;
                return true;
            }
            if (path.contains(QStringLiteral(".deviceforge-part-")))
                out = {true, 7, QStringLiteral("temporary")};
            else
                out = {};
            return true;
        };
        xfer::EnsuredDirCache cache;

        TransferExecutor first(channel, [](int) {}, &cache);
        const auto resultA = first.execute(
            uploadRequest(localA, QStringLiteral("/opt/a.bin")), m_cancel);
        QCOMPARE(resultA.state, TransferState::Succeeded);
        QCOMPARE(channel.makeDirectoryCalls, 1);
        QCOMPARE(channel.makeDirectoryPaths, QVector<QString>({QStringLiteral("/opt")}));
        const int optStatsAfterFirst = optStats;
        QCOMPARE(optStatsAfterFirst, 2);  // 首探 + MKD 失败后复检：臂 (c) 序列指纹

        TransferExecutor second(channel, [](int) {}, &cache);
        const auto resultB = second.execute(
            uploadRequest(localB, QStringLiteral("/opt/b.bin")), m_cancel);
        QCOMPARE(resultB.state, TransferState::Succeeded);

        // 第二文件整体跳过 /opt 的 stat 与 mkdir：缓存项只能来自臂 (c) 的记录点
        QCOMPARE(optStats, optStatsAfterFirst);
        QCOMPARE(channel.makeDirectoryCalls, 1);
        QCOMPARE(channel.statPaths, QVector<QString>({
            QStringLiteral("/opt/a.bin"),
            QStringLiteral("/opt"),
            QStringLiteral("/opt"),
            channel.uploadTargets.at(0),
            QStringLiteral("/opt/a.bin"),
            QStringLiteral("/opt/b.bin"),
            channel.uploadTargets.at(1),
            QStringLiteral("/opt/b.bin")}));
        QVERIFY(cache.contains(QStringLiteral("/opt")));
    }

    void nullEnsuredDirCacheKeepsParentProbeSequence()
    {
        // 默认参守卫（反向用例）：ensuredDirs=nullptr（全部既有调用点）时两个连续
        // 文件的 stat 序列与波2 现状逐字一致——第二文件仍重新 stat /opt、走 mkdir
        // 幂等臂逐层重建。本用例同时是可执行化的「nullptr 复现今日序列」证据。
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString localA = directory.filePath(QStringLiteral("a.bin"));
        const QString localB = directory.filePath(QStringLiteral("b.bin"));
        QVERIFY(writeFile(localA, QByteArray("payload")));
        QVERIFY(writeFile(localB, QByteArray("payload")));

        MockChannel channel;
        makeAtomicUploadStat(channel, 7);

        TransferExecutor first(channel, [](int) {});
        const auto resultA = first.execute(
            uploadRequest(localA, QStringLiteral("/opt/deep/dir/a.bin")), m_cancel);
        QCOMPARE(resultA.state, TransferState::Succeeded);

        TransferExecutor second(channel, [](int) {});
        const auto resultB = second.execute(
            uploadRequest(localB, QStringLiteral("/opt/deep/dir/b.bin")), m_cancel);
        QCOMPARE(resultB.state, TransferState::Succeeded);

        QCOMPARE(channel.statPaths, QVector<QString>({
            QStringLiteral("/opt/deep/dir/a.bin"),
            QStringLiteral("/opt"),
            channel.uploadTargets.at(0),
            QStringLiteral("/opt/deep/dir/a.bin"),
            QStringLiteral("/opt/deep/dir/b.bin"),
            QStringLiteral("/opt"),
            channel.uploadTargets.at(1),
            QStringLiteral("/opt/deep/dir/b.bin")}));
        QCOMPARE(channel.makeDirectoryPaths,
                 QVector<QString>({QStringLiteral("/opt"),
                                   QStringLiteral("/opt/deep"),
                                   QStringLiteral("/opt/deep/dir"),
                                   QStringLiteral("/opt"),
                                   QStringLiteral("/opt/deep"),
                                   QStringLiteral("/opt/deep/dir")}));
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
