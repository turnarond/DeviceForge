// DeployReport.h — v2.8 并行批量部署报告：结果聚合数据结构与 CSV/HTML 渲染纯函数
//
// 设计要点（docs/03-设计/方案设计/2026-08-22-v2.8-并行批量部署设计.md §4.5）：
//   · 纯逻辑零依赖：仅标准库 + 任务层纯数据类型头（TaskRunTypes.h，无网络/适配器引用），
//     便于 tests/deploy 直测；
//   · DeploymentRunner 聚合各 DeployJob 的 DeviceResult 后交由本处渲染：
//     CSV 供 Excel 归档，HTML 为极简打印友好黑白表格（评审决议），无外部资源。
//
// v2.11 Task 5 附加（既有 API 一字不改）：消费任务执行记录 TaskRunRecord，
// 输出设备任务报告行——name/address 取执行期不可变设备快照，
// steps 为逐步结果摘要（错误文本已由 TaskRunStore 落库前脱敏）。

#pragma once

#include "task/TaskRunTypes.h"

#include <ctime>
#include <string>
#include <vector>

struct DeployDeliveredFile {
    std::string localPath;
    std::string remotePath;
    bool nonAtomic = false;
};

struct DeviceResult {
    enum State { Ok, Failed, Cancelled };

    std::string deviceKey;                  // ip:port
    State state = Ok;
    std::vector<std::string> failedFiles;   // 失败文件相对路径
    std::string lastError;                  // 适配器错误摘要
    long long durationMs = 0;
    std::time_t startedAt = 0;
    bool nonAtomic = false;
    std::vector<DeployDeliveredFile> deliveredFiles;
};

struct DeployReport {
    std::string protocol;
    int concurrency = 1;
    std::vector<DeviceResult> results;
};

// 结果态 → 稳定小写令牌（CSV 列值与 HTML class 同源）：ok / failed / cancelled
std::string deviceStateToken(DeviceResult::State state);

// 统一列顺序：device,result,failed_files,last_error,duration_ms,started_at,non_atomic
std::string renderReportCsv(const DeployReport& report);
std::string renderReportHtml(const DeployReport& report);

// —— v2.11 Task 5 附加 API：设备任务执行记录 → 报告数据 ——

// 单设备逐步结果摘要（报告 steps 列）："deploy_files:succeeded;run_commands:failed"，
// 携带尝试次数时追加 "(attempt N)"（N>1）。
std::string taskStepOutcomeSummary(const TaskDeviceResult& device);

// 单设备错误摘要（报告 error 列）：各步骤非空脱敏错误按 "; " 连接。
std::string taskDeviceErrorSummary(const TaskDeviceResult& device);

// 任务报告统一列顺序：name,address,device,result,steps,error
// （name/address 为执行期设备快照，结果令牌与 HTML class 同源）。
std::string renderTaskRunCsv(const TaskRunRecord& record);
std::string renderTaskRunHtml(const TaskRunRecord& record);
