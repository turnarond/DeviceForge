#pragma once

#include "task/TaskRunTypes.h"

#include <string>
#include <vector>

// v2.11 Task 5：设备任务执行记录持久化（ConfigStore 记录类型
// task.run / task.run.device / task.run.step，模式对齐 TaskTemplateStore）。
//
// 调用面为 TaskExecutionEngine（Task 6/7 集成）预留：
//   begin → （每设备）appendDeviceResult 登记/收口 → appendStepResult 流式追加
//        → finish 整轮收口；query 供历史列表，prune 供保留清理。
//
// 不变式：
//   · 设备快照不可变——名称/地址在 begin 登记时定格，之后的 append 只更新
//     状态与步骤，调用方传入的名称/地址改动不影响历史记录（日志提示差异）；
//   · 凭据与命令原文绝不落库——步骤错误文本写入前一律脱敏；
//   · 运行中的记录永不被 prune 删除。
class TaskRunStore {
public:
    // 开始一轮执行记录：生成 "run-<uuid>" 并写入运行行（status=running、
    // startedAt=当前时间、操作者与软件版本快照）+ 每台设备一条快照行
    // （state=pending、sequence 按请求顺序、address 取首个端点 "ip:port"）。
    // 请求非法（模板 ID 为空、版本 <1、设备缺 ID 或重复）输出中文告警；
    // 任一落盘失败回滚已写入行并返回空 TaskRunId。
    TaskRunId begin(const TaskExecutionRequest& request);

    // 写入/收口一台设备结果：runId 必须存在、deviceId 必须是 begin 登记过的
    // 设备（快照不可变）。steps 整体覆盖该设备既有步骤行（写入前逐条脱敏），
    // 并把本运行"最近写入设备"指针置为该设备（供 appendStepResult 归属）。
    bool appendDeviceResult(const TaskRunId& runId, const TaskDeviceResult& result);

    // 流式追加一个步骤结果：归属本运行最近一次 appendDeviceResult 的设备，
    // 按既有步骤数续编下标；运行不存在或尚无设备指针时告警并返回 false。
    bool appendStepResult(const TaskRunId& runId, const TaskStepResult& step);

    // 收口整轮：status 必须为终态（拒绝 Running），写入 finishedAt。
    // 运行不存在或状态非法返回 false。
    bool finish(const TaskRunId& runId, TaskRunStatus status);

    // 历史查询：按 startedAt 倒序；损坏记录（如未知状态令牌）告警跳过、
    // 不阻断有效记录；筛选条件按 TaskRunFilter 逐条叠加（AND 语义）。
    std::vector<TaskRunRecord> query(const TaskRunFilter& filter) const;

    // 保留清理：删除 startedAt 早于 now - retentionDays 天的终态记录，
    // 级联删除其 task.run.device / task.run.step 行；运行中记录不删。
    // retentionDays <= 0 拒绝执行（防误清全部历史）并返回 0。
    // 返回被清理的运行数。
    int prune(int retentionDays);
};

// 步骤错误文本脱敏（落库前与报告共享的唯一入口）：
// 掩蔽 password/passwd/pwd/secret/token/apikey/private_key 等 "键=值"、
// "键: 值" 形式与 URL userinfo（scheme://user:pass@host → scheme://***@host），
// 超过 512 字节按 UTF-8 边界截断并追加 "(已截断)"。
// 契约：输出不含任何被掩蔽的凭据明文；干净文本原样返回。
std::string sanitizeTaskErrorText(const std::string& text);
