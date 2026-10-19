#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QDir>        // v2.15 任务9 单发镜像用例夹具建子目录
#include <QDirIterator>  // v2.15 波2 任务10 环回目标树逐文件镜像断言
#include <QFile>
#include "adapter/IProtocolAdapter.h"
#include "adapter/IDeployable.h"
#include "adapter/ProtocolRegistry.h"
#include "deploy/DeployEntryBuilder.h"     // v2.15 波2 任务10 入口装配缝（真盘 scanner）
#include "framework/DeviceInfo.h"
#include "transfer/LocalTransferChannel.h" // v2.15 波2 任务10 真盘传输链路通道
#include "tools/FtpDeployTool/FtpDeployBackend.h"
#include "tools/FtpDeployTool/DeploymentRunner.h"
#include "MemoryDeployChannel.h"
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <chrono>

// Mock 通道：可配置 connect/upload 成败，记录调用
class MockDeployable : public IProtocolAdapter, public IDeployable {
public:
    std::string protocolId() const override { return "mock"; }
    bool connect(const DeviceInfo&, const AuthInfo&) override { return m_connectOk; }
    void disconnect() override {}
    bool isConnected() const override { return false; }
    std::string lastError() const override { return m_error; }
    std::future<Response> request(const Request&) override {
        return std::async(std::launch::deferred, [] { return Response{}; });
    }
    void subscribe(const Request&, StreamCallback) override {}
    void unsubscribe() override {}
    ProtocolCapability capability() const override { return {}; }

    // v2.8 Task 4：记录进度回调并在上传时发射一次（单文件 50%），
    // 驱动 per-device 进度管道走通 DeployJob 折算 → Runner 收口 → 后端分诊全链
    void setProgressCallback(std::function<void(int)> cb) override {
        m_progressFn = std::move(cb);
    }
    void setCancelFlag(std::atomic<bool>* f) override { m_flag = f; }

    bool uploadFile(const std::string& local, const std::string&) override {
        ++uploadCalls;
        if (m_progressFn) m_progressFn(50);
        // 确定性闸门：releaseGate=false 时阻塞（模拟慢传输），
        // 让测试能在第一台挂起期间注入取消，验证第二台被跳过
        while (!releaseGate.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return m_uploadOk && local != failedLocal;
    }
    bool uploadFolder(const std::string&, const std::string&) override {
        ++uploadCalls;
        if (m_progressFn) m_progressFn(50);
        while (!releaseGate.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return m_uploadOk;
    }
    bool clearRemoteDirectory(const std::string&) override { ++clearCalls; return true; }

    bool m_connectOk = true;
    bool m_uploadOk = true;
    std::string m_error;
    std::atomic<bool>* m_flag = nullptr;
    std::atomic<int> uploadCalls{0};
    std::atomic<int> clearCalls{0};
    std::string failedLocal;
    std::atomic<bool> releaseGate{false};
    std::function<void(int)> m_progressFn;
};

class TstDeployLoop : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_tmpDir;      // 自包含夹具：临时目录，RAII 自动清理
    std::string m_fixtureFile;   // 临时目录中的上传源文件（真实存在的文件）

private slots:
    void retryAfterInitialConnectionFailure_stillClearsDirectory() {
        auto mock = std::make_shared<MockDeployable>();
        mock->m_connectOk = false;
        mock->releaseGate = true;
        ProtocolRegistry::instance()->registerFactory("mock_connect_retry", [mock] { return mock; });
        FtpDeployBackend backend;
        backend.setTransferChannelFactory(memoryDeployChannelFactory());
        QCOMPARE(backend.OnStart(0, nullptr), 0);
        backend.bindDevices({{"192.168.1.1", 21, "", "", ""}});
        backend.bindCredentials({"u", "p"});
        std::atomic<bool> finished{false};
        backend.setFinishedCallback([&](bool, const auto&, const auto&) { finished = true; });
        backend.startUpload({m_fixtureFile}, "/apps", true, false, "mock_connect_retry");
        QTRY_VERIFY_WITH_TIMEOUT(finished.load(), 5000);
        QCOMPARE(backend.lastReport().results.front().state, DeviceResult::Failed);
        QCOMPARE(mock->clearCalls.load(), 0);
        mock->m_connectOk = true;
        finished = false;
        backend.resumePreviousFailures();
        backend.startUpload({m_fixtureFile}, "/apps", true, false, "mock_connect_retry");
        QTRY_VERIFY_WITH_TIMEOUT(finished.load(), 5000);
        QCOMPARE(backend.lastReport().results.front().state, DeviceResult::Ok);
        QCOMPARE(mock->clearCalls.load(), 1);
        QCOMPARE(mock->uploadCalls.load(), 1);
        backend.OnStop();
    }
    void explicitBackendRetry_skipsOnlyPreviousDelivery_withoutClearing() {
        auto mock = std::make_shared<MockDeployable>();
        mock->releaseGate = true;
        const std::string bad = m_tmpDir.filePath("retry-bad.txt").toStdString();
        QFile file(QString::fromStdString(bad));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("retry fixture");
        file.close();
        mock->failedLocal = bad;
        ProtocolRegistry::instance()->registerFactory("mock_retry", [mock] { return mock; });
        FtpDeployBackend backend;
        backend.setTransferChannelFactory(memoryDeployChannelFactory());
        QCOMPARE(backend.OnStart(0, nullptr), 0);
        backend.bindDevices({{"192.168.1.1", 21, "", "", ""}});
        backend.bindCredentials({"u", "p"});
        std::atomic<bool> finished{false};
        backend.setFinishedCallback([&](bool, const auto&, const auto&) { finished = true; });
        backend.startUpload({m_fixtureFile, bad}, "/apps", true, false, "mock_retry");
        QTRY_VERIFY_WITH_TIMEOUT(finished.load(), 5000);
        QCOMPARE(backend.lastReport().results.front().state, DeviceResult::Failed);
        QCOMPARE(mock->uploadCalls.load(), 2);
        mock->failedLocal.clear();
        finished = false;
        backend.resumePreviousFailures();
        backend.startUpload({m_fixtureFile, bad}, "/apps", true, false, "mock_retry");
        QTRY_VERIFY_WITH_TIMEOUT(finished.load(), 5000);
        QCOMPARE(backend.lastReport().results.front().state, DeviceResult::Ok);
        QCOMPARE(mock->uploadCalls.load(), 3);
        QCOMPARE(mock->clearCalls.load(), 1);
        finished = false;
        backend.startUpload({m_fixtureFile, bad}, "/apps", true, false, "mock_retry");
        QTRY_VERIFY_WITH_TIMEOUT(finished.load(), 5000);
        QCOMPARE(mock->uploadCalls.load(), 5);
        QCOMPARE(mock->clearCalls.load(), 2);
        backend.OnStop();
    }
    void initTestCase() {
        QVERIFY(m_tmpDir.isValid());
        m_fixtureFile = m_tmpDir.filePath("a.txt").toStdString();
        QFile file(QString::fromStdString(m_fixtureFile));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("deploy-loop-test-fixture");
        file.close();
    }

    void deploy_twoDevices_allSuccess() {
        auto* mock = new MockDeployable;
        // no-op deleter：mock 生命周期由测试管理，避免工厂多次 create 造成 double free
        ProtocolRegistry::instance()->registerFactory("mock",
            [mock] { return std::shared_ptr<IProtocolAdapter>(mock, [](IProtocolAdapter*) {}); });
        mock->releaseGate = true;  // 本用例不注入取消，直接放闸
        FtpDeployBackend backend;
        backend.setTransferChannelFactory(memoryDeployChannelFactory());
        QCOMPARE(backend.OnStart(0, nullptr), 0);
        backend.bindDevices({{"192.168.1.1", 0, "", "", ""}, {"192.168.1.2", 0, "", "", ""}});
        backend.bindCredentials({"user", "pass"});

        std::vector<std::string> ok, fail;
        std::atomic<bool> finished{false};
        backend.setFinishedCallback([&](bool, const std::vector<std::string>& s,
                                        const std::vector<std::string>& f) {
            finished = true; ok = s; fail = f;
        });
        backend.startUpload({m_fixtureFile}, "/apps", false, false, "mock");
        QTRY_VERIFY_WITH_TIMEOUT(finished.load(), 5000);
        QCOMPARE(ok.size(), 2);   // 两台都成功
        QCOMPARE(fail.size(), 0);
        QCOMPARE(mock->uploadCalls.load(), 2);
        backend.OnStop();
    }
    void deploy_connectFail_deviceSkipped() {
        auto* mock = new MockDeployable;
        mock->m_connectOk = false;
        // no-op deleter：同上，工厂按设备逐台 create，默认 deleter 会在首个
        // shared_ptr 析构时释放 mock，后续设备迭代再 create 即悬垂/双重释放
        ProtocolRegistry::instance()->registerFactory("mock_fail",
            [mock] { return std::shared_ptr<IProtocolAdapter>(mock, [](IProtocolAdapter*) {}); });
        mock->releaseGate = true;  // 本用例不注入取消，直接放闸
        FtpDeployBackend backend;
        backend.setTransferChannelFactory(memoryDeployChannelFactory());
        QCOMPARE(backend.OnStart(0, nullptr), 0);
        backend.bindDevices({{"192.168.1.1", 0, "", "", ""}, {"192.168.1.2", 0, "", "", ""}});
        backend.bindCredentials({"u", "p"});
        std::vector<std::string> ok, fail; std::atomic<bool> finished{false};
        backend.setFinishedCallback([&](bool, const std::vector<std::string>& s,
                                        const std::vector<std::string>& f) {
            finished = true; ok = s; fail = f;
        });
        backend.startUpload({m_fixtureFile}, "/apps", false, false, "mock_fail");
        QTRY_VERIFY_WITH_TIMEOUT(finished.load(), 5000);
        QCOMPARE(ok.size(), 0);
        QCOMPARE(fail.size(), 2);
        backend.OnStop();
    }
    void deploy_cancelStopsRemaining() {
        auto* mock = new MockDeployable;
        // no-op deleter：同上
        ProtocolRegistry::instance()->registerFactory("mock_cancel",
            [mock] { return std::shared_ptr<IProtocolAdapter>(mock, [](IProtocolAdapter*) {}); });
        FtpDeployBackend backend;
        backend.setTransferChannelFactory(memoryDeployChannelFactory());
        QCOMPARE(backend.OnStart(0, nullptr), 0);
        backend.bindDevices({{"192.168.1.1", 0, "", "", ""}, {"192.168.1.2", 0, "", "", ""}});
        backend.bindCredentials({"u", "p"});
        std::vector<std::string> ok, fail; std::atomic<bool> finished{false};
        backend.setFinishedCallback([&](bool, const std::vector<std::string>& s,
                                        const std::vector<std::string>& f) {
            finished = true; ok = s; fail = f;
        });
        // 确定性取消验证：第一台在闸门处挂起 → 注入取消 → 放闸 →
        // 第一台尚未校验/提交即取消，第二台被跳过（uploadCalls 必须 == 1）
        backend.startUpload({m_fixtureFile}, "/apps", false, false, "mock_cancel");
        QTRY_VERIFY_WITH_TIMEOUT(mock->uploadCalls.load() >= 1, 5000);  // 第一台已进入上传（挂起中）
        backend.cancelUpload();
        mock->releaseGate = true;                                        // 放闸，第一台完成
        QTRY_VERIFY_WITH_TIMEOUT(finished.load(), 5000);
        QCOMPARE(mock->uploadCalls.load(), 1);                           // 第二台被取消跳过
        QCOMPARE(ok.size(), 0);
        QCOMPARE(fail.size(), 0);
        for (const auto& result : backend.lastReport().results)
            QCOMPARE(result.state, DeviceResult::Cancelled);
        backend.OnStop();
    }

    // v2.8 Task 4：startUpload → Runner 接线后的进度分诊——
    //   · per-device 通道收到两台 "ip:port" 键（端口覆盖后取值），且无 kOverallKey 混入
    //   · 聚合总进度通道（kOverallKey 分流目标）仍有事件
    //   · 完成回调 successes/failures 映射保持串行时代语义
    void deploy_deviceProgressChannel_wired() {
        auto* mock = new MockDeployable;
        ProtocolRegistry::instance()->registerFactory("mock_prog",
            [mock] { return std::shared_ptr<IProtocolAdapter>(mock, [](IProtocolAdapter*) {}); });
        mock->releaseGate = true;

        FtpDeployBackend backend;
        backend.setTransferChannelFactory(memoryDeployChannelFactory());
        QCOMPARE(backend.OnStart(0, nullptr), 0);
        backend.bindDevices({{"10.0.0.1", 21, "", "", ""}, {"10.0.0.2", 21, "", "", ""}});
        backend.bindCredentials({"u", "p"});

        std::mutex mux;                                   // 回调来自池线程，容器访问加锁
        std::map<std::string, int> devicePcts;            // key → 最后一次上报百分比
        std::atomic<int> overallEvents{0};
        backend.setDeviceProgressCallback([&](const std::string& key, int pct) {
            std::lock_guard<std::mutex> lock(mux);
            devicePcts[key] = pct;
        });
        backend.setProgressCallback([&](int) { ++overallEvents; });

        std::vector<std::string> ok, fail; std::atomic<bool> finished{false};
        backend.setFinishedCallback([&](bool, const std::vector<std::string>& s,
                                        const std::vector<std::string>& f) {
            finished = true; ok = s; fail = f;
        });

        backend.startUpload({m_fixtureFile}, "/apps", false, false, "mock_prog");
        QTRY_VERIFY_WITH_TIMEOUT(finished.load(), 5000);

        {
            std::lock_guard<std::mutex> lock(mux);
            QVERIFY(devicePcts.find("10.0.0.1:21") != devicePcts.end());
            QVERIFY(devicePcts.find("10.0.0.2:21") != devicePcts.end());
            QCOMPARE(static_cast<int>(devicePcts.size()), 2);   // 无多余键
            // 哨兵键必须被后端分流进总进度通道，绝不允许漏进 per-device 行
            QVERIFY(devicePcts.find(DeploymentRunner::kOverallKey) == devicePcts.end());
            // 单文件 × mock 上报 50% → 折算 (0*100+50)/1 = 50
            QCOMPARE(devicePcts.at("10.0.0.1:21"), 50);
            QCOMPARE(devicePcts.at("10.0.0.2:21"), 50);
        }
        QVERIFY(overallEvents.load() >= 1);   // 总进度通道存活（含强制末次）
        QCOMPARE(ok.size(), 2);
        QCOMPARE(fail.size(), 0);
        backend.OnStop();
    }

    // v2.15 任务9 单发镜像用例（控制器裁定：每条目远端路径贯通 Backend/Job/Runner）：
    // ① entryRemotePaths 非空 → 每条目按传入远端全路径交付（镜像相对段直达 Job，
    //    不再派生 basename）；② 同本地清单仅远端布局变化 → requestKey 不同，
    //    显式续传请求**不得**误判同一请求（防错误续传）；③ entryRemotePaths 为空
    //    （纯文件扁平口径）→ 键与 v2.14 逐字相同——与显式条目模式的同文件清单
    //    互不误配，且既有扁平续传用例（explicitBackendRetry_…）继续绿即为零漂移证据。
    void singleShotEntryRemotePaths_mirrorAndResumeIdentity() {
        // 夹具：pkg/bin/app.conf（模拟入口展开产物：本地普通文件+镜像远端段）
        QVERIFY(QDir(m_tmpDir.filePath(QStringLiteral("pkg"))).mkpath(QStringLiteral("bin")));
        const QString appConf = m_tmpDir.filePath(QStringLiteral("pkg/bin/app.conf"));
        {
            QFile f(appConf);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("mirror-fixture");
        }
        const std::string mirrorLocal = appConf.toStdString();
        const std::string mirrorRemote = "/apps/pkg/bin/app.conf";   // 相对段随条目贯通
        auto* mock = new MockDeployable;
        ProtocolRegistry::instance()->registerFactory("mock_mirror",
            [mock] { return std::shared_ptr<IProtocolAdapter>(mock, [](IProtocolAdapter*) {}); });
        mock->releaseGate = true;
        FtpDeployBackend backend;
        backend.setTransferChannelFactory(memoryDeployChannelFactory());
        QCOMPARE(backend.OnStart(0, nullptr), 0);
        backend.bindDevices({{"192.168.1.9", 21, "", "", ""}});
        backend.bindCredentials({"u", "p"});
        std::atomic<bool> finished{false};
        backend.setFinishedCallback([&](bool, const auto&, const auto&) { finished = true; });

        const std::vector<std::string> files = {m_fixtureFile, mirrorLocal};
        const std::vector<std::string> entry = {"/apps/a.txt", mirrorRemote};
        // 首轮：镜像远端布局全量交付（entry 直达 Executor 目标路径）
        backend.startUpload(files, "/apps", false, false, "mock_mirror",
                            false, 0, RebootOptions{}, entry);
        QTRY_VERIFY_WITH_TIMEOUT(finished.load(), 5000);
        {
            // lastReport() 按值返回：先持副本再引用内部向量（防悬垂）
            const DeployReport first = backend.lastReport();
            QCOMPARE(first.results.front().state, DeviceResult::Ok);
            const auto& delivered = first.results.front().deliveredFiles;
            QCOMPARE(delivered.size(), std::size_t{2});
            QCOMPARE(delivered[0].remotePath, std::string("/apps/a.txt"));
            QCOMPARE(delivered[1].remotePath, mirrorRemote);   // 相对段未被拍平
        }

        // 仅结构变化（同本地清单、不同远端布局）+ 显式续传 → 禁止误判同一请求
        const int callsBefore = mock->uploadCalls.load();
        finished = false;
        backend.resumePreviousFailures();
        backend.startUpload(files, "/apps", false, false, "mock_mirror",
                            false, 0, RebootOptions{},
                            {"/apps/a.txt", "/apps/pkg/moved.conf"});
        QTRY_VERIFY_WITH_TIMEOUT(finished.load(), 5000);
        // requestKey 已纳入每条目远端路径：误续传会 0 次上传（全部「跳过已交付」）
        QCOMPARE(mock->uploadCalls.load() - callsBefore, 2);

        // 扁平口径（entry 空）：键与 v2.14 逐字相同——与显式条目模式的同清单
        // 互不误配（键空间分离），且部分失败续传仍按 local+remote 全对命中
        mock->failedLocal = mirrorLocal;
        finished = false;
        backend.startUpload(files, "/apps", false, false, "mock_mirror",
                            false, 0, RebootOptions{}, {});
        QTRY_VERIFY_WITH_TIMEOUT(finished.load(), 5000);
        QCOMPARE(backend.lastReport().results.front().state, DeviceResult::Failed);
        mock->failedLocal.clear();
        const int callsMid = mock->uploadCalls.load();
        finished = false;
        backend.resumePreviousFailures();
        backend.startUpload(files, "/apps", false, false, "mock_mirror",
                            false, 0, RebootOptions{}, {});
        QTRY_VERIFY_WITH_TIMEOUT(finished.load(), 5000);
        QCOMPARE(backend.lastReport().results.front().state, DeviceResult::Ok);
        // 续传命中：首条 basename 目标 /apps/a.txt 已交付被跳过，仅补传第二条
        QCOMPARE(mock->uploadCalls.load() - callsMid, 1);
        backend.OnStop();
    }

    // v2.15 波2 任务10 环回集成（真盘目录树端到端唯一防线）：
    //   folderexpand::scanDir（缺省实扫）→ deployentry::build 入口装配缝 →
    //   FtpDeployBackend/DeployJob **显式条目模式**（其 mkdir 循环被刻意跳过）→
    //   TransferExecutor 上传前父目录确保（任务 8）→ LocalTransferChannel 真实
    //   单层 makeDirectory → 同盘真盘目标树。
    // 既有覆盖的缝隙：入口纯头测试不触执行器；tst_transfer_executor 父目录确保全用
    // MockChannel+POSIX 假路径；memoryDeployChannelFactory 内存假 FS 无目录概念
    // （mkdir 恒成功，环回空转）——本用例是第一处（也是唯一一处）让"远端子目录由
    // 执行器侧创建"落到真实文件系统上的端到端断言：目标树与源树**镜像逐文件一致**
    // （路径集合相等 + 每文件字节相等），多深子链（pkg/lib/plugins）由确保步逐层创建。
    // 远端基目录（lbtarget）刻意不预建：基目录与其下所有层级同样由确保步创建，
    // 是被测链路的超集（生产中基目录通常已存在，确保步自最浅缺失层起同样适用）。
    void folderMirrorEndToEndThroughRealTransferChain() {
        QVERIFY(m_tmpDir.isValid());
        const QString srcRoot = m_tmpDir.filePath(QStringLiteral("lb"));
        QVERIFY(QDir(srcRoot).mkpath(QStringLiteral("pkg/bin")));
        QVERIFY(QDir(srcRoot).mkpath(QStringLiteral("pkg/lib/plugins")));
        const QString topTxt = srcRoot + QStringLiteral("/top.txt");
        const QString readme = srcRoot + QStringLiteral("/pkg/readme.md");
        const QString appConf = srcRoot + QStringLiteral("/pkg/bin/app.conf");
        const QString deepDll = srcRoot + QStringLiteral("/pkg/lib/plugins/deep.dll");
        struct Seed { QString path; QByteArray content; };
        const QList<Seed> seeds = {
            {topTxt, QByteArray("lb-flat-top")},
            {readme, QByteArray("lb-pkg-readme")},
            {appConf, QByteArray("lb-mirror-app-conf")},
            {deepDll, QByteArray("lb-mirror-deep-3-levels")},
        };
        for (const Seed& seed : seeds) {
            QFile f(seed.path);
            QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(f.errorString()));
            QCOMPARE(f.write(seed.content), seed.content.size());
            f.close();
        }

        // 入口装配缝：与生产单发入口同调用（缺省 scanner=scanDir 真盘递归扫描）。
        // 快照旗标按面板口径给出（顶层文件 false / 目录 true），build 以磁盘实况重分类。
        const QString remoteDirectory = m_tmpDir.filePath(QStringLiteral("lbtarget"));
        const QList<folderexpand::Selection> selections = {
            {topTxt, false},
            {srcRoot + QStringLiteral("/pkg"), true},
        };
        std::vector<std::string> files;
        std::vector<std::string> remotePaths;
        QList<folderexpand::ExpandedFile> expandedItems;
        QString entryError;
        QVERIFY2(deployentry::build(selections, remoteDirectory, files, remotePaths,
                                    expandedItems, entryError),
                 qPrintable(entryError));
        QCOMPARE(files.size(), std::size_t{4});
        QCOMPARE(remotePaths.size(), std::size_t{4});
        QCOMPARE(expandedItems.size(), 4);

        // 部署链路：mock 适配器仅供 ProtocolRegistry 建连/能力位；传输通道换成
        // LocalTransferChannel——"远端"即同盘真盘目录，stat/mkdir/upload/rename 全走实盘。
        auto* mock = new MockDeployable;
        ProtocolRegistry::instance()->registerFactory("mock_lb_mirror",
            [mock] { return std::shared_ptr<IProtocolAdapter>(mock, [](IProtocolAdapter*) {}); });
        mock->releaseGate = true;
        FtpDeployBackend backend;
        backend.setTransferChannelFactory(
            [](std::shared_ptr<IProtocolAdapter>) {
                return std::make_unique<LocalTransferChannel>();
            });
        QCOMPARE(backend.OnStart(0, nullptr), 0);
        backend.bindDevices({{"192.168.1.77", 21, "", "", ""}});
        backend.bindCredentials({"u", "p"});
        std::atomic<bool> finished{false};
        backend.setFinishedCallback([&](bool, const auto&, const auto&) { finished = true; });
        backend.startUpload(files, remoteDirectory.toStdString(), false, false,
                            "mock_lb_mirror", false, 0, RebootOptions{}, remotePaths);
        QTRY_VERIFY_WITH_TIMEOUT(finished.load(), 5000);

        {
            const DeployReport report = backend.lastReport();   // 按值持副本（防悬垂）
            QVERIFY2(report.results.front().failedFiles.empty(),
                     report.results.front().lastError.c_str());
            QCOMPARE(report.results.front().state, DeviceResult::Ok);
            QCOMPARE(report.results.front().deliveredFiles.size(), std::size_t{4});
        }
        backend.OnStop();

        // 镜像逐文件断言①：每个装配条目远端路径存在且内容与源文件字节相等。
        for (std::size_t i = 0; i < files.size(); ++i) {
            const QString remotePath = QString::fromStdString(remotePaths[i]);
            QFile remote(remotePath);
            QVERIFY2(remote.exists(), remotePath.toUtf8().constData());
            QFile source(QString::fromStdString(files[i]));
            QVERIFY(source.open(QIODevice::ReadOnly));
            const QByteArray sourceBytes = source.readAll();
            QVERIFY(remote.open(QIODevice::ReadOnly));
            QCOMPARE(remote.readAll(), sourceBytes);
        }

        // 镜像逐文件断言②：目标树文件集合与期望镜像相对路径集合**相等**——
        // 既无缺失（含 pkg/lib/plugins 三层深链目录确由执行器侧父目录确保创建），
        // 也无多余（无 .deviceforge-part-* 残留临时文件）。
        QVERIFY(QFileInfo(remoteDirectory + QStringLiteral("/pkg/lib/plugins")).isDir());
        QStringList mirrored;
        QDirIterator it(remoteDirectory, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            mirrored << QDir(remoteDirectory).relativeFilePath(it.next());
        }
        mirrored.sort();
        QStringList expected = {QStringLiteral("pkg/bin/app.conf"),
                                QStringLiteral("pkg/lib/plugins/deep.dll"),
                                QStringLiteral("pkg/readme.md"),
                                QStringLiteral("top.txt")};
        expected.sort();
        QCOMPARE(mirrored, expected);
    }
};
QTEST_MAIN(TstDeployLoop)
#include "tst_deploy_loop.moc"
