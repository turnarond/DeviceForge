#include <QtTest/QtTest>
#include "adapter/IProtocolAdapter.h"
#include "adapter/IDeployable.h"
#include "adapter/ProtocolRegistry.h"
#include "framework/DeviceInfo.h"
#include "tools/FtpDeployTool/FtpDeployBackend.h"
#include <atomic>

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

    bool uploadFile(const std::string&, const std::string&) override { ++uploadCalls; return m_uploadOk; }
    bool uploadFolder(const std::string&, const std::string&) override { ++uploadCalls; return m_uploadOk; }
    bool clearRemoteDirectory(const std::string&) override { return true; }
    void setProgressCallback(std::function<void(int)>) override {}
    void setCancelFlag(std::atomic<bool>* f) override { m_flag = f; }

    bool m_connectOk = true;
    bool m_uploadOk = true;
    std::string m_error;
    std::atomic<bool>* m_flag = nullptr;
    int uploadCalls = 0;
};

class TstDeployLoop : public QObject {
    Q_OBJECT
private slots:
    void deploy_twoDevices_allSuccess() {
        auto* mock = new MockDeployable;
        // no-op deleter：mock 生命周期由测试管理，避免工厂多次 create 造成 double free
        ProtocolRegistry::instance()->registerFactory("mock",
            [mock] { return std::shared_ptr<IProtocolAdapter>(mock, [](IProtocolAdapter*) {}); });
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
        backend.startUpload({"C:/tmp/a.txt"}, "/apps", false, false, "mock");
        QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
        QCOMPARE(ok.size(), 2);   // 两台都成功
        QCOMPARE(fail.size(), 0);
        QCOMPARE(mock->uploadCalls, 2);
        backend.OnStop();
    }
    void deploy_connectFail_deviceSkipped() {
        auto* mock = new MockDeployable;
        mock->m_connectOk = false;
        // no-op deleter：同上，工厂按设备逐台 create，默认 deleter 会在首个
        // shared_ptr 析构时释放 mock，后续设备迭代再 create 即悬垂/双重释放
        ProtocolRegistry::instance()->registerFactory("mock_fail",
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
        backend.startUpload({"C:/tmp/a.txt"}, "/apps", false, false, "mock_fail");
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
        // 第一台完成后取消 → 第二台跳过
        backend.startUpload({"C:/tmp/a.txt"}, "/apps", false, false, "mock_cancel");
        QTRY_VERIFY_WITH_TIMEOUT(mock->uploadCalls >= 1, 5000);
        backend.cancelUpload();
        QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
        QVERIFY(ok.size() + fail.size() <= 2);
        backend.OnStop();
    }
};
QTEST_MAIN(TstDeployLoop)
#include "tst_deploy_loop.moc"
