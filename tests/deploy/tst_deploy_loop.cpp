#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>
#include "adapter/IProtocolAdapter.h"
#include "adapter/IDeployable.h"
#include "adapter/ProtocolRegistry.h"
#include "framework/DeviceInfo.h"
#include "tools/FtpDeployTool/FtpDeployBackend.h"
#include "tools/FtpDeployTool/DeploymentRunner.h"
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
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

    bool uploadFile(const std::string&, const std::string&) override {
        ++uploadCalls;
        if (m_progressFn) m_progressFn(50);
        // 确定性闸门：releaseGate=false 时阻塞（模拟慢传输），
        // 让测试能在第一台挂起期间注入取消，验证第二台被跳过
        while (!releaseGate.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return m_uploadOk;
    }
    bool uploadFolder(const std::string&, const std::string&) override {
        ++uploadCalls;
        if (m_progressFn) m_progressFn(50);
        while (!releaseGate.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return m_uploadOk;
    }
    bool clearRemoteDirectory(const std::string&) override { return true; }

    bool m_connectOk = true;
    bool m_uploadOk = true;
    std::string m_error;
    std::atomic<bool>* m_flag = nullptr;
    std::atomic<int> uploadCalls{0};
    std::atomic<bool> releaseGate{false};
    std::function<void(int)> m_progressFn;
};

class TstDeployLoop : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_tmpDir;      // 自包含夹具：临时目录，RAII 自动清理
    std::string m_fixtureFile;   // 临时目录中的上传源文件（真实存在的文件）

private slots:
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
        QCOMPARE(backend.OnStart(0, nullptr), 0);
        backend.bindDevices({{"192.168.1.1", 0, "", "", ""}, {"192.168.1.2", 0, "", "", ""}});
        backend.bindCredentials({"user", "pass"});

        std::vector<std::string> ok, fail;
        bool finished = false;
        backend.setFinishedCallback([&](bool, const std::vector<std::string>& s,
                                        const std::vector<std::string>& f) {
            finished = true; ok = s; fail = f;
        });
        backend.startUpload({m_fixtureFile}, "/apps", false, false, "mock");
        QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
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
        QCOMPARE(backend.OnStart(0, nullptr), 0);
        backend.bindDevices({{"192.168.1.1", 0, "", "", ""}, {"192.168.1.2", 0, "", "", ""}});
        backend.bindCredentials({"u", "p"});
        std::vector<std::string> ok, fail; bool finished = false;
        backend.setFinishedCallback([&](bool, const std::vector<std::string>& s,
                                        const std::vector<std::string>& f) {
            finished = true; ok = s; fail = f;
        });
        backend.startUpload({m_fixtureFile}, "/apps", false, false, "mock_fail");
        QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
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
        QCOMPARE(backend.OnStart(0, nullptr), 0);
        backend.bindDevices({{"192.168.1.1", 0, "", "", ""}, {"192.168.1.2", 0, "", "", ""}});
        backend.bindCredentials({"u", "p"});
        std::vector<std::string> ok, fail; bool finished = false;
        backend.setFinishedCallback([&](bool, const std::vector<std::string>& s,
                                        const std::vector<std::string>& f) {
            finished = true; ok = s; fail = f;
        });
        // 确定性取消验证：第一台在闸门处挂起 → 注入取消 → 放闸 →
        // 第一台完成、第二台被取消跳过（uploadCalls 必须 == 1）
        backend.startUpload({m_fixtureFile}, "/apps", false, false, "mock_cancel");
        QTRY_VERIFY_WITH_TIMEOUT(mock->uploadCalls.load() >= 1, 5000);  // 第一台已进入上传（挂起中）
        backend.cancelUpload();
        mock->releaseGate = true;                                        // 放闸，第一台完成
        QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
        QCOMPARE(mock->uploadCalls.load(), 1);                           // 第二台被取消跳过
        QCOMPARE(ok.size(), 1);
        QCOMPARE(fail.size(), 0);
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

        std::vector<std::string> ok, fail; bool finished = false;
        backend.setFinishedCallback([&](bool, const std::vector<std::string>& s,
                                        const std::vector<std::string>& f) {
            finished = true; ok = s; fail = f;
        });

        backend.startUpload({m_fixtureFile}, "/apps", false, false, "mock_prog");
        QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);

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
};
QTEST_MAIN(TstDeployLoop)
#include "tst_deploy_loop.moc"
