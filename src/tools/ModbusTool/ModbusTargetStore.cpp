// v2.12 MODBUS 重构 Task 1：ModbusTargetStore 实现。
// 序列化到 ConfigStore 记录类型 modbus.target（key=deviceId），模式对齐 device.profile /
// task.template；旧 modbus.slave（key=slave:<sid>）仅作只读迁移数据源，永不删除。
#include "tools/ModbusTool/ModbusTargetStore.h"

#include "config/ConfigStore.h"
#include "device/DeviceRegistry.h"
#include "tools/ModbusTool/ModbusBackend.h" // 复用 registerTypeFromComboIndex（旧界面上拉索引口径）

#include <QDebug>
#include <QLatin1String>
#include <QSet>
#include <QVariantList>

#include <algorithm>

namespace {

constexpr QLatin1String kRecordType("modbus.target");
constexpr QLatin1String kLegacyType("modbus.slave");
constexpr QLatin1String kLegacyKeyPrefix("slave:");

// 解析旧 modbus.slave 行 → 临时 SlaveTarget 清单。
// 口径：旧行 regType 存的是界面上拉索引（0=保持/1=输入/2=线圈/3=离散），
// 与 ModbusWidget 写入口径一致，经同源映射换算为 RegisterType 值；
// sid 优先取 slaveId 字段，缺失时解析 key "slave:<sid>"。
// list() 按 updated_at 倒序返回：同一 sid 命中多行时保留最新一条。
QVector<modbus::SlaveTarget> parseLegacyRows(const QList<QVariantMap>& rows)
{
    QVector<modbus::SlaveTarget> targets;
    QSet<int> seenSlaveIds;
    for (const auto& row : rows) {
        const QString key = row.value(QStringLiteral("key")).toString();

        int slaveId = 0;
        const QVariant slaveIdValue = row.value(QStringLiteral("slaveId"));
        if (slaveIdValue.isValid() && !slaveIdValue.isNull()) {
            slaveId = slaveIdValue.toInt();
        } else if (key.startsWith(QString(kLegacyKeyPrefix))) {
            slaveId = key.mid(int(kLegacyKeyPrefix.size())).toInt();
        }
        if (slaveId <= 0) {
            qWarning() << "ModbusTargetStore: 旧从站记录站号非法，跳过" << key;
            continue;
        }
        if (seenSlaveIds.contains(slaveId)) continue;
        seenSlaveIds.insert(slaveId);

        modbus::SlaveTarget target;
        target.slaveId = slaveId;
        target.enabled = true;
        const int comboIndex = row.value(QStringLiteral("regType")).toInt(); // 缺失 → 0 → 保持寄存器
        target.poll.regType = registerTypeFromComboIndex(comboIndex);
        target.poll.startAddr = qMax(0, row.value(QStringLiteral("startAddr")).toInt());
        const QVariant countValue = row.value(QStringLiteral("count"));
        const int count = countValue.isValid() ? countValue.toInt() : 1;
        target.poll.count = count > 0 ? count : 1;
        const QVariant intervalValue = row.value(QStringLiteral("intervalMs"));
        const int intervalMs = intervalValue.isValid() ? intervalValue.toInt() : 1000;
        // 终审 Minor②：全局约束 intervalMs ≥ 500，旧行低于下界一律钳起
        target.poll.intervalMs = intervalMs > 0 ? qMax(500, intervalMs) : 1000;
        targets.push_back(target);
    }
    return targets;
}

bool hasModbusEndpoint(const DeviceProfile& profile)
{
    return std::any_of(profile.endpoints.begin(), profile.endpoints.end(),
                       [](const DeviceEndpoint& endpoint) { return endpoint.protocol == "modbus"; });
}

} // namespace

namespace modbus {

bool ModbusTargetStore::load(const std::string& deviceId, DeviceTargets* out) const
{
    if (deviceId.empty() || !out) return false;
    const QString key = QString::fromStdString(deviceId);

    // 新结构为准：命中可解码记录直接返回
    const QVariantMap raw = ConfigStore::instance().load(QString(kRecordType), key);
    if (!raw.isEmpty()) {
        const auto decoded = decodeDeviceTargets(raw);
        if (decoded) {
            *out = *decoded;
            if (out->deviceId.empty()) out->deviceId = deviceId;
            return true;
        }
        qWarning() << "ModbusTargetStore: 设备目标记录损坏，回退旧从站记录" << key;
    }

    // 回退：读取旧 modbus.slave 合成临时 targets（不写库、不触发迁移）
    const auto legacyTargets =
        parseLegacyRows(ConfigStore::instance().list(QString(kLegacyType), 1000));
    if (legacyTargets.isEmpty()) return false;
    out->deviceId = deviceId;
    out->targets = legacyTargets;
    out->tagSchema = 0;
    return true;
}

bool ModbusTargetStore::save(const DeviceTargets& targets)
{
    if (targets.deviceId.empty()) {
        qWarning() << "ModbusTargetStore: 拒绝保存，deviceId 为空";
        return false;
    }
    const QString key = QString::fromStdString(targets.deviceId);
    if (!ConfigStore::instance().save(QString(kRecordType), key, encodeDeviceTargets(targets))) {
        qWarning() << "ModbusTargetStore: 保存目标记录失败" << key;
        return false;
    }
    return true;
}

bool ModbusTargetStore::remove(const std::string& deviceId)
{
    if (deviceId.empty()) return false;
    return ConfigStore::instance().remove(QString(kRecordType), QString::fromStdString(deviceId));
}

std::vector<std::string> ModbusTargetStore::allDeviceIds() const
{
    std::vector<std::string> ids;
    for (const auto& row : ConfigStore::instance().list(QString(kRecordType), 1000)) {
        const QString key = row.value(QStringLiteral("key")).toString();
        if (key.isEmpty()) continue;
        ids.push_back(key.toStdString());
    }
    return ids;
}

int ModbusTargetStore::migrateLegacySlaves(DeviceRegistry* registry)
{
    if (!registry) {
        qWarning() << "ModbusTargetStore: 迁移失败，设备注册表为空指针";
        return 0;
    }

    const auto legacyTargets =
        parseLegacyRows(ConfigStore::instance().list(QString(kLegacyType), 1000));
    if (legacyTargets.isEmpty()) return 0; // 无旧记录：无事可做，不算失败

    int migrated = 0;
    for (const auto& profile : registry->list()) {
        if (!hasModbusEndpoint(profile) || profile.deviceId.empty()) continue;

        // 已有新 modbus.target 记录（如首次迁移后用户编辑过）→ 跳过，
        // 避免重复迁移把旧 slave 合成的内容整体回写、覆盖用户数据；不计入 migrated。
        if (ConfigStore::instance().exists(QString(kRecordType),
                                           QString::fromStdString(profile.deviceId))) {
            continue;
        }

        DeviceTargets targets;
        targets.deviceId = profile.deviceId;
        targets.tagSchema = 0;
        targets.targets = legacyTargets; // 每条 slave:<sid> 生成一个 enabled=true 目标
        if (!save(targets)) {
            qWarning() << "ModbusTargetStore: 迁移写库失败，中止"
                       << QString::fromStdString(profile.deviceId);
            return 0;
        }
        ++migrated;
    }

    if (migrated == 0) {
        qWarning() << "ModbusTargetStore: 注册表中无含 modbus 端点的设备档案，未迁移";
    }
    return migrated;
}

} // namespace modbus
