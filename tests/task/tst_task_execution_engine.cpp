// tst_task_execution_engine.cpp — v2.11 Task 4 执行引擎状态机与阶段恢复。
// 全部使用 Fake 适配器与内存传输通道：不连真实网络，验证引擎编排语义
// （有序步骤、设备隔离、取消、超时/重试、重启断开按预期成功、失败阶段恢复）。

#include <QtTest>

#include "adapter/IProtocolAdapter.h"
#include "task/TaskExecutionEngine.h"
#include "transfer/AdapterTransferChannel.h"

#include <atomic>
#include <chrono>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

// ——————— 内存夹具：部署通道 + 命令执行脚本 ———————

class EngineFixture
{
public:
    // ===== 部署侧（TransferChannelOps 后端）=====
    bool deployUpload(const QString& ip, const QString& localPath, const QString& remotePath,
                      const std::atomic_bool* cancelFlag)
    {
        const QString finalPath = stripPartSuffix(remotePath);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_uploadSequence.emplace_back(ip, finalPath);
            ++m_uploadCalls[uploadKey(ip, finalPath)];
            m_uploadEntered.fetch_add(1);
        }
        if (isFailingTarget(ip, remotePath)) {
            return false;
        }
        if (m_deployGateArmed.load() && ip == m_deployGateIp) {
            while (!m_deployGateReleased.load()) {
                if (cancelFlag && cancelFlag->load()) {
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            // 放闸后给"引擎取消 → 调度器取消桥接"一个有界窗口：取消测试中
            // engine.cancel() 先于放闸，取消标志必定在窗口内生效，收口确定。
            for (int graceMs = 0; graceMs < 500 && !(cancelFlag && cancelFlag->load()); ++graceMs) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (cancelFlag && cancelFlag->load()) {
                return false;
            }
        }
        QFile source(localPath);
        if (!source.open(QIODevice::ReadOnly)) {
            return false;
        }
        const QByteArray content = source.readAll();
        std::lock_guard<std::mutex> lock(m_mutex);
        m_remoteFiles[ip][remotePath] = content;
        return true;
    }

    bool deployList(const QString& ip, const QString& parent,
                    QVector<TransferChannelEntry>& out)
    {
        out.clear();
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_remoteFiles.find(ip);
        if (it == m_remoteFiles.end()) {
            return true;
        }
        for (const auto& file : it->second) {
            const qsizetype slash = file.first.lastIndexOf(u'/');
            const QString fileParent = slash == 0
                ? QStringLiteral("/")
                : (slash < 0 ? QStringLiteral(".") : file.first.left(slash));
            if (fileParent == parent) {
                out.push_back({file.first.mid(slash + 1),
                               quint64(file.second.size()), {}});
            }
        }
        return true;
    }

    bool deployRename(const QString& ip, const QString& from, const QString& to)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto& files = m_remoteFiles[ip];
        const auto it = files.find(from);
        if (it == files.end()) {
            return false;
        }
        files.insert(std::make_pair(to, it->second));
        files.erase(it);
        return true;
    }

    bool deployRemove(const QString& ip, const QString& path)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_remoteFiles.find(ip);
        if (it == m_remoteFiles.end()) {
            return true;
        }
        it->second.erase(path);
        return true;
    }

    // ===== 命令侧（IProtocolAdapter 后端）=====
    bool commandConnect(const QString& ip, std::string* error)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_hardFailConnectIp.isEmpty() && ip == m_hardFailConnectIp) {
            *error = m_hardFailConnectError;
            return false;
        }
        const int attempt = ++m_connectAttempts[ip];
        if (!m_flakyConnectIp.isEmpty() && ip == m_flakyConnectIp && attempt == 1) {
            *error = "connection timed out";
            return false;
        }
        return true;
    }

    Response commandRequest(const QString& ip, const QString& command)
    {
        if (m_commandGateArmed.load() && command == m_gatedCommand) {
            m_commandGateEntered.fetch_add(1);
            while (!m_commandGateReleased.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        Response response{true, "ok", std::string{}, 0};
        std::lock_guard<std::mutex> lock(m_mutex);
        m_executedCommands[ip].push_back(command);
        if (command == QStringLiteral("reboot")) {
            if (m_rebootDisconnectIps.count(ip)) {
                response = {false, std::string{}, "connection closed after send", 0};
            }
        } else if (m_failingCommands.count(command)) {
            response = {false, std::string{}, "device busy", 0};
        }
        return response;
    }

    // ===== 测试配置与观测 =====
    void setFailingRemote(const QString& ip, const QString& remote)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_failingRemotes.insert(uploadKey(ip, remote));
    }
    void clearFailingRemotes()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_failingRemotes.clear();
    }
    void armDeployGate(const QString& ip)
    {
        m_deployGateReleased.store(false);
        m_deployGateIp = ip;
        m_deployGateArmed.store(true);
    }
    void releaseDeployGate() { m_deployGateReleased.store(true); }
    void disarmDeployGate()
    {
        m_deployGateArmed.store(false);
        m_deployGateReleased.store(true);
    }
    void armCommandGate(const QString& command)
    {
        m_commandGateReleased.store(false);
        m_gatedCommand = command;
        m_commandGateArmed.store(true);
    }
    void releaseCommandGate() { m_commandGateReleased.store(true); }
    void disarmCommandGate()
    {
        m_commandGateArmed.store(false);
        m_commandGateReleased.store(true);
        m_gatedCommand = QString();
    }
    void addFailingCommand(const QString& command)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_failingCommands.insert(command);
    }
    void clearFailingCommands()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_failingCommands.clear();
    }
    void addRebootDisconnectIp(const QString& ip)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_rebootDisconnectIps.insert(ip);
    }
    void setFlakyConnectIp(const QString& ip)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_flakyConnectIp = ip;
    }
    // 终审 Important 2：让指定 IP 的命令连接永久失败并回显自定义错误文本
    void setHardFailingConnect(const QString& ip, const std::string& errorText)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_hardFailConnectIp = ip;
        m_hardFailConnectError = errorText;
    }

    int uploadEntered() const { return m_uploadEntered.load(); }
    int commandGateEntered() const { return m_commandGateEntered.load(); }
    int uploadCalls(const QString& ip, const QString& remote)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_uploadCalls.find(uploadKey(ip, remote));
        return it == m_uploadCalls.end() ? 0 : it->second;
    }
    std::vector<QString> uploadsFor(const QString& ip)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<QString> result;
        for (const auto& entry : m_uploadSequence) {
            if (entry.first == ip) {
                result.push_back(entry.second);
            }
        }
        return result;
    }
    std::vector<QString> executedCommands(const QString& ip)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_executedCommands.find(ip);
        return it == m_executedCommands.end() ? std::vector<QString>{} : it->second;
    }

private:
    static QString uploadKey(const QString& ip, const QString& remote)
    {
        return ip + u'|' + remote;
    }
    static QString stripPartSuffix(const QString& path)
    {
        const qsizetype idx = path.indexOf(QStringLiteral(".deviceforge-part-"));
        return idx < 0 ? path : path.left(idx);
    }
    bool isFailingTarget(const QString& ip, const QString& remotePath)
    {
        const QString finalPath = stripPartSuffix(remotePath);
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_failingRemotes.count(uploadKey(ip, finalPath)) > 0;
    }

    std::mutex m_mutex;
    std::map<QString, std::map<QString, QByteArray>> m_remoteFiles;
    std::vector<std::pair<QString, QString>> m_uploadSequence;
    std::map<QString, int> m_uploadCalls;
    std::set<QString> m_failingRemotes;
    std::map<QString, std::vector<QString>> m_executedCommands;
    std::map<QString, int> m_connectAttempts;
    std::set<QString> m_rebootDisconnectIps;
    std::set<QString> m_failingCommands;
    QString m_flakyConnectIp;
    QString m_hardFailConnectIp;
    std::string m_hardFailConnectError;
    QString m_deployGateIp;
    QString m_gatedCommand;
    std::atomic_bool m_deployGateArmed{false};
    std::atomic_bool m_deployGateReleased{true};
    std::atomic_bool m_commandGateArmed{false};
    std::atomic_bool m_commandGateReleased{true};
    std::atomic_int m_uploadEntered{0};
    std::atomic_int m_commandGateEntered{0};
};

// ——————— Fake 协议适配器 ———————

class FakeDeployAdapter final : public IProtocolAdapter
{
public:
    explicit FakeDeployAdapter(std::shared_ptr<EngineFixture> fixture)
        : m_fixture(std::move(fixture))
    {
    }

    std::string protocolId() const override { return "ftp"; }
    bool connect(const DeviceInfo& device, const AuthInfo&) override
    {
        m_ip = QString::fromStdString(device.ip);
        return true;
    }
    void disconnect() override {}
    bool isConnected() const override { return true; }
    // 非重试类文案：让失败步骤快速收口，测试不依赖退避时长
    std::string lastError() const override { return u8"模拟上传失败"; }
    std::future<Response> request(const Request&) override
    {
        return std::async(std::launch::deferred, [] { return Response{}; });
    }
    void subscribe(const Request&, StreamCallback) override {}
    void unsubscribe() override {}
    ProtocolCapability capability() const override { return {}; }

    const QString& ip() const { return m_ip; }
    void setCancelFlagPtr(std::atomic_bool* flag) { m_cancelFlag = flag; }
    std::atomic_bool* cancelFlag() const { return m_cancelFlag; }

private:
    std::shared_ptr<EngineFixture> m_fixture;
    QString m_ip;
    std::atomic_bool* m_cancelFlag = nullptr;
};

class FakeCommandAdapter final : public IProtocolAdapter
{
public:
    explicit FakeCommandAdapter(std::shared_ptr<EngineFixture> fixture)
        : m_fixture(std::move(fixture))
    {
    }

    std::string protocolId() const override { return "telnet"; }
    bool connect(const DeviceInfo& device, const AuthInfo&) override
    {
        m_ip = QString::fromStdString(device.ip);
        std::string error;
        if (!m_fixture->commandConnect(m_ip, &error)) {
            m_lastError = error;
            return false;
        }
        return true;
    }
    void disconnect() override {}
    bool isConnected() const override { return true; }
    std::string lastError() const override { return m_lastError; }
    std::future<Response> request(const Request& req) override
    {
        const auto fixture = m_fixture;
        const QString ip = m_ip;
        const QString command = QString::fromStdString(req.path);
        return std::async(std::launch::deferred,
                          [fixture, ip, command] { return fixture->commandRequest(ip, command); });
    }
    void subscribe(const Request&, StreamCallback) override {}
    void unsubscribe() override {}
    ProtocolCapability capability() const override { return {}; }

private:
    std::shared_ptr<EngineFixture> m_fixture;
    QString m_ip;
    std::string m_lastError = u8"模拟命令失败";
};

// ——————— 组装与记录 ———————

TaskExecutionEngine::Dependencies makeDependencies(const std::shared_ptr<EngineFixture>& fixture)
{
    TaskExecutionEngine::Dependencies deps;
    deps.adapterFactory = [fixture](const std::string& protocol)
        -> std::shared_ptr<IProtocolAdapter> {
        if (protocol == "ftp") {
            return std::make_shared<FakeDeployAdapter>(fixture);
        }
        if (protocol == "telnet" || protocol == "ssh") {
            return std::make_shared<FakeCommandAdapter>(fixture);
        }
        return {};
    };
    deps.transferChannelFactory = [fixture](std::shared_ptr<IProtocolAdapter> adapter)
        -> std::unique_ptr<ITransferChannel> {
        auto fake = std::dynamic_pointer_cast<FakeDeployAdapter>(adapter);
        TransferChannelOps ops;
        ops.upload = [fixture, fake](const QString& local, const QString& remote) {
            return fake && fixture->deployUpload(fake->ip(), local, remote, fake->cancelFlag());
        };
        ops.list = [fixture, fake](const QString& parent, QVector<TransferChannelEntry>& entries) {
            return fake ? fixture->deployList(fake->ip(), parent, entries) : false;
        };
        ops.rename = [fixture, fake](const QString& from, const QString& to) {
            return fake && fixture->deployRename(fake->ip(), from, to);
        };
        ops.remove = [fixture, fake](const QString& path) {
            return fake && fixture->deployRemove(fake->ip(), path);
        };
        ops.setCancelFlag = [fake](std::atomic_bool* flag) {
            if (fake) {
                fake->setCancelFlagPtr(flag);
            }
        };
        return std::make_unique<AdapterTransferChannel>(QStringLiteral("ftp"), adapter,
                                                        std::move(ops));
    };
    deps.transferRetrySleeper = [](int) {};
    return deps;
}

class Recorder
{
public:
    TaskExecutionEngine::Callbacks make()
    {
        TaskExecutionEngine::Callbacks callbacks;
        callbacks.onDeviceStateChanged = [this](const std::string& deviceId, TaskDeviceState state) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stateSequences[deviceId].push_back(state);
        };
        callbacks.onStepFinished = [this](const std::string& deviceId, const TaskStepResult& result) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stepResults[deviceId].push_back(result);
        };
        callbacks.onRunFinished = [this](const TaskRunOutcome& outcome) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_outcomes.push_back(outcome);
            }
            m_runDone.store(true);
        };
        callbacks.onLog = [this](const std::string& message) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_logs.push_back(message);
        };
        return callbacks;
    }

    void resetRunDone() { m_runDone.store(false); }
    // 阶段恢复前的观测隔离：清空状态/步骤/日志序列，仅保留各轮 outcome
    void resetEvents()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stateSequences.clear();
        m_stepResults.clear();
        m_logs.clear();
    }
    bool runDone() const { return m_runDone.load(); }
    int outcomeCount()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return static_cast<int>(m_outcomes.size());
    }
    TaskRunOutcome outcomeAt(std::size_t index)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return index < m_outcomes.size() ? m_outcomes[index] : TaskRunOutcome{};
    }
    std::vector<TaskDeviceState> statesFor(const std::string& deviceId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_stateSequences[deviceId];
    }
    std::vector<TaskStepResult> stepsFor(const std::string& deviceId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_stepResults[deviceId];
    }
    std::vector<std::string> logs()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_logs;
    }

private:
    std::mutex m_mutex;
    std::map<std::string, std::vector<TaskDeviceState>> m_stateSequences;
    std::map<std::string, std::vector<TaskStepResult>> m_stepResults;
    std::vector<TaskRunOutcome> m_outcomes;
    std::vector<std::string> m_logs;
    std::atomic_bool m_runDone{false};
};

DeviceProfile makeProfile(const std::string& id, const QString& ip, bool withFtpEndpoint = true)
{
    DeviceProfile profile;
    profile.deviceId = id;
    profile.name = "设备-" + id;
    if (withFtpEndpoint) {
        profile.endpoints.push_back({"ftp", ip.toStdString(), 21, "cred-ref"});
    }
    profile.endpoints.push_back({"telnet", ip.toStdString(), 23, "cred-ref"});
    return profile;
}

QVariantMap deployParameters(const QList<QPair<QString, QString>>& files)
{
    QVariantList items;
    for (const auto& file : files) {
        QVariantMap item;
        item.insert(QStringLiteral("localPath"), file.first);
        item.insert(QStringLiteral("remotePath"), file.second);
        items.append(item);
    }
    QVariantMap params;
    params.insert(QStringLiteral("items"), items);
    return params;
}

QVariantMap commandParameters(const QStringList& commands)
{
    QVariantMap params;
    params.insert(QStringLiteral("commands"), commands);
    return params;
}

TaskStep makeStep(TaskStepType type, QVariantMap parameters)
{
    TaskStep step;
    step.type = type;
    step.parameters = std::move(parameters);
    return step;
}

TaskTemplate makeTemplate(std::vector<TaskStep> steps)
{
    TaskTemplate tmpl;
    tmpl.name = "测试模板";
    tmpl.version = 1;
    tmpl.steps = std::move(steps);
    return tmpl;
}

AuthInfo makeAuth()
{
    AuthInfo auth;
    auth.user = "task-user";
    auth.password = "task-secret";
    return auth;
}

} // namespace

class TstTaskExecutionEngine : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tmpDir;
    QString m_fileA;
    QString m_fileB;

    void writeFixture(const QString& name, const QByteArray& content, QString* outPath)
    {
        *outPath = m_tmpDir.filePath(name);
        QFile file(*outPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(content);
        file.close();
    }

    static const TaskDeviceState* findState(const std::vector<TaskDeviceState>& sequence,
                                            TaskDeviceState target)
    {
        const auto it = std::find(sequence.cbegin(), sequence.cend(), target);
        return it == sequence.cend() ? nullptr : &*it;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_tmpDir.isValid());
        writeFixture(QStringLiteral("a.bin"), QByteArray(512, 'A'), &m_fileA);
        writeFixture(QStringLiteral("b.bin"), QByteArray(256, 'B'), &m_fileB);
    }

    // 场景一：部署→命令→重启→恢复按模板顺序逐步执行；重启断开按预期成功
    void orderedStepsRunPerDevice()
    {
        auto fixture = std::make_shared<EngineFixture>();
        Recorder recorder;
        TaskExecutionEngine engine(makeDependencies(fixture));

        TaskTemplate tmpl = makeTemplate({
            makeStep(TaskStepType::DeployFiles,
                     deployParameters({{m_fileA, QStringLiteral("/apps/a.bin")},
                                       {m_fileB, QStringLiteral("/apps/b.bin")}})),
            makeStep(TaskStepType::RunCommands, commandParameters({QStringLiteral("c1"), QStringLiteral("c2")})),
            makeStep(TaskStepType::Reboot, QVariantMap{}),
            makeStep(TaskStepType::Recover, commandParameters({QStringLiteral("chk")})),
        });
        TaskExecutionRequest request;
        request.task = tmpl;
        request.devices = {makeProfile("dev-a", QStringLiteral("10.0.0.11")),
                           makeProfile("dev-b", QStringLiteral("10.0.0.12"))};
        request.authResolver = [](const DeviceProfile&) { return makeAuth(); };
        // dev-b 的重启以“发送后断开”收口，必须仍算成功
        fixture->addRebootDisconnectIp(QStringLiteral("10.0.0.12"));

        const TaskRunId runId = engine.start(request, recorder.make());
        QVERIFY2(!runId.empty(), "start 必须返回非空 TaskRunId");
        QTRY_VERIFY_WITH_TIMEOUT(recorder.runDone(), 20000);

        const TaskRunOutcome outcome = recorder.outcomeAt(0);
        QCOMPARE(outcome.devices.size(), std::size_t{2});
        QCOMPARE(outcome.devices[0].deviceId, std::string("dev-a"));
        QCOMPARE(outcome.devices[0].state, TaskDeviceState::Succeeded);
        QCOMPARE(outcome.devices[1].state, TaskDeviceState::Succeeded);

        // 状态推进顺序（每台设备严格沿模板相位前进）
        const std::vector<TaskDeviceState> expected = {
            TaskDeviceState::Pending, TaskDeviceState::Validating,
            TaskDeviceState::Deploying, TaskDeviceState::Commanding,
            TaskDeviceState::Rebooting, TaskDeviceState::Recovering,
            TaskDeviceState::Succeeded};
        QCOMPARE(recorder.statesFor("dev-a"), expected);
        QCOMPARE(recorder.statesFor("dev-b"), expected);

        // 步骤结果的类型顺序与终态
        const auto stepsA = recorder.stepsFor("dev-a");
        QCOMPARE(stepsA.size(), std::size_t{4});
        QCOMPARE(int(stepsA[0].type), int(TaskStepType::DeployFiles));
        QCOMPARE(int(stepsA[1].type), int(TaskStepType::RunCommands));
        QCOMPARE(int(stepsA[2].type), int(TaskStepType::Reboot));
        QCOMPARE(int(stepsA[3].type), int(TaskStepType::Recover));
        for (const auto& step : stepsA) {
            QCOMPARE(step.state, TaskDeviceState::Succeeded);
            QVERIFY2(step.attempt >= 1, "成功步骤必须记录尝试次数");
        }

        // 命令实际下发顺序：c1 → c2 → reboot → chk（含重启断开的 dev-b）
        QCOMPARE(fixture->executedCommands(QStringLiteral("10.0.0.11")),
                 (std::vector<QString>{QStringLiteral("c1"), QStringLiteral("c2"),
                                       QStringLiteral("reboot"), QStringLiteral("chk")}));
        QCOMPARE(fixture->executedCommands(QStringLiteral("10.0.0.12")),
                 (std::vector<QString>{QStringLiteral("c1"), QStringLiteral("c2"),
                                       QStringLiteral("reboot"), QStringLiteral("chk")}));

        // 部署文件顺序与设备归属
        QCOMPARE(fixture->uploadsFor(QStringLiteral("10.0.0.11")),
                 (std::vector<QString>{QStringLiteral("/apps/a.bin"),
                                       QStringLiteral("/apps/b.bin")}));
        QCOMPARE(fixture->uploadsFor(QStringLiteral("10.0.0.12")).size(), std::size_t{2});

        QVERIFY2(!recorder.logs().empty(), "引擎必须输出中文执行日志");
    }

    // 场景二：单台设备失败不影响其他设备；失败设备停止后续步骤
    void deviceFailureIsolation()
    {
        auto fixture = std::make_shared<EngineFixture>();
        Recorder recorder;
        TaskExecutionEngine engine(makeDependencies(fixture));

        TaskTemplate tmpl = makeTemplate({
            makeStep(TaskStepType::DeployFiles,
                     deployParameters({{m_fileA, QStringLiteral("/apps/a.bin")}})),
            makeStep(TaskStepType::RunCommands, commandParameters({QStringLiteral("c1")})),
        });
        TaskExecutionRequest request;
        request.task = tmpl;
        request.devices = {makeProfile("dev-a", QStringLiteral("10.0.0.21")),
                           makeProfile("dev-b", QStringLiteral("10.0.0.22"))};
        request.authResolver = [](const DeviceProfile&) { return makeAuth(); };
        fixture->setFailingRemote(QStringLiteral("10.0.0.22"), QStringLiteral("/apps/a.bin"));

        const TaskRunId runId = engine.start(request, recorder.make());
        QVERIFY(!runId.empty());
        QTRY_VERIFY_WITH_TIMEOUT(recorder.runDone(), 20000);

        const TaskRunOutcome outcome = recorder.outcomeAt(0);
        QCOMPARE(outcome.devices[0].state, TaskDeviceState::Succeeded);
        QCOMPARE(outcome.devices[1].state, TaskDeviceState::Failed);
        // dev-b 部署失败且无任何已成功步骤 → Failed（不是部分成功），且不执行后续命令
        const auto stepsB = recorder.stepsFor("dev-b");
        QCOMPARE(stepsB.size(), std::size_t{1});
        QCOMPARE(stepsB[0].state, TaskDeviceState::Failed);
        QVERIFY(fixture->executedCommands(QStringLiteral("10.0.0.22")).empty());
        QCOMPARE(fixture->executedCommands(QStringLiteral("10.0.0.21")).size(), std::size_t{1});
    }

    // 场景三：取消在传输检查点收敛，未启动的设备直接收口为已取消
    void cancelDuringDeploy()
    {
        auto fixture = std::make_shared<EngineFixture>();
        Recorder recorder;
        TaskExecutionEngine engine(makeDependencies(fixture));

        TaskTemplate tmpl = makeTemplate({
            makeStep(TaskStepType::DeployFiles,
                     deployParameters({{m_fileA, QStringLiteral("/apps/a.bin")}})),
            makeStep(TaskStepType::RunCommands, commandParameters({QStringLiteral("c1")})),
        });
        TaskExecutionRequest request;
        request.task = tmpl;
        request.devices = {makeProfile("dev-a", QStringLiteral("10.0.0.31")),
                           makeProfile("dev-b", QStringLiteral("10.0.0.32"))};
        request.authResolver = [](const DeviceProfile&) { return makeAuth(); };
        fixture->armDeployGate(QStringLiteral("10.0.0.31"));

        const TaskRunId runId = engine.start(request, recorder.make());
        QVERIFY(!runId.empty());
        QTRY_VERIFY_WITH_TIMEOUT(fixture->uploadEntered() >= 1, 20000);

        engine.cancel();
        fixture->releaseDeployGate();
        QTRY_VERIFY_WITH_TIMEOUT(recorder.runDone(), 20000);

        const TaskRunOutcome outcome = recorder.outcomeAt(0);
        QCOMPARE(outcome.devices[0].deviceId, std::string("dev-a"));
        QCOMPARE(outcome.devices[0].state, TaskDeviceState::Cancelled);
        // 取消且无任何已交付 → Cancelled（不是部分成功），无凭据/命令泄漏
        const auto stepsA = recorder.stepsFor("dev-a");
        QCOMPARE(stepsA.size(), std::size_t{1});
        QCOMPARE(stepsA[0].state, TaskDeviceState::Cancelled);
        const auto statesA = recorder.statesFor("dev-a");
        QCOMPARE(statesA.back(), TaskDeviceState::Cancelled);
        QVERIFY(!findState(statesA, TaskDeviceState::Commanding));

        // dev-b 未启动即被取消
        QCOMPARE(outcome.devices[1].state, TaskDeviceState::Cancelled);
        QVERIFY(recorder.stepsFor("dev-b").empty());
        QVERIFY(fixture->executedCommands(QStringLiteral("10.0.0.31")).empty());
        QVERIFY(fixture->executedCommands(QStringLiteral("10.0.0.32")).empty());
    }

    // 场景四：瞬时超时按既有重试语义恢复，步骤记录尝试次数
    void commandRetryAfterTransientTimeout()
    {
        auto fixture = std::make_shared<EngineFixture>();
        Recorder recorder;
        TaskExecutionEngine engine(makeDependencies(fixture));

        TaskTemplate tmpl = makeTemplate({
            makeStep(TaskStepType::RunCommands, commandParameters({QStringLiteral("c1")})),
        });
        TaskExecutionRequest request;
        request.task = tmpl;
        request.devices = {makeProfile("dev-a", QStringLiteral("10.0.0.7"))};
        request.authResolver = [](const DeviceProfile&) { return makeAuth(); };
        fixture->setFlakyConnectIp(QStringLiteral("10.0.0.7"));

        const TaskRunId runId = engine.start(request, recorder.make());
        QVERIFY(!runId.empty());
        QTRY_VERIFY_WITH_TIMEOUT(recorder.runDone(), 20000);

        const auto steps = recorder.stepsFor("dev-a");
        QCOMPARE(steps.size(), std::size_t{1});
        QCOMPARE(steps[0].state, TaskDeviceState::Succeeded);
        QCOMPARE(steps[0].attempt, 2);
        QCOMPARE(fixture->executedCommands(QStringLiteral("10.0.0.7")),
                 (std::vector<QString>{QStringLiteral("c1")}));
        QCOMPARE(recorder.outcomeAt(0).devices[0].state, TaskDeviceState::Succeeded);
    }

    // 场景五：失败阶段恢复——部署步骤内已成功文件不重传，从失败步骤继续
    void retryResumesFailedDeployStep()
    {
        auto fixture = std::make_shared<EngineFixture>();
        Recorder recorder;
        TaskExecutionEngine engine(makeDependencies(fixture));

        TaskTemplate tmpl = makeTemplate({
            makeStep(TaskStepType::DeployFiles,
                     deployParameters({{m_fileA, QStringLiteral("/apps/a.bin")},
                                       {m_fileB, QStringLiteral("/apps/b.bin")}})),
            makeStep(TaskStepType::RunCommands, commandParameters({QStringLiteral("c1")})),
        });
        TaskExecutionRequest request;
        request.task = tmpl;
        request.devices = {makeProfile("dev-a", QStringLiteral("10.0.0.31")),
                           makeProfile("dev-b", QStringLiteral("10.0.0.32"))};
        request.authResolver = [](const DeviceProfile&) { return makeAuth(); };
        fixture->setFailingRemote(QStringLiteral("10.0.0.31"), QStringLiteral("/apps/b.bin"));

        const TaskRunId runId1 = engine.start(request, recorder.make());
        QVERIFY(!runId1.empty());
        QTRY_VERIFY_WITH_TIMEOUT(recorder.runDone(), 20000);

        const TaskRunOutcome first = recorder.outcomeAt(0);
        // dev-a：a.bin 已交付、b.bin 失败 → 部分成功
        QCOMPARE(first.devices[0].state, TaskDeviceState::PartiallySucceeded);
        const auto stepsA1 = recorder.stepsFor("dev-a");
        QCOMPARE(stepsA1.size(), std::size_t{1});
        QCOMPARE(stepsA1[0].state, TaskDeviceState::PartiallySucceeded);
        // dev-b：全成功
        QCOMPARE(first.devices[1].state, TaskDeviceState::Succeeded);
        QCOMPARE(fixture->uploadCalls(QStringLiteral("10.0.0.31"),
                                      QStringLiteral("/apps/a.bin")), 1);

        fixture->clearFailingRemotes();
        recorder.resetEvents();
        recorder.resetRunDone();
        const TaskRunId runId2 = engine.retryFailed(runId1);
        QVERIFY2(!runId2.empty(), "retryFailed 必须启动恢复运行");
        QTRY_VERIFY_WITH_TIMEOUT(recorder.runDone(), 20000);

        const TaskRunOutcome second = recorder.outcomeAt(1);
        QCOMPARE(second.runId, runId2);
        QCOMPARE(second.resumedFromRunId, runId1);
        QCOMPARE(second.devices[0].state, TaskDeviceState::Succeeded);
        QCOMPARE(second.devices[0].steps.size(), std::size_t{2});
        // 已成功的 dev-b 整体跳过，未再触达
        QCOMPARE(second.devices[1].state, TaskDeviceState::Skipped);
        QCOMPARE(fixture->uploadCalls(QStringLiteral("10.0.0.32"),
                                      QStringLiteral("/apps/a.bin")), 1);
        // 阶段恢复核心锁定：a.bin 不重复执行（仍只 1 次），b.bin 从失败处补投
        QCOMPARE(fixture->uploadCalls(QStringLiteral("10.0.0.31"),
                                      QStringLiteral("/apps/a.bin")), 1);
        QCOMPARE(fixture->uploadCalls(QStringLiteral("10.0.0.31"),
                                      QStringLiteral("/apps/b.bin")), 2);
        // 命令步骤在恢复轮内执行且只执行一次
        const auto stepsA2 = recorder.stepsFor("dev-a");
        QCOMPARE(stepsA2.size(), std::size_t{2});
        QCOMPARE(stepsA2[0].state, TaskDeviceState::Succeeded);
        QCOMPARE(stepsA2[1].state, TaskDeviceState::Succeeded);
        QCOMPARE(fixture->executedCommands(QStringLiteral("10.0.0.31")),
                 (std::vector<QString>{QStringLiteral("c1")}));
        // dev-b 本轮状态序列只有 Pending → Skipped
        QCOMPARE(recorder.statesFor("dev-b"),
                 (std::vector<TaskDeviceState>{TaskDeviceState::Pending,
                                               TaskDeviceState::Skipped}));
    }

    // 场景六：重启断开、取消和部分成功同时发生的状态机收敛 + 阶段恢复不重跑成功阶段
    void rebootDisconnectCancelConvergence()
    {
        auto fixture = std::make_shared<EngineFixture>();
        Recorder recorder;
        TaskExecutionEngine engine(makeDependencies(fixture));

        TaskTemplate tmpl = makeTemplate({
            makeStep(TaskStepType::DeployFiles,
                     deployParameters({{m_fileA, QStringLiteral("/apps/a.bin")}})),
            makeStep(TaskStepType::Reboot, QVariantMap{}),
            makeStep(TaskStepType::Recover, commandParameters({QStringLiteral("hold-me")})),
        });
        TaskExecutionRequest request;
        request.task = tmpl;
        request.devices = {makeProfile("dev-a", QStringLiteral("10.0.0.41")),
                           makeProfile("dev-b", QStringLiteral("10.0.0.42"))};
        request.authResolver = [](const DeviceProfile&) { return makeAuth(); };
        fixture->addRebootDisconnectIp(QStringLiteral("10.0.0.41"));
        fixture->armCommandGate(QStringLiteral("hold-me"));
        fixture->addFailingCommand(QStringLiteral("hold-me"));

        const TaskRunId runId1 = engine.start(request, recorder.make());
        QVERIFY(!runId1.empty());
        // dev-a：部署成功、重启断开成功、恢复检查挂起中
        QTRY_VERIFY_WITH_TIMEOUT(fixture->commandGateEntered() >= 1, 20000);

        engine.cancel();
        fixture->releaseCommandGate();
        QTRY_VERIFY_WITH_TIMEOUT(recorder.runDone(), 20000);

        const TaskRunOutcome first = recorder.outcomeAt(0);
        // 重启断开(预期成功) + 已部署文件 + 取消/失败在恢复阶段同时发生 → 必须收敛为部分成功
        QCOMPARE(first.devices[0].state, TaskDeviceState::PartiallySucceeded);
        const auto stepsA1 = recorder.stepsFor("dev-a");
        QCOMPARE(stepsA1.size(), std::size_t{3});
        QCOMPARE(stepsA1[0].state, TaskDeviceState::Succeeded);
        QCOMPARE(stepsA1[1].state, TaskDeviceState::Succeeded);  // 断开=预期成功
        QCOMPARE(stepsA1[2].state, TaskDeviceState::Failed);
        // dev-b 未启动即取消
        QCOMPARE(first.devices[1].state, TaskDeviceState::Cancelled);
        QCOMPARE(recorder.statesFor("dev-b"),
                 (std::vector<TaskDeviceState>{TaskDeviceState::Pending,
                                               TaskDeviceState::Cancelled}));
        QCOMPARE(fixture->uploadCalls(QStringLiteral("10.0.0.41"),
                                      QStringLiteral("/apps/a.bin")), 1);
        // run1 实际下发：reboot（断开=成功）+ hold-me（放闸后返回失败）
        QCOMPARE(fixture->executedCommands(QStringLiteral("10.0.0.41")),
                 (std::vector<QString>{QStringLiteral("reboot"), QStringLiteral("hold-me")}));

        fixture->disarmCommandGate();
        fixture->clearFailingCommands();
        recorder.resetEvents();
        recorder.resetRunDone();
        const TaskRunId runId2 = engine.retryFailed(runId1);
        QVERIFY(!runId2.empty());
        QTRY_VERIFY_WITH_TIMEOUT(recorder.runDone(), 20000);

        const TaskRunOutcome second = recorder.outcomeAt(1);
        QCOMPARE(second.devices[0].state, TaskDeviceState::Succeeded);
        // dev-a 从失败的恢复检查步骤继续：不重传文件、不重发重启命令
        QCOMPARE(fixture->uploadCalls(QStringLiteral("10.0.0.41"),
                                      QStringLiteral("/apps/a.bin")), 1);
        const auto commandsA = fixture->executedCommands(QStringLiteral("10.0.0.41"));
        QCOMPARE(std::count(commandsA.cbegin(), commandsA.cend(), QStringLiteral("reboot")), 1);
        // hold-me：run1 失败一次 + run2 恢复执行成功一次；reboot 未被重复执行
        QCOMPARE(std::count(commandsA.cbegin(), commandsA.cend(), QStringLiteral("hold-me")), 2);
        // dev-a 本轮状态序列：恢复运行只进入失败阶段（不含部署/重启相位）
        QCOMPARE(recorder.statesFor("dev-a"),
                 (std::vector<TaskDeviceState>{TaskDeviceState::Pending,
                                               TaskDeviceState::Validating,
                                               TaskDeviceState::Recovering,
                                               TaskDeviceState::Succeeded}));
        QCOMPARE(recorder.stepsFor("dev-a").size(), std::size_t{1});
        QCOMPARE(recorder.stepsFor("dev-a")[0].type, TaskStepType::Recover);

        // dev-b 从失败步骤（首步）完整重跑并成功
        QCOMPARE(second.devices[1].state, TaskDeviceState::Succeeded);
        QCOMPARE(fixture->uploadCalls(QStringLiteral("10.0.0.42"),
                                      QStringLiteral("/apps/a.bin")), 1);
        const auto commandsB = fixture->executedCommands(QStringLiteral("10.0.0.42"));
        QCOMPARE(commandsB,
                 (std::vector<QString>{QStringLiteral("reboot"), QStringLiteral("hold-me")}));
    }

    // 场景七：校验阶段——缺少必需端点的设备失败且不触达任何协议执行器
    void validationFailureSkipsProtocolCalls()
    {
        auto fixture = std::make_shared<EngineFixture>();
        Recorder recorder;
        TaskExecutionEngine engine(makeDependencies(fixture));

        TaskTemplate tmpl = makeTemplate({
            makeStep(TaskStepType::DeployFiles,
                     deployParameters({{m_fileA, QStringLiteral("/apps/a.bin")}})),
        });
        TaskExecutionRequest request;
        request.task = tmpl;
        request.devices = {makeProfile("dev-c", QStringLiteral("10.0.0.51"),
                                       /*withFtpEndpoint=*/false)};
        request.authResolver = [](const DeviceProfile&) { return makeAuth(); };

        const TaskRunId runId = engine.start(request, recorder.make());
        QVERIFY(!runId.empty());
        QTRY_VERIFY_WITH_TIMEOUT(recorder.runDone(), 20000);

        const TaskRunOutcome outcome = recorder.outcomeAt(0);
        QCOMPARE(outcome.devices[0].state, TaskDeviceState::Failed);
        QVERIFY2(outcome.devices[0].error.find(u8"端点") != std::string::npos,
                 "校验失败必须给出含缺少端点的中文原因");
        const auto states = recorder.statesFor("dev-c");
        QCOMPARE(states,
                 (std::vector<TaskDeviceState>{TaskDeviceState::Pending,
                                               TaskDeviceState::Validating,
                                               TaskDeviceState::Failed}));
        QVERIFY(recorder.stepsFor("dev-c").empty());
        QCOMPARE(fixture->uploadEntered(), 0);
    }

    // 场景八：活跃运行互斥、非法请求拒绝、空闲取消空操作，收口后恢复运行可用
    void startGuardsAndRetryWhenIdle()
    {
        auto fixture = std::make_shared<EngineFixture>();
        Recorder recorder;
        TaskExecutionEngine engine(makeDependencies(fixture));

        TaskTemplate tmpl = makeTemplate({
            makeStep(TaskStepType::DeployFiles,
                     deployParameters({{m_fileA, QStringLiteral("/apps/a.bin")}})),
        });
        TaskExecutionRequest request;
        request.task = tmpl;
        request.devices = {makeProfile("dev-a", QStringLiteral("10.0.0.61"))};
        request.authResolver = [](const DeviceProfile&) { return makeAuth(); };

        // 非法请求拒绝：缺凭据解析器
        TaskExecutionRequest broken = request;
        broken.authResolver = nullptr;
        QVERIFY(engine.start(broken, recorder.make()).empty());
        // 非法请求拒绝：无设备
        TaskExecutionRequest noDevices = request;
        noDevices.devices.clear();
        QVERIFY(engine.start(noDevices, recorder.make()).empty());
        // 空闲取消是空操作
        engine.cancel();
        QVERIFY(!engine.isRunning());

        fixture->armDeployGate(QStringLiteral("10.0.0.61"));
        const TaskRunId runId1 = engine.start(request, recorder.make());
        QVERIFY(!runId1.empty());
        QTRY_VERIFY_WITH_TIMEOUT(fixture->uploadEntered() >= 1, 20000);
        QVERIFY(engine.isRunning());

        // 活跃运行期间：拒绝二次 start 与 retryFailed
        QVERIFY(engine.start(request, recorder.make()).empty());
        QVERIFY(engine.retryFailed(runId1).empty());

        engine.cancel();
        fixture->releaseDeployGate();
        fixture->disarmDeployGate();
        QTRY_VERIFY_WITH_TIMEOUT(recorder.runDone(), 20000);
        QVERIFY(!engine.isRunning());
        QCOMPARE(recorder.outcomeAt(0).devices[0].state, TaskDeviceState::Cancelled);

        // 收口后阶段恢复可用，并成功走完
        recorder.resetRunDone();
        const TaskRunId runId2 = engine.retryFailed(runId1);
        QVERIFY(!runId2.empty());
        QTRY_VERIFY_WITH_TIMEOUT(recorder.runDone(), 20000);
        QCOMPARE(recorder.outcomeAt(1).devices[0].state, TaskDeviceState::Succeeded);
        // 未知运行 ID 拒绝
        QVERIFY(engine.retryFailed("taskrun-unknown").empty());

        // 脱敏：日志、步骤错误与设备错误都不得含凭据明文
        for (const auto& message : recorder.logs()) {
            QVERIFY2(message.find("task-secret") == std::string::npos,
                     "日志不得含凭据明文");
        }
        for (const auto& outcome : {recorder.outcomeAt(0), recorder.outcomeAt(1)}) {
            for (const auto& device : outcome.devices) {
                QVERIFY2(device.error.find("task-secret") == std::string::npos,
                         "设备错误不得含凭据明文");
                for (const auto& step : device.steps) {
                    QVERIFY2(step.error.find("task-secret") == std::string::npos,
                             "步骤错误不得含凭据明文");
                }
            }
        }
    }

    // 场景九：终审 Important 2——步骤错误文本含凭据时，引擎日志（onLog，
    // 亦即 qDebug 镜像进全局/文件日志的路径）必须先经 sanitizeTaskErrorText 脱敏
    void stepErrorTextInLogsIsSanitized()
    {
        auto fixture = std::make_shared<EngineFixture>();
        Recorder recorder;
        TaskExecutionEngine engine(makeDependencies(fixture));
        fixture->setHardFailingConnect(QStringLiteral("10.0.0.81"),
                                       u8"登录被拒绝：password=task-secret");

        TaskTemplate tmpl = makeTemplate({
            makeStep(TaskStepType::RunCommands,
                     commandParameters({QStringLiteral("uptime")})),
        });
        TaskExecutionRequest request;
        request.task = tmpl;
        request.devices = {makeProfile("dev-log", QStringLiteral("10.0.0.81"))};
        request.authResolver = [](const DeviceProfile&) { return makeAuth(); };

        const TaskRunId runId = engine.start(request, recorder.make());
        QVERIFY(!runId.empty());
        QTRY_VERIFY_WITH_TIMEOUT(recorder.runDone(), 20000);

        const TaskRunOutcome outcome = recorder.outcomeAt(0);
        QCOMPARE(outcome.devices[0].state, TaskDeviceState::Failed);

        bool sawReasonLog = false;
        for (const auto& message : recorder.logs()) {
            QVERIFY2(message.find("task-secret") == std::string::npos,
                     qPrintable(QStringLiteral("引擎日志不得含凭据明文: %1")
                                    .arg(QString::fromStdString(message))));
            if (message.find(u8"原因：") != std::string::npos) {
                sawReasonLog = true;
                QVERIFY2(message.find("password=***") != std::string::npos,
                         qPrintable(QStringLiteral("步骤错误入日志前必须脱敏: %1")
                                        .arg(QString::fromStdString(message))));
            }
        }
        QVERIFY2(sawReasonLog, "失败步骤必须产生含「原因：」的结果日志");
    }
};

QTEST_MAIN(TstTaskExecutionEngine)
#include "tst_task_execution_engine.moc"
