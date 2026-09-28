#include <QtTest>

#include <atomic>
#include <future>
#include <memory>
#include <vector>

#include "adapter/IProtocolAdapter.h"
#include "command/BatchCommandRunner.h"

namespace {

class FakeAdapter final : public IProtocolAdapter {
public:
    bool connect(const DeviceInfo& device, const AuthInfo&) override
    {
        connectedDevice = device;
        ++connectCount;
        return connectResult;
    }
    void disconnect() override { disconnected = true; }
    bool isConnected() const override { return connectResult && !disconnected; }
    std::string lastError() const override { return error; }
    std::future<Response> request(const Request& request) override
    {
        requests.push_back(request);
        Response response = nextResponse;
        return std::async(std::launch::deferred, [response]() { return response; });
    }
    std::string protocolId() const override { return protocol; }
    void subscribe(const Request&, StreamCallback) override {}
    void unsubscribe() override {}
    ProtocolCapability capability() const override { return {}; }

    std::string protocol = "telnet";
    DeviceInfo connectedDevice;
    Response nextResponse{true, "ok", {}, 0};
    std::string error = "connect failed";
    bool connectResult = true;
    bool disconnected = false;
    int connectCount = 0;
    std::vector<Request> requests;
};

DeviceInfo device(std::string ip)
{
    DeviceInfo result;
    result.ip = std::move(ip);
    return result;
}

} // namespace

class TestBatchCommandRunner : public QObject
{
    Q_OBJECT
private slots:
    void protocolDefaultPort();
    void retryClassification();
    void rebootDisconnectAfterSend();
    void emptyCommandRejected();
    void commandsRunInOrder();
};

void TestBatchCommandRunner::protocolDefaultPort()
{
    QCOMPARE(BatchCommandRunner::defaultPort("telnet"), 23);
    QCOMPARE(BatchCommandRunner::defaultPort("ssh"), 22);
}

void TestBatchCommandRunner::retryClassification()
{
    QVERIFY(BatchCommandRunner::isRetryableError("connection reset by peer"));
    QVERIFY(!BatchCommandRunner::isRetryableError("authentication failed"));
}

void TestBatchCommandRunner::rebootDisconnectAfterSend()
{
    auto adapter = std::make_shared<FakeAdapter>();
    adapter->nextResponse = {true, "", {}, 0};
    BatchCommandRunner runner([adapter](const std::string&) { return adapter; });
    CommandRequest request;
    request.protocol = "ssh";
    request.devices = {device("192.168.1.10")};
    request.commands = {"reboot"};
    request.rebootMode = true;
    std::atomic_bool cancelled{false};
    const auto result = runner.run(request, cancelled, {});
    QCOMPARE(result.devices.size(), size_t{1});
    QCOMPARE(result.devices.front().state, CommandResultState::RebootTriggered);
}

void TestBatchCommandRunner::emptyCommandRejected()
{
    BatchCommandRunner runner([](const std::string&) {
        return std::shared_ptr<IProtocolAdapter>{};
    });
    CommandRequest request;
    request.protocol = "telnet";
    request.devices = {device("192.168.1.10")};
    std::atomic_bool cancelled{false};
    const auto result = runner.run(request, cancelled, {});
    QCOMPARE(result.devices.size(), size_t{1});
    QCOMPARE(result.devices.front().state, CommandResultState::Rejected);
}

void TestBatchCommandRunner::commandsRunInOrder()
{
    auto adapter = std::make_shared<FakeAdapter>();
    BatchCommandRunner runner([adapter](const std::string&) { return adapter; });
    CommandRequest request;
    request.protocol = "telnet";
    request.devices = {device("192.168.1.10")};
    request.commands = {"echo one", "echo two"};
    std::atomic_bool cancelled{false};
    const auto result = runner.run(request, cancelled, {});
    QCOMPARE(result.devices.front().state, CommandResultState::Succeeded);
    QCOMPARE(adapter->requests.size(), size_t{2});
    QCOMPARE(QString::fromStdString(adapter->requests[0].path), QStringLiteral("echo one"));
    QCOMPARE(QString::fromStdString(adapter->requests[1].path), QStringLiteral("echo two"));
}

QTEST_MAIN(TestBatchCommandRunner)
#include "tst_batch_command_runner.moc"
