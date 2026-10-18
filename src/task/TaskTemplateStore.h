#pragma once

#include "task/TaskTemplateTypes.h"

#include <QString>

#include <functional>
#include <optional>
#include <string>
#include <vector>

// v2.14 #49-2 第二腿：同名冲突落库的处置选择（任务 3 接线 lambda 消费）。
enum class NameConflictChoice { Overwrite, Rename, Cancel };

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

    // 删除模板：仅移除 task.template 记录；执行历史为不可变快照不受影响（#54）。
    // 空 templateId 直接 false 不碰库；记录不存在返回 false；
    // 存在时转发 ConfigStore::remove 结果。
    bool remove(const std::string& templateId);

    // v2.14 #49-2 第二腿：同名冲突落库。入站 name 与既有模板全等（std::string 全等，
    // 大小写敏感）视为冲突；无冲突直接 save（templateId 为空则生成新 ID）。
    // 冲突时由 onConflict(name) 决定处置：
    //   Cancel：返回 false、reason 写入含「取消」的中文原因，不落盘；
    //   Overwrite：继承既有 templateId 且 version 取 max(既有, 入站)（save 的降级禁则同源）；
    //   Rename：「名称 (2)」从 2 递增找空位后另存新 ID——v2.15（E）起导入路径
    //     （TaskCenterWidget::importTemplateFromFile）与部署页「保存为模板」均经本函数，
    //     改名口径由本函数单源，widget 侧不再有独立实现。
    // 成功时 savedId 写入落库模板 ID（可传 nullptr 忽略）；
    // 失败时 reason 写入中文原因（可传 nullptr 忽略）。
    bool saveUniqueByName(const TaskTemplate& tmpl,
                          const std::function<NameConflictChoice(const QString& name)>& onConflict,
                          std::string* savedId, QString* reason);
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
