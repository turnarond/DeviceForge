#include <QtTest>

#include <atomic>
#include <future>
#include <memory>

#include "adapter/FtpAdapter.h"
#include "adapter/FtpPathUtils.h"
#include "adapter/IProtocolAdapter.h"
#include "adapter/SshAdapter.h"
#include "transfer/AdapterTransferChannel.h"
#include "transfer/LocalTransferChannel.h"

class MockAdapter final : public IProtocolAdapter
{
public:
    std::string protocolId() const override { return "mock"; }

    bool connect(const DeviceInfo& device, const AuthInfo& auth) override
    {
        ++connectCalls;
        lastDevice = device;
        lastAuth = auth;
        return connectResult;
    }

    void disconnect() override { ++disconnectCalls; }
    bool isConnected() const override { return connectResult; }
    std::string lastError() const override { return error; }
    std::future<Response> request(const Request&) override
    {
        return std::async(std::launch::deferred, [] { return Response{}; });
    }
    void subscribe(const Request&, StreamCallback) override {}
    void unsubscribe() override {}
    ProtocolCapability capability() const override { return {}; }

    bool connectResult = true;
    int connectCalls = 0;
    int disconnectCalls = 0;
    DeviceInfo lastDevice;
    AuthInfo lastAuth;
    std::string error;
};

class SshWithoutSftpAdapter final : public SshAdapter
{
public:
    bool connect(const DeviceInfo&, const AuthInfo&) override
    {
        ++connectCalls;
        connected = true;
        return true;
    }

    void disconnect() override
    {
        ++disconnectCalls;
        connected = false;
    }
    bool isConnected() const override { return connected; }
    std::string lastError() const override { return "SFTP subsystem initialization failed"; }

    int connectCalls = 0;
    int disconnectCalls = 0;
    bool connected = false;
};

struct OpsProbe
{
    QString uploadLocal;
    QString uploadRemote;
    QString downloadRemote;
    QString downloadLocal;
    QString renameFrom;
    QString renameTo;
    QString removedPath;
    QString listedPath;
    QString makeDirectoryPath;
    bool makeDirectoryResult = true;
    QVector<TransferChannelEntry> entries;
    bool listResult = true;
    int progressValue = -1;
    std::atomic_bool* cancelFlag = nullptr;
};

static TransferChannelOps makeOps(const std::shared_ptr<OpsProbe>& probe)
{
    TransferChannelOps ops;
    ops.upload = [probe](const QString& local, const QString& remote) {
        probe->uploadLocal = local;
        probe->uploadRemote = remote;
        return true;
    };
    ops.download = [probe](const QString& remote, const QString& local) {
        probe->downloadRemote = remote;
        probe->downloadLocal = local;
        return true;
    };
    ops.rename = [probe](const QString& from, const QString& to) {
        probe->renameFrom = from;
        probe->renameTo = to;
        return true;
    };
    ops.remove = [probe](const QString& path) {
        probe->removedPath = path;
        return true;
    };
    ops.makeDirectory = [probe](const QString& path) {
        probe->makeDirectoryPath = path;
        return probe->makeDirectoryResult;
    };
    ops.list = [probe](const QString& path, QVector<TransferChannelEntry>& entries) {
        probe->listedPath = path;
        entries = probe->entries;
        return probe->listResult;
    };
    ops.setProgressCallback = [probe](std::function<void(int)> callback) {
        if (callback)
            callback(37);
        probe->progressValue = 37;
    };
    ops.setCancelFlag = [probe](std::atomic_bool* flag) { probe->cancelFlag = flag; };
    return ops;
}

class TstTransferChannel : public QObject
{
    Q_OBJECT

private slots:
    void ftpCapabilities()
    {
        auto probe = std::make_shared<OpsProbe>();
        AdapterTransferChannel channel("ftp", std::make_shared<MockAdapter>(), makeOps(probe));

        const auto cap = channel.capabilities();

        QVERIFY(cap.upload);
        QVERIFY(cap.download);
        QVERIFY(cap.rename);
        QVERIFY(cap.remove);
        QVERIFY(cap.stat);
        QVERIFY(cap.makeDirectory);   // v2.15 波2：ops 注入含 makeDirectory 时能力位必须亮
    }

    void sftpCapabilitiesAndUnknownProtocol()
    {
        auto probe = std::make_shared<OpsProbe>();
        AdapterTransferChannel sftp("sftp", std::make_shared<MockAdapter>(), makeOps(probe));
        const auto sftpCap = sftp.capabilities();
        QVERIFY(sftpCap.upload && sftpCap.download && sftpCap.rename && sftpCap.remove
                && sftpCap.stat && sftpCap.makeDirectory);

        AdapterTransferChannel unknown("telnet", std::make_shared<MockAdapter>());
        const auto unknownCap = unknown.capabilities();
        QVERIFY(!unknownCap.upload && !unknownCap.download && !unknownCap.rename
                && !unknownCap.remove && !unknownCap.stat && !unknownCap.makeDirectory);
    }

    void productionBindingsExposeOnlyCallableCapabilities()
    {
        AdapterTransferChannel ftp("ftp", std::make_shared<FtpAdapter>());
        const auto ftpCap = ftp.capabilities();
        QVERIFY(ftpCap.upload && ftpCap.download && ftpCap.rename && ftpCap.remove && ftpCap.stat);
        QVERIFY(ftpCap.makeDirectory);   // FtpAdapter::makeDirectory 已接线（SylixOS 三禁核对见其实现）

        AdapterTransferChannel sftp("sftp", std::make_shared<SshAdapter>());
        const auto sftpCap = sftp.capabilities();
        QVERIFY(sftpCap.upload && sftpCap.download && sftpCap.rename && sftpCap.remove
                && sftpCap.stat && sftpCap.makeDirectory);
        QVERIFY(!sftp.upload(QStringLiteral("missing.bin"), QStringLiteral("/missing.bin")));
        QVERIFY(sftp.lastError().code != TransferErrorCode::Unsupported);

        AdapterTransferChannel mismatched("ftp", std::make_shared<MockAdapter>());
        const auto mismatchedCap = mismatched.capabilities();
        QVERIFY(!mismatchedCap.upload && !mismatchedCap.download && !mismatchedCap.rename
                && !mismatchedCap.remove && !mismatchedCap.stat && !mismatchedCap.makeDirectory);
        QVERIFY(!mismatched.upload(QStringLiteral("a.bin"), QStringLiteral("/a.bin")));
        QCOMPARE(mismatched.lastError().code, TransferErrorCode::Unsupported);
    }

    void reconnectUsesStoredConnectionParameters()
    {
        auto adapter = std::make_shared<MockAdapter>();
        auto probe = std::make_shared<OpsProbe>();
        AdapterTransferChannel channel("ftp", adapter, makeOps(probe));
        const DeviceInfo device{"10.0.0.8", 21, "ftp", "PLC-8", ""};
        const AuthInfo auth{"operator", "secret"};
        QVERIFY(channel.connect(device, auth));

        adapter->connectResult = false;
        adapter->error = "connection timed out";
        QVERIFY(!channel.reconnect());

        QCOMPARE(adapter->disconnectCalls, 1);
        QCOMPARE(adapter->connectCalls, 2);
        QCOMPARE(adapter->lastDevice.ip, device.ip);
        QCOMPARE(adapter->lastDevice.port, device.port);
        QCOMPARE(adapter->lastAuth.user, auth.user);
        QCOMPARE(adapter->lastAuth.password, auth.password);
        QCOMPARE(channel.lastError().code, TransferErrorCode::Timeout);
        QVERIFY(channel.lastError().retryable);
    }

    void clearCredentialsDropsReconnectSnapshot()
    {
        auto adapter = std::make_shared<MockAdapter>();
        auto probe = std::make_shared<OpsProbe>();
        AdapterTransferChannel channel("ftp", adapter, makeOps(probe));
        const DeviceInfo device{"10.0.0.8", 21, "ftp", "PLC-8", ""};
        const AuthInfo auth{"operator", "secret"};
        QVERIFY(channel.connect(device, auth));

        channel.clearCredentials();

        QVERIFY(!channel.reconnect());
        QCOMPARE(adapter->connectCalls, 1);
        QCOMPARE(adapter->disconnectCalls, 1);
        QCOMPARE(channel.lastError().code, TransferErrorCode::Unsupported);
    }

    void clearCredentialsErasesRealFtpAdapterStorage()
    {
        auto adapter = std::make_shared<FtpAdapter>();
        AdapterTransferChannel channel("ftp", adapter);
        const DeviceInfo device{"127.0.0.1", 1, "ftp", "credential-probe", ""};
        const AuthInfo auth{"operator", "secret-password"};

        QVERIFY(!channel.connect(device, auth));
        QVERIFY(!adapter_internal::ftpCredentialsCleared(*adapter));

        channel.clearCredentials();

        QVERIFY(adapter_internal::ftpCredentialsCleared(*adapter));
        QVERIFY(!adapter->isConnected());
    }

    void ftpCredentialBuffersAndDerivedOptionsAreWiped()
    {
        FtpAdapter adapter;
        const DeviceInfo device{"127.0.0.1", 1, "ftp", "credential-probe", ""};
        const AuthInfo longAuth{
            "operator-name-longer-than-small-string-storage",
            "password-value-longer-than-small-string-storage-and-never-logged"};
        const AuthInfo shortAuth{"u", "p"};

        QVERIFY(!adapter.connect(device, longAuth));
        QVERIFY(!adapter.connect(device, shortAuth));
        auto stats = adapter_internal::ftpCredentialWipeStats(adapter);
        QVERIFY(stats.lastUserWipeBytes >= longAuth.user.size());
        QVERIFY(stats.lastPasswordWipeBytes >= longAuth.password.size());

        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString uploadPath = temporaryDirectory.filePath(QStringLiteral("upload.bin"));
        QFile uploadFile(uploadPath);
        QVERIFY(uploadFile.open(QIODevice::WriteOnly));
        QCOMPARE(uploadFile.write("payload"), qint64(7));
        uploadFile.close();

        const int beforeUploadWipes = stats.derivedWipes;
        QVERIFY(!adapter.uploadFile(uploadPath.toUtf8().toStdString(), "/upload.bin"));
        stats = adapter_internal::ftpCredentialWipeStats(adapter);
        QCOMPARE(stats.derivedWipes, beforeUploadWipes + 1);
        QVERIFY(stats.lastDerivedWipeBytes >= shortAuth.user.size() + shortAuth.password.size() + 1);

        const int beforeDownloadWipes = stats.derivedWipes;
        const QString downloadPath = temporaryDirectory.filePath(QStringLiteral("download.bin"));
        QVERIFY(!adapter.downloadFile("/download.bin", downloadPath.toUtf8().toStdString()));
        stats = adapter_internal::ftpCredentialWipeStats(adapter);
        QCOMPARE(stats.derivedWipes, beforeDownloadWipes + 1);
        QVERIFY(adapter_internal::ftpDownloadCancelHookConfigured(adapter));
    }

    void ftpDownloadProgressCallbackObservesCancellation()
    {
        FtpAdapter adapter;
        std::atomic_bool cancelled{true};
        adapter.setCancelFlag(&cancelled);

        QCOMPARE(adapter_internal::invokeFtpProgressCallback(adapter, 100, 20, 0, 0), 1);

        cancelled = false;
        int progress = -1;
        adapter.setProgressCallback([&progress](int value) { progress = value; });
        QCOMPARE(adapter_internal::invokeFtpProgressCallback(adapter, 100, 40, 0, 0), 0);
        QCOMPARE(progress, 40);
    }

    void sftpDownloadCancellationPredicateObservesExternalFlag()
    {
        std::atomic_bool cancelled{false};
        QVERIFY(!adapter_internal::sftpTransferCancellationRequested(&cancelled));
        cancelled = true;
        QVERIFY(adapter_internal::sftpTransferCancellationRequested(&cancelled));
        QVERIFY(!adapter_internal::sftpTransferCancellationRequested(nullptr));
    }

    void clearCredentialsDisconnectsObservableSshAdapter()
    {
        auto adapter = std::make_shared<SshWithoutSftpAdapter>();
        auto probe = std::make_shared<OpsProbe>();
        AdapterTransferChannel channel("sftp", adapter, makeOps(probe));
        const DeviceInfo device{"10.0.0.9", 22, "ssh", "PLC-9", ""};
        const AuthInfo auth{"operator", "secret"};
        QVERIFY(channel.connect(device, auth));
        QVERIFY(adapter->isConnected());

        channel.clearCredentials();

        QCOMPARE(adapter->disconnectCalls, 1);
        QVERIFY(!adapter->isConnected());
        QVERIFY(!channel.reconnect());
    }

    void sftpConnectFailsWhenSubsystemIsNotReady()
    {
        auto adapter = std::make_shared<SshWithoutSftpAdapter>();
        AdapterTransferChannel channel("sftp", adapter);
        const DeviceInfo device{"10.0.0.9", 22, "ssh", "PLC-9", ""};
        const AuthInfo auth{"operator", "secret"};

        QVERIFY(!channel.connect(device, auth));

        QCOMPARE(adapter->connectCalls, 1);
        QCOMPARE(adapter->disconnectCalls, 1);
        QCOMPARE(channel.lastError().code, TransferErrorCode::RemoteIo);
        QVERIFY(channel.lastError().message.contains(QStringLiteral("SFTP")));
    }

    void sftpReconnectFailsWhenSubsystemRemainsNotReady()
    {
        auto adapter = std::make_shared<SshWithoutSftpAdapter>();
        AdapterTransferChannel channel("sftp", adapter);
        const DeviceInfo device{"10.0.0.10", 22, "ssh", "PLC-10", ""};
        const AuthInfo auth{"operator", "secret"};
        QVERIFY(!channel.connect(device, auth));

        QVERIFY(!channel.reconnect());

        QCOMPARE(adapter->connectCalls, 2);
        QCOMPARE(adapter->disconnectCalls, 3);
        QVERIFY(!adapter->isConnected());
        QCOMPARE(channel.lastError().code, TransferErrorCode::RemoteIo);
        QCOMPARE(channel.lastError().message,
                 QStringLiteral("SFTP subsystem initialization failed"));
    }

    void forwardsFileOperationsAndHooks()
    {
        auto probe = std::make_shared<OpsProbe>();
        AdapterTransferChannel channel("sftp", std::make_shared<MockAdapter>(), makeOps(probe));

        QVERIFY(channel.upload("C:/images/a.bin", "/opt/a.bin.part"));
        QVERIFY(channel.download("/opt/a.bin", "C:/downloads/a.bin.part"));
        QVERIFY(channel.rename("/opt/a.bin.part", "/opt/a.bin"));
        QVERIFY(channel.remove("/opt/stale.bin"));

        QCOMPARE(probe->uploadLocal, QStringLiteral("C:/images/a.bin"));
        QCOMPARE(probe->uploadRemote, QStringLiteral("/opt/a.bin.part"));
        QCOMPARE(probe->downloadRemote, QStringLiteral("/opt/a.bin"));
        QCOMPARE(probe->downloadLocal, QStringLiteral("C:/downloads/a.bin.part"));
        QCOMPARE(probe->renameFrom, QStringLiteral("/opt/a.bin.part"));
        QCOMPARE(probe->renameTo, QStringLiteral("/opt/a.bin"));
        QCOMPARE(probe->removedPath, QStringLiteral("/opt/stale.bin"));

        int progress = -1;
        channel.setProgressCallback([&progress](int value) { progress = value; });
        QCOMPARE(progress, 37);
        QCOMPARE(probe->progressValue, 37);

        std::atomic_bool cancelled{false};
        channel.setCancelFlag(&cancelled);
        QCOMPARE(probe->cancelFlag, &cancelled);
    }

    // F6 清理源时，已到达的取消不得再开始不可逆删除。
    void cancellationBeforeRemovePreventsSourceCleanup()
    {
        auto probe = std::make_shared<OpsProbe>();
        AdapterTransferChannel channel("sftp", std::make_shared<MockAdapter>(), makeOps(probe));
        std::atomic_bool cancelled{true};
        channel.setCancelFlag(&cancelled);

        QVERIFY(!channel.remove(QStringLiteral("/source.bin")));
        QCOMPARE(channel.lastError().code, TransferErrorCode::Cancelled);
        QVERIFY(probe->removedPath.isEmpty());
    }

    // v2.15 波2（任务 8）：单层 makeDirectory 能力接线
    void makeDirectoryForwardsToOperationsAndReportsCapabilityGap()
    {
        auto probe = std::make_shared<OpsProbe>();
        AdapterTransferChannel channel("ftp", std::make_shared<MockAdapter>(), makeOps(probe));
        QVERIFY(channel.makeDirectory(QStringLiteral("/opt/new")));
        QCOMPARE(probe->makeDirectoryPath, QStringLiteral("/opt/new"));

        auto failingProbe = std::make_shared<OpsProbe>();
        failingProbe->makeDirectoryResult = false;
        AdapterTransferChannel failing("sftp", std::make_shared<MockAdapter>(),
                                       makeOps(failingProbe));
        QVERIFY(!failing.makeDirectory(QStringLiteral("/opt/new")));
        // ops 已注入：失败来自底层原语而非能力缺口
        QVERIFY(failing.lastError().code != TransferErrorCode::Unsupported);

        TransferChannelOps opsWithoutMakeDirectory = makeOps(std::make_shared<OpsProbe>());
        opsWithoutMakeDirectory.makeDirectory = nullptr;
        AdapterTransferChannel unsupported("ftp", std::make_shared<MockAdapter>(),
                                           std::move(opsWithoutMakeDirectory));
        QVERIFY(!unsupported.capabilities().makeDirectory);
        QVERIFY(!unsupported.makeDirectory(QStringLiteral("/opt/new")));
        QCOMPARE(unsupported.lastError().code, TransferErrorCode::Unsupported);
    }

    void localMakeDirectoryCreatesSingleLevelAndIsIdempotent()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LocalTransferChannel channel;
        QVERIFY(channel.capabilities().makeDirectory);

        const QString target = QDir(directory.path()).filePath(QStringLiteral("bundle"));
        QVERIFY(channel.makeDirectory(target));
        QVERIFY(QFileInfo(target).isDir());
        // 已存在=幂等成功（执行器 stat 竞态窗口下的兜底臂）
        QVERIFY(channel.makeDirectory(target));
        QCOMPARE(channel.lastError().code, TransferErrorCode::None);
    }

    void localMakeDirectoryRejectsMissingIntermediateParentAndFileConflict()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LocalTransferChannel channel;

        // 单层语义：中间父不存在→失败（递归逐级创建归执行器确保步，通道不 mkpath 全路径）
        const QString deep = QDir(directory.path()).filePath(QStringLiteral("missing/deep"));
        QVERIFY(!channel.makeDirectory(deep));
        QCOMPARE(channel.lastError().code, TransferErrorCode::LocalIo);
        QVERIFY(!QFileInfo::exists(QDir(directory.path()).filePath(QStringLiteral("missing"))));

        // 同名普通文件占位→拒绝
        const QString taken = QDir(directory.path()).filePath(QStringLiteral("taken.bin"));
        QFile file(taken);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x");
        file.close();
        QVERIFY(!channel.makeDirectory(taken));
        QCOMPARE(channel.lastError().code, TransferErrorCode::InvalidPath);
    }

    void productionFtpBindingOpensUtf8LocalPath()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString localPath = temporaryDirectory.filePath(QStringLiteral("设备 固件.bin"));
        QFile localFile(localPath);
        QVERIFY(localFile.open(QIODevice::WriteOnly));
        QCOMPARE(localFile.write("firmware"), qint64(8));
        localFile.close();

        auto adapter = std::make_shared<FtpAdapter>();
        AdapterTransferChannel channel("ftp", adapter);
        const auto cap = channel.capabilities();
        QVERIFY(cap.upload && cap.download && cap.rename && cap.remove && cap.stat);

        QVERIFY(!channel.upload(localPath, QStringLiteral("/firmware.bin")));

        QVERIFY2(!channel.lastError().message.contains(QStringLiteral("无法打开本地文件")),
                 qPrintable(channel.lastError().message));
        QCOMPARE(channel.lastError().code, TransferErrorCode::RemoteIo);
    }

    void productionFtpDownloadCreatesUtf8LocalPath()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString localPath = temporaryDirectory.filePath(QStringLiteral("下载 固件.bin"));
        QVERIFY(!QFileInfo::exists(localPath));

        AdapterTransferChannel channel("ftp", std::make_shared<FtpAdapter>());
        QVERIFY(!channel.download(QStringLiteral("/firmware.bin"), localPath));

        QVERIFY2(QFileInfo::exists(localPath), "FTP 下载未以 UTF-8 契约创建本地文件");
        QVERIFY(!channel.lastError().message.contains(QStringLiteral("无法创建本地文件")));
    }

    void productionFtpFolderUploadEnumeratesUtf8Names()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString subdirectory = temporaryDirectory.filePath(QStringLiteral("子目录"));
        QVERIFY(QDir().mkpath(subdirectory));
        const QString localPath = QDir(subdirectory).filePath(QStringLiteral("固件.bin"));
        QFile localFile(localPath);
        QVERIFY(localFile.open(QIODevice::WriteOnly));
        QCOMPARE(localFile.write("firmware"), qint64(8));
        localFile.close();

        FtpAdapter adapter;
        QVERIFY(!adapter.uploadFolder(temporaryDirectory.path().toUtf8().toStdString(), "/apps"));

        const QString error = QString::fromUtf8(adapter.lastError());
        QVERIFY2(!error.contains(QStringLiteral("无法打开本地文件")), qPrintable(error));
    }

    void statMatchesExactBasename()
    {
        auto probe = std::make_shared<OpsProbe>();
        probe->entries = {
            {"firmware.bin.old", 3, "2026-09-10 08:00:00"},
            {"firmware.bin", 4096, "2026-09-11 09:30:00"},
        };
        AdapterTransferChannel channel("ftp", std::make_shared<MockAdapter>(), makeOps(probe));
        TransferFileStat result;

        QVERIFY(channel.stat("/opt/images/firmware.bin", result));

        QCOMPARE(probe->listedPath, QStringLiteral("/opt/images"));
        QVERIFY(result.exists);
        QCOMPARE(result.size, quint64(4096));
        QCOMPARE(result.modifiedText, QStringLiteral("2026-09-11 09:30:00"));
    }

    void statMissingFileIsSuccessfulAbsence()
    {
        auto probe = std::make_shared<OpsProbe>();
        probe->entries = {{"other.bin", 17, "2026-09-09 07:00:00"}};
        AdapterTransferChannel channel("ftp", std::make_shared<MockAdapter>(), makeOps(probe));
        TransferFileStat result{true, 99, "stale"};

        QVERIFY(channel.stat("/target.bin", result));

        QCOMPARE(probe->listedPath, QStringLiteral("/"));
        QVERIFY(!result.exists);
        QCOMPARE(result.size, quint64(0));
        QVERIFY(result.modifiedText.isEmpty());
    }

    void statRelativeBasenameUsesAdapterCurrentDirectory()
    {
        auto probe = std::make_shared<OpsProbe>();
        probe->entries = {{"firmware.bin", 23, ""}};
        AdapterTransferChannel channel("ftp", std::make_shared<MockAdapter>(), makeOps(probe));
        TransferFileStat result;

        QVERIFY(channel.stat("firmware.bin", result));

        QCOMPARE(probe->listedPath, QStringLiteral("."));
        QVERIFY(result.exists);
        QCOMPARE(result.size, quint64(23));
    }

    void statPreservesTrailingSpaceInFilename()
    {
        auto probe = std::make_shared<OpsProbe>();
        probe->entries = {{"firmware.bin ", 29, ""}};
        AdapterTransferChannel channel("ftp", std::make_shared<MockAdapter>(), makeOps(probe));
        TransferFileStat result;

        QVERIFY(channel.stat("/opt/firmware.bin ", result));

        QCOMPARE(probe->listedPath, QStringLiteral("/opt"));
        QVERIFY(result.exists);
        QCOMPARE(result.size, quint64(29));
    }

    void productionFtpStatPreservesSpaceInParentPath()
    {
        auto probe = std::make_shared<OpsProbe>();
        probe->entries = {{"firmware.bin", 41, ""}};
        AdapterTransferChannel channel("ftp", std::make_shared<MockAdapter>(), makeOps(probe));
        TransferFileStat result;

        QVERIFY(channel.stat("/dir /firmware.bin", result));

        QCOMPARE(probe->listedPath, QStringLiteral("/dir "));
        const std::string requestPath = adapter_internal::ftpDirectoryPathForListing(
            probe->listedPath.toUtf8().toStdString());
        QCOMPARE(QString::fromUtf8(requestPath), QStringLiteral("/dir /"));
        QCOMPARE(QString::fromUtf8(adapter_internal::ftpDirectoryPathForListing("")),
                 QStringLiteral("/"));
        QCOMPARE(QString::fromUtf8(adapter_internal::ftpDirectoryPathForListing("/")),
                 QStringLiteral("/"));
        QVERIFY(result.exists);
    }

    void productionFtpUrlBuilderEncodesRemotePathOnce()
    {
        QCOMPARE(QString::fromStdString(adapter_internal::buildFtpUrl(
                     false, "10.0.0.8", 21, "")),
                 QStringLiteral("ftp://10.0.0.8:21/"));
        QCOMPARE(QString::fromStdString(adapter_internal::buildFtpUrl(
                     false, "10.0.0.8", 21, "/")),
                 QStringLiteral("ftp://10.0.0.8:21/"));
        QCOMPARE(QString::fromStdString(adapter_internal::buildFtpUrl(
                     true, "10.0.0.8", 990, "/plain/firmware.bin")),
                 QStringLiteral("ftps://10.0.0.8:990/plain/firmware.bin"));

        const std::string specialPath = QStringLiteral("/dir /固件%20#?.bin")
                                            .toUtf8().toStdString();
        QCOMPARE(QString::fromStdString(adapter_internal::buildFtpUrl(
                     false, "10.0.0.8", 21, specialPath)),
                 QStringLiteral("ftp://10.0.0.8:21/dir%20/"
                                "%E5%9B%BA%E4%BB%B6%2520%23%3F.bin"));
    }

    void statDoesNotTreatBackslashAsRemoteSeparator()
    {
        auto probe = std::make_shared<OpsProbe>();
        probe->entries = {{"folder\\firmware.bin", 31, ""}};
        AdapterTransferChannel channel("sftp", std::make_shared<MockAdapter>(), makeOps(probe));
        TransferFileStat result;

        QVERIFY(channel.stat("folder\\firmware.bin", result));

        QCOMPARE(probe->listedPath, QStringLiteral("."));
        QVERIFY(result.exists);
        QCOMPARE(result.size, quint64(31));
    }

    void statListFailurePreservesAdapterError()
    {
        auto adapter = std::make_shared<MockAdapter>();
        adapter->error = "permission denied while listing directory";
        auto probe = std::make_shared<OpsProbe>();
        probe->listResult = false;
        AdapterTransferChannel channel("sftp", adapter, makeOps(probe));
        TransferFileStat result;

        QVERIFY(!channel.stat("/secure/firmware.bin", result));

        QCOMPARE(channel.lastError().code, TransferErrorCode::Permission);
        QVERIFY(!channel.lastError().retryable);
    }
};

QTEST_MAIN(TstTransferChannel)
#include "tst_transfer_channel.moc"
