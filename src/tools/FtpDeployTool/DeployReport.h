// DeployReport.h — v2.8 并行批量部署报告：结果聚合数据结构与 CSV/HTML 渲染纯函数
//
// 设计要点（docs/03-设计/方案设计/2026-08-22-v2.8-并行批量部署设计.md §4.5）：
//   · 纯逻辑零依赖：仅标准库，无 Qt/网络/适配器引用，便于 tests/deploy 直测；
//   · DeploymentRunner 聚合各 DeployJob 的 DeviceResult 后交由本处渲染：
//     CSV 供 Excel 归档，HTML 为极简打印友好黑白表格（评审决议），无外部资源。

#pragma once

#include <ctime>
#include <string>
#include <vector>

struct DeviceResult {
    enum State { Ok, Failed, Cancelled };

    std::string deviceKey;                  // ip:port
    State state = Ok;
    std::vector<std::string> failedFiles;   // 失败文件相对路径
    std::string lastError;                  // 适配器错误摘要
    long long durationMs = 0;
    std::time_t startedAt = 0;
};

struct DeployReport {
    std::string protocol;
    int concurrency = 1;
    std::vector<DeviceResult> results;
};

// 结果态 → 稳定小写令牌（CSV 列值与 HTML class 同源）：ok / failed / cancelled
std::string deviceStateToken(DeviceResult::State state);

// 统一列顺序：device,result,failed_files,last_error,duration_ms,started_at
std::string renderReportCsv(const DeployReport& report);
std::string renderReportHtml(const DeployReport& report);
