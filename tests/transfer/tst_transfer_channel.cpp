#include <QtTest>

#include <atomic>
#include <future>
#include <memory>

#include "adapter/FtpAdapter.h"
#include "adapter/FtpPathUtils.h"
#include "adapter/IProtocolAdapter.h"
#include "adapter/SshAdapter.h"
#include "transfer/AdapterTransferChannel.h"

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
    }

    void sftpCapabilitiesAndUnknownProtocol()
    {
        auto probe = std::make_shared<OpsProbe>();
        AdapterTransferChannel sftp("sftp", std::make_shared<MockAdapter>(), makeOps(probe));
        const auto sftpCap = sftp.capabilities();
        QVERIFY(sftpCap.upload && sftpCap.download && sftpCap.rename && sftpCap.remove
                && sftpCap.stat);

        AdapterTransferChannel unknown("telnet", std::make_shared<MockAdapter>());
        const auto unknownCap = unknown.capabilities();
        QVERIFY(!unknownCap.upload && !unknownCap.download && !unknownCap.rename
                && !unknownCap.remove && !unknownCap.stat);
    }

    void productionBindingsExposeOnlyCallableCapabilities()
    {
        AdapterTransferChannel ftp("ftp", std::make_shared<FtpAdapter>());
        const auto ftpCap = ftp.capabilities();
        QVERIFY(ftpCap.upload && ftpCap.download && ftpCap.rename && ftpCap.remove && ftpCap.stat);

        AdapterTransferChannel sftp("sftp", std::make_shared<SshAdapter>());
        const auto sftpCap = sftp.capabilities();
        QVERIFY(sftpCap.upload && sftpCap.download && sftpCap.rename && sftpCap.remove
                && sftpCap.stat);
        QVERIFY(!sftp.upload(QStringLiteral("missing.bin"), QStringLiteral("/missing.bin")));
        QVERIFY(sftp.lastError().code != TransferErrorCode::Unsupported);

        AdapterTransferChannel mismatched("ftp", std::make_shared<MockAdapter>());
        const auto mismatchedCap = mismatched.capabilities();
        QVERIFY(!mismatchedCap.upload && !mismatchedCap.download && !mismatchedCap.rename
                && !mismatchedCap.remove && !mismatchedCap.stat);
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
