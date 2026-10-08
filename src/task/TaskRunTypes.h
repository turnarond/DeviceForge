#pragma once

#include "task/TaskExecutionTypes.h"

#include <QString>

#include <ctime>
#include <optional>
#include <string>
#include <vector>

// v2.11 Task 5：设备任务中心执行记录数据模型（历史持久化 + 报告数据）。
// 建立在 Task 4 的 TaskRunId/TaskDeviceState/TaskStepResult 之上；
// 记录保存执行期的不可变模板/设备快照（名称、地址在 begin 时定格），
// 设备改名不影响历史记录。错误文本落库前必须脱敏，绝不携带凭据明文。

// 一轮执行的收口结果（Running 为活跃态，其余为终态）。
enum class TaskRunStatus {
    Running,             // 执行中
    Succeeded,           // 全部设备成功
    Failed,              // 无任何设备成功
    Cancelled,           // 被取消
    PartiallySucceeded   // 部分设备成功/部分收口
};

// 状态的中文描述（仅用于日志与展示，不进入持久化令牌）。
inline QString taskRunStatusToString(TaskRunStatus status)
{
    switch (status) {
    case TaskRunStatus::Running: return QStringLiteral("运行中");
    case TaskRunStatus::Succeeded: return QStringLiteral("成功");
    case TaskRunStatus::Failed: return QStringLiteral("失败");
    case TaskRunStatus::Cancelled: return QStringLiteral("已取消");
    case TaskRunStatus::PartiallySucceeded: return QStringLiteral("部分成功");
    }
    return QStringLiteral("未知状态");
}

// 持久化/报告令牌：稳定小写下划线，CSV 列值与 HTML class 同源。
inline std::string taskRunStatusToken(TaskRunStatus status)
{
    switch (status) {
    case TaskRunStatus::Running: return "running";
    case TaskRunStatus::Succeeded: return "succeeded";
    case TaskRunStatus::Failed: return "failed";
    case TaskRunStatus::Cancelled: return "cancelled";
    case TaskRunStatus::PartiallySucceeded: return "partially_succeeded";
    }
    return std::string();
}

inline std::optional<TaskRunStatus> taskRunStatusFromToken(const std::string& token)
{
    if (token == "running") return TaskRunStatus::Running;
    if (token == "succeeded") return TaskRunStatus::Succeeded;
    if (token == "failed") return TaskRunStatus::Failed;
    if (token == "cancelled") return TaskRunStatus::Cancelled;
    if (token == "partially_succeeded") return TaskRunStatus::PartiallySucceeded;
    return std::nullopt;
}

// 设备状态令牌（11 态全量覆盖，持久化用；未知令牌返回空串由调用方拒绝）。
inline std::string taskDeviceStateToken(TaskDeviceState state)
{
    switch (state) {
    case TaskDeviceState::Pending: return "pending";
    case TaskDeviceState::Validating: return "validating";
    case TaskDeviceState::Deploying: return "deploying";
    case TaskDeviceState::Commanding: return "commanding";
    case TaskDeviceState::Rebooting: return "rebooting";
    case TaskDeviceState::Recovering: return "recovering";
    case TaskDeviceState::Succeeded: return "succeeded";
    case TaskDeviceState::Failed: return "failed";
    case TaskDeviceState::Cancelled: return "cancelled";
    case TaskDeviceState::Skipped: return "skipped";
    case TaskDeviceState::PartiallySucceeded: return "partially_succeeded";
    }
    return std::string();
}

inline std::optional<TaskDeviceState> taskDeviceStateFromToken(const std::string& token)
{
    if (token == "pending") return TaskDeviceState::Pending;
    if (token == "validating") return TaskDeviceState::Validating;
    if (token == "deploying") return TaskDeviceState::Deploying;
    if (token == "commanding") return TaskDeviceState::Commanding;
    if (token == "rebooting") return TaskDeviceState::Rebooting;
    if (token == "recovering") return TaskDeviceState::Recovering;
    if (token == "succeeded") return TaskDeviceState::Succeeded;
    if (token == "failed") return TaskDeviceState::Failed;
    if (token == "cancelled") return TaskDeviceState::Cancelled;
    if (token == "skipped") return TaskDeviceState::Skipped;
    if (token == "partially_succeeded") return TaskDeviceState::PartiallySucceeded;
    return std::nullopt;
}

// 步骤类型令牌（复用 Task 3 的持久化字符串，std 包装便于记录层使用）。
inline std::string taskStepTypeToken(TaskStepType type)
{
    return taskStepTypeToString(type).toStdString();
}

inline std::optional<TaskStepType> taskStepTypeFromToken(const std::string& token)
{
    return taskStepTypeFromString(QString::fromStdString(token));
}

// 单台设备的执行结果。name/address 是执行期设备快照（报告展示用，不可变：
// begin 登记后 append 不得覆盖），deviceId 为稳定设备身份。
struct TaskDeviceResult {
    std::string deviceId;
    std::string name;
    std::string address;
    TaskDeviceState state = TaskDeviceState::Pending;
    std::vector<TaskStepResult> steps;
};

// 历史筛选条件；全部为空 = 不过滤。
// templateId 引用 TaskTemplateStore 的稳定模板 ID；
// deviceId 匹配该轮任一设备快照的稳定身份；status 匹配运行收口结果。
struct TaskRunFilter {
    std::optional<std::string> templateId;
    std::optional<std::string> deviceId;
    std::optional<TaskRunStatus> status;
};

// 一轮执行的完整持久化记录（query 的返回单元，报告数据同源）。
struct TaskRunRecord {
    TaskRunId runId;
    std::string templateId;
    int templateVersion = 1;
    TaskRunStatus status = TaskRunStatus::Running;
    std::time_t startedAt = 0;   // epoch 秒；0 表示未记录
    std::time_t finishedAt = 0;  // epoch 秒；0 表示尚未收口
    std::string operatorName;    // 操作者（展示用，不参与筛选）
    std::string softwareVersion; // 执行时的软件版本
    std::vector<TaskDeviceResult> devices; // 按 begin 登记顺序（sequence）
};
