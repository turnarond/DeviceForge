#pragma once

#include "device/DeviceProfile.h"
#include "task/TaskTemplateTypes.h"

#include <QDialog>
#include <QVariantMap>

#include <string>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;

// v2.13 #54 Task 2/3/4：任务模板编辑器对话框。
// 纯「表单 ↔ TaskTemplate」双向映射：不触碰 ConfigStore/DeviceRegistry，
// 设备档案以构造参数 knownDevices 快照注入（只读）。
// Task 3 已落地 deploy 页（teRemoteDir + teItemList 4 列 items 表 + 批量添加）；
// Task 4 已补齐 commands/recover 面板（teCommands 多行 + teTimeout 1..600 + 共享
// teProtocol），未展示步骤的参数原样透传（collect 不丢失）。
// 共享协议过滤下拉 teProtocol 位于参数栈之外（裁决 1）：四种步骤类型读写同一
// parameters["protocol"] 键，绑定当前步。
// deploy 页裁决（控制器）：teRemoteDir 仅是批量添加的 UI 远端前缀辅助输入，
// **不是**持久化参数键；参数只携带 items[]（canonicalDeployParameters 加载即归一）。
// commands/recover 页裁决：run_commands 与 recover 引擎参数键完全一致（commands[]
// +timeoutSec），二者共享同一个命令面板页（栈 index1，pageIndexForType 分派）而非各持
// 一套控件；绑定/写回按当前步类型分派，recover 步在页顶部额外显示一条说明 label
// （teRecoverNote，run_commands 隐藏）。
class TaskTemplateEditorDialog : public QDialog {
public:
    // knownDevices：本机档案快照（目标设备列表 = knownDevices ∪ initial.deviceIds）
    TaskTemplateEditorDialog(const TaskTemplate& initial,
                             const std::vector<DeviceProfile>& knownDevices,
                             QWidget* parent = nullptr);

    // 空串=通过；非空=中文原因（validateTaskTemplate 三查 + 步骤参数预检）
    QString validate() const;

    // validate 非空 → false；否则填充 out（templateId 原样携带；unknownFields 不动）
    bool collect(TaskTemplate* out) const;

    // deploy 简写归一：items 空且 files+remoteDirectory 齐 → 展开为 items[]（规格 §2）
    static QVariantMap canonicalDeployParameters(const QVariantMap& params);

    // 保存按钮（AcceptRole）路径统一闸口：校验不过弹警告停留，不关闭对话框
    void accept() override;

    // v2.15 任务9：目录展开直调缝——把 dirPath 经 folderexpand::expand 展开为
    // 普通文件条目批量入条目表（remotePath=composeRemotePath(teRemoteDir, rel)
    // 镜像预填；目录不以「目录条目」形态存在于表中）。返回成功新增条数；
    // 拒绝（文件系统根退化/超 kMaxExpandedItems 上限）返回 -1 并把纯头中文
    // error 串**逐字**写入 *error（三入口同文案纪律，禁止改写），表格零改动、
    // 停留编辑不丢内容。teAddDir 按钮与自动化用例共用本缝（QFileDialog 选人腿
    // 仍按 #54 先例人工冒烟）。
    int addDeployDirectoryEntries(const QString& dirPath, QString* error);

private:
    // 表单 → 候选模板（collect/validate 共用组装逻辑，只读快照）
    TaskTemplate assemble() const;
    // 步骤行/参数栈/共享控件与 m_steps[m_currentStep] 的绑定（程序化改控件不写回）
    void bindCurrentStep();
    // 用户操作 teProtocol 后即时写回当前步参数（空值=不限 → 移除 protocol 键）
    void flushProtocolFromCombo();
    // 当前步与相邻步交换（模型+视图同步，保序；越界静默拒绝）
    void moveStepRow(int delta);
    // 重建“步骤N · 类型中文名”行文案（增删/移动后保序），并同步按钮可用性
    void refreshStepRows();

    // ── deploy 页（Task 3）──
    // 构建 teRemoteDir/teItemList/teAddFiles/teAddDir 并接线（构造期调用一次）
    void setupDeployPage(QWidget* page);
    // 把参数 items[] 程序化灌入表格（m_binding+blockSignals 双守卫，不触发写回）
    void fillDeployTable(const QVariantMap& params);
    // 表格逐行读回 items[] 写入当前 deploy 步（仅当当前步为 deploy；缺省值写规范串）
    void flushDeployTable();
    // 表格 → items[] 快照（列 0/1 取文本，列 2/3 取下拉 currentData；跳过全空行）
    QVariantList deployItemsFromTable() const;
    // 追加一行（批量添加用；combo 走 currentData 规范串，默认 upload/overwrite）
    void appendDeployRow(const QString& localPath, const QString& remotePath,
                         const QString& direction, const QString& overwrite);

    // ── commands / recover 共用页（Task 4）──
    // run_commands 与 recover 引擎参数键完全一致，共用同一命令面板页（栈 index1）：
    // teCommands(QPlainTextEdit 一行一条) + teTimeout(QSpinBox 1..600 默认 10)；
    // 顶部 teRecoverNote 说明 label 仅在 recover 步可见。textChanged/valueChanged →
    // flushCommandsLike 即时写回当前步（绑定守卫内不触发）。
    void setupCommandsLikePage(QWidget* page);
    // 参数 commands[]/timeoutSec → 编辑器/自旋（m_binding+blockSignals 双守卫，不写回）
    void fillCommandsLike(const QVariantMap& params);
    // 编辑器/自旋 → 当前步 commands[]（逐行 trim 去空）+timeoutSec(int 恒写) 写回；
    // 仅当当前步为 run_commands/recover 时执行（protocol 由共享 combo 单独管理，不动）
    void flushCommandsLike();

    QLineEdit* m_name = nullptr;             // teName
    QLineEdit* m_description = nullptr;      // teDescription
    QLineEdit* m_tags = nullptr;             // teTags（逗号分隔标签，映射 tagSelectors）
    QSpinBox* m_version = nullptr;           // teVersion(1..999)
    QListWidget* m_devices = nullptr;        // teDevices（多选：knownDevices ∪ 悬空引用）
    QListWidget* m_stepList = nullptr;       // teStepList
    QPushButton* m_removeStepButton = nullptr; // teRemoveStep（无当前步时禁用）
    QPushButton* m_stepUpButton = nullptr;     // teStepUp
    QPushButton* m_stepDownButton = nullptr;   // teStepDown
    QComboBox* m_stepType = nullptr;         // teStepType
    QComboBox* m_protocol = nullptr;         // teProtocol（共享，栈外）
    QStackedWidget* m_pages = nullptr;       // 参数页栈：0 deploy / 1 commands+recover 共用 / 2 reboot
    QLineEdit* m_remoteDir = nullptr;        // teRemoteDir（批量添加 UI 前缀，不持久化）
    QTableWidget* m_itemTable = nullptr;     // teItemList（4 列：本地/远端/方向/覆盖，行=items）
    QPushButton* m_addFiles = nullptr;       // teAddFiles「添加文件…」（非原生多选）
    QPushButton* m_addDir = nullptr;         // teAddDir「添加目录…」（非原生目录选择）
    QPushButton* m_removeItem = nullptr;     // teRemoveItem「移除选中行」（v2.14 冒烟修复，选中启用）
    QLabel* m_recoverNote = nullptr;         // teRecoverNote（recover 步可见的顶部说明）
    QPlainTextEdit* m_commandsEditor = nullptr; // teCommands（run_commands/recover 共用，一行一条）
    QSpinBox* m_commandsTimeout = nullptr;   // teTimeout（1..600，默认 10=引擎缺省）

    std::vector<TaskStep> m_steps;           // 内部步骤向量（顺序即模板顺序）
    QVariantMap m_unknownFields;             // 前向兼容未知字段：编辑器不显示、collect 原样携带
    std::string m_templateId;                // 原样携带（新建模板为空串）
    int m_currentStep = -1;                  // 当前绑定步骤下标（-1=无）
    bool m_binding = false;                  // 绑定守卫：程序化改控件不触发写回/换参
    TaskStepType m_nextAddType = TaskStepType::DeployFiles; // teAddStep 轮转：deploy→commands→reboot→recover
};
