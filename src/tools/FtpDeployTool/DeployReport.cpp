// DeployReport.cpp — 部署报告渲染实现：手写字符串拼接，不引第三方 JSON/HTML 库
//
// 契约（tests/deploy/tst_deploy_report.cpp 锁定）：
//   · CSV 列顺序固定：device,result,failed_files,last_error,duration_ms,started_at；
//     字段含 , " 或换行时包裹双引号且内部 " 翻倍；失败文件分号连接且整体引号包裹；
//   · HTML 为极简打印友好黑白表格（评审决议）：仅内联样式、无外部资源、<>& 转义，
//     结果单元格按状态携带 class="ok|failed|cancelled"。

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
        "device,result,failed_files,last_error,duration_ms,started_at\n";

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
           "<th>last_error</th><th>duration_ms</th><th>started_at</th></tr>\n";

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
        out += "</tr>\n";
    }

    out += "</table>\n"
           "</body>\n"
           "</html>\n";
    return out;
}
