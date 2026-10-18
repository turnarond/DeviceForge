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
#include "task/TaskRunAggregate.h"
#include "task/TaskRunStore.h"
#include "task/TaskTemplateCodec.h"
#include "task/TaskTemplateEditorDialog.h"
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
    // v2.13 #54 Task 5：搜索行上方小工具条——新建（恒可用）+ 删除（选中启用）
    auto* templateToolBar = new QHBoxLayout();
    m_newTemplateButton = new QPushButton(QStringLiteral("新建模板"), leftBox);
    m_newTemplateButton->setObjectName(QStringLiteral("taskNewTemplateButton"));
    m_deleteTemplateButton = new QPushButton(QStringLiteral("删除模板"), leftBox);
    m_deleteTemplateButton->setObjectName(QStringLiteral("taskDeleteTemplateButton"));
    m_deleteTemplateButton->setEnabled(false);   // 未选中禁用（onTemplateSelected 各路重算）
    templateToolBar->addWidget(m_newTemplateButton);
    templateToolBar->addWidget(m_deleteTemplateButton);
    templateToolBar->addStretch(1);
    leftLayout->addLayout(templateToolBar);
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
    // 双击行 = 编辑（与「编辑模板」按钮同流；未选中行不会触发该信号）
    connect(m_templateList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { onEditTemplateClicked(); });
    connect(m_newTemplateButton, &QPushButton::clicked, this,
            &TaskCenterWidget::onNewTemplateClicked);
    connect(m_deleteTemplateButton, &QPushButton::clicked, this,
            &TaskCenterWidget::onDeleteTemplateClicked);

    // ── 中栏：模板摘要 + 步骤与执行参数 ──
    auto* middleBox = new QGroupBox(QStringLiteral("步骤与执行参数"), splitter);
    middleBox->setObjectName(QStringLiteral("taskTemplateDetailColumn"));
    auto* middleLayout = new QVBoxLayout(middleBox);
    m_templateSummaryLabel = new QLabel(QStringLiteral("未选择模板"), middleBox);
    m_templateSummaryLabel->setObjectName(QStringLiteral("templateSummaryLabel"));
    m_templateSummaryLabel->setWordWrap(true);
    middleLayout->addWidget(m_templateSummaryLabel);
    // v2.13 #54 Task 5：摘要行下方编辑入口（选中启用）
    auto* editRow = new QHBoxLayout();
    m_editTemplateButton = new QPushButton(QStringLiteral("编辑模板"), middleBox);
    m_editTemplateButton->setObjectName(QStringLiteral("taskEditTemplateButton"));
    m_editTemplateButton->setEnabled(false);   // 未选中禁用（onTemplateSelected 各路重算）
    editRow->addWidget(m_editTemplateButton);
    editRow->addStretch(1);
    middleLayout->addLayout(editRow);
    connect(m_editTemplateButton, &QPushButton::clicked, this,
            &TaskCenterWidget::onEditTemplateClicked);
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
    m_exportTemplateButton = new QPushButton(QStringLiteral("导出模板"), rightBox);
    m_exportTemplateButton->setObjectName(QStringLiteral("taskExportTemplateButton"));
    m_exportTemplateButton->setEnabled(false);   // 未选中禁用（onTemplateSelected 各路重算）
    m_importTemplateButton = new QPushButton(QStringLiteral("导入模板"), rightBox);
    m_importTemplateButton->setObjectName(QStringLiteral("taskImportTemplateButton"));
    // 导入恒可用（规格 §4：无选中前提，文件来源在对话框里挑）
    buttonRow->addWidget(m_runButton);
    buttonRow->addWidget(m_cancelButton);
    buttonRow->addWidget(m_retryButton);
    buttonRow->addWidget(m_exportButton);
    buttonRow->addWidget(m_cloneButton);
    buttonRow->addWidget(m_exportTemplateButton);
    buttonRow->addWidget(m_importTemplateButton);
    rightLayout->addLayout(buttonRow);
    connect(m_runButton, &QPushButton::clicked, this, &TaskCenterWidget::onExecuteClicked);
    connect(m_cancelButton, &QPushButton::clicked, this, &TaskCenterWidget::onCancelClicked);
    connect(m_retryButton, &QPushButton::clicked, this, &TaskCenterWidget::onRetryClicked);
    connect(m_exportButton, &QPushButton::clicked, this, &TaskCenterWidget::onExportClicked);
    connect(m_cloneButton, &QPushButton::clicked, this, &TaskCenterWidget::onCloneClicked);
    connect(m_exportTemplateButton, &QPushButton::clicked, this,
            &TaskCenterWidget::onExportTemplateClicked);
    connect(m_importTemplateButton, &QPushButton::clicked, this,
            &TaskCenterWidget::onImportTemplateClicked);

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

// v2.14 冒烟修复：见头文件注释——页显刷新，外部落库模板立即可见。
void TaskCenterWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refreshTemplates();
}

void TaskCenterWidget::setRunConfirmer(RunConfirmer confirmer)
{
    m_confirmer = std::move(confirmer);
}

void TaskCenterWidget::setImportConflictResolver(ImportConflictResolver resolver)
{
    m_importConflictResolver = std::move(resolver);
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

    // 终审 Small(a)：活跃运行检查必须先于风险确认对话框——已有运行在跑时直接
    // 拒绝启动，不应先要求操作员确认一份永远不会执行的摘要
    if (!m_engine)
        m_engine = createEngine();
    if (m_engine->isRunning()) {
        emit logMessage(QStringLiteral("任务中心：已有活跃运行，忽略本次启动"));
        return;
    }

    if (!confirmRun(buildSummary(*task, devices, true), totals.risky)) {
        emit logMessage(QStringLiteral("任务中心：执行确认被拒绝，未启动"));
        return;
    }

    // 凭据快照：GUI 线程读 ConfigStore，只保留 user + DPAPI 密文；
    // 协调线程仅本地解密、用后由引擎 AuthScopeGuard 擦除，绝不写回、绝不入日志。
    auto vault = std::make_shared<QHash<QString, QPair<QString, QString>>>();
    for (const auto& profile : devices) {
        // 终审 Important 5：同一档案多协议端点携带不同 credentialRef 时，本轮
        // 只能采用首个可解析引用（per-device AuthResolver 引擎契约的限制）——
        // 显式中文告警，只含设备名与引用数量，绝不输出引用内容
        std::set<std::string> distinctRefs;
        for (const auto& endpoint : profile.endpoints) {
            if (!endpoint.credentialRef.empty())
                distinctRefs.insert(endpoint.credentialRef);
        }
        if (distinctRefs.size() > 1) {
            emit logMessage(QStringLiteral(
                                "任务中心：设备「%1」的端点存在 %2 个不同凭据引用，"
                                "本轮执行仅采用首个可解析引用（跨协议独立凭据暂未支持）")
                                .arg(QString::fromStdString(profile.name.empty()
                                                                ? profile.deviceId
                                                                : profile.name))
                                .arg(static_cast<int>(distinctRefs.size())));
        }
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
    // v2.15 #49-6：收敛规则单源外提 TaskRunAggregate.h，成员入口仅转发
    return taskrun::aggregateRunStatus(outcome);
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
    } else {
        // v2.13 Task 3：空列表复位路——currentItem 已为 null，走 onTemplateSelected 的
        // 失选分支清除陈旧选中态（此前列表清空后 m_currentTemplateId 残留旧值）。
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
        m_exportTemplateButton->setEnabled(false);
        m_editTemplateButton->setEnabled(false);
        m_deleteTemplateButton->setEnabled(false);
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
        m_exportTemplateButton->setEnabled(false);
        m_editTemplateButton->setEnabled(false);
        m_deleteTemplateButton->setEnabled(false);
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
    m_exportTemplateButton->setEnabled(!m_currentTemplateId.empty());
    m_editTemplateButton->setEnabled(!m_currentTemplateId.empty());
    m_deleteTemplateButton->setEnabled(!m_currentTemplateId.empty());
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

QString TaskCenterWidget::retryRejectionMessage(bool lineageKnown, bool engineHasHistory)
{
    if (!lineageKnown)
        return QStringLiteral("任务中心：该历史运行无法从失败阶段重试（仅支持本会话内启动的运行）");
    if (!engineHasHistory)
        return QStringLiteral("任务中心：该运行的引擎历史已淘汰（超出 %1 轮上限），无法阶段恢复")
            .arg(TaskExecutionEngine::kHistoryLimit);
    return QStringLiteral("任务中心：阶段恢复被引擎拒绝（可能有活跃运行）");
}

void TaskCenterWidget::onRetryClicked()
{
    if (!m_engine || !m_runs || m_active)
        return;
    const QString storeRunId = selectedHistoryRunId();
    if (storeRunId.isEmpty())
        return;
    const QString engineRunId = m_storeToEngineRun.value(storeRunId);
    // v2.14 #49-4：先判定 lineage 是否登记（本会话内启动的运行才有）
    const bool lineageKnown =
        !engineRunId.isEmpty() && m_lineageByEngineRun.contains(engineRunId);
    if (!lineageKnown) {
        emit logMessage(retryRejectionMessage(false, false));
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
        // 三分派：lineage 已知却被拒——查引擎存档是否已被 32 轮 FIFO 淘汰，
        // 区分「历史已淘汰（真因）」与「仍有存档但被拒（活跃运行）」两种文案
        const bool engineHasHistory =
            m_engine->hasRunInHistory(engineRunId.toUtf8().toStdString());
        emit logMessage(retryRejectionMessage(true, engineHasHistory));
        if (!engineHasHistory) {
            // 惰性清退：引擎已淘汰该运行存档 → 它永远不可恢复，点击校正时同步
            // 清掉 widget 侧陈旧映射（只清「lineage 已知但已淘汰」这一种；
            // 可恢复运行与本来无 lineage 的运行不受影响）。不做收口时全表清扫：
            // 那等于对引擎 FIFO 淘汰顺序做同构假设。
            m_lineageByEngineRun.remove(engineRunId);
            for (auto it = m_storeToEngineRun.begin(); it != m_storeToEngineRun.end();) {
                if (it.value() == engineRunId)
                    it = m_storeToEngineRun.erase(it);
                else
                    ++it;
            }
        }
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

    const bool asHtml = filePath.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive);
    const std::string text = asHtml ? renderTaskRunHtml(*found)
                                    : renderTaskRunCsv(*found);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    // #34：CSV 前置 UTF-8 BOM（Excel 双击不乱码），HTML 不加
    qint64 headerLen = 0;
    if (!asHtml)
        headerLen = file.write("\xEF\xBB\xBF", 3);
    QTextStream stream(&file);
    stream << QString::fromUtf8(text.c_str());
    stream.flush();
    const bool ok = headerLen == (asHtml ? 0 : 3)
        && stream.status() == QTextStream::Ok && file.flush();
    file.close();
    // #37：写盘不完整/失败时清理半截文件，不留残留
    if (!ok) {
        QFile::remove(filePath);
        return false;
    }
    return true;
}

// ── v2.13 Task 3：模板导出（文件格式契约见 TaskTemplateCodec）──
QString TaskCenterWidget::sanitizedTemplateFileName(const QString& name)
{
    // 清洗口径（规格 §4，计划裁定手工循环更白话）：非法字符 <>:"/\|?* 逐一替换
    // 为下划线 → 首尾点/空格去除 → 补 .json；空结果回退 "task-template.json"。
    static const QString kIllegalChars = QStringLiteral("<>:\"/\\|?*");
    QString cleaned;
    cleaned.reserve(name.size());
    for (const QChar ch : name)
        cleaned.append(kIllegalChars.contains(ch) ? QLatin1Char('_') : ch);
    int start = 0;
    int end = cleaned.size();
    while (start < end && (cleaned.at(start) == QLatin1Char('.') || cleaned.at(start) == QLatin1Char(' ')))
        ++start;
    while (end > start && (cleaned.at(end - 1) == QLatin1Char('.') || cleaned.at(end - 1) == QLatin1Char(' ')))
        --end;
    cleaned = cleaned.mid(start, end - start);
    if (cleaned.isEmpty())
        return QStringLiteral("task-template.json");
    if (cleaned.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))   // #55：防 .json.json 双后缀
        return cleaned;
    return cleaned + QStringLiteral(".json");
}

bool TaskCenterWidget::exportTemplateToFile(const QString& filePath)
{
    if (m_currentTemplateId.empty()) {
        // 未选中属状态保护（按钮已禁用），直调方只拿 false + 日志，不打扰弹窗
        emit logMessage(QStringLiteral("任务中心：未选中模板，无法导出"));
        return false;
    }
    const QByteArray data = encodeTemplateFile(m_currentTemplate);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("导出模板"),
            QStringLiteral("无法写入文件：\n%1\n\n原因：%2").arg(filePath, file.errorString()));
        emit logMessage(QStringLiteral("任务中心：模板导出失败 → %1").arg(filePath));
        return false;
    }
    const qint64 written = file.write(data);
    const bool ok = written == data.size() && file.flush();
    file.close();
    if (!ok) {
        QFile::remove(filePath);   // 不留半截文件（#37 同款收口口径）
        QMessageBox::warning(this, QStringLiteral("导出模板"),
            QStringLiteral("写入不完整，已清理：\n%1").arg(filePath));
        emit logMessage(QStringLiteral("任务中心：模板导出失败 → %1").arg(filePath));
        return false;
    }
    // 日志纪律：只带模板名与文件路径，不含任何凭据内容
    emit logMessage(QStringLiteral("任务中心：模板「%1」已导出 → %2")
        .arg(fromUtf8Std(m_currentTemplate.name), filePath));
    return true;
}

void TaskCenterWidget::onExportTemplateClicked()
{
    if (m_currentTemplateId.empty()) {
        emit logMessage(QStringLiteral("任务中心：请先选择要导出的模板"));
        return;
    }
    QFileDialog dialog(this);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);   // 主题一致 + 测试可见
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilter(QStringLiteral("JSON 模板 (*.json)"));
    dialog.selectFile(sanitizedTemplateFileName(fromUtf8Std(m_currentTemplate.name)));
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QStringList picked = dialog.selectedFiles();
    const QString path = picked.isEmpty() ? QString() : picked.first();
    if (path.isEmpty())
        return;
    // 失败框已由 exportTemplateToFile 弹出；成功弹信息框（规格 §4：含路径）
    if (exportTemplateToFile(path))
        QMessageBox::information(this, QStringLiteral("导出模板"),
                                 QStringLiteral("模板已导出：\n%1").arg(path));
}

void TaskCenterWidget::onImportTemplateClicked()
{
    QFileDialog dialog(this);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);   // 主题一致 + 测试可见
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setNameFilter(QStringLiteral("JSON 模板 (*.json)"));
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QStringList picked = dialog.selectedFiles();
    const QString path = picked.isEmpty() ? QString() : picked.first();
    if (path.isEmpty())
        return;
    // 成功/失败弹窗全部在 importTemplateFromFile 内部（错误分类框 + 成功摘要框），
    // 此处不重复打扰
    importTemplateFromFile(path);
}

bool TaskCenterWidget::importTemplateFromFile(const QString& filePath)
{
    if (!m_templates) {
        QMessageBox::critical(this, QStringLiteral("导入模板"),
                              QStringLiteral("导入失败：模板存储未就绪"));
        emit logMessage(QStringLiteral("任务中心：模板导入失败（存储未就绪）"));
        return false;
    }

    // ① 读文件（规格 §5：不存在/无权限 → 路径 + 系统错误）
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, QStringLiteral("导入模板"),
            QStringLiteral("无法读取文件：\n%1\n\n原因：%2").arg(filePath, file.errorString()));
        emit logMessage(QStringLiteral("任务中心：模板导入失败 → %1").arg(filePath));
        return false;
    }
    const QByteArray data = file.readAll();
    file.close();

    // ② 格式契约四道校验（JSON/魔数/formatVersion/行解码含敏感拒收），reason 为人读中文
    QString reason;
    const auto decoded = decodeTemplateFile(data, &reason);
    if (!decoded) {
        QMessageBox::critical(this, QStringLiteral("导入模板"), reason);
        emit logMessage(QStringLiteral("任务中心：模板导入被拒 → %1").arg(filePath));
        return false;
    }
    TaskTemplate tmpl = *decoded;
    tmpl.templateId.clear();   // 双保险：跨机主键不外溢，新 ID 由 store 生成

    // ③ 同名冲突（大小写敏感精确匹配）；先全量校验后落库，不落半成品（规格 §5）
    const QString importedName = fromUtf8Std(tmpl.name);
    auto hasTemplateName = [this](const QString& name) {
        for (const auto& t : m_templates->list()) {
            if (QString::fromStdString(t.name) == name)
                return true;
        }
        return false;
    };
    std::string existingId;
    int existingVersion = 0;
    for (const auto& t : m_templates->list()) {
        if (QString::fromStdString(t.name) == importedName) {
            existingId = t.templateId;
            existingVersion = t.version;
            break;
        }
    }
    if (!existingId.empty()) {
        int choice = -1;
        if (m_importConflictResolver) {
            choice = m_importConflictResolver(importedName);
        } else {
            // 缺省 resolver：覆盖(Yes)/改名(No)/取消(Cancel) 三按钮。
            // 默认按钮=改名——现场保守，防止回车键误覆盖本机同名模板。
            QMessageBox box(QMessageBox::Question, QStringLiteral("导入模板"),
                QStringLiteral("已存在同名模板「%1」。\n覆盖其内容，还是另存为新名称？")
                    .arg(importedName),
                QMessageBox::NoButton, this);
            box.addButton(QStringLiteral("覆盖"), QMessageBox::YesRole);
            // addButton 实装为 QPushButton（QMessageBox 内部构造），改名按钮为默认按钮
            auto* renameButton = qobject_cast<QPushButton*>(
                box.addButton(QStringLiteral("改名"), QMessageBox::NoRole));
            box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
            box.setDefaultButton(renameButton);
            box.exec();
            const QMessageBox::ButtonRole role = box.buttonRole(box.clickedButton());
            choice = role == QMessageBox::YesRole ? 0
                 : role == QMessageBox::NoRole ? 1 : -1;
        }
        if (choice < 0)
            return false;                    // 取消：静默收场，无弹窗、不落库
        if (choice == 0) {
            tmpl.templateId = existingId;    // 覆盖：赋既有 ID 原位 upsert
            tmpl.version = existingVersion;  // 继承既有版本（store 拒绝降级写入）
        } else {
            for (int n = 2;; ++n) {          // 改名：「名称 (2)」「名称 (3)」…递增找空位，不设上限
                const QString candidate = QStringLiteral("%1 (%2)").arg(importedName).arg(n);
                if (!hasTemplateName(candidate)) {
                    tmpl.name = toUtf8(candidate);
                    break;
                }
            }
        }
    }

    // ④ 落库：store.save 已对无效/降级/敏感 qWarning，这里补人读弹窗（日志不带秘密值）
    if (!m_templates->save(tmpl)) {
        const auto validation = validateTaskTemplate(tmpl);
        QMessageBox::warning(this, QStringLiteral("导入模板"),
            validation.valid
                ? QStringLiteral("导入失败：模板存储写入被拒（详见日志）")
                : QStringLiteral("导入失败：%1").arg(validation.error));
        emit logMessage(QStringLiteral("任务中心：模板导入失败（存储拒绝）→ %1").arg(filePath));
        return false;
    }

    // ⑤ 刷新 + 悬空引用统计（registry 为空=无设备层，不统计、不显示第二行）
    refreshTemplates();
    int missing = 0;
    if (m_registry) {
        for (const auto& id : tmpl.deviceIds) {
            if (!m_registry->find(id))
                ++missing;
        }
    }
    QString summary = QStringLiteral("已导入模板「%1」（%2 个步骤）")
        .arg(fromUtf8Std(tmpl.name))
        .arg(static_cast<int>(tmpl.steps.size()));
    if (missing > 0) {
        // 悬空 deviceIds 原样保留不剥离（已拍板）：建好同端点档案后自动接上
        summary += QStringLiteral("\n引用了 %1 个本机不存在的设备档案，建好同端点档案后自动接上")
                       .arg(missing);
    }
    QMessageBox::information(this, QStringLiteral("导入模板"), summary);
    emit logMessage(summary);
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

// ── v2.13 #54 Task 5：模板新建/编辑/删除接线（编辑器对话框 + 落库编排）──

std::vector<DeviceProfile> TaskCenterWidget::registryDevices() const
{
    // 编辑器设备多选入参：注册表快照；无注册表（测试/无设备层）返回空表
    return m_registry ? m_registry->list() : std::vector<DeviceProfile>{};
}

bool TaskCenterWidget::applyEditorResult(const TaskTemplate& tmpl)
{
    if (!m_templates) {
        emit logMessage(QStringLiteral("任务中心：模板存储未就绪，编辑结果未落库"));
        return false;
    }
    if (!m_templates->save(tmpl)) {
        // store.save 已对无效/降级/敏感 qWarning，这里补人读原因；
        // 错误框刻意非阻塞（open）：本缝供测试直调，用例不布防模态兜底
        const auto validation = validateTaskTemplate(tmpl);
        const QString reason = validation.valid
            ? QStringLiteral("保存失败：模板存储写入被拒（详见日志）")
            : validation.error;
        auto* box = new QMessageBox(QMessageBox::Warning, QStringLiteral("保存模板"),
                                    reason, QMessageBox::Ok, this);
        box->setObjectName(QStringLiteral("taskEditorSaveErrorBox"));
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
        emit logMessage(QStringLiteral("任务中心：模板保存失败 — %1").arg(reason));
        return false;
    }
    // 成功：刷新列表；既有选中/首行回选由 refreshTemplates 保证，
    // 已落库模板（id 非空）再按 id 精确回选一次（best-effort，不打扰失败路径）
    refreshTemplates();
    if (!tmpl.templateId.empty()) {
        const QString wantedId = fromUtf8Std(tmpl.templateId);
        for (int row = 0; row < m_templateList->count(); ++row) {
            if (m_templateList->item(row)->data(Qt::UserRole).toString() == wantedId) {
                m_templateList->setCurrentRow(row);
                break;
            }
        }
    }
    // 日志纪律：只带模板名与步骤数，不含任何凭据内容
    emit logMessage(QStringLiteral("已保存模板「%1」（%2 个步骤）")
                        .arg(fromUtf8Std(tmpl.name))
                        .arg(static_cast<int>(tmpl.steps.size())));
    return true;
}

void TaskCenterWidget::onNewTemplateClicked()
{
    // 新建不依赖选中态（按钮恒可用）；初值为空模板，id 由 store 生成
    TaskTemplateEditorDialog dlg(TaskTemplate{}, registryDevices(), this);
    dlg.setWindowTitle(QStringLiteral("新建任务模板"));
    if (dlg.exec() != QDialog::Accepted)
        return;
    TaskTemplate out;
    if (!dlg.collect(&out))
        return;   // accept() 已被 validate 闸住，此处为防御性收口
    applyEditorResult(out);
}

void TaskCenterWidget::onEditTemplateClicked()
{
    if (!m_templates || m_currentTemplateId.empty()) {
        emit logMessage(QStringLiteral("任务中心：请先选择要编辑的模板"));
        return;
    }
    // 以存储重载版为初值（选中后可能有导入/克隆改动），重载失败兜底内存快照
    const auto reloaded = m_templates->load(m_currentTemplateId);
    const TaskTemplate base = reloaded.value_or(m_currentTemplate);
    TaskTemplateEditorDialog dlg(base, registryDevices(), this);
    dlg.setWindowTitle(QStringLiteral("编辑任务模板"));
    if (dlg.exec() != QDialog::Accepted)
        return;
    TaskTemplate out;
    if (!dlg.collect(&out))
        return;   // 同上，防御性收口
    applyEditorResult(out);
}

void TaskCenterWidget::onDeleteTemplateClicked()
{
    if (!m_templates || m_currentTemplateId.empty()) {
        emit logMessage(QStringLiteral("任务中心：请先选择要删除的模板"));
        return;
    }
    const QString name = fromUtf8Std(m_currentTemplate.name);
    const int stepCount = static_cast<int>(m_currentTemplate.steps.size());
    QMessageBox box(QMessageBox::Question, QStringLiteral("删除模板"),
        QStringLiteral("确定删除模板「%1」（%2 个步骤）？删除不影响历史执行记录。")
            .arg(name)
            .arg(stepCount),
        QMessageBox::Yes | QMessageBox::No, this);
    box.setObjectName(QStringLiteral("taskDeleteConfirmBox"));
    box.setDefaultButton(QMessageBox::No);   // 破坏性操作默认「否」（v2.11 确认框同款纪律）
    if (box.exec() != QMessageBox::Yes) {
        emit logMessage(QStringLiteral("任务中心：删除模板「%1」已取消").arg(name));
        return;
    }
    // remove 语义（Task 1）：空/不存在 → false；存在 → 转发 ConfigStore::remove。
    // false 说明记录已被并行路径移除（或存储异常），中文错误框后刷新对齐 UI。
    if (!m_templates->remove(m_currentTemplateId)) {
        QMessageBox::warning(this, QStringLiteral("删除模板"),
            QStringLiteral("删除失败：模板「%1」不存在或已被移除").arg(name));
        emit logMessage(QStringLiteral("任务中心：删除模板「%1」失败（记录不存在）").arg(name));
        refreshTemplates();
        return;
    }
    emit logMessage(QStringLiteral("已删除模板「%1」（%2 个步骤）").arg(name).arg(stepCount));
    refreshTemplates();
}
