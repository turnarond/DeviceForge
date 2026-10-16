#pragma once
#include "task/TaskTemplateTypes.h"
#include <QByteArray>
#include <QLatin1String>
#include <QVariantMap>
#include <optional>

#include <algorithm>
#include <cctype>
#include <string>

// 行编解码（自 TaskTemplateStore 匿名命名空间外提，字段口径唯一来源）
QVariantMap encodeTaskTemplate(const TaskTemplate& tmpl);
std::optional<TaskTemplate> decodeTaskTemplate(const QVariantMap& row, QString* reason);
bool containsSensitiveKey(const QVariant& value, QString* sensitiveKey);

// ── Task 2：文件格式契约（跨机模板文件导出/导入，v2.13）──
constexpr QLatin1String kTemplateFileFormat("deviceforge.task-template");
constexpr int kTemplateFileFormatVersion = 1;
// 美化缩进 JSON；template 对象内强制剔除 templateId（跨机主键不外溢）
QByteArray encodeTemplateFile(const TaskTemplate& tmpl);
// 四道校验：JSON 解析 → 魔数 → formatVersion（只认 1，过高给升级文案）→ 行解码（含敏感拒收）
// 返回的模板 templateId 恒为空串（新建/覆盖 ID 归 UI 决定）；失败 reason 为人读中文
std::optional<TaskTemplate> decodeTemplateFile(const QByteArray& data, QString* reason);

// 去除首尾空白。store 的 validateTaskTemplate/save 与 decode 共用同一口径（v2.13 Task 1 裁定：
// 实现随头文件外提，避免私有副本重复逻辑；validate 仍是 store 的门面，不外宣）。
inline std::string trimTaskString(const std::string& value)
{
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    return first >= last ? std::string() : std::string(first, last);
}
