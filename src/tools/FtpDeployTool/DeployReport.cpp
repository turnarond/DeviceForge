// DeployReport.cpp — 部署报告渲染实现：手写字符串拼接，不引第三方 JSON/HTML 库
//
// 契约（tests/deploy/tst_deploy_report.cpp 锁定）：
//   · CSV 列顺序固定：device,result,failed_files,last_error,duration_ms,started_at；
//     字段含 , " 或换行时包裹双引号且内部 " 翻倍；失败文件分号连接且整体引号包裹；
//   · HTML 为极简打印友好黑白表格（评审决议）：仅内联样式、无外部资源、<>& 转义，
//     结果单元格按状态携带 class="ok|failed|cancelled"。
//   · v2.11 Task 5 附加（tests/task/tst_task_run_store.cpp 锁定）：任务执行记录
//     报告行 renderTaskRunCsv/renderTaskRunHtml，列 name,address,device,result,steps,error
//     （name/address 为执行期设备快照），转义契约与既有报告一致。

#include "DeployReport.h"

#include <ctime>

namespace {

// --- CSV 转义 ---------------------------------------------------------------

std::string csvQuote(const std::string& value)
{
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for (char c : value) {
        if (c == '"') {
            out.push_back('"');
            out.push_back('"');
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

// 字段含 , " 或换行时包裹双引号且内部 " 翻倍，否则原样返回
std::string csvEscape(const std::string& value)
{
    const bool needQuote =
        value.find_first_of(",\"\n\r") != std::string::npos;
    return needQuote ? csvQuote(value) : value;
}

// 失败文件清单 → 单一 CSV 字段：分号连接且整体引号包裹（含内部 " 翻倍）
std::string csvJoinFailedFiles(const std::vector<std::string>& files)
{
    if (files.empty())
        return {};

    std::string joined;
    for (size_t i = 0; i < files.size(); ++i) {
        if (i > 0)
            joined.push_back(';');
        joined += files[i];
    }
    return csvQuote(joined);
}

// 时间列：本地时间 yyyy-MM-dd HH:mm:ss；未开始（0）留空，不虚构时间
std::string formatTimestamp(std::time_t t)
{
    if (t <= 0)
        return {};

    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif

    char buf[32] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmBuf);
    return buf;
}

void appendCsvRecord(std::string& out, const DeviceResult& r)
{
    out += csvEscape(r.deviceKey);
    out.push_back(',');
    out += csvEscape(deviceStateToken(r.state));
    out.push_back(',');
    out += csvJoinFailedFiles(r.failedFiles);
    out.push_back(',');
    out += csvEscape(r.lastError);
    out.push_back(',');
    out += std::to_string(r.durationMs);
    out.push_back(',');
    out += csvEscape(formatTimestamp(r.startedAt));
    out += r.nonAtomic ? ",true" : ",false";
    out.push_back('\n');
}

// --- HTML 转义 --------------------------------------------------------------

// 仅转义文本节点三字符 <>&（属性值为固定类名，无注入面）
std::string htmlEscape(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        switch (c) {
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '&': out += "&amp;"; break;
        default:  out.push_back(c); break;
        }
    }
    return out;
}

// 多行摘要（如适配器错误）：转义后换行渲染为 <br/>
std::string htmlMultiline(const std::string& value)
{
    const std::string escaped = htmlEscape(value);
    std::string out;
    out.reserve(escaped.size());
    for (char c : escaped) {
        if (c == '\n')
            out += "<br/>";
        else
            out.push_back(c);
    }
    return out;
}

} // namespace

std::string deviceStateToken(DeviceResult::State state)
{
    switch (state) {
    case DeviceResult::Ok:        return "ok";
    case DeviceResult::Failed:    return "failed";
    case DeviceResult::Cancelled: return "cancelled";
    }
    return "failed"; // 兜底未知枚举值，按失败处理
}

std::string renderReportCsv(const DeployReport& report)
{
    static const char* kHeader =
        "device,result,failed_files,last_error,duration_ms,started_at,non_atomic\n";

    std::string out(kHeader);
    for (const auto& r : report.results)
        appendCsvRecord(out, r);
    return out;
}

std::string renderReportHtml(const DeployReport& report)
{
    // 极简打印友好黑白表格：无外部资源、无脚本；状态差异以文字令牌 +
    // 单元格 class 表达（failed 加粗、cancelled 灰阶），打印时天然可读
    std::string out;
    out += "<!DOCTYPE html>\n"
           "<html>\n"
           "<head>\n"
           "<meta charset=\"utf-8\">\n"
           "<title>部署报告</title>\n"
           "<style>\n"
           "body{font-family:\"Microsoft YaHei\",sans-serif;color:#000;background:#fff;margin:16px}\n"
           "table{border-collapse:collapse;font-size:13px}\n"
           "th,td{border:1px solid #000;padding:2px 8px;text-align:left}\n"
           "th{background:#eee}\n"
           "td.failed{font-weight:bold}\n"
           "td.cancelled{color:#666}\n"
           "@media print{body{margin:0}}\n"
           "</style>\n"
           "</head>\n"
           "<body>\n";
    out += "<p>protocol: " + htmlEscape(report.protocol)
         + " | concurrency: " + std::to_string(report.concurrency) + "</p>\n";
    out += "<table>\n"
           "<tr><th>device</th><th>result</th><th>failed_files</th>"
           "<th>last_error</th><th>duration_ms</th><th>started_at</th><th>non_atomic</th></tr>\n";

    for (const auto& r : report.results) {
        const std::string token = deviceStateToken(r.state);
        out += "<tr>";
        out += "<td>" + htmlEscape(r.deviceKey) + "</td>";
        out += "<td class=\"" + token + "\">" + token + "</td>";
        out += "<td>";
        for (size_t i = 0; i < r.failedFiles.size(); ++i) {
            if (i > 0)
                out.push_back(';');
            out += htmlEscape(r.failedFiles[i]);
        }
        out += "</td>";
        out += "<td>" + htmlMultiline(r.lastError) + "</td>";
        out += "<td>" + std::to_string(r.durationMs) + "</td>";
        out += "<td>" + htmlEscape(formatTimestamp(r.startedAt)) + "</td>";
        out += r.nonAtomic ? "<td>true</td>" : "<td>false</td>";
        out += "</tr>\n";
    }

    out += "</table>\n"
           "</body>\n"
           "</html>\n";
    return out;
}

// —— v2.11 Task 5 附加 API：设备任务执行记录 → 报告数据（既有 API 不变） ——

std::string taskStepOutcomeSummary(const TaskDeviceResult& device)
{
    // 报告 steps 列："deploy_files:succeeded;run_commands:failed (attempt 2)"
    std::string out;
    for (std::size_t i = 0; i < device.steps.size(); ++i) {
        const TaskStepResult& step = device.steps[i];
        if (i > 0)
            out.push_back(';');
        const std::string typeToken = taskStepTypeToken(step.type);
        out += typeToken.empty() ? "unknown_step" : typeToken;
        out.push_back(':');
        const std::string stateToken = taskDeviceStateToken(step.state);
        out += stateToken.empty() ? "unknown" : stateToken;
        if (step.attempt > 1)
            out += " (attempt " + std::to_string(step.attempt) + ")";
    }
    return out;
}

std::string taskDeviceErrorSummary(const TaskDeviceResult& device)
{
    // 报告 error 列：各步骤非空（已脱敏）错误摘要按 "; " 连接
    std::string out;
    for (const auto& step : device.steps) {
        if (step.error.empty())
            continue;
        if (!out.empty())
            out += "; ";
        out += step.error;
    }
    return out;
}

std::string renderTaskRunCsv(const TaskRunRecord& record)
{
    // 统一列顺序：name,address,device,result,steps,error
    // （name/address 为执行期不可变设备快照——设备改名后历史报告仍显示当时值）
    static const char* kHeader = "name,address,device,result,steps,error\n";

    std::string out(kHeader);
    for (const auto& device : record.devices) {
        std::string stateToken = taskDeviceStateToken(device.state);
        if (stateToken.empty())
            stateToken = "unknown";
        out += csvEscape(device.name);
        out.push_back(',');
        out += csvEscape(device.address);
        out.push_back(',');
        out += csvEscape(device.deviceId);
        out.push_back(',');
        out += csvEscape(stateToken);
        out.push_back(',');
        out += csvEscape(taskStepOutcomeSummary(device));
        out.push_back(',');
        out += csvEscape(taskDeviceErrorSummary(device));
        out.push_back('\n');
    }
    return out;
}

std::string renderTaskRunHtml(const TaskRunRecord& record)
{
    // 与部署报告同款极简打印友好黑白表格：仅内联样式、无外部资源、<>& 转义
    std::string statusToken = taskRunStatusToken(record.status);
    if (statusToken.empty())
        statusToken = "unknown";

    std::string out;
    out += "<!DOCTYPE html>\n"
           "<html>\n"
           "<head>\n"
           "<meta charset=\"utf-8\">\n"
           "<title>设备任务执行报告</title>\n"
           "<style>\n"
           "body{font-family:\"Microsoft YaHei\",sans-serif;color:#000;background:#fff;margin:16px}\n"
           "table{border-collapse:collapse;font-size:13px}\n"
           "th,td{border:1px solid #000;padding:2px 8px;text-align:left}\n"
           "th{background:#eee}\n"
           "td.failed,td.cancelled{color:#666}\n"
           "@media print{body{margin:0}}\n"
           "</style>\n"
           "</head>\n"
           "<body>\n";
    out += "<p>run: " + htmlEscape(record.runId)
         + " | template: " + htmlEscape(record.templateId)
         + " v" + std::to_string(record.templateVersion)
         + " | status: " + htmlEscape(statusToken) + "</p>\n";
    out += "<p>started_at: " + htmlEscape(formatTimestamp(record.startedAt))
         + " | finished_at: " + htmlEscape(formatTimestamp(record.finishedAt))
         + " | operator: " + htmlEscape(record.operatorName)
         + " | software: " + htmlEscape(record.softwareVersion) + "</p>\n";
    out += "<table>\n"
           "<tr><th>name</th><th>address</th><th>device</th><th>result</th>"
           "<th>steps</th><th>error</th></tr>\n";

    for (const auto& device : record.devices) {
        std::string stateToken = taskDeviceStateToken(device.state);
        if (stateToken.empty())
            stateToken = "unknown";
        out += "<tr>";
        out += "<td>" + htmlEscape(device.name) + "</td>";
        out += "<td>" + htmlEscape(device.address) + "</td>";
        out += "<td>" + htmlEscape(device.deviceId) + "</td>";
        out += "<td class=\"" + stateToken + "\">" + stateToken + "</td>";
        out += "<td>" + htmlEscape(taskStepOutcomeSummary(device)) + "</td>";
        out += "<td>" + htmlMultiline(taskDeviceErrorSummary(device)) + "</td>";
        out += "</tr>\n";
    }

    out += "</table>\n"
           "</body>\n"
           "</html>\n";
    return out;
}
