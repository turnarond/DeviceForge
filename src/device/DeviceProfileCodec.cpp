#include "device/DeviceProfileCodec.h"

#include <QLatin1String>
#include <QStringList>
#include <QVariantList>

namespace {

constexpr QLatin1String kFieldDeviceId("deviceId");
constexpr QLatin1String kFieldName("name");
constexpr QLatin1String kFieldNote("note");
constexpr QLatin1String kFieldTags("tags");
constexpr QLatin1String kFieldEndpoints("endpoints");
constexpr QLatin1String kFieldProtocol("protocol");
constexpr QLatin1String kFieldIp("ip");
constexpr QLatin1String kFieldPort("port");
constexpr QLatin1String kFieldCredentialRef("credentialRef");
constexpr QLatin1String kFieldDisplayName("displayName");

std::string toStdString(const QVariant& value)
{
    return value.toString().toStdString();
}

} // namespace

QVariantMap encodeDeviceProfile(const DeviceProfile& profile)
{
    QVariantMap record;
    record.insert(QString(kFieldDeviceId), QString::fromStdString(profile.deviceId));
    record.insert(QString(kFieldName), QString::fromStdString(profile.name));
    record.insert(QString(kFieldNote), QString::fromStdString(profile.note));

    QStringList tags;
    tags.reserve(static_cast<int>(profile.tags.size()));
    for (const auto& tag : profile.tags) tags.append(QString::fromStdString(tag));
    record.insert(QString(kFieldTags), tags);

    QVariantList endpoints;
    endpoints.reserve(static_cast<int>(profile.endpoints.size()));
    for (const auto& endpoint : profile.endpoints) {
        QVariantMap item;
        item.insert(QString(kFieldProtocol), QString::fromStdString(endpoint.protocol));
        item.insert(QString(kFieldIp), QString::fromStdString(endpoint.ip));
        item.insert(QString(kFieldPort), endpoint.port);
        // 仅存凭据引用；密码明文由 ftp.credential/DPAPI 独立管理
        item.insert(QString(kFieldCredentialRef), QString::fromStdString(endpoint.credentialRef));
        endpoints.append(item);
    }
    record.insert(QString(kFieldEndpoints), endpoints);
    return record;
}

std::optional<DeviceProfile> decodeDeviceProfile(const QVariantMap& record)
{
    const auto endpointsIt = record.constFind(QString(kFieldEndpoints));
    if (endpointsIt != record.constEnd()) {
        // 新格式 device.profile 记录
        const QVariantList endpoints =
            endpointsIt->canConvert<QVariantList>() ? endpointsIt->toList() : QVariantList();
        if (endpoints.isEmpty()) return std::nullopt;

        DeviceProfile profile;
        profile.deviceId = toStdString(record.value(QString(kFieldDeviceId)));
        profile.name = toStdString(record.value(QString(kFieldName)));
        profile.note = toStdString(record.value(QString(kFieldNote)));
        const QStringList tags = record.value(QString(kFieldTags)).toStringList();
        for (const auto& tag : tags) profile.tags.push_back(tag.toStdString());

        for (const auto& item : endpoints) {
            if (!item.canConvert<QVariantMap>()) continue;
            const QVariantMap map = item.toMap();
            const QString ip = map.value(QString(kFieldIp)).toString().trimmed();
            if (ip.isEmpty()) continue;
            DeviceEndpoint endpoint;
            endpoint.protocol = toStdString(map.value(QString(kFieldProtocol)));
            endpoint.ip = ip.toStdString();
            endpoint.port = map.value(QString(kFieldPort)).toInt();
            endpoint.credentialRef = toStdString(map.value(QString(kFieldCredentialRef)));
            profile.endpoints.push_back(std::move(endpoint));
        }
        if (profile.endpoints.empty()) return std::nullopt;
        return profile;
    }

    // 回退读取：旧 device.list 记录（无 deviceId/端点结构）
    const QString ip = record.value(QString(kFieldIp)).toString().trimmed();
    if (ip.isEmpty()) return std::nullopt;

    DeviceProfile profile;
    const QString displayName = record.value(QString(kFieldDisplayName)).toString();
    // 与 DeviceBusWidget 加载语义一致：displayName 等于 ip 视为无别名
    if (!displayName.isEmpty() && displayName != ip) profile.name = displayName.toStdString();
    profile.note = toStdString(record.value(QString(kFieldNote)));

    QString protocol = record.value(QString(kFieldProtocol)).toString();
    if (protocol.isEmpty()) protocol = QStringLiteral("ftp"); // 设备总线历史默认 FTP

    DeviceEndpoint endpoint;
    endpoint.protocol = protocol.toStdString();
    endpoint.ip = ip.toStdString();
    endpoint.port = record.value(QString(kFieldPort)).toInt();
    endpoint.credentialRef = toStdString(record.value(QString(kFieldCredentialRef)));
    profile.endpoints.push_back(std::move(endpoint));
    return profile;
}
