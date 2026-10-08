#pragma once

#include "device/DeviceProfile.h"
#include "framework/DeviceInfo.h"
#include "task/TaskTemplateTypes.h"

#include <QString>

#include <functional>
#include <string>
#include <vector>

// v2.11 Task 4：设备任务中心执行引擎的数据契约。
// 引擎只编排既有 TransferScheduler 与 BatchCommandRunner，不直接连接协议库；
// 凭据仅在执行期经 AuthResolver 解析，绝不写入模板、日志、结果与错误文本。

// 设备级状态机（执行进度的唯一真相源）。
// 中间态 Pending/Validating/Deploying/Commanding/Rebooting/Recovering 表示"正在处于该阶段"，
// 终态 Succeeded/Failed/Cancelled/Skipped/PartiallySucceeded 表示"该阶段/设备的收口结果"。
enum class TaskDeviceState {
    Pending,               // 等待调度
    Validating,            // 端点/参数校验中
    Deploying,             // 文件部署阶段
    Commanding,            // 批量命令阶段
    Rebooting,             // 重启阶段（断开连接视为预期成功）
    Recovering,            // 恢复检查阶段
    Succeeded,             // 全部步骤成功
    Failed,                // 无任何进展的失败
    Cancelled,             // 被取消且无任何已交付进展
    Skipped,               // 上一轮已成功，阶段恢复时整体跳过
    PartiallySucceeded     // 有已交付/已成功进展，但未走完全部步骤
};

// 状态的中文描述（仅用于日志与展示，不进入协议文本）。
inline QString taskDeviceStateToString(TaskDeviceState state)
{
    switch (state) {
    case TaskDeviceState::Pending: return QStringLiteral("等待中");
    case TaskDeviceState::Validating: return QStringLiteral("校验中");
    case TaskDeviceState::Deploying: return QStringLiteral("部署文件");
    case TaskDeviceState::Commanding: return QStringLiteral("执行命令");
    case TaskDeviceState::Rebooting: return QStringLiteral("重启设备");
    case TaskDeviceState::Recovering: return QStringLiteral("恢复检查");
    case TaskDeviceState::Succeeded: return QStringLiteral("成功");
    case TaskDeviceState::Failed: return QStringLiteral("失败");
    case TaskDeviceState::Cancelled: return QStringLiteral("已取消");
    case TaskDeviceState::Skipped: return QStringLiteral("已跳过");
    case TaskDeviceState::PartiallySucceeded: return QStringLiteral("部分成功");
    }
    return QStringLiteral("未知状态");
}

using TaskRunId = std::string;

// 凭据只在执行期按设备档案解析（一次一取，用后 AuthInfo::clear 擦除）。
using AuthResolver = std::function<AuthInfo(const DeviceProfile&)>;

// 一次执行请求：模板 + 已解析的目标设备档案 + 凭据解析器。
// devices 为 Task 6 UI 依据 template.deviceIds/tagSelectors 解析出的最终设备列表。
struct TaskExecutionRequest {
    TaskTemplate task;
    std::vector<DeviceProfile> devices;
    AuthResolver authResolver;
};

// 单步骤结果。state 取终态语义：Succeeded/Failed/Cancelled/PartiallySucceeded；
// attempt 为本步骤实际执行尝试数（含重试，0 表示无需执行）。
struct TaskStepResult {
    TaskStepType type = TaskStepType::DeployFiles;
    TaskDeviceState state = TaskDeviceState::Pending;
    int attempt = 0;
    std::string error;
};

inline QString taskStepTypeToChineseName(TaskStepType type)
{
    switch (type) {
    case TaskStepType::DeployFiles: return QStringLiteral("文件部署");
    case TaskStepType::RunCommands: return QStringLiteral("批量命令");
    case TaskStepType::Reboot: return QStringLiteral("重启");
    case TaskStepType::Recover: return QStringLiteral("恢复检查");
    }
    return QStringLiteral("未知步骤");
}

// 部署步骤内已真实交付的文件记录（文件级阶段恢复的依据）。
// 与 DeployDeliveredFile 语义对齐但归属任务层，避免跨 Tool 耦合。
struct TaskDeliveredFile {
    std::string localPath;
    std::string remotePath;
    bool nonAtomic = false;
};

// 设备级执行结果；steps 按模板顺序记录（阶段恢复时前缀为上一轮已成功结果的原样携带）。
struct TaskDeviceOutcome {
    std::string deviceId;
    std::string name;
    std::string address;                    // "ip:port" 展示用地址（首个端点）
    TaskDeviceState state = TaskDeviceState::Pending;
    std::string error;                      // 校验失败等设备级错误（脱敏）
    std::vector<TaskStepResult> steps;
};

// 一轮执行的完整快照（Task 5 将持久化为不可变执行记录）。
struct TaskRunOutcome {
    TaskRunId runId;
    TaskRunId resumedFromRunId;             // 非空表示由阶段恢复发起
    std::string templateId;
    int templateVersion = 1;
    std::vector<TaskDeviceOutcome> devices; // 与 request.devices 顺序对齐
};
