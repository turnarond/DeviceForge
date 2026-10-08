#pragma once

#include "task/TaskTemplateTypes.h"

#include <QString>

#include <optional>
#include <string>
#include <vector>

// v2.11 Task 3：任务模板 ConfigStore 持久化（记录类型 task.template，key=templateId）。
// 模板不得保存凭据秘密值：save() 拒绝校验不通过的模板且不落盘；
// 加载（load/list）同样拒绝含密码/私钥类字段（含未知未来字段）的记录。
class TaskTemplateStore {
public:
    // 校验并保存模板（按 templateId upsert）。
    // templateId 为空时生成 "template-<uuid>" 唯一 ID；
    // 同 ID 已存在记录时拒绝版本降级（新 version 不得低于已存 version）。
    // 无效模板、降级或写盘失败返回 false。
    bool save(const TaskTemplate&);

    // 按 ID 加载模板；记录缺失、结构损坏或含敏感凭据字段时返回 std::nullopt。
    std::optional<TaskTemplate> load(const std::string& templateId);

    // 克隆为新模板：生成新 ID、name 取 newName、version 重置为 1，
    // 步骤与目标设备深拷贝。源不存在、新名称为空或保存失败返回 std::nullopt。
    std::optional<TaskTemplate> clone(const std::string& templateId, const std::string& newName);

    // 全部可解码模板；损坏或含敏感字段的记录跳过并告警（回退读取不受阻断）。
    std::vector<TaskTemplate> list() const;
};

// 校验结果：valid=false 时 error 为中文失败原因（供日志与 UI 提示）。
struct TaskTemplateValidationResult {
    bool valid = false;
    QString error;
};

// 模板校验：必填字段（名称非空、version>=1、至少一个步骤）与凭据秘密值扫描
// （递归检查各步骤参数与未知未来字段的键名，password/passwd/privkey/
// private_key/key_material/token/secret 等一律拒绝）。
TaskTemplateValidationResult validateTaskTemplate(const TaskTemplate&);
