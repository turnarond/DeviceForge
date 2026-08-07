#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>
#include "adapter/IProtocolAdapter.h"
#include "adapter/IDeployable.h"
#include "adapter/ProtocolRegistry.h"
#include "framework/DeviceInfo.h"
#include "tools/FtpDeployTool/FtpDeployBackend.h"
#include <atomic>
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

    bool uploadFile(const std::string&, const std::string&) override {
        ++uploadCalls;
        // 确定性闸门：releaseGate=false 时阻塞（模拟慢传输），
        // 让测试能在第一台挂起期间注入取消，验证第二台被跳过
        while (!releaseGate.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return m_uploadOk;
    }
    bool uploadFolder(const std::string&, const std::string&) override {
        ++uploadCalls;
        while (!releaseGate.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return m_uploadOk;
    }
    bool clearRemoteDirectory(const std::string&) override { return true; }
    void setProgressCallback(std::function<void(int)>) override {}
    void setCancelFlag(std::atomic<bool>* f) override { m_flag = f; }

    bool m_connectOk = true;
    bool m_uploadOk = true;
    std::string m_error;
    std::atomic<bool>* m_flag = nullptr;
    std::atomic<int> uploadCalls{0};
    std::atomic<bool> releaseGate{false};
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
};
QTEST_MAIN(TstDeployLoop)
#include "tst_deploy_loop.moc"
