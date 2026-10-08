#pragma once

#include <QString>
#include <QVariantMap>

#include <optional>
#include <string>
#include <vector>

// v2.11 Task 3：设备任务中心的模板数据模型。
// 模板只描述“对哪些设备、按什么顺序做哪些步骤”，绝不保存凭据秘密值；
// 密码/私钥只由 ConfigStore/DPAPI 独立管理，模板中至多出现引用（如 credentialRef）。

// 步骤类型：文件部署 / 批量命令 / 重启 / 恢复检查
enum class TaskStepType {
    DeployFiles,
    RunCommands,
    Reboot,
    Recover
};

// 单个有序步骤：类型 + 协议无关参数（路径、命令、超时等，不含任何秘密值）
struct TaskStep {
    TaskStepType type = TaskStepType::DeployFiles;
    QVariantMap parameters;
};

// 可复用任务模板。deviceIds 引用 DeviceProfile 的稳定 deviceId，
// 不在此处重新定义设备身份。
struct TaskTemplate {
    std::string templateId;   // 空串表示新模板，save 时生成稳定唯一 ID
    std::string name;
    std::string description;
    int version = 1;
    std::vector<std::string> deviceIds;
    std::vector<std::string> tagSelectors;
    std::vector<TaskStep> steps;    // 有序，执行引擎按此顺序编排
    QVariantMap unknownFields;      // 前向兼容：加载时保留的未知未来字段，保存时原样写回
};

// 步骤类型与序列化字符串的双向映射（持久化用）。
inline QString taskStepTypeToString(TaskStepType type)
{
    switch (type) {
    case TaskStepType::DeployFiles: return QStringLiteral("deploy_files");
    case TaskStepType::RunCommands: return QStringLiteral("run_commands");
    case TaskStepType::Reboot:      return QStringLiteral("reboot");
    case TaskStepType::Recover:     return QStringLiteral("recover");
    }
    return QString();
}

inline std::optional<TaskStepType> taskStepTypeFromString(const QString& value)
{
    if (value == QStringLiteral("deploy_files")) return TaskStepType::DeployFiles;
    if (value == QStringLiteral("run_commands")) return TaskStepType::RunCommands;
    if (value == QStringLiteral("reboot")) return TaskStepType::Reboot;
    if (value == QStringLiteral("recover")) return TaskStepType::Recover;
    return std::nullopt;
}
