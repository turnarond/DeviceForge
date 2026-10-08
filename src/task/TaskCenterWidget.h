/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: TaskCenterWidget.h
 *
 * Date: 2026-10（v2.11 Task 6）
 *
 * Author: turnarond
 *
 * Description: 设备任务中心三栏 UI — 左栏模板列表/搜索，中栏模板步骤与执行参数
 *              摘要，右栏目标设备、执行状态、历史结果与操作按钮（执行/取消/
 *              从失败阶段重试/导出报告/复制模板）。
 *
 * 依赖边界（规格 §7）：本组件只依赖 DeviceRegistry / TaskTemplateStore /
 * TaskRunStore / TaskExecutionEngine 业务接口，不直接访问任何协议库；逻辑下沉
 * 到引擎与存储层，Widget 仅做展示与输入（项目反模式禁令）。
 *
 * 线程契约（Task 5 评审裁定）：
 *   · TaskRunStore 与 ConfigStore 无内部同步——全部执行记录写入（begin/
 *     appendDeviceResult/appendStepResult/finish）只发生在 GUI 线程：
 *     引擎回调在协调线程触发，一律经 Qt::QueuedConnection 编组回本窗口后
 *     再写存储与刷新 UI；
 *   · 凭据解析快照（user + DPAPI 密文）在 startTemplate 时于 GUI 线程读取，
 *     协调线程只解 DPAPI（CryptUnprotectData 线程安全），不再触碰 ConfigStore；
 *   · 异步回调携带 QPointer 守卫 + 运行谱系令牌（generation），stop/restart 或
 *     新一轮运行开启后的陈旧回调直接丢弃，防 UAF 与跨轮串写。
 */

#pragma once

#include "task/TaskExecutionTypes.h"
#include "task/TaskRunTypes.h"
#include "task/TaskTemplateTypes.h"

#include <QHash>
#include <QSet>
#include <QWidget>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class DeviceRegistry;
class TaskExecutionEngine;
class TaskRunStore;
class TaskTemplateStore;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QPushButton;
class QTableWidget;

class TaskCenterWidget : public QWidget {
    Q_OBJECT

public:
    explicit TaskCenterWidget(QWidget* parent = nullptr);
    ~TaskCenterWidget() override;

    void setRegistry(DeviceRegistry* registry);
    void setStores(TaskTemplateStore* templates, TaskRunStore* runs);

    // 执行前确认钩子：入参为摘要文本与风险标志（含重启等危险动作），返回是否
    // 启动执行。缺省弹出确认对话框（默认按钮=否）；测试与宿主场景可注入。
    using RunConfirmer = std::function<bool (const QString& summary, bool risky)>;
    void setRunConfirmer(RunConfirmer confirmer);

    // 启动一次模板执行：加载模板 → 解析目标设备（deviceIds+tagSelectors）→
    // 摘要/风险确认 → TaskRunStore::begin（GUI 线程）→ 引擎 start。
    // 确认被拒绝或前置校验失败时不启动、不落记录。
    void startTemplate(const std::string& templateId);

    // 导出某轮历史记录为 CSV/HTML 报告（按 filePath 后缀分流），供「导出报告」
    // 按钮与测试直接调用。记录不存在或写盘失败返回 false。
    bool exportRunReport(const QString& storeRunId, const QString& filePath);

signals:
    // 一轮执行（含阶段恢复）已启动，携带 TaskRunStore 记录 ID
    void runStarted(const QString& storeRunId);
    // 一轮执行收口（历史记录已写入终态）
    void runFinished(const QString& storeRunId, TaskRunStatus status);
    // 中文脱敏日志（由宿主窗口路由到全局日志面板）
    void logMessage(const QString& text);

protected:
    // 引擎工厂缝：测试子类可注入带 Fake 依赖的引擎（默认生产配置）
    virtual std::unique_ptr<TaskExecutionEngine> createEngine();

private slots:
    void refreshTemplates();
    void refreshHistory();
    void onTemplateSelected();
    void onExecuteClicked();
    void onCancelClicked();
    void onRetryClicked();
    void onExportClicked();
    void onCloneClicked();
    void onHistorySelectionChanged();

private:
    // 一轮执行在 UI 侧的记账（GUI 线程独占访问）
    struct ActiveRun {
        QString storeRunId;                                   // TaskRunStore 记录 ID
        QString engineRunId;                                  // 引擎运行 ID
        quint64 lineage = 0;                                  // 运行谱系令牌
        QHash<QString, TaskDeviceResult> devices;             // deviceId → 快照+累计步骤
        QSet<QString> registered;                             // 已 appendDeviceResult 登记
        int terminalCount = 0;
    };

    // 运行谱系登记（阶段恢复需要复用原始请求与谱系令牌）
    struct RunLineageInfo {
        TaskExecutionRequest request;
        quint64 lineage = 0;
    };

    void setupUi();
    std::vector<DeviceProfile> resolveTargets(const TaskTemplate& task) const;
    QString buildSummary(const TaskTemplate& task,
                         const std::vector<DeviceProfile>& devices,
                         bool forConfirm) const;
    bool confirmRun(const QString& summary, bool risky);
    void startRun(const TaskTemplate& task, const std::vector<DeviceProfile>& devices);
    void installActiveRun(const QString& storeRunId, const QString& engineRunId,
                          quint64 lineage, const std::vector<DeviceProfile>& devices);
    void handleDeviceStateChanged(const QString& deviceId, TaskDeviceState state);
    void handleStepFinished(const QString& deviceId, const TaskStepResult& step);
    void handleRunFinished(TaskRunOutcome outcome);
    void handleLog(const QString& message);
    void updateDeviceRow(const QString& deviceId, TaskDeviceState state);
    void setRunningUi(bool running);
    QString selectedHistoryRunId() const;
    static TaskRunStatus aggregateStatus(const TaskRunOutcome& outcome);

    DeviceRegistry* m_registry = nullptr;
    TaskTemplateStore* m_templates = nullptr;
    TaskRunStore* m_runs = nullptr;
    std::unique_ptr<TaskExecutionEngine> m_engine;
    RunConfirmer m_confirmer;

    // 代际令牌：每开启新一轮「原始」运行 +1；阶段恢复沿用被恢复运行的谱系号，
    // 陈旧回调（谱系不匹配当前记账轮）一律丢弃
    quint64 m_generation = 0;
    std::optional<ActiveRun> m_active;
    QHash<QString, RunLineageInfo> m_lineageByEngineRun;     // engineRunId → 谱系信息
    QHash<QString, QString> m_storeToEngineRun;             // storeRunId → engineRunId

    // 当前模板上下文
    std::string m_currentTemplateId;
    TaskTemplate m_currentTemplate;
    std::vector<DeviceProfile> m_currentDevices;

    // ── UI 控件 ──
    QLineEdit* m_templateSearch = nullptr;
    QListWidget* m_templateList = nullptr;
    QLabel* m_templateSummaryLabel = nullptr;
    QTableWidget* m_templateSteps = nullptr;
    QListWidget* m_targetDevices = nullptr;
    QListWidget* m_deviceStates = nullptr;
    QProgressBar* m_overallBar = nullptr;
    QTableWidget* m_runHistory = nullptr;
    QPushButton* m_runButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QPushButton* m_retryButton = nullptr;
    QPushButton* m_exportButton = nullptr;
    QPushButton* m_cloneButton = nullptr;
    QHash<QString, QListWidgetItem*> m_stateRows;            // deviceId → 执行状态行
};
