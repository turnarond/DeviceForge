#pragma once
#include "task/TaskTemplateTypes.h"
#include <QVariantMap>
#include <optional>

#include <algorithm>
#include <cctype>
#include <string>

// 行编解码（自 TaskTemplateStore 匿名命名空间外提，字段口径唯一来源）
QVariantMap encodeTaskTemplate(const TaskTemplate& tmpl);
std::optional<TaskTemplate> decodeTaskTemplate(const QVariantMap& row, QString* reason);
bool containsSensitiveKey(const QVariant& value, QString* sensitiveKey);

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
