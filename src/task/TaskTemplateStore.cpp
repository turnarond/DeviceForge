// v2.11 Task 3：TaskTemplateStore 实现。
// 序列化到 ConfigStore 记录类型 task.template（key=templateId），模式对齐 device.profile。
// 模板不得保存凭据秘密值：save/load 双向递归扫描键名，命中密码/私钥类字段即拒绝。
// v2.13 Task 1：行编解码外提至 TaskTemplateCodec（字段口径唯一来源），本文件只保留存储与校验门面。
#include "task/TaskTemplateStore.h"

#include "config/ConfigStore.h"
#include "task/TaskTemplateCodec.h"

#include <QDebug>
#include <QLatin1String>
#include <QUuid>

namespace {

constexpr QLatin1String kRecordType("task.template");

std::string generateTemplateId()
{
    return "template-" + QUuid::createUuid().toString(QUuid::Id128).toStdString();
}

} // namespace

TaskTemplateValidationResult validateTaskTemplate(const TaskTemplate& tmpl)
{
    if (trimTaskString(tmpl.name).empty()) {
        return {false, QStringLiteral("模板名称为必填项，不能为空")};
    }
    if (tmpl.version < 1) {
        return {false, QStringLiteral("模板版本必须大于等于 1")};
    }
    if (tmpl.steps.empty()) {
        return {false, QStringLiteral("模板必须包含至少一个有序步骤")};
    }
    for (std::size_t i = 0; i < tmpl.steps.size(); ++i) {
        QString sensitiveKey;
        if (containsSensitiveKey(tmpl.steps[i].parameters, &sensitiveKey)) {
            return {false, QStringLiteral("模板不得保存凭据字段 \"%1\"（步骤 %2），"
                                          "密码/私钥只由 ConfigStore/DPAPI 管理")
                                .arg(sensitiveKey)
                                .arg(i + 1)};
        }
    }
    QString sensitiveKey;
    if (containsSensitiveKey(tmpl.unknownFields, &sensitiveKey)) {
        return {false, QStringLiteral("模板不得保存凭据字段 \"%1\"（未来兼容字段），"
                                      "密码/私钥只由 ConfigStore/DPAPI 管理")
                            .arg(sensitiveKey)};
    }
    return {true, QString()};
}

bool TaskTemplateStore::save(const TaskTemplate& input)
{
    const auto validation = validateTaskTemplate(input);
    if (!validation.valid) {
        qWarning() << "TaskTemplateStore: 拒绝保存无效模板" << validation.error;
        return false;
    }

    TaskTemplate stored = input;
    stored.name = trimTaskString(stored.name);
    if (stored.templateId.empty()) stored.templateId = generateTemplateId();
    const QString key = QString::fromStdString(stored.templateId);

    auto& store = ConfigStore::instance();

    // 版本化：同 ID 已存在可解码记录时拒绝降级写入（损坏记录允许覆盖修复）
    const QVariantMap existingRaw = store.load(kRecordType, key);
    if (!existingRaw.isEmpty()) {
        const auto existing = decodeTaskTemplate(existingRaw, nullptr);
        if (existing && stored.version < existing->version) {
            qWarning() << "TaskTemplateStore: 拒绝版本降级" << key
                       << "新.version=" << stored.version
                       << "已存.version=" << existing->version;
            return false;
        }
    }

    const QVariantMap record = encodeTaskTemplate(stored);

    // 双保险：持久化前对整条编码记录递归扫描，任何敏感键都不落盘
    QString sensitiveKey;
    if (containsSensitiveKey(record, &sensitiveKey)) {
        qWarning() << "TaskTemplateStore: 拒绝保存含敏感凭据字段的记录" << sensitiveKey;
        return false;
    }

    if (!store.save(kRecordType, key, record)) {
        qWarning() << "TaskTemplateStore: 保存模板失败" << key;
        return false;
    }
    return true;
}

std::optional<TaskTemplate> TaskTemplateStore::load(const std::string& templateId)
{
    if (templateId.empty()) return std::nullopt;
    const QString key = QString::fromStdString(templateId);
    const QVariantMap raw = ConfigStore::instance().load(kRecordType, key);
    if (raw.isEmpty()) return std::nullopt;

    QString reason;
    auto tmpl = decodeTaskTemplate(raw, &reason);
    if (!tmpl) {
        qWarning() << "TaskTemplateStore: 跳过损坏或不合规记录" << key << reason;
        return std::nullopt;
    }
    if (tmpl->templateId.empty()) tmpl->templateId = templateId;
    return tmpl;
}

std::optional<TaskTemplate> TaskTemplateStore::clone(const std::string& templateId,
                                                    const std::string& newName)
{
    const auto source = load(templateId);
    if (!source) {
        qWarning() << "TaskTemplateStore: 克隆失败，源模板不存在或不合规"
                   << QString::fromStdString(templateId);
        return std::nullopt;
    }
    const QString trimmedName = QString::fromStdString(newName).trimmed();
    if (trimmedName.isEmpty()) {
        qWarning() << "TaskTemplateStore: 克隆失败，新模板名称为空";
        return std::nullopt;
    }

    TaskTemplate copy = *source;
    copy.templateId = generateTemplateId();   // 新模板独立唯一 ID
    copy.name = trimmedName.toStdString();
    copy.version = 1;                          // 新模板独立版本线
    if (!save(copy)) return std::nullopt;      // save 内部已输出中文告警
    return copy;
}

bool TaskTemplateStore::remove(const std::string& templateId)
{
    if (templateId.empty())
        return false;
    auto& store = ConfigStore::instance();
    const QString key = QString::fromStdString(templateId);
    // ConfigStore::remove 对不存在的 key 也返回 true（DELETE 成功但零行命中），
    // 故先确认记录存在再转发删除结果，保证“不存在 → false”的删除语义（#54）。
    if (!store.exists(kRecordType, key))
        return false;
    return store.remove(kRecordType, key);
}

std::vector<TaskTemplate> TaskTemplateStore::list() const
{
    std::vector<TaskTemplate> templates;
    for (const auto& row : ConfigStore::instance().list(kRecordType, 1000)) {
        QString reason;
        auto tmpl = decodeTaskTemplate(row, &reason);
        if (!tmpl) {
            qWarning() << "TaskTemplateStore: 跳过损坏或不合规记录 key="
                       << row.value(QStringLiteral("key")).toString() << reason;
            continue;
        }
        if (tmpl->templateId.empty()) {
            tmpl->templateId = row.value(QStringLiteral("key")).toString().toStdString();
        }
        templates.push_back(std::move(*tmpl));
    }
    return templates;
}
