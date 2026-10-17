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
 *
 * 凭据解析限制（v2.11 终审 Important 5，绑定引擎 per-device AuthResolver 签名）：
 *   引擎按「设备档案」粒度取一份 AuthInfo——同一档案的多个协议端点若携带不同
 *   credentialRef（如 FTP 与 Telnet 各一套凭据），本轮执行只用首个可解析引用，
 *   其余端点共用该凭据；此时 startTemplate 发出中文告警（仅设备名与引用数量，
 *   不含引用内容）。跨协议独立凭据需引擎签名演进（per-endpoint 解析）后支持。
 *   本限制已于 v2.14 定档为书面契约，见 v2.11 设计文档 §凭据解析降级契约。
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

    // ── v2.13 Task 3：模板导出（供宿主与测试直调，exportRunReport 同款缝）──
    // 当前选中模板导出到 filePath；未选中返回 false（仅记日志，不弹窗）；
    // 写盘失败返回 false 并弹中文错误框；成功返回 true 并记日志
    // （成功信息框由「导出模板」按钮流程弹出，API 本身无弹窗）
    bool exportTemplateToFile(const QString& filePath);
    // 文件名清洗：非法字符 <>:"/\|?* 替换为下划线，首尾点/空格去除；
    // 空结果回退 "task-template.json"
    static QString sanitizedTemplateFileName(const QString& name);

    // ── v2.13 Task 4：模板导入（供宿主与测试直调）──
    // 从 filePath 导入模板；全链任一失败返回 false（不落半成品，内部中文弹窗）。
    // 同名 → conflictResolver(name)：0=覆盖既有(继承其 templateId 与 version)，
    // 1=自动改名"名称 (2)"递增，-1=取消（静默，无弹窗）。返回 true 时已 refreshTemplates()。
    bool importTemplateFromFile(const QString& filePath);
    using ImportConflictResolver = std::function<int (const QString& name)>;
    void setImportConflictResolver(ImportConflictResolver resolver);   // 测试缝，仿 setRunConfirmer

    // ── v2.13 #54 Task 5：编辑器结果落库编排（公共测试缝，仿 export/import 直调口径）──
    // m_templates->save(tmpl)：成功 → refreshTemplates() 并按 id 回选（best-effort）
    // + logMessage「已保存模板「%1」（%2 个步骤）」，返回 true；
    // 存储未就绪或 save 拒绝（无效/降级/敏感）→ 非阻塞中文错误框 + logMessage，
    // 返回 false。错误框刻意非模态：本缝供测试直调，不允许在无事件循环兜底的
    // 用例里阻塞（对话框流内弹窗才走模态 exec）。
    bool applyEditorResult(const TaskTemplate& tmpl);

    // ── v2.14 #49-4：阶段恢复拒绝文案三分派（公共静态纯函数，供直测）──
    // lineageKnown=false：本会话外启动的运行（lineage 从未登记），沿用旧文案；
    // true+!engineHasHistory：引擎 kHistoryLimit 轮 FIFO 已淘汰该运行存档，
    // 说明真因；true+engineHasHistory：存档仍在却被拒（最可能是活跃运行占用）。
    static QString retryRejectionMessage(bool lineageKnown, bool engineHasHistory);

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
    void onExportTemplateClicked();
    void onImportTemplateClicked();
    void onCloneClicked();
    // v2.13 #54 Task 5：模板新建/编辑/删除入口（编辑器对话框 + 落库编排）
    void onNewTemplateClicked();
    void onEditTemplateClicked();
    void onDeleteTemplateClicked();
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
    // v2.13 #54 Task 5：编辑器对话框的设备档案快照入参（无注册表时返回空表）
    std::vector<DeviceProfile> registryDevices() const;
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
    ImportConflictResolver m_importConflictResolver;   // 缺省=Yes/No/Cancel 三按钮 QMessageBox

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
    QPushButton* m_exportTemplateButton = nullptr;
    QPushButton* m_importTemplateButton = nullptr;
    QPushButton* m_cloneButton = nullptr;
    // v2.13 #54 Task 5：模板新建/编辑/删除入口按钮
    QPushButton* m_newTemplateButton = nullptr;
    QPushButton* m_editTemplateButton = nullptr;
    QPushButton* m_deleteTemplateButton = nullptr;
    QHash<QString, QListWidgetItem*> m_stateRows;            // deviceId → 执行状态行
};
