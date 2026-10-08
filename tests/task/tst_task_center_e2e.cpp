// tst_task_center_e2e.cpp — v2.11 Task 7 设备任务中心端到端集成验收。
//
// 全链路：设备档案持久化与圈选（DeviceRegistry）→ 模板持久化读取
// （TaskTemplateStore）→ 真实编排执行（TaskExecutionEngine + Fake 适配器，
// 零网络）→ 执行记录落库与复查（TaskRunStore）。
// 存储写入路径复刻 TaskCenterWidget 的集成形态（协调线程回调 →
// QueuedConnection 编组回宿主线程 → 单线程写契约），但不引入 QWidget：
// 选择/编排/落库的每一步都走生产类，Fake 只替代协议通道本身。
// 断言覆盖：run-1 部分成功收口（设备名/地址不可变快照 + 全部步骤状态）、
// retryFailed 阶段恢复 run-2（已交付文件不重传、成功设备整体跳过、
// 收敛为成功）、ConfigStore 关库重开后的记录耐久，以及凭据永不落库。

#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTemporaryDir>
#include <QUuid>
#include <QVariantList>

#include "adapter/IProtocolAdapter.h"
#include "config/ConfigStore.h"
#include "device/DeviceRegistry.h"
#include "task/TaskExecutionEngine.h"
#include "task/TaskRunStore.h"
#include "task/TaskTemplateStore.h"
#include "transfer/AdapterTransferChannel.h"

#include <algorithm>
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

// ——————— 内存夹具：部署通道 + 命令执行脚本（对齐 tst_task_execution_engine）———————

class E2EFixture
{
public:
    bool deployUpload(const QString& ip, const QString& localPath, const QString& remotePath)
    {
        const QString finalPath = stripPartSuffix(remotePath);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_uploadCalls[uploadKey(ip, finalPath)];
            m_uploads[ip].push_back(finalPath);
        }
        if (isFailingTarget(ip, finalPath)) {
            return false;
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

    bool commandConnect(const QString&, std::string* /*error*/)
    {
        return true;
    }

    Response commandRequest(const QString& ip, const QString& command)
    {
        Response response{true, "ok", std::string{}, 0};
        std::lock_guard<std::mutex> lock(m_mutex);
        m_executedCommands[ip].push_back(command);
        if (command == QStringLiteral("reboot") && m_rebootDisconnectIps.count(ip)) {
            response = {false, std::string{}, "connection closed after send", 0};
        }
        return response;
    }

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
    void addRebootDisconnectIp(const QString& ip)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_rebootDisconnectIps.insert(ip);
    }

    int uploadCalls(const QString& ip, const QString& remote)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_uploadCalls.find(uploadKey(ip, remote));
        return it == m_uploadCalls.end() ? 0 : it->second;
    }
    std::vector<QString> uploadsFor(const QString& ip)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_uploads[ip];
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
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_failingRemotes.count(uploadKey(ip, remotePath)) > 0;
    }

    std::mutex m_mutex;
    std::map<QString, std::map<QString, QByteArray>> m_remoteFiles;
    std::map<QString, std::vector<QString>> m_uploads;
    std::map<QString, int> m_uploadCalls;
    std::set<QString> m_failingRemotes;
    std::set<QString> m_rebootDisconnectIps;
    std::map<QString, std::vector<QString>> m_executedCommands;
};

// ——————— Fake 协议适配器（ftp=部署通道载体，telnet=命令载体）———————

class FakeDeployAdapter final : public IProtocolAdapter
{
public:
    explicit FakeDeployAdapter(std::shared_ptr<E2EFixture> fixture)
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
    // 非重试类文案：失败步骤快速收口，测试不依赖退避时长
    std::string lastError() const override { return u8"模拟上传失败"; }
    std::future<Response> request(const Request&) override
    {
        return std::async(std::launch::deferred, [] { return Response{}; });
    }
    void subscribe(const Request&, StreamCallback) override {}
    void unsubscribe() override {}
    ProtocolCapability capability() const override { return {}; }

    const QString& ip() const { return m_ip; }

private:
    std::shared_ptr<E2EFixture> m_fixture;
    QString m_ip;
};

class FakeCommandAdapter final : public IProtocolAdapter
{
public:
    explicit FakeCommandAdapter(std::shared_ptr<E2EFixture> fixture)
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
    std::shared_ptr<E2EFixture> m_fixture;
    QString m_ip;
    std::string m_lastError = u8"模拟命令失败";
};

TaskExecutionEngine::Dependencies makeDependencies(const std::shared_ptr<E2EFixture>& fixture)
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
            return fake && fixture->deployUpload(fake->ip(), local, remote);
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
        ops.setCancelFlag = [](std::atomic_bool*) {};
        return std::make_unique<AdapterTransferChannel>(QStringLiteral("ftp"), adapter,
                                                        std::move(ops));
    };
    deps.transferRetrySleeper = [](int) {};
    return deps;
}

bool isTerminalState(TaskDeviceState state)
{
    switch (state) {
    case TaskDeviceState::Succeeded:
    case TaskDeviceState::Failed:
    case TaskDeviceState::Cancelled:
    case TaskDeviceState::Skipped:
    case TaskDeviceState::PartiallySucceeded:
        return true;
    default:
        return false;
    }
}

// 与 TaskCenterWidget::aggregateStatus 相同的收敛规则（运行级终态的唯一口径）
TaskRunStatus aggregateRunStatus(const TaskRunOutcome& outcome)
{
    int succeeded = 0;
    int partial = 0;
    int cancelled = 0;
    int failed = 0;
    for (const auto& device : outcome.devices) {
        switch (device.state) {
        case TaskDeviceState::Succeeded:
        case TaskDeviceState::Skipped:
            ++succeeded;
            break;
        case TaskDeviceState::PartiallySucceeded:
            ++partial;
            break;
        case TaskDeviceState::Cancelled:
            ++cancelled;
            break;
        default:
            ++failed;
            break;
        }
    }
    const int total = static_cast<int>(outcome.devices.size());
    if (total > 0 && succeeded == total)
        return TaskRunStatus::Succeeded;
    if (succeeded > 0 || partial > 0)
        return TaskRunStatus::PartiallySucceeded;
    if (cancelled > 0 && failed == 0)
        return TaskRunStatus::Cancelled;
    return TaskRunStatus::Failed;
}

// ——————— 无头集成形态：复刻 TaskCenterWidget 的存储写入契约 ———————
// 引擎回调都在协调线程触发；一律 QueuedConnection 编组回本对象宿主线程
// （= 测试主线程）后单线程写 TaskRunStore，与生产 UI 同构。QPointer 守卫
// 复刻谱系安全：宿主析构后陈旧回调直接丢弃。
class CenterHarness : public QObject
{
    Q_OBJECT
public:
    explicit CenterHarness(TaskRunStore* runs) : m_runs(runs) {}

    // 对齐 TaskCenterWidget::installActiveRun：名称/地址快照就位
    void installActiveRun(const std::string& storeRunId,
                          const std::vector<DeviceProfile>& devices)
    {
        m_storeRunId = storeRunId;
        m_rows.clear();
        m_registered.clear();
        m_terminalCount = 0;
        m_storeWriteFailures = 0;
        m_runProcessed.store(false);
        for (const auto& profile : devices) {
            TaskDeviceResult result;
            result.deviceId = profile.deviceId;
            result.name = profile.name;
            if (!profile.endpoints.empty()) {
                result.address = profile.endpoints.front().ip + ":"
                    + std::to_string(profile.endpoints.front().port);
            }
            result.state = TaskDeviceState::Pending;
            m_rows[result.deviceId] = result;
        }
    }

    TaskExecutionEngine::Callbacks makeCallbacks()
    {
        TaskExecutionEngine::Callbacks callbacks;
        const QPointer<CenterHarness> guard(this);
        auto post = [guard, this](std::function<void()> task) {
            QMetaObject::invokeMethod(this, [guard, task = std::move(task)] {
                if (!guard)
                    return;
                task();
            }, Qt::QueuedConnection);
        };
        callbacks.onDeviceStateChanged = [post, this](const std::string& deviceId,
                                                      TaskDeviceState state) {
            post([this, deviceId, state] { handleDeviceStateChanged(deviceId, state); });
        };
        callbacks.onStepFinished = [post, this](const std::string& deviceId,
                                                const TaskStepResult& step) {
            post([this, deviceId, step] { handleStepFinished(deviceId, step); });
        };
        callbacks.onRunFinished = [post, this](const TaskRunOutcome& outcome) {
            const TaskRunOutcome snapshot = outcome;   // 值拷贝跨线程投递
            post([this, snapshot] { handleRunFinished(snapshot); });
        };
        callbacks.onLog = [post, this](const std::string& message) {
            post([this, message] { m_logs.push_back(message); });
        };
        return callbacks;
    }

    bool runProcessed() const { return m_runProcessed.load(); }
    TaskRunStatus lastStatus() const { return m_lastStatus; }
    int terminalCount() const { return m_terminalCount; }
    int storeWriteFailures() const { return m_storeWriteFailures; }
    std::vector<std::string> logs() const { return m_logs; }

private:
    void handleDeviceStateChanged(const std::string& deviceId, TaskDeviceState state)
    {
        const auto it = m_rows.find(deviceId);
        if (it == m_rows.end())
            return;
        it->second.state = state;
        // 首事件登记：快照行由 begin 写入，这里把设备指针就位
        if (!m_registered.count(deviceId)) {
            m_registered.insert(deviceId);
            if (!m_runs->appendDeviceResult(m_storeRunId, it->second))
                ++m_storeWriteFailures;
        }
        if (isTerminalState(state)) {
            if (!m_runs->appendDeviceResult(m_storeRunId, it->second))
                ++m_storeWriteFailures;
            ++m_terminalCount;
        }
    }

    void handleStepFinished(const std::string& deviceId, const TaskStepResult& step)
    {
        const auto it = m_rows.find(deviceId);
        if (it == m_rows.end())
            return;
        if (!m_registered.count(deviceId)) {
            m_registered.insert(deviceId);
            if (!m_runs->appendDeviceResult(m_storeRunId, it->second))
                ++m_storeWriteFailures;
        }
        it->second.steps.push_back(step);
        if (!m_runs->appendStepResult(m_storeRunId, step))
            ++m_storeWriteFailures;
    }

    // 对齐 TaskCenterWidget::handleRunFinished：以引擎快照为准逐台收口
    // （幂等覆盖，含阶段恢复携带的已成功步骤），再按收敛状态 finish
    void handleRunFinished(const TaskRunOutcome& outcome)
    {
        const TaskRunStatus status = aggregateRunStatus(outcome);
        for (const auto& device : outcome.devices) {
            TaskDeviceResult result;
            result.deviceId = device.deviceId;
            result.name = device.name;
            result.address = device.address;
            result.state = device.state;
            result.steps = device.steps;
            if (!m_runs->appendDeviceResult(m_storeRunId, result))
                ++m_storeWriteFailures;
        }
        if (!m_runs->finish(m_storeRunId, status))
            ++m_storeWriteFailures;
        m_lastStatus = status;
        m_runProcessed.store(true);
    }

    TaskRunStore* m_runs = nullptr;
    std::string m_storeRunId;
    std::map<std::string, TaskDeviceResult> m_rows;
    std::set<std::string> m_registered;
    int m_terminalCount = 0;
    int m_storeWriteFailures = 0;
    TaskRunStatus m_lastStatus = TaskRunStatus::Running;
    std::atomic_bool m_runProcessed{false};
    std::vector<std::string> m_logs;
};

// ——————— 夹具组装 ———————

DeviceProfile makeProfile(const std::string& id, const QString& name, const QString& ip,
                          std::vector<std::string> tags = {})
{
    DeviceProfile profile;
    profile.deviceId = id;
    profile.name = name.toStdString();
    profile.tags = std::move(tags);
    profile.endpoints.push_back({"ftp", ip.toStdString(), 21, "cred-ref"});
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

// 全链路模板：部署→命令→重启→恢复检查（生产顺序）
TaskTemplate makeFullTemplate(std::string templateId, const QString& fileA, const QString& fileB,
                              std::vector<std::string> deviceIds)
{
    TaskTemplate tmpl;
    tmpl.templateId = std::move(templateId);
    tmpl.name = "产线批量投产";
    tmpl.description = "部署两个文件后执行初始化命令并重启校验";
    tmpl.version = 2;
    tmpl.deviceIds = std::move(deviceIds);
    tmpl.steps = {
        makeStep(TaskStepType::DeployFiles,
                 deployParameters({{fileA, QStringLiteral("/apps/a.bin")},
                                   {fileB, QStringLiteral("/apps/b.bin")}})),
        makeStep(TaskStepType::RunCommands, commandParameters({QStringLiteral("c1")})),
        makeStep(TaskStepType::Reboot, QVariantMap{}),
        makeStep(TaskStepType::Recover, commandParameters({QStringLiteral("chk")})),
    };
    return tmpl;
}

std::vector<DeviceProfile> resolveTargetsByIds(const DeviceRegistry& registry,
                                               const TaskTemplate& task)
{
    // 复刻 TaskCenterWidget::resolveTargets 的 deviceIds 顺序解析（生产契约）
    std::vector<DeviceProfile> result;
    for (const auto& deviceId : task.deviceIds) {
        if (const auto found = registry.find(deviceId))
            result.push_back(*found);
    }
    return result;
}

} // namespace

class TstTaskCenterE2E : public QObject
{
    Q_OBJECT

private:
    QString m_dbPath;
    QString m_workDir;
    QTemporaryDir m_filesDir;
    QString m_fileA;
    QString m_fileB;

    static const TaskRunRecord* findRecord(const std::vector<TaskRunRecord>& records,
                                           const TaskRunId& runId)
    {
        for (const auto& record : records) {
            if (record.runId == runId)
                return &record;
        }
        return nullptr;
    }

    static QString dbRowDump(const QString& type)
    {
        QString dump;
        const auto rows = ConfigStore::instance().list(type, 500);
        for (const auto& row : rows) {
            dump += QString::fromUtf8(
                QJsonDocument(QJsonObject::fromVariantMap(row)).toJson(QJsonDocument::Compact));
        }
        return dump;
    }

    void writeFixtureFiles()
    {
        m_fileA = m_filesDir.filePath(QStringLiteral("a.bin"));
        m_fileB = m_filesDir.filePath(QStringLiteral("b.bin"));
        QFile fileA(m_fileA);
        QVERIFY(fileA.open(QIODevice::WriteOnly));
        fileA.write(QByteArray(512, 'A'));
        QFile fileB(m_fileB);
        QVERIFY(fileB.open(QIODevice::WriteOnly));
        fileB.write(QByteArray(256, 'B'));
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_filesDir.isValid());
        QCoreApplication::setApplicationVersion(QStringLiteral("2.11.0-e2e"));
    }

    void init()
    {
        // 每用例独立进程专属库文件，避免 CI 残留/并发污染（对齐 tst_task_run_store）
        m_workDir = QDir::temp().filePath(
            QStringLiteral("tst_task_center_e2e_%1")
                .arg(QUuid::createUuid().toString(QUuid::Id128)));
        QVERIFY(QDir().mkpath(m_workDir));
        m_dbPath = QDir(m_workDir).filePath(QStringLiteral("config.db"));
        QVERIFY2(ConfigStore::instance().open(m_dbPath),
                 qPrintable(QStringLiteral("open failed path=%1").arg(m_dbPath)));
        writeFixtureFiles();
    }

    void cleanup()
    {
        ConfigStore::instance().close();
        QDir(m_workDir).removeRecursively();
    }

    // 场景一：档案圈选契约——持久化的 DeviceProfile 经 DeviceRegistry.load()
    // 复原后，模板 deviceIds 按序解析、tagSelectors 命中、未知 ID 静默剔除
    void profileSelectionResolvesPersistedRegistry()
    {
        DeviceRegistry registry;
        QVERIFY(registry.save(makeProfile("device-sel-a", QStringLiteral("产线A-机器人"),
                                          QStringLiteral("10.0.1.11"), {"plc"})));
        QVERIFY(registry.save(makeProfile("device-sel-b", QStringLiteral("产线B-机器人"),
                                          QStringLiteral("10.0.1.12"), {"plc", "hot"})));

        TaskTemplateStore templates;
        TaskTemplate tmpl;
        tmpl.templateId = "template-selection";
        tmpl.name = "圈选验证";
        tmpl.version = 1;
        tmpl.deviceIds = {"device-sel-b", "device-ghost", "device-sel-a"};
        tmpl.tagSelectors = {"hot"};
        tmpl.steps = {makeStep(TaskStepType::RunCommands,
                               commandParameters({QStringLiteral("c1")}))};
        QVERIFY2(templates.save(tmpl), "模板必须通过校验并落库");

        // 全新注册表只从 ConfigStore 读取（模拟进程重启后的选择路径）
        DeviceRegistry reloaded;
        QVERIFY(reloaded.load());
        const auto loaded = templates.load("template-selection");
        QVERIFY(loaded.has_value());

        const auto devices = resolveTargetsByIds(reloaded, *loaded);
        QCOMPARE(devices.size(), std::size_t{2});
        QCOMPARE(devices[0].deviceId, std::string("device-sel-b"));
        QCOMPARE(devices[1].deviceId, std::string("device-sel-a"));
        // 名称/端点跨持久化往返保真
        QCOMPARE(QString::fromStdString(devices[0].name), QStringLiteral("产线B-机器人"));
        QCOMPARE(devices[0].endpoints.size(), std::size_t{2});
        QCOMPARE(QString::fromStdString(devices[0].endpoints.front().protocol),
                 QStringLiteral("ftp"));
        // 标签圈选：hot 命中 device-sel-b 且不与 deviceIds 结果重复
        std::set<std::string> tagMatches;
        for (const auto& profile : reloaded.list()) {
            if (std::find(profile.tags.begin(), profile.tags.end(), "hot")
                    != profile.tags.end())
                tagMatches.insert(profile.deviceId);
        }
        QCOMPARE(tagMatches, (std::set<std::string>{"device-sel-b"}));
    }

    // 场景二：全链路——圈选后的设备按"部署→命令→重启→恢复"执行，重启断开按
    // 预期成功；run-1 以部分成功落库（快照/步骤全量），retryFailed 阶段恢复
    // run-2 收敛为成功（已交付文件不重传、成功设备跳过）；关库重开记录仍在；
    // 凭据明文不出现在任何记录行与日志
    void fullChainDeployCommandRebootRecoverThenRetry()
    {
        // —— 档案与模板：生产持久化缝 ——
        DeviceRegistry registry;
        QVERIFY(registry.save(makeProfile("device-a", QStringLiteral("产线A-控制器1"),
                                          QStringLiteral("10.0.0.11"), {"prod"})));
        QVERIFY(registry.save(makeProfile("device-b", QStringLiteral("产线A-控制器2"),
                                          QStringLiteral("10.0.0.12"), {"prod"})));
        QVERIFY(registry.save(makeProfile("device-c", QStringLiteral("产线B-控制器1"),
                                          QStringLiteral("10.0.0.13"), {"prod"})));

        TaskTemplateStore templates;
        const auto full = makeFullTemplate("template-e2e-center", m_fileA, m_fileB,
                                           {"device-a", "device-b", "device-c"});
        QVERIFY2(templates.save(full), "全链路模板必须通过校验并落库");

        // —— 选择：只信 ConfigStore 里的档案与模板 ——
        DeviceRegistry reloaded;
        QVERIFY(reloaded.load());
        const auto task = templates.load("template-e2e-center");
        QVERIFY(task.has_value());
        const auto devices = resolveTargetsByIds(reloaded, *task);
        QCOMPARE(devices.size(), std::size_t{3});

        TaskExecutionRequest request;
        request.task = *task;
        request.devices = devices;
        request.authResolver = [](const DeviceProfile&) {
            AuthInfo auth;
            auth.user = "e2e-user";
            auth.password = "e2e-secret-勿落库";
            return auth;
        };

        // —— 执行 + 落库：TaskCenterWidget 同构的无头形态 ——
        auto fixture = std::make_shared<E2EFixture>();
        TaskRunStore runs;
        CenterHarness harness(&runs);
        TaskExecutionEngine engine(makeDependencies(fixture));
        fixture->addRebootDisconnectIp(QStringLiteral("10.0.0.11"));
        // dev-b 的第二份文件投递失败 → 设备级部分成功，触发阶段恢复场景
        fixture->setFailingRemote(QStringLiteral("10.0.0.12"), QStringLiteral("/apps/b.bin"));

        const std::string storeRunId1 = runs.begin(request);
        QVERIFY2(!storeRunId1.empty(), "run-1 执行记录 begin 必须成功");
        harness.installActiveRun(storeRunId1, devices);
        const std::string engineRunId1 = engine.start(request, harness.makeCallbacks());
        QVERIFY2(!engineRunId1.empty(), "引擎必须接受全链路请求");
        QTRY_VERIFY_WITH_TIMEOUT(harness.runProcessed(), 30000);
        QCOMPARE(harness.storeWriteFailures(), 0);
        QCOMPARE(harness.terminalCount(), 3);

        // —— 引擎快照收口口径 ——
        QCOMPARE(harness.lastStatus(), TaskRunStatus::PartiallySucceeded);

        // —— run-1 持久化记录复查（落库真相，而非内存态）——
        {
            const auto records = runs.query({});
            const TaskRunRecord* record = findRecord(records, storeRunId1);
            QVERIFY2(record != nullptr, "run-1 必须可按 ID 查回");
            QCOMPARE(record->templateId, std::string("template-e2e-center"));
            QCOMPARE(record->templateVersion, 2);
            QCOMPARE(record->status, TaskRunStatus::PartiallySucceeded);
            QVERIFY2(record->startedAt > 0, "startedAt 必须快照");
            QVERIFY2(record->finishedAt >= record->startedAt, "finishedAt 必须收口");
            QCOMPARE(QString::fromStdString(record->softwareVersion),
                     QStringLiteral("2.11.0-e2e"));
            QVERIFY(!record->operatorName.empty());

            QCOMPARE(record->devices.size(), std::size_t{3});
            // 名称/地址不可变快照（begin 定格，中文档案名 UTF-8 保真）
            QCOMPARE(record->devices[0].deviceId, std::string("device-a"));
            QCOMPARE(QString::fromStdString(record->devices[0].name),
                     QStringLiteral("产线A-控制器1"));
            QCOMPARE(record->devices[1].deviceId, std::string("device-b"));
            QCOMPARE(QString::fromStdString(record->devices[1].name),
                     QStringLiteral("产线A-控制器2"));
            QCOMPARE(record->devices[2].deviceId, std::string("device-c"));
            QCOMPARE(record->devices[2].address, std::string("10.0.0.13:21"));

            // dev-a：四步全成功，重启断开按预期成功
            QCOMPARE(record->devices[0].state, TaskDeviceState::Succeeded);
            QCOMPARE(record->devices[0].steps.size(), std::size_t{4});
            QCOMPARE(record->devices[0].steps[0].type, TaskStepType::DeployFiles);
            QCOMPARE(record->devices[0].steps[1].type, TaskStepType::RunCommands);
            QCOMPARE(record->devices[0].steps[2].type, TaskStepType::Reboot);
            QCOMPARE(record->devices[0].steps[3].type, TaskStepType::Recover);
            for (const auto& step : record->devices[0].steps) {
                QCOMPARE(step.state, TaskDeviceState::Succeeded);
                QVERIFY2(step.attempt >= 1, "成功步骤必须记录尝试次数");
            }
            // dev-b：部署部分成功后停止（失败隔离）
            QCOMPARE(record->devices[1].state, TaskDeviceState::PartiallySucceeded);
            QCOMPARE(record->devices[1].steps.size(), std::size_t{1});
            QCOMPARE(record->devices[1].steps[0].type, TaskStepType::DeployFiles);
            QCOMPARE(record->devices[1].steps[0].state, TaskDeviceState::PartiallySucceeded);
            // dev-c：不受 dev-b 失败影响，全链路成功
            QCOMPARE(record->devices[2].state, TaskDeviceState::Succeeded);
            QCOMPARE(record->devices[2].steps.size(), std::size_t{4});
        }

        // 执行动作真相：dev-a 重启断开仍走完恢复；dev-b 只交付 a.bin 即停
        QCOMPARE(fixture->uploadsFor(QStringLiteral("10.0.0.11")),
                 (std::vector<QString>{QStringLiteral("/apps/a.bin"),
                                       QStringLiteral("/apps/b.bin")}));
        QCOMPARE(fixture->executedCommands(QStringLiteral("10.0.0.11")),
                 (std::vector<QString>{QStringLiteral("c1"), QStringLiteral("reboot"),
                                       QStringLiteral("chk")}));
        QCOMPARE(fixture->executedCommands(QStringLiteral("10.0.0.12")),
                 std::vector<QString>{});
        QCOMPARE(fixture->uploadCalls(QStringLiteral("10.0.0.12"),
                                      QStringLiteral("/apps/a.bin")), 1);

        // —— 阶段恢复：run-2 走 TaskCenterWidget::onRetryClicked 同构流程 ——
        fixture->clearFailingRemotes();
        const std::string engineRunId2 = engine.retryFailed(engineRunId1);
        QVERIFY2(!engineRunId2.empty(), "retryFailed 必须启动恢复运行");
        const std::string storeRunId2 = runs.begin(request);
        QVERIFY2(!storeRunId2.empty(), "run-2 执行记录 begin 必须成功");
        QVERIFY2(storeRunId2 != storeRunId1, "每轮执行必须是独立记录");
        harness.installActiveRun(storeRunId2, devices);
        QTRY_VERIFY_WITH_TIMEOUT(harness.runProcessed(), 30000);
        QCOMPARE(harness.storeWriteFailures(), 0);
        QCOMPARE(harness.lastStatus(), TaskRunStatus::Succeeded);

        // 恢复语义锁定：已交付 a.bin 不重传、补投 b.bin、成功设备不再触达
        QCOMPARE(fixture->uploadCalls(QStringLiteral("10.0.0.12"),
                                      QStringLiteral("/apps/a.bin")), 1);
        QCOMPARE(fixture->uploadCalls(QStringLiteral("10.0.0.12"),
                                      QStringLiteral("/apps/b.bin")), 2);
        QCOMPARE(fixture->executedCommands(QStringLiteral("10.0.0.12")),
                 (std::vector<QString>{QStringLiteral("c1"), QStringLiteral("reboot"),
                                       QStringLiteral("chk")}));
        QCOMPARE(fixture->uploadCalls(QStringLiteral("10.0.0.11"),
                                      QStringLiteral("/apps/a.bin")), 1);
        QCOMPARE(fixture->executedCommands(QStringLiteral("10.0.0.13")),
                 (std::vector<QString>{QStringLiteral("c1"), QStringLiteral("reboot"),
                                       QStringLiteral("chk")}));

        {
            const auto records = runs.query({});
            const TaskRunRecord* record = findRecord(records, storeRunId2);
            QVERIFY2(record != nullptr, "run-2 必须可按 ID 查回");
            QCOMPARE(record->status, TaskRunStatus::Succeeded);
            QCOMPARE(record->devices[0].state, TaskDeviceState::Skipped);
            QCOMPARE(record->devices[0].steps.size(), std::size_t{4});
            // 快照完整性：跳过设备的携带步骤原样入库
            for (const auto& step : record->devices[0].steps)
                QCOMPARE(step.state, TaskDeviceState::Succeeded);
            QCOMPARE(record->devices[1].state, TaskDeviceState::Succeeded);
            QCOMPARE(record->devices[1].steps.size(), std::size_t{4});
            // dev-c 与 dev-a 同理：run-1 已成功 → run-2 整体跳过
            QCOMPARE(record->devices[2].state, TaskDeviceState::Skipped);
            QCOMPARE(record->devices[2].steps.size(), std::size_t{4});
            // 历史筛选：模板 ID 命中两轮；单设备命中同样两轮
            TaskRunFilter byTemplate;
            byTemplate.templateId = "template-e2e-center";
            QCOMPARE(runs.query(byTemplate).size(), std::size_t{2});
            TaskRunFilter byDevice;
            byDevice.deviceId = "device-b";
            QCOMPARE(runs.query(byDevice).size(), std::size_t{2});
            TaskRunFilter byGhost;
            byGhost.templateId = "template-不存在";
            QVERIFY(runs.query(byGhost).empty());
        }

        // —— 耐久：关库重开，执行记录/档案/模板全部原样可读 ——
        ConfigStore::instance().close();
        QVERIFY(ConfigStore::instance().open(m_dbPath));
        {
            TaskRunStore reopened;
            const auto records = reopened.query({});
            QVERIFY2(findRecord(records, storeRunId1) != nullptr, "run-1 必须跨重启耐久");
            const TaskRunRecord* reread = findRecord(records, storeRunId2);
            QVERIFY2(reread != nullptr, "run-2 必须跨重启耐久");
            QCOMPARE(reread->status, TaskRunStatus::Succeeded);
            QCOMPARE(reread->devices[1].steps.size(), std::size_t{4});
            DeviceRegistry again;
            QVERIFY(again.load());
            QCOMPARE(again.list().size(), std::size_t{3});
            TaskTemplateStore againTemplates;
            QVERIFY(againTemplates.load("template-e2e-center").has_value());
        }

        // —— 凭据脱敏铁律：记录行与引擎日志都不得含密码明文 ——
        ConfigStore::instance().close();
        QVERIFY(ConfigStore::instance().open(m_dbPath));
        for (const QString& type : {QStringLiteral("task.run"),
                                    QStringLiteral("task.run.device"),
                                    QStringLiteral("task.run.step"),
                                    QStringLiteral("device.profile"),
                                    QStringLiteral("task.template")}) {
            const QString dump = dbRowDump(type);
            QVERIFY2(!dump.contains(QStringLiteral("e2e-secret-勿落库")),
                     qPrintable(QStringLiteral("%1 记录不得含凭据明文").arg(type)));
            QVERIFY2(!dump.contains(QStringLiteral("e2e-user")),
                     qPrintable(QStringLiteral("%1 记录不得含凭据用户名").arg(type)));
        }
        for (const auto& message : harness.logs()) {
            QVERIFY2(message.find("e2e-secret") == std::string::npos,
                     "引擎日志不得含凭据明文");
        }
    }

    // 场景三：校验失败也要持久收口——缺部署端点的设备在 Validating 阶段
    // 失败，记录以失败收口且错误文本脱敏，不触达任何协议动作
    void validationFailurePersistsFailedRun()
    {
        DeviceRegistry registry;
        DeviceProfile telnetOnly = makeProfile("device-telnet", QStringLiteral("纯命令网关"),
                                               QStringLiteral("10.0.0.21"));
        telnetOnly.endpoints.erase(
            std::remove_if(telnetOnly.endpoints.begin(), telnetOnly.endpoints.end(),
                           [](const DeviceEndpoint& endpoint) { return endpoint.protocol == "ftp"; }),
            telnetOnly.endpoints.end());
        QVERIFY(registry.save(telnetOnly));

        TaskTemplateStore templates;
        QVERIFY(templates.save(makeFullTemplate("template-e2e-bad-endpoint", m_fileA, m_fileB,
                                                {"device-telnet"})));
        DeviceRegistry reloaded;
        QVERIFY(reloaded.load());
        const auto task = templates.load("template-e2e-bad-endpoint");
        QVERIFY(task.has_value());

        auto fixture = std::make_shared<E2EFixture>();
        TaskRunStore runs;
        CenterHarness harness(&runs);
        TaskExecutionEngine engine(makeDependencies(fixture));

        TaskExecutionRequest request;
        request.task = *task;
        request.devices = resolveTargetsByIds(reloaded, *task);
        request.authResolver = [](const DeviceProfile&) {
            AuthInfo auth;
            auth.user = "e2e-user";
            auth.password = "e2e-secret-勿落库";
            return auth;
        };

        const std::string storeRunId = runs.begin(request);
        QVERIFY(!storeRunId.empty());
        harness.installActiveRun(storeRunId, request.devices);
        QVERIFY(!engine.start(request, harness.makeCallbacks()).empty());
        QTRY_VERIFY_WITH_TIMEOUT(harness.runProcessed(), 20000);

        QCOMPARE(harness.lastStatus(), TaskRunStatus::Failed);
        const auto records = runs.query({});
        const TaskRunRecord* record = findRecord(records, storeRunId);
        QVERIFY2(record != nullptr, "失败轮也必须留有记录");
        QCOMPARE(record->status, TaskRunStatus::Failed);
        QCOMPARE(record->devices.size(), std::size_t{1});
        QCOMPARE(record->devices[0].state, TaskDeviceState::Failed);
        QVERIFY(record->devices[0].steps.empty());
        // 校验失败零触达协议执行器
        QCOMPARE(fixture->uploadsFor(QStringLiteral("10.0.0.21")).size(), std::size_t{0});
        QVERIFY(fixture->executedCommands(QStringLiteral("10.0.0.21")).empty());
    }
};

QTEST_MAIN(TstTaskCenterE2E)
#include "tst_task_center_e2e.moc"
