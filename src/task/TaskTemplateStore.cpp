// v2.11 Task 3：TaskTemplateStore 实现。
// 序列化到 ConfigStore 记录类型 task.template（key=templateId），模式对齐 device.profile。
// 模板不得保存凭据秘密值：save/load 双向递归扫描键名，命中密码/私钥类字段即拒绝。
#include "task/TaskTemplateStore.h"

#include "config/ConfigStore.h"

#include <QDebug>
#include <QLatin1String>
#include <QMetaType>
#include <QStringList>
#include <QUuid>
#include <QVariantList>

#include <algorithm>
#include <cctype>

namespace {

constexpr QLatin1String kRecordType("task.template");
constexpr QLatin1String kFieldTemplateId("templateId");
constexpr QLatin1String kFieldName("name");
constexpr QLatin1String kFieldDescription("description");
constexpr QLatin1String kFieldVersion("version");
constexpr QLatin1String kFieldDeviceIds("deviceIds");
constexpr QLatin1String kFieldTagSelectors("tagSelectors");
constexpr QLatin1String kFieldSteps("steps");
constexpr QLatin1String kFieldStepType("type");
constexpr QLatin1String kFieldStepParameters("parameters");

std::string trim(const std::string& value)
{
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    return first >= last ? std::string() : std::string(first, last);
}

// 密码/私钥类键名黑名单（大小写不敏感的子串匹配）。
// credentialRef 之类的引用字段不受影响。
bool isSensitiveKey(const QString& key)
{
    static const QLatin1String needles[] = {
        QLatin1String("password"), QLatin1String("passwd"),
        QLatin1String("privkey"), QLatin1String("private_key"),
        QLatin1String("privatekey"), QLatin1String("key_material"),
        QLatin1String("keymaterial"), QLatin1String("token"),
        QLatin1String("secret")};
    const QString lower = key.toLower();
    return std::any_of(std::begin(needles), std::end(needles),
                       [&lower](const QLatin1String& needle) { return lower.contains(needle); });
}

// 递归查找 map/list 结构中的首个敏感键名；前向兼容字段同样在检查范围内。
bool containsSensitiveKey(const QVariant& value, QString* sensitiveKey)
{
    if (value.typeId() == QMetaType::QVariantMap) {
        const QVariantMap map = value.toMap();
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            if (isSensitiveKey(it.key())) {
                if (sensitiveKey) *sensitiveKey = it.key();
                return true;
            }
            if (containsSensitiveKey(it.value(), sensitiveKey)) return true;
        }
        return false;
    }
    if (value.typeId() == QMetaType::QVariantList) {
        for (const QVariant& item : value.toList()) {
            if (containsSensitiveKey(item, sensitiveKey)) return true;
        }
    }
    return false;
}

// ConfigStore::list() 行注入的保留键，不参与记录字段与未知字段还原
bool isReservedRowKey(const QString& key)
{
    return key == QLatin1String("key") || key == QLatin1String("type")
        || key == QLatin1String("updated_at");
}

bool isKnownFieldKey(const QString& key)
{
    return key == QString(kFieldTemplateId) || key == QString(kFieldName)
        || key == QString(kFieldDescription) || key == QString(kFieldVersion)
        || key == QString(kFieldDeviceIds) || key == QString(kFieldTagSelectors)
        || key == QString(kFieldSteps);
}

std::string generateTemplateId()
{
    return "template-" + QUuid::createUuid().toString(QUuid::Id128).toStdString();
}

QVariantMap encodeTaskTemplate(const TaskTemplate& tmpl)
{
    QVariantMap record;
    // 前向兼容：先铺未知未来字段，已知字段随后覆盖（保留键无法 round-trip，跳过）
    for (auto it = tmpl.unknownFields.constBegin(); it != tmpl.unknownFields.constEnd(); ++it) {
        if (isReservedRowKey(it.key())) continue;
        record.insert(it.key(), it.value());
    }
    record.insert(QString(kFieldTemplateId), QString::fromStdString(tmpl.templateId));
    record.insert(QString(kFieldName), QString::fromStdString(tmpl.name));
    record.insert(QString(kFieldDescription), QString::fromStdString(tmpl.description));
    record.insert(QString(kFieldVersion), tmpl.version);

    QStringList deviceIds;
    deviceIds.reserve(static_cast<int>(tmpl.deviceIds.size()));
    for (const auto& id : tmpl.deviceIds) deviceIds.append(QString::fromStdString(id));
    record.insert(QString(kFieldDeviceIds), deviceIds);

    QStringList tagSelectors;
    tagSelectors.reserve(static_cast<int>(tmpl.tagSelectors.size()));
    for (const auto& tag : tmpl.tagSelectors) tagSelectors.append(QString::fromStdString(tag));
    record.insert(QString(kFieldTagSelectors), tagSelectors);

    QVariantList steps;
    steps.reserve(static_cast<int>(tmpl.steps.size()));
    for (const auto& step : tmpl.steps) {
        QVariantMap item;
        item.insert(QString(kFieldStepType), taskStepTypeToString(step.type));
        item.insert(QString(kFieldStepParameters), step.parameters);
        steps.append(item);
    }
    record.insert(QString(kFieldSteps), steps);
    return record;
}

// 解码记录：结构损坏（缺名称/步骤非法/未知步骤类型）或含敏感字段返回 nullopt。
// reason 可为空；调用方据此记录中文告警。
std::optional<TaskTemplate> decodeTaskTemplate(const QVariantMap& row, QString* reason)
{
    const auto fail = [reason](const QString& message) {
        if (reason) *reason = message;
        return std::nullopt;
    };

    QString sensitiveKey;
    if (containsSensitiveKey(row, &sensitiveKey)) {
        return fail(QStringLiteral("记录含敏感凭据字段 \"%1\"，模板不得保存密码/私钥类值")
                        .arg(sensitiveKey));
    }

    TaskTemplate tmpl;
    tmpl.templateId = row.value(QString(kFieldTemplateId)).toString().toStdString();
    tmpl.name = trim(row.value(QString(kFieldName)).toString().toStdString());
    if (tmpl.name.empty()) return fail(QStringLiteral("记录缺少模板名称"));
    tmpl.description = row.value(QString(kFieldDescription)).toString().toStdString();

    const auto versionIt = row.constFind(QString(kFieldVersion));
    if (versionIt != row.constEnd()) {
        tmpl.version = versionIt->toInt();
        if (tmpl.version < 1) return fail(QStringLiteral("记录版本非法"));
    }

    const QStringList deviceIds = row.value(QString(kFieldDeviceIds)).toStringList();
    for (const auto& id : deviceIds) tmpl.deviceIds.push_back(id.toStdString());
    const QStringList tagSelectors = row.value(QString(kFieldTagSelectors)).toStringList();
    for (const auto& tag : tagSelectors) tmpl.tagSelectors.push_back(tag.toStdString());

    const QVariant stepsValue = row.value(QString(kFieldSteps));
    if (stepsValue.typeId() != QMetaType::QVariantList) return fail(QStringLiteral("记录步骤列表结构非法"));
    const QVariantList steps = stepsValue.toList();
    if (steps.isEmpty()) return fail(QStringLiteral("记录缺少有序步骤"));

    for (const QVariant& item : steps) {
        if (item.typeId() != QMetaType::QVariantMap) return fail(QStringLiteral("步骤记录结构非法"));
        const QVariantMap map = item.toMap();
        const auto parsedType = taskStepTypeFromString(map.value(QString(kFieldStepType)).toString());
        if (!parsedType) {
            return fail(QStringLiteral("未知步骤类型 \"%1\"")
                            .arg(map.value(QString(kFieldStepType)).toString()));
        }
        TaskStep step;
        step.type = *parsedType;
        const QVariant parameters = map.value(QString(kFieldStepParameters));
        if (!parameters.isNull()) {
            if (parameters.typeId() != QMetaType::QVariantMap) {
                return fail(QStringLiteral("步骤参数结构非法"));
            }
            step.parameters = parameters.toMap();
        }
        tmpl.steps.push_back(std::move(step));
    }

    // 前向兼容：保留未知未来字段原样回存（保留键除外）
    for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
        if (isKnownFieldKey(it.key()) || isReservedRowKey(it.key())) continue;
        tmpl.unknownFields.insert(it.key(), it.value());
    }
    return tmpl;
}

} // namespace

TaskTemplateValidationResult validateTaskTemplate(const TaskTemplate& tmpl)
{
    if (trim(tmpl.name).empty()) {
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
    stored.name = trim(stored.name);
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
