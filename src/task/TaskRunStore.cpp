// v2.11 Task 5：TaskRunStore 实现。
// 序列化到 ConfigStore 记录类型 task.run / task.run.device / task.run.step
// （模式对齐 task.template 与 device.profile）：
//   task.run        key=runId
//     {runId, templateId, templateVersion, status, startedAt, finishedAt,
//      operator, softwareVersion, lastDeviceId}
//   task.run.device key=runId + "/" + deviceId
//     {runId, deviceId, name, address, state, sequence}
//   task.run.step   key=runId + "/" + deviceId + "/" + stepIndex
//     {runId, deviceId, stepIndex, stepType, state, attempt, error}
//     （步骤类型字段名为 stepType——ConfigStore::list() 会剥掉顶层 "type" 保留键）
//
// 不变式：
//   · 设备快照不可变——begin 登记名称/地址后，appendDeviceResult 只更新状态与
//     步骤，调用方传入的名称/地址差异仅告警、不覆盖快照；
//   · 凭据与命令原文绝不落库——步骤错误写入前一律 sanitizeTaskErrorText 脱敏；
//   · 运行中的记录永不被 prune 删除（状态无法解析的记录保守保留）。
#include "task/TaskRunStore.h"

#include "config/ConfigStore.h"

#include <QCoreApplication>
#include <QDebug>
#include <QLatin1Char>
#include <QLatin1String>
#include <QStringList>
#include <QUuid>
#include <QVariantList>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <map>
#include <set>
#include <utility>

namespace {

constexpr QLatin1String kRunType("task.run");
constexpr QLatin1String kDeviceType("task.run.device");
constexpr QLatin1String kStepType("task.run.step");

// 记录字段（与 tst_task_run_store 的 raw 行 schema 对齐）
constexpr QLatin1String kFieldRunId("runId");
constexpr QLatin1String kFieldTemplateId("templateId");
constexpr QLatin1String kFieldTemplateVersion("templateVersion");
constexpr QLatin1String kFieldStatus("status");
constexpr QLatin1String kFieldStartedAt("startedAt");
constexpr QLatin1String kFieldFinishedAt("finishedAt");
constexpr QLatin1String kFieldOperator("operator");
constexpr QLatin1String kFieldSoftwareVersion("softwareVersion");
constexpr QLatin1String kFieldLastDeviceId("lastDeviceId");
constexpr QLatin1String kFieldDeviceId("deviceId");
constexpr QLatin1String kFieldName("name");
constexpr QLatin1String kFieldAddress("address");
constexpr QLatin1String kFieldState("state");
constexpr QLatin1String kFieldSequence("sequence");
constexpr QLatin1String kFieldStepIndex("stepIndex");
// 注意：不能叫 "type"——ConfigStore::list() 把行内 "type"/"key"/"updated_at"
// 视为保留键并剥除，会导致步骤类型无法回读
constexpr QLatin1String kFieldType("stepType");
constexpr QLatin1String kFieldAttempt("attempt");
constexpr QLatin1String kFieldError("error");
// ConfigStore::list() 行注入的保留键
constexpr QLatin1String kRowKey("key");
constexpr QLatin1String kRowUpdatedAt("updated_at");

// 有界读取：单次查询最多加载的运行/子记录行数（防止历史无限增长拖垮 UI）
constexpr int kRunListLimit = 1000;
constexpr int kChildListLimit = 20000;

// 步骤错误文本字节上限（超出按 UTF-8 边界截断）
constexpr std::size_t kMaxErrorBytes = 512;

QString qstr(const std::string& value)
{
    return QString::fromStdString(value);
}

QString deviceRowKey(const std::string& runId, const std::string& deviceId)
{
    return qstr(runId) + QLatin1Char('/') + qstr(deviceId);
}

QString stepRowKey(const std::string& runId, const std::string& deviceId, int stepIndex)
{
    return deviceRowKey(runId, deviceId) + QLatin1Char('/') + QString::number(stepIndex);
}

std::time_t nowSeconds()
{
    return std::time(nullptr);
}

std::string currentOperator()
{
    QString name = qEnvironmentVariable("USERNAME");
    if (name.isEmpty()) name = qEnvironmentVariable("USER");
    if (name.isEmpty()) name = QStringLiteral("unknown");
    return name.toStdString();
}

std::string currentSoftwareVersion()
{
    const QString version = QCoreApplication::applicationVersion();
    return version.isEmpty() ? std::string("unknown") : version.toStdString();
}

// 设备展示地址快照：首个端点 "ip:port"（与 TaskDeviceOutcome 约定一致）
std::string deviceAddressSnapshot(const DeviceProfile& profile)
{
    if (profile.endpoints.empty()) return {};
    const DeviceEndpoint& endpoint = profile.endpoints.front();
    return endpoint.ip + ":" + std::to_string(endpoint.port);
}

// 写入一台设备的一个步骤行（错误文本落库前脱敏）。失败输出中文告警。
bool writeStepRow(ConfigStore& store, const std::string& runId,
                  const std::string& deviceId, int stepIndex, const TaskStepResult& step)
{
    const std::string typeToken = taskStepTypeToken(step.type);
    const std::string stateToken = taskDeviceStateToken(step.state);
    if (typeToken.empty() || stateToken.empty()) {
        qWarning() << "TaskRunStore: 拒绝写入未知步骤类型/状态令牌 run=" << qstr(runId)
                   << "device=" << qstr(deviceId);
        return false;
    }
    QVariantMap row;
    row.insert(QString(kFieldRunId), qstr(runId));
    row.insert(QString(kFieldDeviceId), qstr(deviceId));
    row.insert(QString(kFieldStepIndex), stepIndex);
    row.insert(QString(kFieldType), qstr(typeToken));
    row.insert(QString(kFieldState), qstr(stateToken));
    row.insert(QString(kFieldAttempt), step.attempt);
    row.insert(QString(kFieldError), qstr(sanitizeTaskErrorText(step.error)));
    if (!store.save(kStepType, stepRowKey(runId, deviceId, stepIndex), row)) {
        qWarning() << "TaskRunStore: 步骤行写入失败 run=" << qstr(runId)
                   << "device=" << qstr(deviceId) << "index=" << stepIndex;
        return false;
    }
    return true;
}

// —— 错误文本脱敏（sanitizeTaskErrorText 的实现细节） ——

// 凭据类键名（小写匹配；前后非字母数字下划线才算独立单词）
constexpr const char* kCredentialKeys[] = {
    "password", "passwd", "pwd", "pass", "secret", "token",
    "privkey", "private_key", "key_material", "keymaterial",
    "apikey", "api_key", "authtoken", "access_token", "sessiontoken"};

std::string asciiLower(const std::string& value)
{
    std::string out = value;
    for (char& ch : out) {
        if (std::isupper(static_cast<unsigned char>(ch)) != 0)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

std::size_t skipSpaces(const std::string& text, std::size_t pos)
{
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) ++pos;
    return pos;
}

// 掩蔽 valueStart 起的凭据值：引号包裹值掩内部，裸值掩到空白/;/,；
// 值已为 "***" 或为空则不动（返回 false，调用方继续前移避免死循环）。
bool maskCredentialValue(std::string& text, std::size_t valueStart)
{
    if (valueStart >= text.size()) return false;
    const char quote = text[valueStart];
    if (quote == '"' || quote == '\'') {
        const auto end = text.find(quote, valueStart + 1);
        const std::size_t stop = end == std::string::npos ? text.size() : end;
        if (stop <= valueStart + 1) return false; // 空值无需掩蔽
        text.replace(valueStart + 1, stop - valueStart - 1, "***");
        return true;
    }
    std::size_t end = valueStart;
    while (end < text.size() && std::isspace(static_cast<unsigned char>(text[end])) == 0
           && text[end] != ',' && text[end] != ';') {
        ++end;
    }
    if (end <= valueStart) return false;
    if (text.compare(valueStart, end - valueStart, "***") == 0) return false; // 已脱敏
    text.replace(valueStart, end - valueStart, "***");
    return true;
}

} // namespace

std::string sanitizeTaskErrorText(const std::string& text)
{
    if (text.empty()) return {};
    std::string out = text;

    // 1) URL userinfo：scheme://user:pass@host → scheme://***@host
    for (std::size_t pos = 0; pos <= out.size();) {
        const auto scheme = out.find("://", pos);
        if (scheme == std::string::npos) break;
        const std::size_t start = scheme + 3;
        std::size_t at = std::string::npos;
        for (std::size_t j = start; j < out.size(); ++j) {
            if (std::isspace(static_cast<unsigned char>(out[j])) != 0) break;
            if (out[j] == '@') {
                at = j;
                break;
            }
        }
        if (at == std::string::npos) {
            pos = start;
            continue;
        }
        out.replace(start, at - start, "***");
        // 掩蔽后字符串缩短，游标必须回到掩蔽结果之后（不能用旧下标）
        pos = start + 3;
    }

    // 2) 凭据类 "键 = 值" / "键: 值" 掩蔽；每轮重算小写副本，游标单调前移保证收敛
    for (const char* key : kCredentialKeys) {
        const std::size_t keyLen = std::strlen(key);
        std::size_t searchFrom = 0;
        for (;;) {
            const std::string lower = asciiLower(out);
            const auto p = lower.find(key, searchFrom);
            if (p == std::string::npos) break;
            // 前导边界只排除字母数字：下划线前缀键（db_password、session_token、
            // client_secret 等配置文件解析错误常见形态）同样属于凭据，必须掩蔽；
            // 键表已含 access_token/private_key 等复合键，放开 '_' 不会漏掉枚举项
            if (p > 0 && std::isalnum(static_cast<unsigned char>(lower[p - 1])) != 0) {
                searchFrom = p + keyLen;
                continue;
            }
            const std::size_t sep = skipSpaces(out, p + keyLen);
            const bool separator = sep < out.size() && (out[sep] == '=' || out[sep] == ':');
            // "token://…" 之类协议冒号不是凭据赋值，跳过
            const bool protocolColon = separator && out[sep] == ':'
                && sep + 1 < out.size() && out[sep + 1] == '/' && out.compare(sep, 3, "://") == 0;
            if (!separator || protocolColon) {
                searchFrom = p + keyLen;
                continue;
            }
            const std::size_t valueStart = skipSpaces(out, sep + 1);
            searchFrom = maskCredentialValue(out, valueStart) ? p + keyLen : p + 1;
        }
    }

    // 3) 超长按字节上限截断（不切开 UTF-8 多字节序列）
    if (out.size() > kMaxErrorBytes) {
        std::size_t cut = kMaxErrorBytes;
        while (cut > 0
               && (static_cast<unsigned char>(out[cut]) & 0xC0U) == 0x80U) {
            --cut;
        }
        out = out.substr(0, cut) + "(已截断)";
    }
    return out;
}

TaskRunId TaskRunStore::begin(const TaskExecutionRequest& request)
{
    if (request.task.templateId.empty()) {
        qWarning() << "TaskRunStore: begin 拒绝——模板 ID 为空，执行记录必须引用稳定模板";
        return {};
    }
    if (request.task.version < 1) {
        qWarning() << "TaskRunStore: begin 拒绝——模板版本非法" << request.task.version;
        return {};
    }
    std::set<std::string> seenDeviceIds;
    for (const auto& device : request.devices) {
        if (device.deviceId.empty()) {
            qWarning() << "TaskRunStore: begin 拒绝——设备档案缺少稳定 deviceId";
            return {};
        }
        if (!seenDeviceIds.insert(device.deviceId).second) {
            qWarning() << "TaskRunStore: begin 拒绝——设备重复登记"
                       << qstr(device.deviceId);
            return {};
        }
    }

    const TaskRunId runId = "run-" + QUuid::createUuid().toString(QUuid::Id128).toStdString();
    auto& store = ConfigStore::instance();
    const std::time_t startedAt = nowSeconds();

    QVariantMap runRow;
    runRow.insert(QString(kFieldRunId), qstr(runId));
    runRow.insert(QString(kFieldTemplateId), qstr(request.task.templateId));
    runRow.insert(QString(kFieldTemplateVersion), request.task.version);
    runRow.insert(QString(kFieldStatus), qstr(taskRunStatusToken(TaskRunStatus::Running)));
    runRow.insert(QString(kFieldStartedAt), qint64(startedAt));
    runRow.insert(QString(kFieldFinishedAt), qint64(0));
    runRow.insert(QString(kFieldOperator), qstr(currentOperator()));
    runRow.insert(QString(kFieldSoftwareVersion), qstr(currentSoftwareVersion()));
    runRow.insert(QString(kFieldLastDeviceId), QString());
    if (!store.save(kRunType, qstr(runId), runRow)) {
        qWarning() << "TaskRunStore: begin 写入运行行失败 run=" << qstr(runId);
        return {};
    }

    // 设备快照登记（不可变）：名称/地址在 begin 定格
    QStringList writtenDeviceKeys;
    int sequence = 0;
    for (const auto& device : request.devices) {
        QVariantMap row;
        row.insert(QString(kFieldRunId), qstr(runId));
        row.insert(QString(kFieldDeviceId), qstr(device.deviceId));
        row.insert(QString(kFieldName), qstr(device.name));
        row.insert(QString(kFieldAddress), qstr(deviceAddressSnapshot(device)));
        row.insert(QString(kFieldState),
                   qstr(taskDeviceStateToken(TaskDeviceState::Pending)));
        row.insert(QString(kFieldSequence), sequence);
        const QString key = deviceRowKey(runId, device.deviceId);
        if (!store.save(kDeviceType, key, row)) {
            qWarning() << "TaskRunStore: begin 写入设备快照失败 run=" << qstr(runId)
                       << "device=" << qstr(device.deviceId);
            for (const QString& written : writtenDeviceKeys)
                store.remove(kDeviceType, written);
            store.remove(kRunType, qstr(runId));
            return {};
        }
        writtenDeviceKeys << key;
        ++sequence;
    }
    return runId;
}

bool TaskRunStore::appendDeviceResult(const TaskRunId& runId, const TaskDeviceResult& result)
{
    if (runId.empty() || result.deviceId.empty()) {
        qWarning() << "TaskRunStore: appendDeviceResult 拒绝——runId 或 deviceId 为空";
        return false;
    }
    auto& store = ConfigStore::instance();
    const QString runKey = qstr(runId);
    QVariantMap runRow = store.load(kRunType, runKey);
    if (runRow.isEmpty()) {
        qWarning() << "TaskRunStore: appendDeviceResult 拒绝——运行记录不存在" << runKey;
        return false;
    }
    const QString key = deviceRowKey(runId, result.deviceId);
    const QVariantMap stored = store.load(kDeviceType, key);
    if (stored.isEmpty()) {
        qWarning() << "TaskRunStore: appendDeviceResult 拒绝——设备未经 begin 登记，"
                      "设备快照不可追加新成员"
                   << runKey << qstr(result.deviceId);
        return false;
    }

    const std::string stateToken = taskDeviceStateToken(result.state);
    if (stateToken.empty()) {
        qWarning() << "TaskRunStore: appendDeviceResult 拒绝——未知设备状态" << runKey;
        return false;
    }

    // 快照不可变：begin 定格的名称/地址为准，调用方差异仅提示不覆盖
    const QString snapshotName = stored.value(QString(kFieldName)).toString();
    const QString snapshotAddress = stored.value(QString(kFieldAddress)).toString();
    if (!result.name.empty() && qstr(result.name) != snapshotName) {
        qInfo() << "TaskRunStore: 设备名称与执行期快照不一致，保留快照" << snapshotName
                << "收到" << qstr(result.name);
    }
    if (!result.address.empty() && qstr(result.address) != snapshotAddress) {
        qInfo() << "TaskRunStore: 设备地址与执行期快照不一致，保留快照" << snapshotAddress
                << "收到" << qstr(result.address);
    }

    QVariantMap row;
    row.insert(QString(kFieldRunId), runKey);
    row.insert(QString(kFieldDeviceId), qstr(result.deviceId));
    row.insert(QString(kFieldName), snapshotName);
    row.insert(QString(kFieldAddress), snapshotAddress);
    row.insert(QString(kFieldState), qstr(stateToken));
    row.insert(QString(kFieldSequence), stored.value(QString(kFieldSequence)).toInt());
    if (!store.save(kDeviceType, key, row)) {
        qWarning() << "TaskRunStore: 设备结果写入失败" << runKey << qstr(result.deviceId);
        return false;
    }

    // 步骤整体覆盖该设备既有行（重复 append 幂等，不累加）
    const QList<QVariantMap> existingSteps = store.list(kStepType, kChildListLimit);
    for (const QVariantMap& stepRow : existingSteps) {
        if (stepRow.value(QString(kFieldRunId)).toString() == runKey
            && stepRow.value(QString(kFieldDeviceId)).toString()
                   == qstr(result.deviceId)) {
            store.remove(kStepType, stepRow.value(QString(kRowKey)).toString());
        }
    }
    for (std::size_t i = 0; i < result.steps.size(); ++i) {
        if (!writeStepRow(store, runId, result.deviceId, static_cast<int>(i), result.steps[i]))
            return false;
    }

    // 更新本运行"最近写入设备"指针（appendStepResult 的归属目标）
    runRow.insert(QString(kFieldLastDeviceId), qstr(result.deviceId));
    if (!store.save(kRunType, runKey, runRow)) {
        qWarning() << "TaskRunStore: 运行行设备指针更新失败" << runKey;
        return false;
    }
    return true;
}

bool TaskRunStore::appendStepResult(const TaskRunId& runId, const TaskStepResult& step)
{
    if (runId.empty()) {
        qWarning() << "TaskRunStore: appendStepResult 拒绝——runId 为空";
        return false;
    }
    auto& store = ConfigStore::instance();
    QVariantMap runRow = store.load(kRunType, qstr(runId));
    if (runRow.isEmpty()) {
        qWarning() << "TaskRunStore: appendStepResult 拒绝——运行记录不存在"
                   << qstr(runId);
        return false;
    }
    const QString deviceId = runRow.value(QString(kFieldLastDeviceId)).toString();
    if (deviceId.isEmpty()) {
        qWarning() << "TaskRunStore: appendStepResult 拒绝——运行尚无设备指针，"
                      "请先对该设备调用 appendDeviceResult"
                   << qstr(runId);
        return false;
    }

    // 下一个步骤下标 = 该设备既有步骤行数（行键连续）
    int nextIndex = 0;
    const QList<QVariantMap> stepRows = store.list(kStepType, kChildListLimit);
    for (const QVariantMap& row : stepRows) {
        if (row.value(QString(kFieldRunId)).toString() == qstr(runId)
            && row.value(QString(kFieldDeviceId)).toString() == deviceId) {
            ++nextIndex;
        }
    }
    return writeStepRow(store, runId, deviceId.toStdString(), nextIndex, step);
}

bool TaskRunStore::finish(const TaskRunId& runId, TaskRunStatus status)
{
    if (runId.empty()) {
        qWarning() << "TaskRunStore: finish 拒绝——runId 为空";
        return false;
    }
    const std::string statusToken = taskRunStatusToken(status);
    if (statusToken.empty() || status == TaskRunStatus::Running) {
        qWarning() << "TaskRunStore: finish 拒绝——只接受终态收口（Running 不是终态）"
                   << qstr(runId);
        return false;
    }
    auto& store = ConfigStore::instance();
    const QString runKey = qstr(runId);
    QVariantMap runRow = store.load(kRunType, runKey);
    if (runRow.isEmpty()) {
        qWarning() << "TaskRunStore: finish 拒绝——运行记录不存在" << runKey;
        return false;
    }
    runRow.insert(QString(kFieldStatus), qstr(statusToken));
    runRow.insert(QString(kFieldFinishedAt), qint64(nowSeconds()));
    if (!store.save(kRunType, runKey, runRow)) {
        qWarning() << "TaskRunStore: finish 写入失败" << runKey;
        return false;
    }
    return true;
}

std::vector<TaskRunRecord> TaskRunStore::query(const TaskRunFilter& filter) const
{
    auto& store = ConfigStore::instance();
    const QList<QVariantMap> runRows = store.list(kRunType, kRunListLimit);
    const QList<QVariantMap> deviceRows = store.list(kDeviceType, kChildListLimit);
    const QList<QVariantMap> stepRows = store.list(kStepType, kChildListLimit);

    struct IndexedStep {
        int index = 0;
        TaskStepResult step;
    };

    // 步骤行按 runId/deviceId 分组
    std::map<QString, std::vector<IndexedStep>> stepsByDevice;
    for (const QVariantMap& row : stepRows) {
        const QString runKey = row.value(QString(kFieldRunId)).toString();
        const QString deviceId = row.value(QString(kFieldDeviceId)).toString();
        const std::optional<TaskStepType> type = taskStepTypeFromToken(
            row.value(QString(kFieldType)).toString().toStdString());
        const std::optional<TaskDeviceState> state = taskDeviceStateFromToken(
            row.value(QString(kFieldState)).toString().toStdString());
        if (runKey.isEmpty() || deviceId.isEmpty() || !type || !state) {
            qWarning() << "TaskRunStore: query 跳过损坏步骤行"
                       << row.value(QString(kRowKey)).toString();
            continue;
        }
        TaskStepResult step;
        step.type = *type;
        step.state = *state;
        step.attempt = row.value(QString(kFieldAttempt)).toInt();
        step.error = row.value(QString(kFieldError)).toString().toStdString();
        stepsByDevice[runKey + QLatin1Char('/') + deviceId].push_back(
            {row.value(QString(kFieldStepIndex)).toInt(), step});
    }

    // 设备行按 runId 分组（携带步骤，按下标排序）
    struct StoredDevice {
        int sequence = 0;
        TaskDeviceResult result;
    };
    std::map<QString, std::vector<StoredDevice>> devicesByRun;
    for (const QVariantMap& row : deviceRows) {
        const QString runKey = row.value(QString(kFieldRunId)).toString();
        const QString deviceId = row.value(QString(kFieldDeviceId)).toString();
        const std::optional<TaskDeviceState> state = taskDeviceStateFromToken(
            row.value(QString(kFieldState)).toString().toStdString());
        if (runKey.isEmpty() || deviceId.isEmpty() || !state) {
            qWarning() << "TaskRunStore: query 跳过损坏设备行"
                       << row.value(QString(kRowKey)).toString();
            continue;
        }
        TaskDeviceResult result;
        result.deviceId = deviceId.toStdString();
        result.name = row.value(QString(kFieldName)).toString().toStdString();
        result.address = row.value(QString(kFieldAddress)).toString().toStdString();
        result.state = *state;
        auto& indexed = stepsByDevice[runKey + QLatin1Char('/') + deviceId];
        std::sort(indexed.begin(), indexed.end(),
                  [](const IndexedStep& a, const IndexedStep& b) { return a.index < b.index; });
        for (const IndexedStep& item : indexed) result.steps.push_back(item.step);
        devicesByRun[runKey].push_back(
            {row.value(QString(kFieldSequence)).toInt(), std::move(result)});
    }

    std::vector<TaskRunRecord> records;
    for (const QVariantMap& row : runRows) {
        QString runKey = row.value(QString(kFieldRunId)).toString();
        if (runKey.isEmpty()) runKey = row.value(QString(kRowKey)).toString();
        const std::optional<TaskRunStatus> status = taskRunStatusFromToken(
            row.value(QString(kFieldStatus)).toString().toStdString());
        if (runKey.isEmpty() || !status) {
            qWarning() << "TaskRunStore: query 跳过损坏运行行"
                       << row.value(QString(kRowKey)).toString();
            continue;
        }

        TaskRunRecord record;
        record.runId = runKey.toStdString();
        record.templateId = row.value(QString(kFieldTemplateId)).toString().toStdString();
        record.templateVersion = row.contains(QString(kFieldTemplateVersion))
                                     ? row.value(QString(kFieldTemplateVersion)).toInt()
                                     : 1;
        record.status = *status;
        record.startedAt = static_cast<std::time_t>(
            row.value(QString(kFieldStartedAt)).toLongLong());
        record.finishedAt = static_cast<std::time_t>(
            row.value(QString(kFieldFinishedAt)).toLongLong());
        record.operatorName = row.value(QString(kFieldOperator)).toString().toStdString();
        record.softwareVersion = row.value(QString(kFieldSoftwareVersion))
                                    .toString().toStdString();

        auto& stored = devicesByRun[runKey];
        std::sort(stored.begin(), stored.end(),
                  [](const StoredDevice& a, const StoredDevice& b) {
                      return a.sequence < b.sequence;
                  });
        for (const StoredDevice& device : stored) record.devices.push_back(device.result);

        if (filter.templateId && record.templateId != *filter.templateId) continue;
        if (filter.status && record.status != *filter.status) continue;
        if (filter.deviceId) {
            const bool matched = std::any_of(
                record.devices.begin(), record.devices.end(),
                [&filter](const TaskDeviceResult& device) {
                    return device.deviceId == *filter.deviceId;
                });
            if (!matched) continue;
        }
        records.push_back(std::move(record));
    }

    // 历史列表默认最新在前
    std::stable_sort(records.begin(), records.end(),
                     [](const TaskRunRecord& a, const TaskRunRecord& b) {
                         return a.startedAt > b.startedAt;
                     });
    return records;
}

int TaskRunStore::prune(int retentionDays)
{
    return prune(retentionDays, kRunListLimit);
}

int TaskRunStore::prune(int retentionDays, int pageSize)
{
    if (retentionDays <= 0) {
        qWarning() << "TaskRunStore: 拒绝执行非正数保留天数" << retentionDays
                   << "（防止误清全部历史，记录未做任何修改）";
        return 0;
    }
    if (pageSize <= 0) {
        qWarning() << "TaskRunStore: 拒绝执行非正数分页窗口" << pageSize;
        return 0;
    }
    const std::time_t cutoff = nowSeconds()
        - static_cast<std::time_t>(retentionDays) * 86400;
    auto& store = ConfigStore::instance();

    // 分页遍历到底收集全部超期终态运行键：ConfigStore::list() 是
    // updated_at 倒序 + LIMIT 的窗口读取，历史积压超过窗口时若不翻页，
    // 恰好是"最老的过期记录"永远落在窗口之外，prune 将永久返回 0、库无限增长
    QStringList expiredRunKeys;
    for (int offset = 0;; offset += pageSize) {
        const QList<QVariantMap> page = store.list(kRunType, pageSize, offset);
        for (const QVariantMap& row : page) {
            const QString key = row.value(QString(kRowKey)).toString();
            const std::optional<TaskRunStatus> status = taskRunStatusFromToken(
                row.value(QString(kFieldStatus)).toString().toStdString());
            // 状态无法解析或仍在运行：保守保留，绝不删除可能活跃的记录
            if (!status || *status == TaskRunStatus::Running) continue;
            std::time_t started = static_cast<std::time_t>(
                row.value(QString(kFieldStartedAt)).toLongLong());
            if (started <= 0)
                started = static_cast<std::time_t>(
                    row.value(QString(kRowUpdatedAt)).toLongLong() / 1000);
            if (started < cutoff) expiredRunKeys << key;
        }
        if (page.size() < pageSize) break;
    }
    if (expiredRunKeys.isEmpty()) return 0;

    // 子行级联同样分页遍历（收集与删除分两段，翻页期间行集保持稳定）：
    // 逐行按 runId 归属精确删除过期运行的设备/步骤行，
    // 超窗口的旧子行不再成为永久孤儿
    const auto removeChildren = [&store, &expiredRunKeys, pageSize](const QLatin1String& type) {
        QStringList keysToRemove;
        for (int offset = 0;; offset += pageSize) {
            const QList<QVariantMap> page = store.list(type, pageSize, offset);
            for (const QVariantMap& row : page) {
                if (expiredRunKeys.contains(row.value(QString(kFieldRunId)).toString()))
                    keysToRemove << row.value(QString(kRowKey)).toString();
            }
            if (page.size() < pageSize) break;
        }
        for (const QString& key : keysToRemove) store.remove(type, key);
    };
    removeChildren(kDeviceType);
    removeChildren(kStepType);

    for (const QString& key : expiredRunKeys) store.remove(kRunType, key);

    qInfo() << "TaskRunStore: 保留清理完成，删除终态运行记录" << expiredRunKeys.size()
            << "条（保留天数" << retentionDays << "）";
    return expiredRunKeys.size();
}
