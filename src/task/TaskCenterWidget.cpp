/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: TaskCenterWidget.cpp
 *
 * Date: 2026-10（v2.11 Task 6）
 *
 * Author: turnarond
 *
 * Description: 设备任务中心三栏 UI 实现。见头文件线程契约。
 */

#include "task/TaskCenterWidget.h"

#include "config/ConfigStore.h"
#include "config/DpapiCrypto.h"
#include "device/DeviceRegistry.h"
#include "task/TaskExecutionEngine.h"
#include "task/TaskRunStore.h"
#include "task/TaskTemplateStore.h"
#include "tools/FtpDeployTool/DeployReport.h"

#include <QApplication>
#include <QDateTime>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QVariantList>

#include <QColor>
#include <QBrush>
#include <QFile>
#include <QTextStream>

#include <algorithm>
#include <set>

namespace {

std::string toUtf8(const QString& value)
{
    return value.toUtf8().toStdString();
}

QString fromUtf8Std(const std::string& value)
{
    return QString::fromUtf8(value.c_str());
}

// 设备展示标签：名称优先、地址次级（与设备栏胶囊语义一致）
QString deviceLabel(const DeviceProfile& profile)
{
    QString address;
    if (!profile.endpoints.empty()) {
        address = QStringLiteral("%1:%2")
            .arg(fromUtf8Std(profile.endpoints.front().ip))
            .arg(profile.endpoints.front().port);
    }
    const QString name = fromUtf8Std(profile.name);
    if (name.isEmpty())
        return address.isEmpty() ? fromUtf8Std(profile.deviceId) : address;
    return address.isEmpty() ? name : QStringLiteral("%1 %2").arg(name, address);
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

// 步骤参数摘要：只展示数量与目标路径，不展示命令原文（规格 §6）。
QString stepParamSummary(const TaskStep& step)
{
    switch (step.type) {
    case TaskStepType::DeployFiles: {
        const QVariantList items = step.parameters.value(QStringLiteral("items")).toList();
        if (!items.isEmpty())
            return QStringLiteral("%1 个文件项").arg(items.size());
        const QStringList files =
            step.parameters.value(QStringLiteral("files")).toStringList();
        const QString dir =
            step.parameters.value(QStringLiteral("remoteDirectory")).toString();
        if (!files.isEmpty())
            return dir.isEmpty()
                ? QStringLiteral("%1 个文件").arg(files.size())
                : QStringLiteral("%1 个文件 → %2").arg(files.size()).arg(dir);
        return QStringLiteral("未配置文件清单");
    }
    case TaskStepType::RunCommands: {
        const QStringList commands =
            step.parameters.value(QStringLiteral("commands")).toStringList();
        return commands.isEmpty() ? QStringLiteral("未配置命令")
                                  : QStringLiteral("%1 条命令").arg(commands.size());
    }
    case TaskStepType::Reboot:
        return QStringLiteral("发送重启命令（断开视为成功）");
    case TaskStepType::Recover: {
        const QStringList commands =
            step.parameters.value(QStringLiteral("commands")).toStringList();
        return commands.isEmpty() ? QStringLiteral("恢复检查（无校验命令）")
                                  : QStringLiteral("%1 条校验命令").arg(commands.size());
    }
    }
    return QString();
}

// 统计与风险位（供摘要行与确认提示共用）。
struct RunTotals {
    int files = 0;
    int commands = 0;
    int reboots = 0;
    bool risky = false;
};

RunTotals tallySteps(const TaskTemplate& task)
{
    RunTotals totals;
    for (const auto& step : task.steps) {
        switch (step.type) {
        case TaskStepType::DeployFiles: {
            const QVariantList items = step.parameters.value(QStringLiteral("items")).toList();
            if (!items.isEmpty())
                totals.files += items.size();
            else
                totals.files +=
                    step.parameters.value(QStringLiteral("files")).toStringList().size();
            break;
        }
        case TaskStepType::RunCommands:
        case TaskStepType::Recover:
            totals.commands +=
                step.parameters.value(QStringLiteral("commands")).toStringList().size();
            break;
        case TaskStepType::Reboot:
            ++totals.reboots;
            totals.risky = true;   // 危险操作：重启（规格 §8 明确要求确认提示）
            break;
        }
    }
    return totals;
}

} // namespace

TaskCenterWidget::TaskCenterWidget(QWidget* parent) : QWidget(parent)
{
    setupUi();
}

TaskCenterWidget::~TaskCenterWidget()
{
    // 析构顺序契约：先落盘取消并等待协调线程收口（引擎析构内部 cancel+join），
    // 陈旧排队回调由 QPointer 守卫 + 谱系令牌双保险丢弃。
    m_active.reset();
    if (m_engine) {
        m_engine->cancel();
        m_engine.reset();
    }
}

void TaskCenterWidget::setupUi()
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(6);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    mainLayout->addWidget(splitter);

    // ── 左栏：模板列表 + 搜索（名称/ID/标签子串过滤）──
    auto* leftBox = new QGroupBox(QStringLiteral("任务模板"), splitter);
    leftBox->setObjectName(QStringLiteral("taskTemplateColumn"));
    auto* leftLayout = new QVBoxLayout(leftBox);
    m_templateSearch = new QLineEdit(leftBox);
    m_templateSearch->setObjectName(QStringLiteral("templateSearch"));
    m_templateSearch->setPlaceholderText(QStringLiteral("搜索模板名称 / 标签…"));
    leftLayout->addWidget(m_templateSearch);
    m_templateList = new QListWidget(leftBox);
    m_templateList->setObjectName(QStringLiteral("templateList"));
    leftLayout->addWidget(m_templateList, 1);
    connect(m_templateSearch, &QLineEdit::textChanged, this,
            &TaskCenterWidget::refreshTemplates);
    connect(m_templateList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem*, QListWidgetItem*) { onTemplateSelected(); });

    // ── 中栏：模板摘要 + 步骤与执行参数 ──
    auto* middleBox = new QGroupBox(QStringLiteral("步骤与执行参数"), splitter);
    middleBox->setObjectName(QStringLiteral("taskTemplateDetailColumn"));
    auto* middleLayout = new QVBoxLayout(middleBox);
    m_templateSummaryLabel = new QLabel(QStringLiteral("未选择模板"), middleBox);
    m_templateSummaryLabel->setObjectName(QStringLiteral("templateSummaryLabel"));
    m_templateSummaryLabel->setWordWrap(true);
    middleLayout->addWidget(m_templateSummaryLabel);
    m_templateSteps = new QTableWidget(0, 3, middleBox);
    m_templateSteps->setObjectName(QStringLiteral("templateSteps"));
    m_templateSteps->setHorizontalHeaderLabels(
        {QStringLiteral("步骤"), QStringLiteral("类型"), QStringLiteral("参数摘要")});
    m_templateSteps->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_templateSteps->verticalHeader()->setVisible(false);
    m_templateSteps->setEditTriggers(QAbstractItemView::NoEditTriggers);
    middleLayout->addWidget(m_templateSteps, 1);

    // ── 右栏：目标设备 / 执行状态 / 历史结果 / 操作按钮 ──
    auto* rightBox = new QGroupBox(QStringLiteral("设备 · 执行 · 历史"), splitter);
    rightBox->setObjectName(QStringLiteral("taskActionColumn"));
    auto* rightLayout = new QVBoxLayout(rightBox);

    rightLayout->addWidget(new QLabel(QStringLiteral("目标设备"), rightBox));
    m_targetDevices = new QListWidget(rightBox);
    m_targetDevices->setObjectName(QStringLiteral("taskTargetDevices"));
    m_targetDevices->setMaximumHeight(96);
    rightLayout->addWidget(m_targetDevices);

    rightLayout->addWidget(new QLabel(QStringLiteral("执行状态"), rightBox));
    m_overallBar = new QProgressBar(rightBox);
    m_overallBar->setObjectName(QStringLiteral("taskOverallBar"));
    m_overallBar->setRange(0, 1);
    m_overallBar->setValue(0);
    m_overallBar->setTextVisible(true);
    m_overallBar->setFormat(QStringLiteral("空闲"));
    rightLayout->addWidget(m_overallBar);
    m_deviceStates = new QListWidget(rightBox);
    m_deviceStates->setObjectName(QStringLiteral("taskDeviceStates"));
    m_deviceStates->setMaximumHeight(96);
    rightLayout->addWidget(m_deviceStates);

    rightLayout->addWidget(new QLabel(QStringLiteral("最近执行历史"), rightBox));
    m_runHistory = new QTableWidget(0, 4, rightBox);
    m_runHistory->setObjectName(QStringLiteral("taskRunHistory"));
    m_runHistory->setHorizontalHeaderLabels(
        {QStringLiteral("开始时间"), QStringLiteral("模板"),
         QStringLiteral("状态"), QStringLiteral("设备")});
    m_runHistory->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_runHistory->verticalHeader()->setVisible(false);
    m_runHistory->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_runHistory->setSelectionBehavior(QAbstractItemView::SelectRows);
    rightLayout->addWidget(m_runHistory, 1);
    connect(m_runHistory, &QTableWidget::itemSelectionChanged, this,
            &TaskCenterWidget::onHistorySelectionChanged);

    auto* buttonRow = new QHBoxLayout();
    m_runButton = new QPushButton(QStringLiteral("执行任务"), rightBox);
    m_runButton->setObjectName(QStringLiteral("btnTaskRun"));   // 主按钮：琴色信号态走双主题 QSS
    m_cancelButton = new QPushButton(QStringLiteral("取消"), rightBox);
    m_cancelButton->setObjectName(QStringLiteral("taskCancelButton"));
    m_retryButton = new QPushButton(QStringLiteral("从失败阶段重试"), rightBox);
    m_retryButton->setObjectName(QStringLiteral("taskRetryFailedButton"));
    m_exportButton = new QPushButton(QStringLiteral("导出报告"), rightBox);
    m_exportButton->setObjectName(QStringLiteral("taskExportReportButton"));
    m_cloneButton = new QPushButton(QStringLiteral("复制为新模板"), rightBox);
    m_cloneButton->setObjectName(QStringLiteral("taskCloneTemplateButton"));
    buttonRow->addWidget(m_runButton);
    buttonRow->addWidget(m_cancelButton);
    buttonRow->addWidget(m_retryButton);
    buttonRow->addWidget(m_exportButton);
    buttonRow->addWidget(m_cloneButton);
    rightLayout->addLayout(buttonRow);
    connect(m_runButton, &QPushButton::clicked, this, &TaskCenterWidget::onExecuteClicked);
    connect(m_cancelButton, &QPushButton::clicked, this, &TaskCenterWidget::onCancelClicked);
    connect(m_retryButton, &QPushButton::clicked, this, &TaskCenterWidget::onRetryClicked);
    connect(m_exportButton, &QPushButton::clicked, this, &TaskCenterWidget::onExportClicked);
    connect(m_cloneButton, &QPushButton::clicked, this, &TaskCenterWidget::onCloneClicked);

    splitter->addWidget(leftBox);
    splitter->addWidget(middleBox);
    splitter->addWidget(rightBox);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    splitter->setStretchFactor(2, 3);

    setRunningUi(false);
}

void TaskCenterWidget::setRegistry(DeviceRegistry* registry)
{
    m_registry = registry;
    refreshTemplates();
}

void TaskCenterWidget::setStores(TaskTemplateStore* templates, TaskRunStore* runs)
{
    m_templates = templates;
    m_runs = runs;
    refreshTemplates();
    refreshHistory();
}

void TaskCenterWidget::setRunConfirmer(RunConfirmer confirmer)
{
    m_confirmer = std::move(confirmer);
}

std::unique_ptr<TaskExecutionEngine> TaskCenterWidget::createEngine()
{
    // 生产默认依赖：适配器工厂走 ProtocolRegistry、传输通道走 AdapterTransferChannel，
    // 全部经由既有内核，UI 不直连协议库。
    return std::make_unique<TaskExecutionEngine>();
}

std::vector<DeviceProfile> TaskCenterWidget::resolveTargets(const TaskTemplate& task) const
{
    std::vector<DeviceProfile> result;
    if (!m_registry)
        return result;
    std::set<std::string> seen;
    for (const auto& deviceId : task.deviceIds) {
        if (const auto found = m_registry->find(deviceId)) {
            if (seen.insert(found->deviceId).second)
                result.push_back(*found);
        }
    }
    // 标签圈选：任一标签命中即纳入（同设备多标签不重复）
    if (!task.tagSelectors.empty()) {
        for (const auto& profile : m_registry->list()) {
            bool matched = false;
            for (const auto& selector : task.tagSelectors) {
                if (std::find(profile.tags.begin(), profile.tags.end(), selector)
                        != profile.tags.end()) {
                    matched = true;
                    break;
                }
            }
            if (matched && seen.insert(profile.deviceId).second)
                result.push_back(profile);
        }
    }
    return result;
}

QString TaskCenterWidget::buildSummary(const TaskTemplate& task,
                                       const std::vector<DeviceProfile>& devices,
                                       bool forConfirm) const
{
    const RunTotals totals = tallySteps(task);
    QString line = QStringLiteral("%1 v%2 · 目标 %3 台 · 文件 %4 个 · 命令 %5 条")
        .arg(fromUtf8Std(task.name))
        .arg(task.version)
        .arg(devices.size())
        .arg(totals.files)
        .arg(totals.commands);
    if (totals.reboots > 0)
        line += QStringLiteral(" · 含重启动作");
    if (!forConfirm)
        return line;

    QString summary = line + QStringLiteral("\n");
    for (const auto& profile : devices)
        summary += QStringLiteral("• %1\n").arg(deviceLabel(profile));
    if (totals.risky) {
        summary += QStringLiteral(
            "风险提示：任务包含重启动作，执行期间设备连接将中断，"
            "请确认现场安全后再执行。");
    }
    return summary.trimmed();
}

bool TaskCenterWidget::confirmRun(const QString& summary, bool risky)
{
    if (m_confirmer)
        return m_confirmer(summary, risky);
    QMessageBox box(QMessageBox::Question,
                    QStringLiteral("执行确认"), summary,
                    QMessageBox::Yes | QMessageBox::No, this);
    box.setObjectName(QStringLiteral("taskRiskConfirmBox"));
    box.setDefaultButton(QMessageBox::No);
    if (risky)
        box.setIcon(QMessageBox::Warning);
    return box.exec() == QMessageBox::Yes;
}

void TaskCenterWidget::startTemplate(const std::string& templateId)
{
    if (!m_registry || !m_templates || !m_runs) {
        emit logMessage(QStringLiteral("任务中心：尚未挂载注册表/存储，无法执行"));
        return;
    }
    const auto task = m_templates->load(templateId);
    if (!task) {
        emit logMessage(QStringLiteral("任务中心：模板不存在或已被拒绝（结构损坏/含凭据）"));
        return;
    }
    if (task->steps.empty()) {
        emit logMessage(QStringLiteral("任务中心：模板没有有序步骤，拒绝执行"));
        return;
    }
    const auto devices = resolveTargets(*task);
    if (devices.empty()) {
        emit logMessage(QStringLiteral("任务中心：模板没有可用目标设备（deviceIds/tagSelectors 未命中）"));
        return;
    }

    const RunTotals totals = tallySteps(*task);
    if (!confirmRun(buildSummary(*task, devices, true), totals.risky)) {
        emit logMessage(QStringLiteral("任务中心：执行确认被拒绝，未启动"));
        return;
    }

    if (!m_engine)
        m_engine = createEngine();
    if (m_engine->isRunning()) {
        emit logMessage(QStringLiteral("任务中心：已有活跃运行，忽略本次启动"));
        return;
    }

    // 凭据快照：GUI 线程读 ConfigStore，只保留 user + DPAPI 密文；
    // 协调线程仅本地解密、用后由引擎 AuthScopeGuard 擦除，绝不写回、绝不入日志。
    auto vault = std::make_shared<QHash<QString, QPair<QString, QString>>>();
    for (const auto& profile : devices) {
        for (const auto& endpoint : profile.endpoints) {
            if (endpoint.credentialRef.empty())
                continue;
            const QVariantMap record = ConfigStore::instance().load(
                QStringLiteral("ftp.credential"),
                fromUtf8Std(endpoint.credentialRef));
            if (record.isEmpty())
                continue;
            (*vault)[fromUtf8Std(profile.deviceId)] = qMakePair(
                record.value(QStringLiteral("username")).toString(),
                record.value(QStringLiteral("password")).toString());
            break;
        }
    }

    TaskExecutionRequest request;
    request.task = *task;
    request.devices = devices;
    request.authResolver = [vault](const DeviceProfile& profile) {
        AuthInfo auth;
        const auto it = vault->constFind(fromUtf8Std(profile.deviceId));
        if (it != vault->constEnd()) {
            auth.user = it->first.toUtf8().toStdString();
            if (!it->second.isEmpty())
                auth.password = DpapiCrypto::unprotect(it->second).toUtf8().toStdString();
        }
        return auth;
    };

    // 执行记录 begin：设备名/地址快照在此定格（GUI 线程，单线程写入契约）
    const std::string storeRunId = m_runs->begin(request);
    if (storeRunId.empty()) {
        emit logMessage(QStringLiteral("任务中心：执行记录创建失败，拒绝执行"));
        return;
    }

    ++m_generation;
    const quint64 lineage = m_generation;

    // 引擎回调：协调线程触发 → 一律 QueuedConnection 编组回 GUI 线程再写存储/刷 UI。
    // QPointer 守卫 + 谱系令牌防 stop/restart 后陈旧回调 UAF 与跨轮串写。
    const QPointer<TaskCenterWidget> guard(this);
    auto post = [guard, lineage, this](std::function<void()> task2) {
        QMetaObject::invokeMethod(this, [guard, lineage, task = std::move(task2), this] {
            if (!guard)
                return;   // 组件已析构：丢弃
            if (!m_active || m_active->lineage != lineage)
                return;   // 陈旧谱系：新一轮运行已开启或已收口
            task();
        }, Qt::QueuedConnection);
    };

    TaskExecutionEngine::Callbacks callbacks;
    callbacks.onDeviceStateChanged = [post, this](const std::string& deviceId,
                                                  TaskDeviceState state) {
        const QString id = fromUtf8Std(deviceId);
        post([this, id, state] { handleDeviceStateChanged(id, state); });
    };
    callbacks.onStepFinished = [post, this](const std::string& deviceId,
                                            const TaskStepResult& result) {
        const QString id = fromUtf8Std(deviceId);
        post([this, id, result] { handleStepFinished(id, result); });
    };
    callbacks.onRunFinished = [post, this](const TaskRunOutcome& outcome) {
        TaskRunOutcome snapshot = outcome;   // 值拷贝跨线程投递
        post([this, snapshot] { handleRunFinished(snapshot); });
    };
    callbacks.onLog = [post, this](const std::string& message) {
        const QString text = fromUtf8Std(message);
        post([this, text] { handleLog(text); });
    };

    const std::string engineRunId = m_engine->start(request, callbacks);
    if (engineRunId.empty()) {
        // 理论上已预检 isRunning，不应失败；防御性收口，避免悬挂 running 记录
        m_runs->finish(storeRunId, TaskRunStatus::Failed);
        refreshHistory();
        emit logMessage(QStringLiteral("任务中心：引擎拒绝启动，本轮记录已按失败收口"));
        return;
    }

    installActiveRun(fromUtf8Std(storeRunId), fromUtf8Std(engineRunId), lineage, devices);
    m_lineageByEngineRun[fromUtf8Std(engineRunId)] = RunLineageInfo{request, lineage};
    m_storeToEngineRun[fromUtf8Std(storeRunId)] = fromUtf8Std(engineRunId);
    setRunningUi(true);
    emit runStarted(fromUtf8Std(storeRunId));
    emit logMessage(QStringLiteral("任务中心：执行已启动（%1）")
                        .arg(fromUtf8Std(task->name)));
}

void TaskCenterWidget::installActiveRun(const QString& storeRunId,
                                        const QString& engineRunId,
                                        quint64 lineage,
                                        const std::vector<DeviceProfile>& devices)
{
    ActiveRun active;
    active.storeRunId = storeRunId;
    active.engineRunId = engineRunId;
    active.lineage = lineage;
    for (const auto& profile : devices) {
        TaskDeviceResult result;
        result.deviceId = profile.deviceId;
        result.name = profile.name;
        if (!profile.endpoints.empty()) {
            result.address = profile.endpoints.front().ip + ":"
                + std::to_string(profile.endpoints.front().port);
        }
        result.state = TaskDeviceState::Pending;
        active.devices.insert(fromUtf8Std(profile.deviceId), result);
    }
    m_active = active;

    // 执行状态面板重建（名称+地址行）
    m_stateRows.clear();
    m_deviceStates->clear();
    for (const auto& profile : devices) {
        const QString label = deviceLabel(profile);
        auto* item = new QListWidgetItem(
            QStringLiteral("%1（%2）").arg(label,
                                           taskDeviceStateToString(TaskDeviceState::Pending)),
            m_deviceStates);
        item->setData(Qt::UserRole, label);   // 稳定基名，状态刷新时不重复解析
        m_stateRows.insert(fromUtf8Std(profile.deviceId), item);
    }
    m_overallBar->setRange(0, std::max<int>(1, static_cast<int>(devices.size())));
    m_overallBar->setValue(0);
    m_overallBar->setFormat(QStringLiteral("收口 %v/%m 台"));
}

void TaskCenterWidget::handleDeviceStateChanged(const QString& deviceId,
                                                TaskDeviceState state)
{
    if (!m_active || !m_runs)
        return;
    auto it = m_active->devices.find(deviceId);
    if (it == m_active->devices.end())
        return;
    it->state = state;
    const bool hasStore = !m_active->storeRunId.isEmpty();

    // 首事件登记：快照行由 begin 写入，这里把设备指针就位（appendStepResult 归属前提）
    if (!m_active->registered.contains(deviceId)) {
        m_active->registered.insert(deviceId);
        if (hasStore)
            m_runs->appendDeviceResult(toUtf8(m_active->storeRunId), *it);
    }

    updateDeviceRow(deviceId, state);

    if (isTerminalState(state)) {
        if (hasStore)
            m_runs->appendDeviceResult(toUtf8(m_active->storeRunId), *it);
        ++m_active->terminalCount;
        m_overallBar->setValue(m_active->terminalCount);
    }
}

void TaskCenterWidget::handleStepFinished(const QString& deviceId,
                                          const TaskStepResult& step)
{
    if (!m_active || !m_runs)
        return;
    auto it = m_active->devices.find(deviceId);
    if (it == m_active->devices.end())
        return;
    const bool hasStore = !m_active->storeRunId.isEmpty();
    if (!m_active->registered.contains(deviceId)) {
        m_active->registered.insert(deviceId);
        if (hasStore)
            m_runs->appendDeviceResult(toUtf8(m_active->storeRunId), *it);
    }
    it->steps.push_back(step);
    if (hasStore)
        m_runs->appendStepResult(toUtf8(m_active->storeRunId), step);
}

void TaskCenterWidget::handleRunFinished(TaskRunOutcome outcome)
{
    if (!m_active || !m_runs)
        return;
    const QString storeRunId = m_active->storeRunId;
    const bool hasStore = !storeRunId.isEmpty();
    const TaskRunStatus status = aggregateStatus(outcome);

    // 以引擎快照为准逐台收口（幂等覆盖：包含阶段恢复携带的已成功步骤，
    // 保证历史记录与内存快照一致；错误文本由存储层统一脱敏）
    for (const auto& device : outcome.devices) {
        TaskDeviceResult result;
        result.deviceId = device.deviceId;
        result.name = device.name;
        result.address = device.address;
        result.state = device.state;
        result.steps = device.steps;
        if (hasStore)
            m_runs->appendDeviceResult(toUtf8(storeRunId), result);
        updateDeviceRow(fromUtf8Std(device.deviceId), device.state);
        if (!device.error.empty()) {
            emit logMessage(QStringLiteral("任务中心：[%1] %2")
                                .arg(fromUtf8Std(device.name.empty()
                                                     ? device.deviceId : device.name))
                                .arg(QString::fromStdString(sanitizeTaskErrorText(device.error))));
        }
    }
    if (hasStore)
        m_runs->finish(toUtf8(storeRunId), status);

    m_active.reset();
    setRunningUi(false);
    refreshHistory();
    emit logMessage(QStringLiteral("任务中心：执行收口 — %1")
                        .arg(taskRunStatusToString(status)));
    emit runFinished(storeRunId, status);
}

void TaskCenterWidget::handleLog(const QString& message)
{
    emit logMessage(QStringLiteral("任务中心：%1").arg(message));
}

void TaskCenterWidget::updateDeviceRow(const QString& deviceId, TaskDeviceState state)
{
    const auto it = m_stateRows.constFind(deviceId);
    if (it == m_stateRows.constEnd())
        return;
    const QString base = it.value()->data(Qt::UserRole).toString();
    const QString text = QStringLiteral("%1（%2）").arg(base, taskDeviceStateToString(state));
    it.value()->setText(text);
    // 信号态配色（青绿=成功收口，红=失败告警；中间态/跳过保持默认石墨）
    if (state == TaskDeviceState::Succeeded)
        it.value()->setForeground(QBrush(QColor(QStringLiteral("#40C8A0"))));
    else if (state == TaskDeviceState::Failed)
        it.value()->setForeground(QBrush(QColor(QStringLiteral("#E85848"))));
    else if (state == TaskDeviceState::PartiallySucceeded)
        it.value()->setForeground(QBrush(QColor(QStringLiteral("#F0A030"))));
}

TaskRunStatus TaskCenterWidget::aggregateStatus(const TaskRunOutcome& outcome)
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

void TaskCenterWidget::setRunningUi(bool running)
{
    m_runButton->setEnabled(!running);
    m_cancelButton->setEnabled(running);
    if (!running)
        onHistorySelectionChanged();   // 依据最新历史选择重算重试/导出可用性
}

void TaskCenterWidget::refreshTemplates()
{
    if (!m_templates)
        return;
    const QString filter = m_templateSearch
        ? m_templateSearch->text().trimmed() : QString();
    const QString currentId = m_templateList->currentItem()
        ? m_templateList->currentItem()->data(Qt::UserRole).toString()
        : QString();

    m_templateList->blockSignals(true);
    m_templateList->clear();
    QListWidgetItem* reselect = nullptr;
    for (const auto& task : m_templates->list()) {
        const QString id = fromUtf8Std(task.templateId);
        QStringList haystacks{fromUtf8Std(task.name), fromUtf8Std(task.description), id};
        for (const auto& tag : task.tagSelectors)
            haystacks << fromUtf8Std(tag);
        if (!filter.isEmpty()) {
            bool matched = false;
            for (const QString& text : haystacks) {
                if (text.contains(filter, Qt::CaseInsensitive)) {
                    matched = true;
                    break;
                }
            }
            if (!matched)
                continue;
        }
        auto* item = new QListWidgetItem(
            QStringLiteral("%1（v%2）").arg(fromUtf8Std(task.name)).arg(task.version),
            m_templateList);
        item->setData(Qt::UserRole, id);
        if (id == currentId)
            reselect = item;
    }
    if (!reselect && m_templateList->count() > 0)
        reselect = m_templateList->item(0);
    m_templateList->blockSignals(false);
    if (reselect) {
        m_templateList->setCurrentItem(reselect);
        onTemplateSelected();
    }
}

void TaskCenterWidget::onTemplateSelected()
{
    if (!m_templates)
        return;
    QListWidgetItem* item = m_templateList->currentItem();
    if (!item) {
        m_currentTemplateId.clear();
        m_templateSummaryLabel->setText(QStringLiteral("未选择模板"));
        m_templateSteps->setRowCount(0);
        m_targetDevices->clear();
        m_runButton->setEnabled(false);
        return;
    }
    const QString templateId = item->data(Qt::UserRole).toString();
    const auto task = m_templates->load(templateId.toUtf8().toStdString());
    if (!task) {
        m_currentTemplateId.clear();
        m_templateSummaryLabel->setText(QStringLiteral("模板无法读取"));
        m_templateSteps->setRowCount(0);
        m_targetDevices->clear();
        m_runButton->setEnabled(false);
        return;
    }
    m_currentTemplate = *task;
    m_currentTemplateId = task->templateId;
    m_currentDevices = resolveTargets(*task);

    m_templateSummaryLabel->setText(buildSummary(*task, m_currentDevices, false));

    m_templateSteps->setRowCount(static_cast<int>(task->steps.size()));
    for (int row = 0; row < static_cast<int>(task->steps.size()); ++row) {
        const TaskStep& step = task->steps[row];
        m_templateSteps->setItem(row, 0, new QTableWidgetItem(
            QString::number(row + 1)));
        m_templateSteps->setItem(row, 1, new QTableWidgetItem(
            taskStepTypeToChineseName(step.type)));
        m_templateSteps->setItem(row, 2, new QTableWidgetItem(
            stepParamSummary(step)));
    }

    m_targetDevices->clear();
    for (const auto& profile : m_currentDevices) {
        auto* deviceItem = new QListWidgetItem(deviceLabel(profile), m_targetDevices);
        deviceItem->setData(Qt::UserRole, fromUtf8Std(profile.deviceId));
    }
    m_runButton->setEnabled(!m_currentDevices.empty());
}

void TaskCenterWidget::onExecuteClicked()
{
    if (m_currentTemplateId.empty()) {
        emit logMessage(QStringLiteral("任务中心：请先选择任务模板"));
        return;
    }
    startTemplate(m_currentTemplateId);
}

void TaskCenterWidget::onCancelClicked()
{
    if (!m_engine || !m_engine->isRunning()) {
        emit logMessage(QStringLiteral("任务中心：没有活跃运行可取消"));
        return;
    }
    m_engine->cancel();
    emit logMessage(QStringLiteral("任务中心：已请求取消，将在步骤边界与执行内核检查点收口"));
}

void TaskCenterWidget::onRetryClicked()
{
    if (!m_engine || !m_runs || m_active)
        return;
    const QString storeRunId = selectedHistoryRunId();
    if (storeRunId.isEmpty())
        return;
    const QString engineRunId = m_storeToEngineRun.value(storeRunId);
    if (engineRunId.isEmpty() || !m_lineageByEngineRun.contains(engineRunId)) {
        emit logMessage(QStringLiteral(
            "任务中心：该历史运行无法从失败阶段重试（仅支持本会话内启动的运行）"));
        return;
    }
    if (m_engine->isRunning()) {
        emit logMessage(QStringLiteral("任务中心：已有活跃运行"));
        return;
    }

    const RunLineageInfo lineageInfo = m_lineageByEngineRun.value(engineRunId);
    const std::string newEngineRunId =
        m_engine->retryFailed(engineRunId.toUtf8().toStdString());
    if (newEngineRunId.empty()) {
        emit logMessage(QStringLiteral("任务中心：阶段恢复被引擎拒绝"));
        return;
    }
    const std::string storeRunId2 = m_runs->begin(lineageInfo.request);
    if (storeRunId2.empty())
        emit logMessage(QStringLiteral("任务中心：恢复轮执行记录创建失败（本轮不写历史）"));

    const QString newEngine = fromUtf8Std(newEngineRunId);
    installActiveRun(fromUtf8Std(storeRunId2), newEngine,
                     lineageInfo.lineage, lineageInfo.request.devices);
    m_lineageByEngineRun[newEngine] = lineageInfo;   // 复用同谱系与请求
    if (!storeRunId2.empty())
        m_storeToEngineRun[fromUtf8Std(storeRunId2)] = newEngine;
    setRunningUi(true);
    emit runStarted(fromUtf8Std(storeRunId2));
    emit logMessage(QStringLiteral("任务中心：已从失败阶段恢复执行（跳过已成功步骤/文件）"));
}

QString TaskCenterWidget::selectedHistoryRunId() const
{
    if (!m_runHistory)
        return QString();
    const int row = m_runHistory->currentRow();
    if (row < 0 || row >= m_runHistory->rowCount())
        return QString();
    QTableWidgetItem* item = m_runHistory->item(row, 0);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void TaskCenterWidget::refreshHistory()
{
    if (!m_runs)
        return;
    const QString selected = selectedHistoryRunId();
    const auto records = m_runs->query({});

    m_runHistory->blockSignals(true);
    m_runHistory->setRowCount(0);
    int restoredRow = -1;
    for (std::size_t i = 0; i < records.size(); ++i) {
        const TaskRunRecord& record = records[i];
        const int row = static_cast<int>(i);
        m_runHistory->insertRow(row);
        auto* timeItem = new QTableWidgetItem(
            QDateTime::fromSecsSinceEpoch(static_cast<qint64>(record.startedAt))
                .toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")));
        timeItem->setData(Qt::UserRole, fromUtf8Std(record.runId));
        m_runHistory->setItem(row, 0, timeItem);
        m_runHistory->setItem(row, 1, new QTableWidgetItem(fromUtf8Std(record.templateId)));
        m_runHistory->setItem(row, 2, new QTableWidgetItem(
            taskRunStatusToString(record.status)));
        m_runHistory->setItem(row, 3, new QTableWidgetItem(
            QString::number(record.devices.size())));
        if (fromUtf8Std(record.runId) == selected)
            restoredRow = row;
    }
    m_runHistory->blockSignals(false);
    if (restoredRow >= 0)
        m_runHistory->selectRow(restoredRow);
    onHistorySelectionChanged();
}

void TaskCenterWidget::onHistorySelectionChanged()
{
    const QString storeRunId = selectedHistoryRunId();
    const bool selectable = !storeRunId.isEmpty();
    m_exportButton->setEnabled(selectable);

    bool retryable = false;
    if (selectable && !m_active && m_engine && !m_engine->isRunning()
            && m_storeToEngineRun.contains(storeRunId)) {
        const int row = m_runHistory->currentRow();
        const QString statusText = row >= 0 && m_runHistory->item(row, 2)
            ? m_runHistory->item(row, 2)->text() : QString();
        retryable = statusText == taskRunStatusToString(TaskRunStatus::Failed)
            || statusText == taskRunStatusToString(TaskRunStatus::PartiallySucceeded)
            || statusText == taskRunStatusToString(TaskRunStatus::Cancelled);
    }
    m_retryButton->setEnabled(retryable);
}

void TaskCenterWidget::onExportClicked()
{
    const QString storeRunId = selectedHistoryRunId();
    if (storeRunId.isEmpty())
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出执行报告"),
        QStringLiteral("任务报告-%1.csv").arg(storeRunId.left(12)),
        QStringLiteral("CSV 报告 (*.csv);;HTML 报告 (*.html)"));
    if (path.isEmpty())
        return;
    if (exportRunReport(storeRunId, path))
        emit logMessage(QStringLiteral("任务中心：报告已导出 → %1").arg(path));
    else
        emit logMessage(QStringLiteral("任务中心：报告导出失败"));
}

bool TaskCenterWidget::exportRunReport(const QString& storeRunId, const QString& filePath)
{
    if (!m_runs)
        return false;
    const auto records = m_runs->query({});
    const TaskRunRecord* found = nullptr;
    for (const auto& record : records) {
        if (fromUtf8Std(record.runId) == storeRunId) {
            found = &record;
            break;
        }
    }
    if (!found)
        return false;

    const std::string text = filePath.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive)
        ? renderTaskRunHtml(*found)
        : renderTaskRunCsv(*found);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    QTextStream stream(&file);
    stream << QString::fromUtf8(text.c_str());
    file.flush();
    return true;
}

void TaskCenterWidget::onCloneClicked()
{
    if (!m_templates || m_currentTemplateId.empty()) {
        emit logMessage(QStringLiteral("任务中心：请先选择要复制的模板"));
        return;
    }
    bool ok = false;
    const QString newName = QInputDialog::getText(
        this, QStringLiteral("复制为新模板"), QStringLiteral("新模板名称"),
        QLineEdit::Normal,
        QString::fromUtf8(m_currentTemplate.name.c_str()) + QStringLiteral(" 副本"),
        &ok);
    if (!ok || newName.trimmed().isEmpty())
        return;
    const auto cloned = m_templates->clone(m_currentTemplateId,
                                           newName.trimmed().toUtf8().toStdString());
    if (!cloned) {
        emit logMessage(QStringLiteral("任务中心：模板复制失败（名称为空或存储拒绝）"));
        return;
    }
    emit logMessage(QStringLiteral("任务中心：已复制为新模板「%1」").arg(newName.trimmed()));
    refreshTemplates();
}
