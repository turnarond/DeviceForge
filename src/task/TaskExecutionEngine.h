#pragma once

#include "command/BatchCommandRunner.h"
#include "task/TaskExecutionTypes.h"
#include "transfer/ITransferChannel.h"
#include "transfer/TransferExecutor.h"

#include <QMutex>
#include <QThreadPool>

#include <atomic>
#include <cstddef>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

// v2.11 Task 4：设备任务中心执行引擎。
//
// 架构：worker-owns-coordinator——每次执行由单一协调线程按模板顺序推进
// "设备 × 步骤"（设备间串行、步骤严格有序，避免跨线程竞态；文件部署内部
// 仍由 TransferScheduler 的可靠性内核执行）。绝不绕过既有执行机制：
//   · DeployFiles → TransferScheduler + TransferExecutor + AdapterTransferChannel（复用 v2.9 内核）
//   · RunCommands / Reboot / Recover → BatchCommandRunner（复用 v2.10 内核，
//     重启后断开连接按既有语义视为预期成功 RebootTriggered）
//
// 取消收口：cancel() 只置原子标志，协调线程在步骤边界、传输等待与命令循环
// 检查点收敛；已交付文件与已成功步骤在恢复运行中不重复执行。
//
// 阶段恢复：retryFailed(runId) 依据上一轮快照从每台失败设备的第一个未成功
// 步骤继续；部署步骤内已真实交付的文件按 TaskDeliveredFile 记录过滤跳过；
// 上一轮整体成功的设备标记 Skipped，不再触达。
//
// 步骤参数约定（QVariantMap，全部不含凭据；凭据只经 AuthResolver 执行期注入）：
//   DeployFiles : "items" = [{ "localPath": str, "remotePath": str,
//                              "direction": "upload"|"download"(缺省 upload),
//                              "overwrite": "skip"(缺省覆盖) }, ...]
//               或 "files" = [str 本地路径, ...] + "remoteDirectory" = str（按文件名映射）
//               可选 "protocol" 指定端点协议（如 "ftp"/"sftp"）
//   RunCommands : "commands" = [str, ...]，可选 "protocol"、"timeoutSec"(默认10)、
//               "retryCount"(默认1)
//   Reboot      : 可选 "command"(默认 "reboot") 或 "commands"，协议/超时同上；
//               发送后端点断开（connection closed 类错误）按预期成功
//   Recover     : 同 RunCommands（恢复检查即对设备执行校验命令）
//
// 线程契约：所有回调（Callbacks）都在协调线程上调用；引擎析构会请求取消并
// 等待协调线程收口，回调中不得再调用本引擎的 start/cancel/retryFailed 之外
// 的阻塞操作。密码等秘密值不进入任何回调文本、日志与结果字符串。
class TaskExecutionEngine
{
public:
    // 与 DeployJob::Params::ChannelFactory 相同的注入缝：把既有协议适配器
    // 包装为可靠传输通道。生产默认构造 AdapterTransferChannel。
    using TransferChannelFactory = std::function<std::unique_ptr<ITransferChannel>(
        std::shared_ptr<IProtocolAdapter>)>;

    struct Dependencies {
        BatchCommandRunner::AdapterFactory adapterFactory;   // 命令与部署通道共用的适配器工厂
        TransferChannelFactory transferChannelFactory;       // 缺省：AdapterTransferChannel
        TransferExecutor::Sleeper transferRetrySleeper;      // 测试可注入免等待退避钩子
    };

    struct Callbacks {
        // 设备状态迁移（含中间态与终态），deviceId 为 DeviceProfile 稳定 ID
        std::function<void(const std::string& deviceId, TaskDeviceState state)> onDeviceStateChanged;
        // 每个本轮实际执行的步骤收口后调用；阶段恢复携带的已成功步骤不重复回调，
        // 但会原样保留在 TaskRunOutcome.devices[].steps 中（快照完整性）。
        std::function<void(const std::string& deviceId, const TaskStepResult& result)> onStepFinished;
        // 整轮收口（协调线程最后一次回调）
        std::function<void(const TaskRunOutcome& outcome)> onRunFinished;
        // 中文脱敏日志（不含命令原文与凭据）
        std::function<void(const std::string& message)> onLog;
    };

    explicit TaskExecutionEngine(Dependencies dependencies = {});
    ~TaskExecutionEngine();

    TaskExecutionEngine(const TaskExecutionEngine&) = delete;
    TaskExecutionEngine& operator=(const TaskExecutionEngine&) = delete;

    // 启动一次执行。同一引擎同时只允许一个活跃运行；请求非法或已有运行
    // 时返回空 TaskRunId 并输出中文告警。
    TaskRunId start(const TaskExecutionRequest& request, Callbacks callbacks);

    // 请求取消当前活跃运行（无活跃运行时为空操作）；在步骤边界与执行内核
    // 检查点收敛，已不可逆的提交如实保留。
    void cancel();

    // 阶段恢复：按上一轮快照从失败设备的失败步骤重跑；成功设备跳过。
    // 复用上一轮的请求与回调。无可恢复运行（ID 未知/仍在运行）返回空。
    TaskRunId retryFailed(const TaskRunId& runId);

    bool isRunning() const;

private:
    // 每台设备在本轮的恢复计划（start 时全部为默认值）
    struct DevicePlan {
        std::size_t resumeIndex = 0;                       // 从第几步开始真正执行
        bool skipEntirely = false;                         // 上一轮整体成功 → Skipped
        std::vector<TaskStepResult> carriedSteps;          // [0, resumeIndex) 的原样携带结果
        std::map<std::size_t, std::vector<TaskDeliveredFile>> deliveredByStep;
    };

    struct RunContext {
        TaskRunId runId;
        TaskRunId resumedFromRunId;
        TaskExecutionRequest request;
        Callbacks callbacks;
        std::atomic_bool cancelRequested{false};
        std::vector<DevicePlan> plans;                     // 与 request.devices 对齐
    };

    // 供 retryFailed 使用的一轮完整存档
    struct StoredRun {
        TaskExecutionRequest request;
        Callbacks callbacks;
        TaskRunOutcome outcome;
        std::map<std::string, std::map<std::size_t, std::vector<TaskDeliveredFile>>> delivered;
    };

    void executeRun(const std::shared_ptr<RunContext>& run);
    // 单台设备的完整流水线（在协调线程上执行）；deliveredOut 收集本轮部署步骤
    // 实际交付的文件记录，供下一轮阶段恢复过滤
    TaskDeviceOutcome runDevicePipeline(const std::shared_ptr<RunContext>& run,
                                        std::size_t deviceIndex,
                                        std::map<std::size_t, std::vector<TaskDeliveredFile>>& deliveredOut);
    TaskStepResult runDeployStep(RunContext& run,
                                 const DeviceProfile& profile,
                                 const DeviceEndpoint& endpoint,
                                 const AuthInfo& auth,
                                 const TaskStep& step,
                                 const std::vector<TaskDeliveredFile>& alreadyDelivered,
                                 std::vector<TaskDeliveredFile>& deliveredOut);
    TaskStepResult runCommandStep(RunContext& run,
                                  const DeviceProfile& profile,
                                  const DeviceEndpoint& endpoint,
                                  const AuthInfo& auth,
                                  const TaskStep& step,
                                  bool rebootMode);
    std::size_t resumePrefix(const std::vector<TaskStepResult>& steps) const;

    Dependencies m_deps;
    BatchCommandRunner m_commandRunner;
    QThreadPool m_pool;
    mutable QMutex m_stateMutex;
    std::shared_ptr<RunContext> m_active;                  // 非空表示有活跃运行
    std::map<TaskRunId, StoredRun> m_history;              // 阶段恢复的内存快照（上限截断）
    std::deque<TaskRunId> m_historyOrder;                  // 插入顺序，用于淘汰最旧记录

    static constexpr std::size_t kHistoryLimit = 32;
};
