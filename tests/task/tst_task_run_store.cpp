// v2.11 Task 5：TaskRunStore 执行记录持久化、历史筛选与报告数据测试。
// 覆盖：begin 写入不可变模板/设备快照、设备结果与步骤 round-trip、
// 流式步骤追加（appendStepResult 归属最近写入设备）、finish 收口守卫、
// 按模板/设备/结果筛选、保留天数清理（级联删除且永不删运行中记录）、
// 步骤错误落盘前脱敏（凭据明文不落库），以及报告行含设备名称+地址快照。
// 记录类型 schema（与 src/task/TaskRunStore.cpp 对齐，损坏记录直测用）：
//   task.run        key=runId
//     {runId, templateId, templateVersion, status, startedAt, finishedAt,
//      operator, softwareVersion, lastDeviceId}
//   task.run.device key=runId + "/" + deviceId
//     {runId, deviceId, name, address, state, sequence}
//   task.run.step   key=runId + "/" + deviceId + "/" + stepIndex
//     {runId, deviceId, stepIndex, stepType, state, attempt, error}
//     （stepType 不叫 type：ConfigStore::list() 剥除顶层 "type" 保留键）
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QVariantList>

#include "config/ConfigStore.h"
#include "task/TaskRunStore.h"
#include "task/TaskRunTypes.h"
#include "tools/FtpDeployTool/DeployReport.h"

#include <ctime>

namespace {

DeviceProfile makeDevice(const QString& deviceId, const QString& name,
                         const QString& ip, int port)
{
    DeviceProfile profile;
    profile.deviceId = deviceId.toStdString();
    profile.name = name.toStdString();
    DeviceEndpoint endpoint;
    endpoint.protocol = "ftp";
    endpoint.ip = ip.toStdString();
    endpoint.port = port;
    profile.endpoints.push_back(endpoint);
    return profile;
}

TaskExecutionRequest makeRequest(const QString& templateId,
                                 std::vector<DeviceProfile> devices)
{
    TaskExecutionRequest request;
    request.task.templateId = templateId.toStdString();
    request.task.name = (templateId + QStringLiteral("-模板")).toStdString();
    request.task.version = 2;
    TaskStep deployStep;
    deployStep.type = TaskStepType::DeployFiles;
    request.task.steps.push_back(deployStep);
    TaskStep commandStep;
    commandStep.type = TaskStepType::RunCommands;
    request.task.steps.push_back(commandStep);
    request.devices = std::move(devices);
    return request;
}

TaskStepResult makeStep(TaskStepType type, TaskDeviceState state, int attempt,
                        const std::string& error = {})
{
    TaskStepResult step;
    step.type = type;
    step.state = state;
    step.attempt = attempt;
    step.error = error;
    return step;
}

TaskDeviceResult makeDeviceResult(const QString& deviceId, const QString& name,
                                  const QString& address, TaskDeviceState state)
{
    TaskDeviceResult result;
    result.deviceId = deviceId.toStdString();
    result.name = name.toStdString();
    result.address = address.toStdString();
    result.state = state;
    return result;
}

QVariantMap rawRunRow(const QString& runId, const QString& templateId,
                      const QString& statusToken, std::time_t startedAt,
                      std::time_t finishedAt = 0)
{
    QVariantMap row;
    row.insert(QStringLiteral("runId"), runId);
    row.insert(QStringLiteral("templateId"), templateId);
    row.insert(QStringLiteral("templateVersion"), 1);
    row.insert(QStringLiteral("status"), statusToken);
    row.insert(QStringLiteral("startedAt"), qint64(startedAt));
    row.insert(QStringLiteral("finishedAt"), qint64(finishedAt));
    row.insert(QStringLiteral("operator"), QStringLiteral("tester"));
    row.insert(QStringLiteral("softwareVersion"), QStringLiteral("2.11.0"));
    row.insert(QStringLiteral("lastDeviceId"), QString());
    return row;
}

QVariantMap rawDeviceRow(const QString& runId, const QString& deviceId,
                         const QString& name, const QString& address, int sequence)
{
    QVariantMap row;
    row.insert(QStringLiteral("runId"), runId);
    row.insert(QStringLiteral("deviceId"), deviceId);
    row.insert(QStringLiteral("name"), name);
    row.insert(QStringLiteral("address"), address);
    row.insert(QStringLiteral("state"), QStringLiteral("succeeded"));
    row.insert(QStringLiteral("sequence"), sequence);
    return row;
}

QVariantMap rawStepRow(const QString& runId, const QString& deviceId, int stepIndex)
{
    QVariantMap row;
    row.insert(QStringLiteral("runId"), runId);
    row.insert(QStringLiteral("deviceId"), deviceId);
    row.insert(QStringLiteral("stepIndex"), stepIndex);
    row.insert(QStringLiteral("stepType"), QStringLiteral("deploy_files"));
    row.insert(QStringLiteral("state"), QStringLiteral("succeeded"));
    row.insert(QStringLiteral("attempt"), 1);
    row.insert(QStringLiteral("error"), QString());
    return row;
}

bool rowExists(const char* type, const QString& key)
{
    return ConfigStore::instance().exists(QString::fromLatin1(type), key);
}

} // namespace

class TstTaskRunStore : public QObject
{
    Q_OBJECT

private:
    QString m_dbPath;
    QString m_workDir;

    static const TaskRunRecord* findRecord(const std::vector<TaskRunRecord>& records,
                                           const QString& runId)
    {
        for (const auto& record : records) {
            if (QString::fromStdString(record.runId) == runId) return &record;
        }
        return nullptr;
    }

private slots:
    void initTestCase()
    {
        // 进程专属临时目录，避免 CI 残留/并发污染（同 tst_task_template_store）
        m_workDir = QDir::temp().filePath(
            QStringLiteral("tst_task_run_store_%1")
                .arg(QUuid::createUuid().toString(QUuid::Id128)));
        QVERIFY(QDir().mkpath(m_workDir));
        m_dbPath = QDir(m_workDir).filePath(QStringLiteral("config.db"));
    }

    void cleanupTestCase()
    {
        ConfigStore::instance().close();
        QDir(m_workDir).removeRecursively();
    }

    void init()
    {
        ConfigStore::instance().close();
        QFile::remove(m_dbPath);
        QFile::remove(m_dbPath + QStringLiteral("-wal"));
        QFile::remove(m_dbPath + QStringLiteral("-shm"));
        QVERIFY2(ConfigStore::instance().open(m_dbPath),
                 qPrintable(QStringLiteral("open failed path=%1").arg(m_dbPath)));
    }

    void cleanup()
    {
        ConfigStore::instance().close();
    }

    // begin 写入不可变模板/设备快照：名称/地址在 begin 时定格，
    // 后续调用方传入的改名/改址不影响记录
    void beginWritesImmutableSnapshots()
    {
        TaskRunStore store;
        const TaskRunId runId = store.begin(makeRequest(
            QStringLiteral("tpl-snap"), {makeDevice(QStringLiteral("device-1"),
                                                    QStringLiteral("产线A控制器"),
                                                    QStringLiteral("10.0.0.7"), 21)}));
        QVERIFY2(!runId.empty(), "begin 必须返回非空 runId");
        QVERIFY(QString::fromStdString(runId).startsWith(QStringLiteral("run-")));

        TaskRunStore fresh;
        auto records = fresh.query({});
        QCOMPARE(records.size(), size_t(1));
        const auto& record = records.front();
        QCOMPARE(QString::fromStdString(record.runId), QString::fromStdString(runId));
        QCOMPARE(QString::fromStdString(record.templateId), QStringLiteral("tpl-snap"));
        QCOMPARE(record.templateVersion, 2);
        QCOMPARE(int(record.status), int(TaskRunStatus::Running));
        QVERIFY(record.startedAt > 0);
        QCOMPARE(record.devices.size(), size_t(1));
        QCOMPARE(QString::fromStdString(record.devices.front().deviceId), QStringLiteral("device-1"));
        QCOMPARE(QString::fromStdString(record.devices.front().name), QStringLiteral("产线A控制器"));
        QCOMPARE(QString::fromStdString(record.devices.front().address),
                 QStringLiteral("10.0.0.7:21"));
        QCOMPARE(int(record.devices.front().state), int(TaskDeviceState::Pending));

        // 快照不可变：设备改名后 append 结果携带的名称/地址不得覆盖 begin 快照
        TaskDeviceResult renamed = makeDeviceResult(QStringLiteral("device-1"),
                                                    QStringLiteral("改名后的设备"),
                                                    QStringLiteral("192.168.9.9:2121"),
                                                    TaskDeviceState::Succeeded);
        QVERIFY(store.appendDeviceResult(runId, renamed));
        const auto after = fresh.query({});
        QCOMPARE(after.size(), size_t(1));
        QCOMPARE(QString::fromStdString(after.front().devices.front().name),
                 QStringLiteral("产线A控制器"));
        QCOMPARE(QString::fromStdString(after.front().devices.front().address),
                 QStringLiteral("10.0.0.7:21"));
        QCOMPARE(int(after.front().devices.front().state), int(TaskDeviceState::Succeeded));
    }

    // 无效请求：模板 ID 为空/版本非法/设备缺 ID → begin 拒绝
    void beginRejectsInvalidRequest()
    {
        TaskRunStore store;
        TaskExecutionRequest noTemplate = makeRequest(QStringLiteral("tpl-x"), {});
        noTemplate.task.templateId.clear();
        QVERIFY(store.begin(noTemplate).empty());

        TaskExecutionRequest badVersion = makeRequest(QStringLiteral("tpl-x"), {});
        badVersion.task.version = 0;
        QVERIFY(store.begin(badVersion).empty());

        TaskExecutionRequest noDeviceId = makeRequest(QStringLiteral("tpl-x"),
                                                      {makeDevice(QString(),
                                                                  QStringLiteral("无ID"),
                                                                  QStringLiteral("10.0.0.1"), 21)});
        QVERIFY(store.begin(noDeviceId).empty());

        TaskExecutionRequest duplicated = makeRequest(
            QStringLiteral("tpl-x"),
            {makeDevice(QStringLiteral("device-dup"), QStringLiteral("甲"),
                        QStringLiteral("10.0.0.1"), 21),
             makeDevice(QStringLiteral("device-dup"), QStringLiteral("乙"),
                        QStringLiteral("10.0.0.2"), 21)});
        QVERIFY(store.begin(duplicated).empty());

        QVERIFY(store.query({}).empty());
    }

    // 设备结果与步骤 round-trip：类型/状态/尝试次数/错误全部可回读
    void deviceResultRoundTripsSteps()
    {
        TaskRunStore store;
        const TaskRunId runId = store.begin(makeRequest(
            QStringLiteral("tpl-rt"), {makeDevice(QStringLiteral("device-a"),
                                                  QStringLiteral("A机"),
                                                  QStringLiteral("10.0.0.1"), 21)}));
        QVERIFY(!runId.empty());

        TaskDeviceResult result = makeDeviceResult(QStringLiteral("device-a"),
                                                   QStringLiteral("A机"),
                                                   QStringLiteral("10.0.0.1:21"),
                                                   TaskDeviceState::PartiallySucceeded);
        result.steps = {
            makeStep(TaskStepType::DeployFiles, TaskDeviceState::Succeeded, 1),
            makeStep(TaskStepType::RunCommands, TaskDeviceState::Failed, 2,
                     "connection timeout"),
            makeStep(TaskStepType::Reboot, TaskDeviceState::Cancelled, 0),
        };
        QVERIFY(store.appendDeviceResult(runId, result));
        QVERIFY(store.finish(runId, TaskRunStatus::PartiallySucceeded));

        TaskRunStore fresh;
        const auto records = fresh.query({});
        QCOMPARE(records.size(), size_t(1));
        QCOMPARE(int(records.front().status), int(TaskRunStatus::PartiallySucceeded));
        QVERIFY(records.front().finishedAt >= records.front().startedAt);
        const auto& device = records.front().devices.front();
        QCOMPARE(int(device.state), int(TaskDeviceState::PartiallySucceeded));
        QCOMPARE(device.steps.size(), size_t(3));
        QCOMPARE(int(device.steps[0].type), int(TaskStepType::DeployFiles));
        QCOMPARE(int(device.steps[0].state), int(TaskDeviceState::Succeeded));
        QCOMPARE(device.steps[0].attempt, 1);
        QCOMPARE(int(device.steps[1].type), int(TaskStepType::RunCommands));
        QCOMPARE(int(device.steps[1].state), int(TaskDeviceState::Failed));
        QCOMPARE(device.steps[1].attempt, 2);
        QCOMPARE(QString::fromStdString(device.steps[1].error), QStringLiteral("connection timeout"));
        QCOMPARE(int(device.steps[2].type), int(TaskStepType::Reboot));
        QCOMPARE(int(device.steps[2].state), int(TaskDeviceState::Cancelled));

        // 重复 append：步骤整体替换而非累加
        TaskDeviceResult repeated = result;
        repeated.steps.pop_back();
        repeated.state = TaskDeviceState::Failed;
        QVERIFY(store.appendDeviceResult(runId, repeated));
        const auto again = fresh.query({});
        QCOMPARE(again.front().devices.front().steps.size(), size_t(2));
        QCOMPARE(int(again.front().devices.front().state), int(TaskDeviceState::Failed));
    }

    // appendDeviceResult 拒绝未知运行与未注册设备
    void appendDeviceResultGuards()
    {
        TaskRunStore store;
        QVERIFY(!store.appendDeviceResult("run-missing",
                                          makeDeviceResult(QStringLiteral("device-x"),
                                                           QStringLiteral("X"),
                                                           QStringLiteral("1.2.3.4:21"),
                                                           TaskDeviceState::Succeeded)));

        const TaskRunId runId = store.begin(makeRequest(QStringLiteral("tpl-guard"), {}));
        QVERIFY(!runId.empty());
        // 未经 begin 注册的设备不得写入快照结果
        QVERIFY(!store.appendDeviceResult(runId,
                                          makeDeviceResult(QStringLiteral("device-unknown"),
                                                           QStringLiteral("野设备"),
                                                           QStringLiteral("1.2.3.4:21"),
                                                           TaskDeviceState::Succeeded)));
        QVERIFY(!store.appendDeviceResult(std::string(), TaskDeviceResult{}));
    }

    // 流式追加：步骤归属最近一次 appendDeviceResult 的设备
    void stepResultsStreamToLastDevice()
    {
        TaskRunStore store;
        const TaskRunId runId = store.begin(makeRequest(
            QStringLiteral("tpl-stream"),
            {makeDevice(QStringLiteral("device-1"), QStringLiteral("一号机"),
                        QStringLiteral("10.0.0.1"), 21),
             makeDevice(QStringLiteral("device-2"), QStringLiteral("二号机"),
                        QStringLiteral("10.0.0.2"), 22)}));
        QVERIFY(!runId.empty());

        // 无设备指针时流式追加被拒绝
        QVERIFY(!store.appendStepResult(runId,
                                       makeStep(TaskStepType::DeployFiles,
                                                TaskDeviceState::Succeeded, 1)));

        QVERIFY(store.appendDeviceResult(runId,
                                         makeDeviceResult(QStringLiteral("device-1"),
                                                          QStringLiteral("一号机"),
                                                          QStringLiteral("10.0.0.1:21"),
                                                          TaskDeviceState::Deploying)));
        QVERIFY(store.appendStepResult(runId, makeStep(TaskStepType::DeployFiles,
                                                       TaskDeviceState::Succeeded, 1)));
        QVERIFY(store.appendStepResult(runId, makeStep(TaskStepType::RunCommands,
                                                       TaskDeviceState::Succeeded, 2)));

        // 指针切到 device-2 后，步骤只归属 device-2
        QVERIFY(store.appendDeviceResult(runId,
                                         makeDeviceResult(QStringLiteral("device-2"),
                                                          QStringLiteral("二号机"),
                                                          QStringLiteral("10.0.0.2:22"),
                                                          TaskDeviceState::Commanding)));
        QVERIFY(store.appendStepResult(runId, makeStep(TaskStepType::RunCommands,
                                                       TaskDeviceState::Failed, 1,
                                                       "connection refused")));

        const auto records = store.query({});
        QCOMPARE(records.size(), size_t(1));
        const auto& devices = records.front().devices;
        QCOMPARE(devices.size(), size_t(2));
        QCOMPARE(QString::fromStdString(devices[0].deviceId), QStringLiteral("device-1"));
        QCOMPARE(devices[0].steps.size(), size_t(2));
        QCOMPARE(int(devices[0].steps[0].type), int(TaskStepType::DeployFiles));
        QCOMPARE(int(devices[0].steps[1].type), int(TaskStepType::RunCommands));
        QCOMPARE(devices[0].steps[1].attempt, 2);
        QCOMPARE(QString::fromStdString(devices[1].deviceId), QStringLiteral("device-2"));
        QCOMPARE(devices[1].steps.size(), size_t(1));
        QCOMPARE(int(devices[1].steps[0].state), int(TaskDeviceState::Failed));

        // 未知运行直接拒绝
        QVERIFY(!store.appendStepResult("run-missing",
                                        makeStep(TaskStepType::Reboot,
                                                 TaskDeviceState::Succeeded, 1)));
    }

    // finish：运行中状态与未知 runId 被拒绝，收口后状态/时间落定
    void finishGuards()
    {
        TaskRunStore store;
        const TaskRunId runId = store.begin(makeRequest(QStringLiteral("tpl-finish"), {}));
        QVERIFY(!runId.empty());
        QVERIFY(!store.finish(runId, TaskRunStatus::Running));
        QVERIFY(!store.finish("run-missing", TaskRunStatus::Succeeded));
        QVERIFY(store.finish(runId, TaskRunStatus::Cancelled));

        const auto records = store.query({});
        QCOMPARE(records.size(), size_t(1));
        QCOMPARE(int(records.front().status), int(TaskRunStatus::Cancelled));
        QVERIFY(records.front().finishedAt > 0);
    }

    // 历史筛选：按模板 ID / 设备 ID / 运行结果，且支持组合
    void queryFilters()
    {
        TaskRunStore store;
        const TaskRunId run1 = store.begin(makeRequest(
            QStringLiteral("tpl-A"), {makeDevice(QStringLiteral("device-1"),
                                                 QStringLiteral("一号机"),
                                                 QStringLiteral("10.0.0.1"), 21)}));
        const TaskRunId run2 = store.begin(makeRequest(
            QStringLiteral("tpl-B"), {makeDevice(QStringLiteral("device-2"),
                                                 QStringLiteral("二号机"),
                                                 QStringLiteral("10.0.0.2"), 21)}));
        const TaskRunId run3 = store.begin(makeRequest(
            QStringLiteral("tpl-A"), {makeDevice(QStringLiteral("device-3"),
                                                 QStringLiteral("三号机"),
                                                 QStringLiteral("10.0.0.3"), 21)}));
        QVERIFY(!run1.empty() && !run2.empty() && !run3.empty());
        QVERIFY(store.finish(run1, TaskRunStatus::Succeeded));
        QVERIFY(store.finish(run2, TaskRunStatus::Failed));
        QVERIFY(store.finish(run3, TaskRunStatus::Succeeded));

        TaskRunStore fresh;
        QCOMPARE(fresh.query({}).size(), size_t(3));

        TaskRunFilter byTemplate;
        byTemplate.templateId = "tpl-A";
        auto records = fresh.query(byTemplate);
        QCOMPARE(records.size(), size_t(2));
        for (const auto& r : records)
            QCOMPARE(QString::fromStdString(r.templateId), QStringLiteral("tpl-A"));

        TaskRunFilter byDevice;
        byDevice.deviceId = "device-2";
        records = fresh.query(byDevice);
        QCOMPARE(records.size(), size_t(1));
        QCOMPARE(QString::fromStdString(records.front().runId), QString::fromStdString(run2));

        TaskRunFilter byStatus;
        byStatus.status = TaskRunStatus::Succeeded;
        records = fresh.query(byStatus);
        QCOMPARE(records.size(), size_t(2));
        for (const auto& r : records)
            QCOMPARE(int(r.status), int(TaskRunStatus::Succeeded));

        byStatus.status = TaskRunStatus::Running;
        QCOMPARE(fresh.query(byStatus).size(), size_t(0));

        TaskRunFilter combined;
        combined.templateId = "tpl-A";
        combined.status = TaskRunStatus::Succeeded;
        combined.deviceId = "device-3";
        records = fresh.query(combined);
        QCOMPARE(records.size(), size_t(1));
        QCOMPARE(QString::fromStdString(records.front().runId), QString::fromStdString(run3));

        // 无匹配
        TaskRunFilter none;
        none.deviceId = "device-zzz";
        QVERIFY(fresh.query(none).empty());
    }

    // 排序与损坏记录：startedAt 倒序；无法解释的记录跳过且不阻断
    void querySortsAndSkipsCorrupted()
    {
        TaskRunStore store;
        const TaskRunId fresh = store.begin(makeRequest(QStringLiteral("tpl-good"), {}));
        QVERIFY(!fresh.empty());
        QVERIFY(store.finish(fresh, TaskRunStatus::Succeeded));

        auto& cs = ConfigStore::instance();
        // 一条十天前的有效记录（用于排序与 prune）
        const std::time_t now = std::time(nullptr);
        QVERIFY(cs.save(QStringLiteral("task.run"), QStringLiteral("run-old"),
                        rawRunRow(QStringLiteral("run-old"), QStringLiteral("tpl-old"),
                                  QStringLiteral("succeeded"), now - 10 * 86400,
                                  now - 10 * 86400 + 60)));
        // 损坏：未知状态令牌
        QVariantMap badStatus = rawRunRow(QStringLiteral("run-broken"),
                                         QStringLiteral("tpl-broken"),
                                         QStringLiteral("exploded"), now - 3600);
        QVERIFY(cs.save(QStringLiteral("task.run"), QStringLiteral("run-broken"), badStatus));

        const auto records = store.query({});
        QCOMPARE(records.size(), size_t(2));
        QCOMPARE(QString::fromStdString(records[0].runId), QString::fromStdString(fresh));
        QCOMPARE(QString::fromStdString(records[1].runId), QStringLiteral("run-old"));
    }

    // 保留清理：只删超期的终态记录并级联删除设备/步骤行；
    // 运行中记录永不删除；非正数天数拒绝（防误清全部历史）
    void pruneRemovesExpiredFinishedRunsOnly()
    {
        TaskRunStore store;
        const std::time_t now = std::time(nullptr);
        auto& cs = ConfigStore::instance();

        // 新记录（store 正常写入，含设备与步骤）
        const TaskRunId freshId = store.begin(makeRequest(
            QStringLiteral("tpl-fresh"), {makeDevice(QStringLiteral("device-f"),
                                                    QStringLiteral("新设备"),
                                                    QStringLiteral("10.0.0.9"), 21)}));
        QVERIFY(!freshId.empty());
        TaskDeviceResult freshResult = makeDeviceResult(QStringLiteral("device-f"),
                                                        QStringLiteral("新设备"),
                                                        QStringLiteral("10.0.0.9:21"),
                                                        TaskDeviceState::Succeeded);
        freshResult.steps = {makeStep(TaskStepType::DeployFiles,
                                      TaskDeviceState::Succeeded, 1)};
        QVERIFY(store.appendDeviceResult(freshId, freshResult));
        QVERIFY(store.finish(freshId, TaskRunStatus::Succeeded));

        // 十条前的终态旧记录：raw 写入三条记录类型
        QVERIFY(cs.save(QStringLiteral("task.run"), QStringLiteral("run-old"),
                        rawRunRow(QStringLiteral("run-old"), QStringLiteral("tpl-old"),
                                  QStringLiteral("succeeded"), now - 10 * 86400)));
        QVERIFY(cs.save(QStringLiteral("task.run.device"), QStringLiteral("run-old/device-o"),
                        rawDeviceRow(QStringLiteral("run-old"), QStringLiteral("device-o"),
                                     QStringLiteral("旧设备"), QStringLiteral("10.0.0.8:21"), 0)));
        QVERIFY(cs.save(QStringLiteral("task.run.step"), QStringLiteral("run-old/device-o/0"),
                        rawStepRow(QStringLiteral("run-old"), QStringLiteral("device-o"), 0)));

        // 十条前的运行中记录：不得被清理
        QVERIFY(cs.save(QStringLiteral("task.run"), QStringLiteral("run-old-running"),
                        rawRunRow(QStringLiteral("run-old-running"), QStringLiteral("tpl-run"),
                                  QStringLiteral("running"), now - 10 * 86400)));

        // 非法保留天数：拒绝且不动任何东西
        QCOMPARE(store.prune(0), 0);
        QCOMPARE(store.prune(-3), 0);
        QVERIFY(rowExists("task.run", QStringLiteral("run-old")));

        const int removed = store.prune(5);
        QCOMPARE(removed, 1);
        QVERIFY(!rowExists("task.run", QStringLiteral("run-old")));
        QVERIFY(!rowExists("task.run.device", QStringLiteral("run-old/device-o")));
        QVERIFY(!rowExists("task.run.step", QStringLiteral("run-old/device-o/0")));
        QVERIFY(rowExists("task.run", QStringLiteral("run-old-running")));
        QVERIFY(rowExists("task.run", QString::fromStdString(freshId)));

        const auto records = store.query({});
        QCOMPARE(records.size(), size_t(2));
        for (const auto& r : records)
            QVERIFY(QString::fromStdString(r.runId) != QStringLiteral("run-old"));
    }

    // 保留清理的分页排空：积压超过读取窗口时，最老的过期记录也必须被清理，
    // 其子行不得成为永久孤儿（回归 ConfigStore::list 的 LIMIT 窗口盲区）
    void prunePagesPastReadWindow()
    {
        TaskRunStore store;
        const std::time_t now = std::time(nullptr);
        auto& cs = ConfigStore::instance();

        // 12 条超期终态旧记录（每条带设备+步骤子行），注入小窗口 pageSize=5
        // 迫使多页翻页；旧记录先写入，保证在 updated_at 倒序里全部落在首页之外
        const int expiredCount = 12;
        for (int i = 0; i < expiredCount; ++i) {
            const QString runId = QStringLiteral("run-stale-%1").arg(i, 2, 10, QLatin1Char('0'));
            QVERIFY(cs.save(QStringLiteral("task.run"), runId,
                            rawRunRow(runId, QStringLiteral("tpl-stale"),
                                      QStringLiteral("succeeded"), now - 10 * 86400 + i)));
            QVERIFY(cs.save(QStringLiteral("task.run.device"), runId + QStringLiteral("/dev-s"),
                            rawDeviceRow(runId, QStringLiteral("dev-s"),
                                         QStringLiteral("积压设备"), QStringLiteral("10.9.9.9:21"), 0)));
            QVERIFY(cs.save(QStringLiteral("task.run.step"),
                            runId + QStringLiteral("/dev-s/0"),
                            rawStepRow(runId, QStringLiteral("dev-s"), 0)));
        }
        // 窗口之后再写近期终态与超期但运行中的记录（翻页后仍须甄别保留）
        for (int i = 0; i < 2; ++i) {
            const QString runId = QStringLiteral("run-keep-%1").arg(i);
            QVERIFY(cs.save(QStringLiteral("task.run"), runId,
                            rawRunRow(runId, QStringLiteral("tpl-keep"),
                                      QStringLiteral("succeeded"), now)));
        }
        for (int i = 0; i < 2; ++i) {
            const QString runId = QStringLiteral("run-running-%1").arg(i);
            QVERIFY(cs.save(QStringLiteral("task.run"), runId,
                            rawRunRow(runId, QStringLiteral("tpl-live"),
                                      QStringLiteral("running"), now - 10 * 86400)));
        }

        // 非法分页窗口同样拒绝
        QCOMPARE(store.prune(5, 0), 0);
        QCOMPARE(store.prune(5, -4), 0);

        const int removed = store.prune(5, 5);
        QCOMPARE(removed, expiredCount);
        for (int i = 0; i < expiredCount; ++i) {
            const QString runId = QStringLiteral("run-stale-%1").arg(i, 2, 10, QLatin1Char('0'));
            QVERIFY2(!rowExists("task.run", runId), qPrintable(runId));
            QVERIFY2(!rowExists("task.run.device", runId + QStringLiteral("/dev-s")),
                     qPrintable(runId));
            QVERIFY2(!rowExists("task.run.step", runId + QStringLiteral("/dev-s/0")),
                     qPrintable(runId));
        }
        for (int i = 0; i < 2; ++i) {
            QVERIFY(rowExists("task.run", QStringLiteral("run-keep-%1").arg(i)));
            QVERIFY(rowExists("task.run", QStringLiteral("run-running-%1").arg(i)));
        }

        // 收敛性：再跑一次没有新的过期记录可删
        QCOMPARE(store.prune(5, 5), 0);
        const auto records = store.query({});
        QCOMPARE(records.size(), size_t(4));
    }

    // 脱敏：步骤错误中的凭据明文（password=/token=/URL userinfo）不落库
    void stepErrorsAreSanitizedOnPersist()
    {
        TaskRunStore store;
        const TaskRunId runId = store.begin(makeRequest(
            QStringLiteral("tpl-sanitize"), {makeDevice(QStringLiteral("device-s"),
                                                        QStringLiteral("S机"),
                                                        QStringLiteral("10.0.0.5"), 21)}));
        QVERIFY(!runId.empty());
        QVERIFY(store.appendDeviceResult(runId,
                                         makeDeviceResult(QStringLiteral("device-s"),
                                                          QStringLiteral("S机"),
                                                          QStringLiteral("10.0.0.5:21"),
                                                          TaskDeviceState::Deploying)));

        // 下划线前缀凭据键（db_password / session_token / client_secret）必须同样脱敏：
        // 协议/工具错误文本常回显配置文件解析错误，形如 db_password=… 的键极常见
        const std::string dirty =
            "open failed ftp://deploy:hunter2@10.0.0.5:21 refused; password=abc123; "
            "TOKEN=deadbeef; private_key: 0xAAAA; "
            "config parse error: db_password=hunter2; session_token=ses_s3cr3t; "
            "client_secret=cli_s3cr3t";
        QVERIFY(store.appendStepResult(runId, makeStep(TaskStepType::DeployFiles,
                                                       TaskDeviceState::Failed, 1, dirty)));

        const auto records = store.query({});
        QCOMPARE(records.size(), size_t(1));
        QCOMPARE(records.front().devices.size(), size_t(1));
        QCOMPARE(records.front().devices.front().steps.size(), size_t(1));
        const std::string stored = records.front().devices.front().steps.front().error;
        const QString storedQt = QString::fromStdString(stored);
        QVERIFY2(storedQt.contains(QStringLiteral("open failed")),
                 qPrintable(storedQt));
        QVERIFY2(storedQt.contains(QStringLiteral("***")), qPrintable(storedQt));
        for (const QString& secret : {QStringLiteral("hunter2"), QStringLiteral("abc123"),
                                      QStringLiteral("deadbeef"), QStringLiteral("0xAAAA"),
                                      QStringLiteral("ses_s3cr3t"), QStringLiteral("cli_s3cr3t")}) {
            QVERIFY2(!storedQt.contains(secret),
                     qPrintable(QStringLiteral("脱敏失败，残留明文: %1").arg(secret)));
        }

        // 原始库行同样不得含凭据明文（写入即脱敏，而非仅读取时隐藏）
        const QList<QVariantMap> stepRows =
            ConfigStore::instance().list(QStringLiteral("task.run.step"), 100);
        QCOMPARE(stepRows.size(), 1);
        const QString rawJson = QString::fromUtf8(QJsonDocument(
            QJsonObject::fromVariantMap(stepRows.first())).toJson(QJsonDocument::Compact));
        QVERIFY2(!rawJson.contains(QStringLiteral("hunter2")), qPrintable(rawJson));
        QVERIFY2(!rawJson.contains(QStringLiteral("abc123")), qPrintable(rawJson));
        QVERIFY2(!rawJson.contains(QStringLiteral("ses_s3cr3t")), qPrintable(rawJson));
        QVERIFY2(!rawJson.contains(QStringLiteral("cli_s3cr3t")), qPrintable(rawJson));

        // 超长错误按字节上限截断并带标记；sanitize 自由函数可直接验证
        const QString huge(5000, QLatin1Char('x'));
        const std::string trimmed = sanitizeTaskErrorText(huge.toStdString());
        QVERIFY(trimmed.size() < 1024);
        QVERIFY(QString::fromStdString(trimmed).endsWith(QStringLiteral("(已截断)")));
        // 干净文本原样保留
        QCOMPARE(QString::fromStdString(sanitizeTaskErrorText("connection timeout")),
                 QStringLiteral("connection timeout"));
        // 直测自由函数：下划线前缀凭据键的 "键=值" 必须掩值留键，而非整键视为非匹配放行
        const QString prefixedQt = QString::fromStdString(sanitizeTaskErrorText(
            "db_password=hunter2; session_token=ses_s3cr3t; client_secret=cli_s3cr3t"));
        QVERIFY2(prefixedQt.contains(QStringLiteral("db_password=***")), qPrintable(prefixedQt));
        QVERIFY2(prefixedQt.contains(QStringLiteral("session_token=***")), qPrintable(prefixedQt));
        QVERIFY2(prefixedQt.contains(QStringLiteral("client_secret=***")), qPrintable(prefixedQt));
    }

    // 报告数据：renderTaskRunCsv/Html 行含设备快照名称+地址与逐步结果
    void reportRowsCarryNameAddressAndOutcomes()
    {
        TaskRunStore store;
        const TaskRunId runId = store.begin(makeRequest(
            QStringLiteral("tpl-report"),
            {makeDevice(QStringLiteral("device-r1"), QStringLiteral("产线A控制器"),
                        QStringLiteral("10.0.0.7"), 21),
             makeDevice(QStringLiteral("device-r2"), QStringLiteral("焊接机械臂"),
                        QStringLiteral("10.0.0.8"), 22)}));
        QVERIFY(!runId.empty());
        TaskDeviceResult ok = makeDeviceResult(QStringLiteral("device-r1"),
                                               QStringLiteral("产线A控制器"),
                                               QStringLiteral("10.0.0.7:21"),
                                               TaskDeviceState::Succeeded);
        ok.steps = {makeStep(TaskStepType::DeployFiles, TaskDeviceState::Succeeded, 1),
                    makeStep(TaskStepType::RunCommands, TaskDeviceState::Succeeded, 2)};
        QVERIFY(store.appendDeviceResult(runId, ok));
        TaskDeviceResult partial = makeDeviceResult(QStringLiteral("device-r2"),
                                                    QStringLiteral("焊接机械臂"),
                                                    QStringLiteral("10.0.0.8:22"),
                                                    TaskDeviceState::PartiallySucceeded);
        partial.steps = {makeStep(TaskStepType::DeployFiles, TaskDeviceState::Succeeded, 1),
                         makeStep(TaskStepType::RunCommands, TaskDeviceState::Failed, 3,
                                  "connection timeout")};
        QVERIFY(store.appendDeviceResult(runId, partial));
        QVERIFY(store.finish(runId, TaskRunStatus::PartiallySucceeded));

        const auto records = store.query({});
        QCOMPARE(records.size(), size_t(1));

        const std::string csv = renderTaskRunCsv(records.front());
        const QString csvQt = QString::fromStdString(csv);
        QVERIFY2(csvQt.startsWith(QStringLiteral("name,address,device,result,steps,error")),
                 qPrintable(csvQt));
        QVERIFY(csvQt.contains(QStringLiteral("产线A控制器")));
        QVERIFY(csvQt.contains(QStringLiteral("10.0.0.7:21")));
        QVERIFY(csvQt.contains(QStringLiteral("焊接机械臂")));
        QVERIFY(csvQt.contains(QStringLiteral("10.0.0.8:22")));
        QVERIFY(csvQt.contains(QStringLiteral("succeeded")));
        QVERIFY(csvQt.contains(QStringLiteral("partially_succeeded")));
        QVERIFY(csvQt.contains(QStringLiteral("deploy_files:succeeded")));
        QVERIFY(csvQt.contains(QStringLiteral("run_commands:failed")));
        QVERIFY(csvQt.contains(QStringLiteral("connection timeout")));

        const std::string html = renderTaskRunHtml(records.front());
        const QString htmlQt = QString::fromStdString(html);
        QVERIFY(htmlQt.contains(QStringLiteral("<!DOCTYPE html>")));
        QVERIFY(htmlQt.contains(QStringLiteral("产线A控制器")));
        QVERIFY(htmlQt.contains(QStringLiteral("10.0.0.8:22")));
        QVERIFY(htmlQt.contains(QStringLiteral("class=\"partially_succeeded\"")));
        QVERIFY(htmlQt.contains(QStringLiteral("tpl-report")));
        // 转义契约与既有报告一致：< > & 不得裸出
        QVERIFY(!htmlQt.contains(QStringLiteral("<script")));

        // 既有部署报告 API 原样可用（附加式扩展不改变旧行为）
        DeployReport legacy;
        DeviceResult legacyRow;
        legacyRow.deviceKey = "10.0.0.1:21";
        legacyRow.state = DeviceResult::Ok;
        legacy.results.push_back(legacyRow);
        const QString legacyCsv = QString::fromStdString(renderReportCsv(legacy));
        QVERIFY(legacyCsv.startsWith(
            QStringLiteral("device,result,failed_files,last_error,duration_ms,started_at,non_atomic")));
    }

    // 令牌编解码：所有状态/类型令牌 round-trip
    void statusAndStateTokensRoundTrip()
    {
        const TaskRunStatus statuses[] = {
            TaskRunStatus::Running, TaskRunStatus::Succeeded, TaskRunStatus::Failed,
            TaskRunStatus::Cancelled, TaskRunStatus::PartiallySucceeded};
        for (const auto status : statuses) {
            const std::string token = taskRunStatusToken(status);
            QVERIFY(!token.empty());
            const auto parsed = taskRunStatusFromToken(token);
            QVERIFY(parsed.has_value());
            QCOMPARE(int(*parsed), int(status));
        }
        QVERIFY(!taskRunStatusFromToken("exploded").has_value());

        const TaskDeviceState states[] = {
            TaskDeviceState::Pending, TaskDeviceState::Validating, TaskDeviceState::Deploying,
            TaskDeviceState::Commanding, TaskDeviceState::Rebooting, TaskDeviceState::Recovering,
            TaskDeviceState::Succeeded, TaskDeviceState::Failed, TaskDeviceState::Cancelled,
            TaskDeviceState::Skipped, TaskDeviceState::PartiallySucceeded};
        for (const auto state : states) {
            const std::string token = taskDeviceStateToken(state);
            QVERIFY(!token.empty());
            const auto parsed = taskDeviceStateFromToken(token);
            QVERIFY(parsed.has_value());
            QCOMPARE(int(*parsed), int(state));
        }
        QVERIFY(!taskDeviceStateFromToken("wtf").has_value());

        const TaskStepType types[] = {TaskStepType::DeployFiles, TaskStepType::RunCommands,
                                      TaskStepType::Reboot, TaskStepType::Recover};
        for (const auto type : types) {
            const auto parsed = taskStepTypeFromToken(taskStepTypeToken(type));
            QVERIFY(parsed.has_value());
            QCOMPARE(int(*parsed), int(type));
        }
        QVERIFY(!taskStepTypeFromToken("teleport").has_value());
    }
};

QTEST_MAIN(TstTaskRunStore)
#include "tst_task_run_store.moc"
