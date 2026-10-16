// v2.13 Task 1：任务模板行级编解码，自 TaskTemplateStore.cpp 匿名命名空间整体外提（纯搬移，行为不变）。
// 字段口径唯一来源：store 持久化与后续文件通道（导出/导入）共用同一 encode/decode。
// 模板不得保存凭据秘密值：敏感键递归扫描（containsSensitiveKey）随本模块外提，供 store 与文件通道复用。
#include "task/TaskTemplateCodec.h"

#include <QLatin1String>
#include <QMetaType>
#include <QStringList>
#include <QVariantList>

#include <algorithm>

namespace {

constexpr QLatin1String kFieldTemplateId("templateId");
constexpr QLatin1String kFieldName("name");
constexpr QLatin1String kFieldDescription("description");
constexpr QLatin1String kFieldVersion("version");
constexpr QLatin1String kFieldDeviceIds("deviceIds");
constexpr QLatin1String kFieldTagSelectors("tagSelectors");
constexpr QLatin1String kFieldSteps("steps");
constexpr QLatin1String kFieldStepType("type");
constexpr QLatin1String kFieldStepParameters("parameters");

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

} // namespace

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
    tmpl.name = trimTaskString(row.value(QString(kFieldName)).toString().toStdString());
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
