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

#include <algorithm>

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

// v2.14 #49-2 第二腿：同名冲突落库（声明处注释为语义权威）。
// 匹配口径：list() 全量遍历、std::string 全等（大小写敏感），
// 与 TaskCenterWidget::importTemplateFromFile（v2.13 导入）刻意同口径；本次不重构导入路径（留债观察）。
bool TaskTemplateStore::saveUniqueByName(
    const TaskTemplate& tmpl,
    const std::function<NameConflictChoice(const QString& name)>& onConflict,
    std::string* savedId, QString* reason)
{
    const auto templates = list();

    // 同名冲突检测：与既有模板 name 全等即冲突（std::string operator== 大小写敏感）
    const TaskTemplate* conflict = nullptr;
    for (const auto& t : templates) {
        if (t.name == tmpl.name) {
            conflict = &t;
            break;
        }
    }

    TaskTemplate copy = tmpl;
    if (!conflict) {
        // 无冲突直接落库；空 templateId 先自行生成——save 内部生成的 ID 不外露，
        // 若不预生成则 savedId 无从回填。
        if (copy.templateId.empty()) copy.templateId = generateTemplateId();
    } else {
        const NameConflictChoice choice = onConflict(QString::fromStdString(tmpl.name));
        if (choice == NameConflictChoice::Cancel) {
            if (reason)
                *reason = QStringLiteral("已取消：与既有模板同名「%1」，未写入任何记录")
                              .arg(QString::fromStdString(tmpl.name));
            return false;
        }
        if (choice == NameConflictChoice::Overwrite) {
            // 覆盖：继承既有 templateId 原位 upsert；version 取 max(新旧)——
            // save 的降级禁则（同 ID 新 version 低于已存即拒绝）决定了必须抬版本才能覆盖成功。
            copy.templateId = conflict->templateId;
            copy.version = std::max(conflict->version, tmpl.version);
        } else {
            // 改名：「名称 (2)」「名称 (3)」…递增找空位，不设上限——
            // 文案规则与 TaskCenterWidget::importTemplateFromFile 的改名循环（v2.13 导入）同源。
            const QString base = QString::fromStdString(tmpl.name);
            auto nameTaken = [&templates](const std::string& name) {
                for (const auto& t : templates)
                    if (t.name == name) return true;
                return false;
            };
            for (int n = 2;; ++n) {
                const QString candidate = QStringLiteral("%1 (%2)").arg(base).arg(n);
                if (!nameTaken(candidate.toStdString())) {
                    copy.name = candidate.toStdString();
                    break;
                }
            }
            // 改名即另存独立新模板：无条件换新 ID（入站若带旧 ID 也不可沿用，
            // 且预生成才能让 savedId 回填）。
            copy.templateId = generateTemplateId();
        }
    }

    if (!save(copy)) {
        if (reason)
            *reason = QStringLiteral("落库失败：模板存储拒绝写入（无效模板/版本降级/敏感字段，详见日志）");
        return false;
    }
    if (savedId) *savedId = copy.templateId;
    return true;
}

std::vector<TaskTemplate> TaskTemplateStore::list() const
{
    std::vector<TaskTemplate> templates;
    for (const auto& row : ConfigStore::instance().list(kRecordType)) {
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
