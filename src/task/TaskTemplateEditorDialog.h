#pragma once

#include "device/DeviceProfile.h"
#include "task/TaskTemplateTypes.h"

#include <QDialog>
#include <QVariantMap>

#include <string>
#include <vector>

class QComboBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QStackedWidget;

// v2.13 #54 Task 2：任务模板编辑器对话框骨架。
// 纯「表单 ↔ TaskTemplate」双向映射：不触碰 ConfigStore/DeviceRegistry，
// 设备档案以构造参数 knownDevices 快照注入（只读）。
// Task 3/4 将在右侧参数栈补齐 deploy/commands/recover 面板；本阶段 deploy 与
// recover/commands 页为空白 QWidget，未展示步骤的参数原样透传（collect 不丢失）。
// 共享协议过滤下拉 teProtocol 位于参数栈之外（裁决 1）：四种步骤类型读写同一
// parameters["protocol"] 键，绑定当前步。
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
    QStackedWidget* m_pages = nullptr;       // 参数页栈：0 deploy / 1 commands / 2 reboot / 3 recover

    std::vector<TaskStep> m_steps;           // 内部步骤向量（顺序即模板顺序）
    QVariantMap m_unknownFields;             // 前向兼容未知字段：编辑器不显示、collect 原样携带
    std::string m_templateId;                // 原样携带（新建模板为空串）
    int m_currentStep = -1;                  // 当前绑定步骤下标（-1=无）
    bool m_binding = false;                  // 绑定守卫：程序化改控件不触发写回/换参
    TaskStepType m_nextAddType = TaskStepType::DeployFiles; // teAddStep 轮转：deploy→commands→reboot→recover
};
