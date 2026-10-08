// TaskExecutionEngine.cpp — v2.11 Task 4 执行引擎：状态机与阶段恢复。
//
// 编排原则（绑定约束）：绝不绕过 IDeployable/TransferScheduler/BatchCommandRunner
// 直连协议库。文件部署复用 v2.9 可靠传输内核（TransferScheduler 注入 Executor →
// TransferExecutor → AdapterTransferChannel），命令/重启/恢复检查复用 v2.10
// BatchCommandRunner（重启后断开连接按既有语义视为预期成功）。
// 凭据沿用 FtpDeployWidget 的 credentialKey 金库模式：AuthResolver 只在执行期
// 供值，任务/通道各持私有副本并在终态 AuthInfo::clear 擦除；日志与错误文本
// 不含凭据明文与命令原文。

#include "task/TaskExecutionEngine.h"

#include "adapter/FtpAdapter.h"
#include "adapter/ProtocolRegistry.h"
#include "task/TaskRunStore.h"   // sanitizeTaskErrorText：日志路径错误文本统一脱敏
#include "transfer/AdapterTransferChannel.h"
#include "transfer/TransferScheduler.h"
#include "transfer/TransferTypes.h"

#include <QDebug>
#include <QFileInfo>
#include <QMetaType>
#include <QStringList>
#include <QUuid>
#include <QVariantList>
#include <QMutexLocker>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace {

// —— 阶段恢复轮询间隔：协调线程在等待传输收口与检查取消之间的节拍 ——
constexpr auto kCancelPollInterval = std::chrono::milliseconds(10);

std::string toLowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string utf8(const QString& value)
{
    return value.toUtf8().toStdString();
}

QString utf8ToQString(const std::string& value)
{
    return QString::fromUtf8(value.c_str());
}

// 部署端点协议 → 调度器展示协议 / 注册表键 / FTPS 开关（对齐 FtpDeployWidget）。
struct DeployRoute {
    QString display;
    QString registryKey;
    bool ftps = false;
    int defaultPort = 21;
};

std::optional<DeployRoute> deployRouteFor(const std::string& protocol)
{
    const std::string lowered = toLowerAscii(protocol);
    if (lowered == "ftp") {
        return DeployRoute{QStringLiteral("ftp"), QStringLiteral("ftp"), false, 21};
    }
    if (lowered == "ftps") {
        return DeployRoute{QStringLiteral("ftp"), QStringLiteral("ftp"), true, 21};
    }
    if (lowered == "sftp" || lowered == "ssh") {
        return DeployRoute{QStringLiteral("sftp"), QStringLiteral("ssh"), false, 22};
    }
    return std::nullopt;
}

bool isCommandProtocol(const std::string& protocol)
{
    const std::string lowered = toLowerAscii(protocol);
    return lowered == "telnet" || lowered == "ssh";
}

const DeviceEndpoint* findDeployEndpoint(const DeviceProfile& profile, const QString& filter)
{
    for (const auto& endpoint : profile.endpoints) {
        const QString protocol = utf8ToQString(endpoint.protocol).trimmed().toLower();
        if (!filter.isEmpty() && protocol != filter) {
            continue;
        }
        if (deployRouteFor(endpoint.protocol)) {
            return &endpoint;
        }
    }
    return nullptr;
}

const DeviceEndpoint* findCommandEndpoint(const DeviceProfile& profile, const QString& filter)
{
    for (const auto& endpoint : profile.endpoints) {
        const QString protocol = utf8ToQString(endpoint.protocol).trimmed().toLower();
        if (!filter.isEmpty()) {
            if (protocol == filter && isCommandProtocol(endpoint.protocol)) {
                return &endpoint;
            }
            continue;
        }
        if (isCommandProtocol(endpoint.protocol)) {
            return &endpoint;
        }
    }
    return nullptr;
}

TaskDeviceState phaseStateFor(TaskStepType type)
{
    switch (type) {
    case TaskStepType::DeployFiles: return TaskDeviceState::Deploying;
    case TaskStepType::RunCommands: return TaskDeviceState::Commanding;
    case TaskStepType::Reboot: return TaskDeviceState::Rebooting;
    case TaskStepType::Recover: return TaskDeviceState::Recovering;
    }
    return TaskDeviceState::Pending;
}

QStringList stringListParam(const QVariantMap& params, const QString& key)
{
    return params.value(key).toStringList();
}

int intParam(const QVariantMap& params, const QString& key, int fallback)
{
    bool ok = false;
    const int value = params.value(key).toInt(&ok);
    return ok ? value : fallback;
}

// 部署步骤参数解析（items 数组，或 files+remoteDirectory 简写）
bool parseDeployItems(const QVariantMap& params,
                      QVector<TransferItemRequest>& items,
                      QString& error)
{
    const QVariant itemsValue = params.value(QStringLiteral("items"));
    if (itemsValue.typeId() == QMetaType::QVariantList && !itemsValue.toList().isEmpty()) {
        const QVariantList list = itemsValue.toList();
        for (const QVariant& value : list) {
            if (value.typeId() != QMetaType::QVariantMap) {
                error = QStringLiteral("文件部署步骤条目结构非法（应为对象）");
                return false;
            }
            const QVariantMap map = value.toMap();
            TransferItemRequest item;
            item.localPath = map.value(QStringLiteral("localPath")).toString();
            item.remotePath = map.value(QStringLiteral("remotePath")).toString();
            if (item.localPath.isEmpty() || item.remotePath.isEmpty()) {
                error = QStringLiteral("文件部署步骤存在缺少 localPath/remotePath 的条目");
                return false;
            }
            item.direction = map.value(QStringLiteral("direction")).toString() == QLatin1String("download")
                ? TransferDirection::Download : TransferDirection::Upload;
            item.overwrite = map.value(QStringLiteral("overwrite")).toString() == QLatin1String("skip")
                ? OverwritePolicy::Skip : OverwritePolicy::Overwrite;
            items.push_back(item);
        }
        return true;
    }

    const QStringList files = params.value(QStringLiteral("files")).toStringList();
    const QString remoteDirectory = params.value(QStringLiteral("remoteDirectory")).toString();
    if (!files.isEmpty() && !remoteDirectory.isEmpty()) {
        QString base = remoteDirectory;
        while (base.size() > 1 && base.endsWith(u'/')) {
            base.chop(1);
        }
        for (const QString& file : files) {
            if (file.trimmed().isEmpty()) {
                error = QStringLiteral("文件部署清单存在空路径");
                return false;
            }
            TransferItemRequest item;
            item.localPath = file;
            const QString name = QFileInfo(file).fileName();
            item.remotePath = base == QLatin1String("/")
                ? QStringLiteral("/") + name
                : base + QLatin1Char('/') + name;
            items.push_back(item);
        }
        return true;
    }

    error = QStringLiteral("文件部署步骤缺少文件清单（items 数组，或 files + remoteDirectory）");
    return false;
}

struct StepValidation {
    bool ok = true;
    std::string error;
};

// Validating 阶段：静态检查每台设备的端点与步骤参数（不含任何凭据判断）。
StepValidation validateDeviceSteps(const TaskTemplate& tmpl, const DeviceProfile& profile)
{
    for (std::size_t i = 0; i < tmpl.steps.size(); ++i) {
        const TaskStep& step = tmpl.steps[i];
        const QString filter = step.parameters.value(QStringLiteral("protocol")).toString()
                                   .trimmed().toLower();
        switch (step.type) {
        case TaskStepType::DeployFiles: {
            if (!findDeployEndpoint(profile, filter)) {
                return {false, utf8(QStringLiteral("步骤 %1（文件部署）缺少可用端点："
                                                   "设备未配置 FTP/SFTP 端点").arg(i + 1))};
            }
            QVector<TransferItemRequest> items;
            QString parseError;
            if (!parseDeployItems(step.parameters, items, parseError)) {
                return {false, utf8(QStringLiteral("步骤 %1（文件部署）参数非法：%2")
                                        .arg(i + 1).arg(parseError))};
            }
            break;
        }
        case TaskStepType::RunCommands:
        case TaskStepType::Recover: {
            if (!findCommandEndpoint(profile, filter)) {
                return {false, utf8(QStringLiteral("步骤 %1（%2）缺少可用端点："
                                                   "设备未配置 Telnet/SSH 端点")
                                        .arg(i + 1)
                                        .arg(taskStepTypeToChineseName(step.type)))};
            }
            if (stringListParam(step.parameters, QStringLiteral("commands")).isEmpty()) {
                return {false, utf8(QStringLiteral("步骤 %1（%2）缺少命令列表")
                                        .arg(i + 1)
                                        .arg(taskStepTypeToChineseName(step.type)))};
            }
            break;
        }
        case TaskStepType::Reboot: {
            if (!findCommandEndpoint(profile, filter)) {
                return {false, utf8(QStringLiteral("步骤 %1（重启）缺少可用端点："
                                                   "设备未配置 Telnet/SSH 端点").arg(i + 1))};
            }
            break;
        }
        }
    }
    return {true, {}};
}

bool containsDelivered(const std::vector<TaskDeliveredFile>& delivered,
                       const TransferItemRequest& item)
{
    return std::any_of(delivered.cbegin(), delivered.cend(),
                       [&item](const TaskDeliveredFile& record) {
        return record.localPath == item.localPath.toStdString()
            && record.remotePath == item.remotePath.toStdString();
    });
}

void emitState(const TaskExecutionEngine::Callbacks& callbacks,
               const std::string& deviceId,
               TaskDeviceState state)
{
    if (callbacks.onDeviceStateChanged) {
        callbacks.onDeviceStateChanged(deviceId, state);
    }
}

void emitLog(const TaskExecutionEngine::Callbacks& callbacks, const QString& message)
{
    qDebug() << "TaskExecutionEngine:" << message;
    if (callbacks.onLog) {
        callbacks.onLog(utf8(message));
    }
}

// 凭据金库：对齐 FtpDeployWidget 的 credentialKey 模式——TransferTask 只携带
// 引用键，秘密值留在调度器 worker 按项取用的私有副本里，步骤结束即擦除。
struct CredentialVault {
    std::mutex mutex;
    std::unordered_map<std::string, AuthInfo> entries;

    ~CredentialVault()
    {
        // 异常兜底：随作用域销毁清扫所有仍驻留的凭据副本
        for (auto& entry : entries) {
            entry.second.clear();
        }
    }

    std::string insert(const AuthInfo& auth)
    {
        const std::string key = QUuid::createUuid().toString(QUuid::Id128).toStdString();
        std::lock_guard<std::mutex> lock(mutex);
        entries.emplace(key, auth);
        return key;
    }
    AuthInfo lookup(const QString& key)
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto it = entries.find(key.toStdString());
        return it == entries.cend() ? AuthInfo{} : it->second;
    }
    void erase(const QString& key)
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto it = entries.find(key.toStdString());
        if (it != entries.end()) {
            it->second.clear();
            entries.erase(it);
        }
    }
};

TransferItemResult transferFailureResult(TransferErrorCode code, const QString& message)
{
    TransferItemResult result;
    result.state = TransferState::Failed;
    result.error = {code, message, message, false};
    result.attempts = 1;
    return result;
}

// 生产部署执行器：与 FtpDeployWidget::executePanelTransfer 同构，
// 每条目独立 adapter/channel/凭据副本，由 TransferExecutor 负责重试与擦除。
TransferScheduler::Executor makeTransferExecutor(const TaskExecutionEngine::Dependencies& deps,
                                                 const std::shared_ptr<CredentialVault>& vault)
{
    return [deps, vault](const TransferTask& task,
                         int,
                         const TransferItemRequest& item,
                         std::atomic_bool& cancel,
                         const TransferScheduler::ProgressSink& progress) {
        const QString registryKey = task.protocol == QLatin1String("sftp")
            ? QStringLiteral("ssh") : task.protocol;
        auto adapter = deps.adapterFactory
            ? deps.adapterFactory(registryKey.toStdString()) : nullptr;
        if (!adapter) {
            return transferFailureResult(TransferErrorCode::Unsupported,
                                         QStringLiteral("协议 %1 的适配器不可用").arg(task.protocol));
        }
        if (task.protocol == QLatin1String("ftp") && task.useFtps) {
            if (const auto ftp = std::dynamic_pointer_cast<FtpAdapter>(adapter)) {
                ftp->setUseFtps(true);
            }
        }
        AuthInfo credentials = vault->lookup(task.credentialKey);
        std::unique_ptr<ITransferChannel> channel = deps.transferChannelFactory
            ? deps.transferChannelFactory(adapter)
            : std::unique_ptr<ITransferChannel>(
                  new AdapterTransferChannel(task.protocol, adapter));
        if (!channel) {
            credentials.clear();
            return transferFailureResult(TransferErrorCode::Unsupported,
                                         QStringLiteral("传输通道不可用"));
        }
        channel->setProgressCallback(progress);
        // credentials 是本闭包私有副本；TransferExecutor 在终态安全擦除它。
        TransferExecutor executor(*channel, task.device, credentials, deps.transferRetrySleeper);
        return executor.execute(item, cancel);
    };
}

TaskRunId generateRunId()
{
    return "taskrun-" + QUuid::createUuid().toString(QUuid::Id128).toStdString();
}

class FunctionRunnable final : public QRunnable
{
public:
    explicit FunctionRunnable(std::function<void()> function)
        : m_function(std::move(function))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        try {
            if (m_function) {
                m_function();
            }
        } catch (const std::exception& error) {
            qWarning() << "TaskExecutionEngine: 协调线程异常:" << error.what();
        } catch (...) {
            qWarning() << "TaskExecutionEngine: 协调线程未知异常";
        }
    }

private:
    std::function<void()> m_function;
};

} // namespace

TaskExecutionEngine::TaskExecutionEngine(Dependencies dependencies)
    : m_deps(std::move(dependencies))
    , m_commandRunner(m_deps.adapterFactory)
{
    if (!m_deps.adapterFactory) {
        m_deps.adapterFactory = [](const std::string& protocol) {
            return ProtocolRegistry::instance()->create(protocol);
        };
    }
    m_pool.setMaxThreadCount(1);
}

TaskExecutionEngine::~TaskExecutionEngine()
{
    cancel();
    m_pool.waitForDone();
}

TaskRunId TaskExecutionEngine::start(const TaskExecutionRequest& request, Callbacks callbacks)
{
    if (!request.authResolver) {
        qWarning() << "TaskExecutionEngine: 启动被拒绝——请求缺少凭据解析器";
        return {};
    }
    if (request.devices.empty()) {
        qWarning() << "TaskExecutionEngine: 启动被拒绝——请求没有目标设备";
        return {};
    }
    if (request.task.steps.empty()) {
        qWarning() << "TaskExecutionEngine: 启动被拒绝——模板没有有序步骤";
        return {};
    }

    std::shared_ptr<RunContext> context;
    {
        QMutexLocker locker(&m_stateMutex);
        if (m_active) {
            qWarning() << "TaskExecutionEngine: 启动被拒绝——已有活跃运行";
            return {};
        }
        context = std::make_shared<RunContext>();
        context->runId = generateRunId();
        context->request = request;
        context->callbacks = std::move(callbacks);
        context->plans.assign(request.devices.size(), DevicePlan{});
        m_active = context;
    }

    m_pool.start(new FunctionRunnable([this, context] { executeRun(context); }));
    return context->runId;
}

void TaskExecutionEngine::cancel()
{
    QMutexLocker locker(&m_stateMutex);
    if (m_active) {
        qDebug() << "TaskExecutionEngine: 取消请求已发出，将在步骤边界与执行内核检查点收敛";
        m_active->cancelRequested.store(true);
    }
}

TaskRunId TaskExecutionEngine::retryFailed(const TaskRunId& runId)
{
    std::shared_ptr<RunContext> context;
    {
        QMutexLocker locker(&m_stateMutex);
        if (m_active) {
            qWarning() << "TaskExecutionEngine: 阶段恢复被拒绝——仍有活跃运行";
            return {};
        }
        const auto stored = m_history.find(runId);
        if (stored == m_history.cend()) {
            qWarning() << "TaskExecutionEngine: 阶段恢复被拒绝——未知运行 ID"
                       << utf8ToQString(runId);
            return {};
        }

        context = std::make_shared<RunContext>();
        context->runId = generateRunId();
        context->resumedFromRunId = runId;
        context->request = stored->second.request;
        context->callbacks = stored->second.callbacks;

        const auto& outcomes = stored->second.outcome.devices;
        for (std::size_t i = 0; i < stored->second.request.devices.size(); ++i) {
            DevicePlan plan;
            if (i < outcomes.size()) {
                const TaskDeviceOutcome& device = outcomes[i];
                const bool resumable = device.state == TaskDeviceState::Failed
                    || device.state == TaskDeviceState::Cancelled
                    || device.state == TaskDeviceState::PartiallySucceeded;
                if (!resumable) {
                    // 上一轮整体成功（或已跳过）：本轮直接标记 Skipped，不触达设备。
                    plan.skipEntirely = true;
                    plan.carriedSteps = device.steps;
                } else {
                    plan.resumeIndex = resumePrefix(device.steps);
                    plan.carriedSteps.assign(
                        device.steps.cbegin(),
                        device.steps.cbegin()
                            + static_cast<std::ptrdiff_t>(plan.resumeIndex));
                    const auto delivered = stored->second.delivered.find(device.deviceId);
                    if (delivered != stored->second.delivered.cend()) {
                        const auto stepDelivered = delivered->second.find(plan.resumeIndex);
                        if (stepDelivered != delivered->second.cend()) {
                            plan.deliveredByStep[plan.resumeIndex] = stepDelivered->second;
                        }
                    }
                }
            }
            context->plans.push_back(std::move(plan));
        }
        m_active = context;
    }

    emitLog(context->callbacks,
            QStringLiteral("阶段恢复运行启动：源自运行 %1 → 新运行 %2")
                .arg(utf8ToQString(runId), utf8ToQString(context->runId)));
    m_pool.start(new FunctionRunnable([this, context] { executeRun(context); }));
    return context->runId;
}

bool TaskExecutionEngine::isRunning() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_active != nullptr;
}

std::size_t TaskExecutionEngine::resumePrefix(const std::vector<TaskStepResult>& steps) const
{
    std::size_t prefix = 0;
    while (prefix < steps.size() && steps[prefix].state == TaskDeviceState::Succeeded) {
        ++prefix;
    }
    return prefix;
}

void TaskExecutionEngine::executeRun(const std::shared_ptr<RunContext>& run)
{
    TaskRunOutcome outcome;
    std::map<std::string, std::map<std::size_t, std::vector<TaskDeliveredFile>>> deliveredArchive;
    try {
        outcome.runId = run->runId;
        outcome.resumedFromRunId = run->resumedFromRunId;
        outcome.templateId = run->request.task.templateId;
        outcome.templateVersion = run->request.task.version;

        emitLog(run->callbacks,
                QStringLiteral("任务执行开始：模板 \"%1\"（版本 %2），目标设备 %3 台，运行 %4%5")
                    .arg(utf8ToQString(run->request.task.name))
                    .arg(run->request.task.version)
                    .arg(run->request.devices.size())
                    .arg(utf8ToQString(run->runId),
                         run->resumedFromRunId.empty()
                             ? QString()
                             : QStringLiteral("（源自 %1）")
                                   .arg(utf8ToQString(run->resumedFromRunId))));

        for (std::size_t i = 0; i < run->request.devices.size(); ++i) {
            std::map<std::size_t, std::vector<TaskDeliveredFile>> deviceDelivered;
            TaskDeviceOutcome deviceOutcome = runDevicePipeline(run, i, deviceDelivered);
            if (!deviceDelivered.empty()) {
                deliveredArchive[deviceOutcome.deviceId] = std::move(deviceDelivered);
            }
            outcome.devices.push_back(std::move(deviceOutcome));
        }

        StoredRun stored;
        stored.request = run->request;
        stored.callbacks = run->callbacks;
        stored.outcome = outcome;
        stored.delivered = deliveredArchive;
        {
            QMutexLocker locker(&m_stateMutex);
            m_history[run->runId] = std::move(stored);
            m_historyOrder.push_back(run->runId);
            while (m_historyOrder.size() > kHistoryLimit) {
                m_history.erase(m_historyOrder.front());
                m_historyOrder.pop_front();
            }
            m_active.reset();   // 先开放新运行，再广播收口
        }

        int succeeded = 0;
        int partial = 0;
        int failed = 0;
        int cancelled = 0;
        int skipped = 0;
        for (const auto& device : outcome.devices) {
            switch (device.state) {
            case TaskDeviceState::Succeeded: ++succeeded; break;
            case TaskDeviceState::PartiallySucceeded: ++partial; break;
            case TaskDeviceState::Failed: ++failed; break;
            case TaskDeviceState::Cancelled: ++cancelled; break;
            case TaskDeviceState::Skipped: ++skipped; break;
            default: break;
            }
        }
        emitLog(run->callbacks,
                QStringLiteral("任务执行收口：运行 %1，成功 %2 台，部分成功 %3 台，失败 %4 台，"
                               "取消 %5 台，跳过 %6 台")
                    .arg(utf8ToQString(run->runId))
                    .arg(succeeded).arg(partial).arg(failed).arg(cancelled).arg(skipped));
    } catch (const std::exception& error) {
        qWarning() << "TaskExecutionEngine: 执行协调异常收口:" << error.what();
        QMutexLocker locker(&m_stateMutex);
        if (m_active == run) {
            m_active.reset();
        }
    }

    if (run->callbacks.onRunFinished) {
        run->callbacks.onRunFinished(outcome);
    }
}

TaskDeviceOutcome TaskExecutionEngine::runDevicePipeline(
    const std::shared_ptr<RunContext>& run,
    std::size_t deviceIndex,
    std::map<std::size_t, std::vector<TaskDeliveredFile>>& deliveredOut)
{
    const DeviceProfile& profile = run->request.devices[deviceIndex];
    const DevicePlan& plan = run->plans[deviceIndex];
    const Callbacks& callbacks = run->callbacks;

    TaskDeviceOutcome outcome;
    outcome.deviceId = profile.deviceId;
    outcome.name = profile.name;
    if (!profile.endpoints.empty()) {
        outcome.address = profile.endpoints.front().ip + ":"
            + std::to_string(profile.endpoints.front().port);
    }
    const QString displayLabel = utf8ToQString(profile.name.empty() ? profile.deviceId : profile.name);

    emitState(callbacks, profile.deviceId, TaskDeviceState::Pending);
    if (plan.skipEntirely) {
        outcome.steps = plan.carriedSteps;
        outcome.state = TaskDeviceState::Skipped;
        emitState(callbacks, profile.deviceId, TaskDeviceState::Skipped);
        emitLog(callbacks, QStringLiteral("[%1] 上一轮已成功，本轮阶段恢复跳过").arg(displayLabel));
        return outcome;
    }
    if (run->cancelRequested.load()) {
        outcome.state = TaskDeviceState::Cancelled;
        outcome.error = u8"执行已取消";
        emitState(callbacks, profile.deviceId, TaskDeviceState::Cancelled);
        return outcome;
    }

    emitState(callbacks, profile.deviceId, TaskDeviceState::Validating);
    const StepValidation validation = validateDeviceSteps(run->request.task, profile);
    if (!validation.ok) {
        outcome.state = TaskDeviceState::Failed;
        outcome.error = validation.error;
        emitState(callbacks, profile.deviceId, TaskDeviceState::Failed);
        // 终审 Important 2：错误文本入日志（含 qDebug 镜像）前统一脱敏
        emitLog(callbacks, QStringLiteral("[%1] 设备校验失败：%2")
                    .arg(displayLabel,
                         utf8ToQString(sanitizeTaskErrorText(validation.error))));
        return outcome;
    }

    AuthInfo auth = run->request.authResolver(profile);
    // RAII 兜底：无论正常收口还是异常退出，设备级凭据副本都必须擦除
    struct AuthScopeGuard {
        AuthInfo& credentials;
        ~AuthScopeGuard() { credentials.clear(); }
    } authGuard{auth};
    outcome.steps = plan.carriedSteps;

    const auto& steps = run->request.task.steps;
    bool broken = false;
    bool cancelledAtBoundary = false;
    TaskStepResult lastResult;
    std::size_t nextIndex = plan.resumeIndex;
    for (; nextIndex < steps.size(); ++nextIndex) {
        if (run->cancelRequested.load()) {
            cancelledAtBoundary = true;
            break;
        }
        const TaskStep& step = steps[nextIndex];
        emitState(callbacks, profile.deviceId, phaseStateFor(step.type));
        emitLog(callbacks, QStringLiteral("[%1] 步骤 %2/%3（%4）开始")
                    .arg(displayLabel)
                    .arg(nextIndex + 1)
                    .arg(steps.size())
                    .arg(taskStepTypeToChineseName(step.type)));

        TaskStepResult stepResult;
        switch (step.type) {
        case TaskStepType::DeployFiles: {
            const QString filter = step.parameters.value(QStringLiteral("protocol")).toString()
                                       .trimmed().toLower();
            const DeviceEndpoint* endpoint = findDeployEndpoint(profile, filter);
            if (!endpoint) {
                stepResult.type = step.type;
                stepResult.state = TaskDeviceState::Failed;
                stepResult.error = u8"部署端点在运行期间不可用";
                break;
            }
            std::vector<TaskDeliveredFile> previous;
            if (nextIndex == plan.resumeIndex) {
                const auto carried = plan.deliveredByStep.find(nextIndex);
                if (carried != plan.deliveredByStep.cend()) {
                    previous = carried->second;
                }
            }
            // 以上一轮交付记录为底：本轮新交付追加，保证链式恢复不丢已交付事实
            std::vector<TaskDeliveredFile> stepDelivered = previous;
            stepResult = runDeployStep(*run, profile, *endpoint, auth, step, previous, stepDelivered);
            if (!stepDelivered.empty()) {
                deliveredOut[nextIndex] = stepDelivered;
            }
            break;
        }
        case TaskStepType::RunCommands:
        case TaskStepType::Recover: {
            const QString filter = step.parameters.value(QStringLiteral("protocol")).toString()
                                       .trimmed().toLower();
            const DeviceEndpoint* endpoint = findCommandEndpoint(profile, filter);
            if (!endpoint) {
                stepResult.type = step.type;
                stepResult.state = TaskDeviceState::Failed;
                stepResult.error = u8"命令端点在运行期间不可用";
                break;
            }
            stepResult = runCommandStep(*run, profile, *endpoint, auth, step, false);
            break;
        }
        case TaskStepType::Reboot: {
            const QString filter = step.parameters.value(QStringLiteral("protocol")).toString()
                                       .trimmed().toLower();
            const DeviceEndpoint* endpoint = findCommandEndpoint(profile, filter);
            if (!endpoint) {
                stepResult.type = step.type;
                stepResult.state = TaskDeviceState::Failed;
                stepResult.error = u8"命令端点在运行期间不可用";
                break;
            }
            stepResult = runCommandStep(*run, profile, *endpoint, auth, step, true);
            break;
        }
        }

        if (stepResult.state == TaskDeviceState::Succeeded && stepResult.attempt == 0) {
            emitLog(callbacks, QStringLiteral("[%1] 步骤 %2（%3）无需执行：上一轮已全部交付")
                        .arg(displayLabel)
                        .arg(nextIndex + 1)
                        .arg(taskStepTypeToChineseName(step.type)));
        }

        outcome.steps.push_back(stepResult);
        if (callbacks.onStepFinished) {
            callbacks.onStepFinished(profile.deviceId, stepResult);
        }
        // 终审 Important 2：stepResult.error 入日志（含 qDebug→LogBridge→文件
        // 日志镜像）前必须脱敏；落库路径由 TaskRunStore 写前脱敏，两者互不依赖
        emitLog(callbacks, QStringLiteral("[%1] 步骤 %2（%3）结果：%4%5")
                    .arg(displayLabel)
                    .arg(nextIndex + 1)
                    .arg(taskStepTypeToChineseName(step.type),
                         taskDeviceStateToString(stepResult.state))
                    .arg(stepResult.error.empty()
                             ? QString()
                             : QStringLiteral("，原因：%1").arg(utf8ToQString(
                                   sanitizeTaskErrorText(stepResult.error)))));

        if (stepResult.state != TaskDeviceState::Succeeded) {
            broken = true;
            lastResult = stepResult;
            break;
        }
    }
    auth.clear();

    const bool complete = !broken && !cancelledAtBoundary;
    bool hasProgress = !plan.carriedSteps.empty()
        || !deliveredOut.empty()
        || std::any_of(outcome.steps.cbegin(), outcome.steps.cend(),
                       [](const TaskStepResult& result) {
                           return result.state == TaskDeviceState::Succeeded;
                       });
    // 重启断开/取消/部分成功同时发生时的统一收敛规则（与 TransferScheduler
    // 的 PartiallySucceeded 语义对齐）：有任何已成功进展 → 部分成功；
    // 无进展时按最后一个收口来源归类为 失败/取消。
    TaskDeviceState finalState = TaskDeviceState::Succeeded;
    if (!complete) {
        if (broken && lastResult.state == TaskDeviceState::Failed) {
            finalState = hasProgress ? TaskDeviceState::PartiallySucceeded
                                     : TaskDeviceState::Failed;
        } else if (broken && lastResult.state == TaskDeviceState::Cancelled) {
            finalState = hasProgress ? TaskDeviceState::PartiallySucceeded
                                     : TaskDeviceState::Cancelled;
        } else if (broken) {
            finalState = TaskDeviceState::PartiallySucceeded;   // 步骤部分成功
        } else {
            finalState = hasProgress ? TaskDeviceState::PartiallySucceeded
                                     : TaskDeviceState::Cancelled;   // 边界取消
        }
        outcome.error = broken
            ? lastResult.error
            : utf8(QStringLiteral("在步骤边界收敛取消"));
    }
    outcome.state = finalState;
    emitState(callbacks, profile.deviceId, finalState);
    return outcome;
}

TaskStepResult TaskExecutionEngine::runDeployStep(
    RunContext& run,
    const DeviceProfile& profile,
    const DeviceEndpoint& endpoint,
    const AuthInfo& auth,
    const TaskStep& step,
    const std::vector<TaskDeliveredFile>& alreadyDelivered,
    std::vector<TaskDeliveredFile>& deliveredOut)
{
    TaskStepResult result;
    result.type = TaskStepType::DeployFiles;
    result.state = TaskDeviceState::Failed;

    QString parseError;
    QVector<TransferItemRequest> allItems;
    if (!parseDeployItems(step.parameters, allItems, parseError)) {
        result.error = utf8(parseError);
        return result;
    }

    // 阶段恢复过滤：上一轮已真实交付的文件不再重复上传（文件级恢复）。
    QVector<TransferItemRequest> items;
    for (const auto& item : allItems) {
        if (containsDelivered(alreadyDelivered, item)) {
            continue;
        }
        items.push_back(item);
    }
    if (items.isEmpty()) {
        result.state = TaskDeviceState::Succeeded;
        result.attempt = 0;
        return result;
    }

    const std::optional<DeployRoute> route = deployRouteFor(endpoint.protocol);
    if (!route) {
        result.error = utf8(QStringLiteral("部署端点协议 %1 不受支持")
                                .arg(utf8ToQString(endpoint.protocol)));
        return result;
    }

    auto vault = std::make_shared<CredentialVault>();
    const std::string credentialKey = vault->insert(auth);

    TransferTask task;
    task.displayName = utf8ToQString(profile.name);
    task.protocol = route->display;
    task.device.ip = endpoint.ip;
    task.device.port = endpoint.port > 0 ? endpoint.port : route->defaultPort;
    task.device.protocol = route->registryKey.toStdString();
    task.device.alias = profile.name;
    task.credentialKey = utf8ToQString(credentialKey);
    task.useFtps = route->ftps;
    task.items = items;

    struct TerminalWaiter {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
        TransferTaskSnapshot snapshot;
    };
    auto waiter = std::make_shared<TerminalWaiter>();

    TransferScheduler scheduler(1, makeTransferExecutor(m_deps, vault));
    scheduler.setEventSink([waiter](const TransferEvent& event) {
        if (event.type != TransferEventType::TaskFinished) {
            return;
        }
        std::lock_guard<std::mutex> lock(waiter->mutex);
        if (!waiter->done) {
            waiter->done = true;
            waiter->snapshot = event.snapshot;
            waiter->cv.notify_all();
        }
    });

    const QUuid taskId = scheduler.submit(task);
    if (taskId.isNull()) {
        vault->erase(utf8ToQString(credentialKey));
        result.error = u8"传输调度器拒绝提交（可能正在关闭或目标冲突）";
        return result;
    }

    // 等待收口 + 取消桥接：scheduler.cancel 只能在协调线程（非其 worker）
    // 上安全发起；waiter 锁与调度器事件锁不得嵌套持有。
    bool cancelIssued = false;
    for (;;) {
        bool finished = false;
        {
            std::unique_lock<std::mutex> lock(waiter->mutex);
            finished = waiter->cv.wait_for(lock, kCancelPollInterval,
                                           [waiter] { return waiter->done; });
        }
        if (finished) {
            break;
        }
        if (run.cancelRequested.load() && !cancelIssued) {
            cancelIssued = true;
            scheduler.cancel(taskId);
        }
    }

    vault->erase(utf8ToQString(credentialKey));

    const TransferTaskSnapshot& snapshot = waiter->snapshot;
    int maxAttempts = 0;
    for (const auto& itemResult : snapshot.itemResults) {
        maxAttempts = std::max(maxAttempts, itemResult.attempts);
    }
    result.attempt = std::max(maxAttempts, 1);

    switch (snapshot.state) {
    case TransferState::Succeeded:
        result.state = TaskDeviceState::Succeeded;
        break;
    case TransferState::PartiallySucceeded:
        result.state = TaskDeviceState::PartiallySucceeded;
        break;
    case TransferState::Cancelled:
        result.state = TaskDeviceState::Cancelled;
        break;
    default:   // Failed / NeedsAttention 等按失败收口
        result.state = TaskDeviceState::Failed;
        break;
    }
    if (result.state != TaskDeviceState::Succeeded && !snapshot.error.message.isEmpty()) {
        result.error = utf8(snapshot.error.message);
    }

    for (qsizetype i = 0; i < snapshot.itemResults.size() && i < items.size(); ++i) {
        const TransferItemResult& itemResult = snapshot.itemResults.at(i);
        if (hasDeliveredTarget(itemResult)) {
            deliveredOut.push_back({items.at(i).localPath.toStdString(),
                                    items.at(i).remotePath.toStdString(),
                                    itemResult.nonAtomic});
        }
    }
    return result;
}

TaskStepResult TaskExecutionEngine::runCommandStep(
    RunContext& run,
    const DeviceProfile& profile,
    const DeviceEndpoint& endpoint,
    const AuthInfo& auth,
    const TaskStep& step,
    bool rebootMode)
{
    TaskStepResult result;
    result.type = step.type;
    result.state = TaskDeviceState::Failed;

    const std::string protocol = toLowerAscii(endpoint.protocol);
    CommandRequest request;
    request.protocol = protocol;
    DeviceInfo device;
    device.ip = endpoint.ip;
    device.port = endpoint.port > 0 ? endpoint.port
                                    : BatchCommandRunner::defaultPort(protocol);
    device.protocol = protocol;
    device.alias = profile.name;
    request.devices.push_back(device);
    request.auth = auth;   // 私有副本；执行前后各层自行擦除
    request.timeoutSec = std::max(1, intParam(step.parameters, QStringLiteral("timeoutSec"), 10));
    request.retryCount = std::max(0, intParam(step.parameters, QStringLiteral("retryCount"), 1));
    request.rebootMode = rebootMode;

    QStringList commands = stringListParam(step.parameters, QStringLiteral("commands"));
    if (commands.isEmpty() && rebootMode) {
        const QString single = step.parameters.value(QStringLiteral("command")).toString().trimmed();
        commands = {single.isEmpty() ? QStringLiteral("reboot") : single};
    }
    for (const QString& command : commands) {
        const QString trimmed = command.trimmed();
        if (!trimmed.isEmpty()) {
            request.commands.push_back(trimmed.toStdString());
        }
    }
    if (request.commands.empty()) {
        request.auth.clear();
        result.error = rebootMode
            ? u8"重启步骤缺少命令（默认 reboot 不可用？）"
            : u8"命令步骤缺少命令列表";
        return result;
    }

    // 命令原文不写入日志：只把设备级结果映射为步骤结果。
    const CommandBatchResult batch = m_commandRunner.run(request, run.cancelRequested, {});
    request.auth.clear();
    if (batch.devices.empty()) {
        result.error = u8"命令执行器未返回设备结果";
        return result;
    }

    const CommandDeviceResult& deviceResult = batch.devices.front();
    result.attempt = deviceResult.retryCount + 1;
    switch (deviceResult.state) {
    case CommandResultState::Succeeded:
        result.state = TaskDeviceState::Succeeded;
        break;
    case CommandResultState::RebootTriggered:
        // 重启发送后连接断开是预期成功（v2.10 既有语义）
        result.state = TaskDeviceState::Succeeded;
        break;
    case CommandResultState::Cancelled:
        result.state = TaskDeviceState::Cancelled;
        break;
    case CommandResultState::Rejected:
        result.error = deviceResult.error;
        result.state = TaskDeviceState::Failed;
        result.attempt = std::max(result.attempt, 1);
        break;
    case CommandResultState::Failed:
    default:
        result.error = deviceResult.error.empty() ? std::string(u8"命令执行失败") : deviceResult.error;
        result.state = TaskDeviceState::Failed;
        break;
    }
    return result;
}
