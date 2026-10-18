// v2.11 Task 2：DeviceRegistry ConfigStore 持久化与兼容迁移测试
// 覆盖：round-trip 持久化、缺失 deviceId 的旧 DeviceInfo::alias 记录迁移、
// 多协议端点合并持久化、损坏记录回退跳过、仅存凭据引用（不存密码明文）、
// v2.15 #49-8：超上限（>1000）legacy 行的全表清扫不漏扫。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QVariantList>

#include "config/ConfigStore.h"
#include "device/DeviceProfileCodec.h"
#include "device/DeviceRegistry.h"

namespace {

DeviceProfile makeProfile(const QString& name, const QString& ip, int port,
                          const QString& protocol = QStringLiteral("ftp"),
                          const QString& credentialRef = QString())
{
    DeviceProfile profile;
    profile.name = name.toStdString();
    profile.note = (QStringLiteral("备注-") + name).toStdString();
    profile.tags = {QStringLiteral("产线A").toStdString(), QStringLiteral("PLC").toStdString()};
    profile.endpoints.push_back({protocol.toStdString(), ip.toStdString(), port,
                                 credentialRef.toStdString()});
    return profile;
}

} // namespace

class TstDeviceRegistryStore : public QObject
{
    Q_OBJECT

private:
    QString m_dbPath;
    QString m_workDir;

private slots:
    void initTestCase()
    {
        // 进程专属临时目录，避免 CI 残留/并发污染（同 tst_config_store）
        m_workDir = QDir::temp().filePath(
            QStringLiteral("tst_device_registry_store_%1")
                .arg(QUuid::createUuid().toString(QUuid::Id128)));
        QVERIFY(QDir().mkpath(m_workDir));
        m_dbPath = QDir(m_workDir).filePath(QStringLiteral("config.db"));
    }

    void cleanupTestCase()
    {
        ConfigStore::instance().close();
        QDir(m_workDir).removeRecursively();
    }

    void init()
    {
        ConfigStore::instance().close();
        QFile::remove(m_dbPath);
        QFile::remove(m_dbPath + QStringLiteral("-wal"));
        QFile::remove(m_dbPath + QStringLiteral("-shm"));
        QVERIFY2(ConfigStore::instance().open(m_dbPath),
                 qPrintable(QStringLiteral("open failed path=%1").arg(m_dbPath)));
    }

    void cleanup()
    {
        ConfigStore::instance().close();
    }

    // 编解码纯函数 round-trip
    void codecRoundTrip()
    {
        const DeviceProfile profile = makeProfile(QStringLiteral("控制器-01"),
                                                  QStringLiteral("192.168.20.123"), 21,
                                                  QStringLiteral("ftp"),
                                                  QStringLiteral("ftp-main"));
        const QVariantMap record = encodeDeviceProfile(profile);
        const auto decoded = decodeDeviceProfile(record);
        QVERIFY(decoded.has_value());
        QCOMPARE(QString::fromStdString(decoded->name), QStringLiteral("控制器-01"));
        QCOMPARE(QString::fromStdString(decoded->note), QStringLiteral("备注-控制器-01"));
        QCOMPARE(decoded->tags.size(), size_t(2));
        QCOMPARE(QString::fromStdString(decoded->tags.at(0)), QStringLiteral("产线A"));
        QCOMPARE(decoded->endpoints.size(), size_t(1));
        QCOMPARE(QString::fromStdString(endpointKey(decoded->endpoints.at(0))),
                 QStringLiteral("ftp://192.168.20.123:21"));
        QCOMPARE(QString::fromStdString(decoded->endpoints.at(0).credentialRef),
                 QStringLiteral("ftp-main"));
    }

    // Registry 保存后全新 Registry 从 ConfigStore 完整加载
    void roundTripPersistence()
    {
        DeviceRegistry registry;
        const auto first = registry.upsert(makeProfile(QStringLiteral("控制器-01"),
                                                       QStringLiteral("192.168.20.123"), 21));
        const auto second = registry.upsert(makeProfile(QStringLiteral("控制器-02"),
                                                        QStringLiteral("192.168.20.124"), 22,
                                                        QStringLiteral("sftp")));
        QVERIFY(registry.save(first));
        QVERIFY(registry.save(second));

        DeviceRegistry reloaded;
        QVERIFY(reloaded.load());
        QCOMPARE(reloaded.list().size(), size_t(2));
        for (const auto& saved : {first, second}) {
            const auto found = reloaded.find(saved.deviceId);
            QVERIFY(found.has_value());
            QCOMPARE(QString::fromStdString(found->name), QString::fromStdString(saved.name));
            QCOMPARE(QString::fromStdString(found->note), QString::fromStdString(saved.note));
            QCOMPARE(found->tags.size(), saved.tags.size());
            QCOMPARE(found->endpoints.size(), size_t(1));
            QCOMPARE(QString::fromStdString(endpointKey(found->endpoints.at(0))),
                     QString::fromStdString(endpointKey(saved.endpoints.at(0))));
        }
    }

    // 同一设备多协议端点合并后持久化，重载仍为一条档案两个端点
    void protocolEndpointMergePersistence()
    {
        DeviceRegistry registry;
        const auto base = registry.upsert(makeProfile(QStringLiteral("控制器-01"),
                                                      QStringLiteral("192.168.20.123"), 21,
                                                      QStringLiteral("ftp"),
                                                      QStringLiteral("ftp-main")));
        QVERIFY(registry.save(base));

        DeviceProfile update;
        update.deviceId = base.deviceId;
        update.endpoints.push_back({"ssh", "192.168.20.123", 22, "shell-main"});
        QVERIFY(registry.save(update));
        QCOMPARE(registry.list().size(), size_t(1));

        DeviceRegistry reloaded;
        QVERIFY(reloaded.load());
        QCOMPARE(reloaded.list().size(), size_t(1));
        const auto merged = reloaded.list().front();
        QCOMPARE(merged.endpoints.size(), size_t(2));
        QCOMPARE(QString::fromStdString(endpointKey(merged.endpoints.at(1))),
                 QStringLiteral("ssh://192.168.20.123:22"));
        QCOMPARE(QString::fromStdString(merged.name), QStringLiteral("控制器-01"));
    }

    // 旧 device.list 记录（DeviceInfo::alias → displayName）迁移为设备档案
    void migratesLegacyDeviceListRecords()
    {
        const DeviceInfo legacy{"192.168.20.128", 23, "telnet",
                                QStringLiteral("现场柜-02").toStdString(),
                                QStringLiteral("A线").toStdString()};
        QVariantMap row;
        row.insert(QStringLiteral("ip"), QStringLiteral("192.168.20.128"));
        row.insert(QStringLiteral("port"), 23);
        row.insert(QStringLiteral("protocol"), QStringLiteral("telnet"));
        row.insert(QStringLiteral("displayName"), QString::fromStdString(legacy.alias));
        row.insert(QStringLiteral("note"), QString::fromStdString(legacy.note));
        QVERIFY(ConfigStore::instance().save(QStringLiteral("device.list"),
                                             QStringLiteral("192.168.20.128:23"), row));

        DeviceRegistry registry;
        QVERIFY(registry.load());
        QCOMPARE(registry.list().size(), size_t(1));
        const auto profile = registry.list().front();
        QCOMPARE(QString::fromStdString(profile.name), QStringLiteral("现场柜-02"));
        QCOMPARE(QString::fromStdString(profile.note), QStringLiteral("A线"));
        QCOMPARE(profile.endpoints.size(), size_t(1));
        QCOMPARE(QString::fromStdString(endpointKey(profile.endpoints.at(0))),
                 QStringLiteral("telnet://192.168.20.128:23"));
        // 缺失 deviceId 的旧记录按规范化端点身份生成确定性 ID
        const auto expected = fromDeviceInfo(legacy);
        QCOMPARE(QString::fromStdString(profile.deviceId),
                 QString::fromStdString(expected.deviceId));
    }

    // displayName 与 ip 相同视为无别名，迁移后名称为空
    void legacyRecordWithoutAliasGetsEmptyName()
    {
        QVariantMap row;
        row.insert(QStringLiteral("ip"), QStringLiteral("10.0.0.9"));
        row.insert(QStringLiteral("port"), 21);
        row.insert(QStringLiteral("displayName"), QStringLiteral("10.0.0.9"));
        QVERIFY(ConfigStore::instance().save(QStringLiteral("device.list"),
                                             QStringLiteral("10.0.0.9:21"), row));

        DeviceRegistry registry;
        QVERIFY(registry.load());
        QCOMPARE(registry.list().size(), size_t(1));
        QVERIFY(registry.list().front().name.empty());
    }

    // 新格式记录缺失 deviceId 时同样按端点身份生成确定性 ID
    void missingDeviceIdYieldsStableId()
    {
        QVariantMap endpoint;
        endpoint.insert(QStringLiteral("protocol"), QStringLiteral("ftp"));
        endpoint.insert(QStringLiteral("ip"), QStringLiteral("192.168.20.123"));
        endpoint.insert(QStringLiteral("port"), 21);
        QVariantMap record;
        record.insert(QStringLiteral("name"), QStringLiteral("控制器-01"));
        record.insert(QStringLiteral("endpoints"), QVariantList{endpoint});
        QVERIFY(ConfigStore::instance().save(QStringLiteral("device.profile"),
                                             QStringLiteral("manual-key"), record));

        DeviceRegistry registry;
        QVERIFY(registry.load());
        QCOMPARE(registry.list().size(), size_t(1));
        DeviceProfile probe;
        probe.name = QStringLiteral("控制器-01").toStdString();
        probe.endpoints.push_back({"ftp", "192.168.20.123", 21, {}});
        DeviceRegistry fresh;
        QCOMPARE(QString::fromStdString(registry.list().front().deviceId),
                 QString::fromStdString(fresh.upsert(probe).deviceId));
    }

    // 损坏记录被跳过且不阻断加载（回退读取）
    void skipsMalformedRecords()
    {
        QVariantMap noEndpoints;
        noEndpoints.insert(QStringLiteral("deviceId"), QStringLiteral("broken-1"));
        QVERIFY(ConfigStore::instance().save(QStringLiteral("device.profile"),
                                             QStringLiteral("broken-1"), noEndpoints));

        QVariantMap notAList;
        notAList.insert(QStringLiteral("endpoints"), QStringLiteral("oops"));
        QVERIFY(ConfigStore::instance().save(QStringLiteral("device.profile"),
                                             QStringLiteral("broken-2"), notAList));

        QVariantMap emptyIp;
        QVariantMap badEndpoint;
        badEndpoint.insert(QStringLiteral("ip"), QString());
        emptyIp.insert(QStringLiteral("endpoints"), QVariantList{badEndpoint});
        QVERIFY(ConfigStore::instance().save(QStringLiteral("device.profile"),
                                             QStringLiteral("broken-3"), emptyIp));

        DeviceRegistry registry;
        const auto valid = registry.upsert(makeProfile(QStringLiteral("控制器-01"),
                                                       QStringLiteral("192.168.20.123"), 21));
        QVERIFY(registry.save(valid));

        DeviceRegistry reloaded;
        QVERIFY(reloaded.load());
        QCOMPARE(reloaded.list().size(), size_t(1));
        QCOMPARE(QString::fromStdString(reloaded.list().front().deviceId),
                 QString::fromStdString(valid.deviceId));
    }

    // 持久化记录仅保存凭据引用，不含密码明文
    void storesCredentialReferenceOnly()
    {
        DeviceRegistry registry;
        const auto profile = registry.upsert(makeProfile(
            QStringLiteral("控制器-01"), QStringLiteral("192.168.20.123"), 21,
            QStringLiteral("ftp"), QStringLiteral("admin@192.168.20.123:21")));
        QVERIFY(registry.save(profile));

        const QVariantMap raw = ConfigStore::instance().load(
            QStringLiteral("device.profile"), QString::fromStdString(profile.deviceId));
        QVERIFY(!raw.isEmpty());
        QCOMPARE(raw.value(QStringLiteral("deviceId")).toString(),
                 QString::fromStdString(profile.deviceId));
        const QString json = QString::fromUtf8(
            QJsonDocument(QJsonObject::fromVariantMap(raw)).toJson(QJsonDocument::Compact));
        QVERIFY(!json.contains(QStringLiteral("password")));
        QVERIFY(!json.contains(QStringLiteral("username")));
        QVERIFY(json.contains(QStringLiteral("credentialRef")));
    }

    // 由旧 device.list 迁移来的档案：remove() 须同步删除匹配的旧行，删除必须持久
    void removeIsDurableForLegacyMigratedDevices()
    {
        QVariantMap legacyRow;
        legacyRow.insert(QStringLiteral("ip"), QStringLiteral("192.168.20.128"));
        legacyRow.insert(QStringLiteral("port"), 23);
        legacyRow.insert(QStringLiteral("protocol"), QStringLiteral("telnet"));
        legacyRow.insert(QStringLiteral("displayName"), QStringLiteral("现场柜-02"));
        QVERIFY(ConfigStore::instance().save(QStringLiteral("device.list"),
                                             QStringLiteral("192.168.20.128:23"), legacyRow));

        // 同库另一台旧设备：不应被误删（仅删除匹配被移除档案端点的旧行）
        QVariantMap otherRow;
        otherRow.insert(QStringLiteral("ip"), QStringLiteral("192.168.20.200"));
        otherRow.insert(QStringLiteral("port"), 21);
        otherRow.insert(QStringLiteral("displayName"), QStringLiteral("别-01"));
        QVERIFY(ConfigStore::instance().save(QStringLiteral("device.list"),
                                             QStringLiteral("192.168.20.200:21"), otherRow));

        DeviceRegistry registry;
        QVERIFY(registry.load());
        QCOMPARE(registry.list().size(), size_t(2));
        // 按端点 IP 定位迁移来的目标档案（list() 顺序不保证）
        DeviceProfile migrated;
        for (const auto& candidate : registry.list()) {
            if (candidate.name == QStringLiteral("现场柜-02").toStdString())
                migrated = candidate;
        }
        QVERIFY(!migrated.deviceId.empty());
        const std::string migratedKey = migrated.deviceId;

        QVERIFY(registry.remove(migrated.deviceId));
        QCOMPARE(registry.list().size(), size_t(1));

        // 全新 Registry 重新加载：被移除的旧迁移设备不得复活
        DeviceRegistry reloaded;
        QVERIFY(reloaded.load());
        QCOMPARE(reloaded.list().size(), size_t(1));
        QVERIFY(!reloaded.find(migratedKey).has_value());
        QVERIFY(!ConfigStore::instance().exists(QStringLiteral("device.list"),
                                                QStringLiteral("192.168.20.128:23")));
        // 未匹配的旧行保留（回退读取兼容不受影响）
        QVERIFY(ConfigStore::instance().exists(QStringLiteral("device.list"),
                                               QStringLiteral("192.168.20.200:21")));
    }

    // 删除后内存与 ConfigStore 同步，重载不再出现
    void removeDeletesPersistedRecord()
    {
        DeviceRegistry registry;
        const auto first = registry.upsert(makeProfile(QStringLiteral("控制器-01"),
                                                       QStringLiteral("192.168.20.123"), 21));
        const auto second = registry.upsert(makeProfile(QStringLiteral("控制器-02"),
                                                        QStringLiteral("192.168.20.124"), 22));
        QVERIFY(registry.save(first));
        QVERIFY(registry.save(second));
        QVERIFY(registry.remove(first.deviceId));
        QCOMPARE(registry.list().size(), size_t(1));
        QVERIFY(!ConfigStore::instance().exists(QStringLiteral("device.profile"),
                                                QString::fromStdString(first.deviceId)));

        DeviceRegistry reloaded;
        QVERIFY(reloaded.load());
        QCOMPARE(reloaded.list().size(), size_t(1));
        QCOMPARE(QString::fromStdString(reloaded.list().front().deviceId),
                 QString::fromStdString(second.deviceId));
    }

    // 终审 Critical 1：编辑器替换语义保存——改地址/删端点后旧端点必须消失，
    // 被取代的旧 device.list ip:port 行同步清扫，重载不复活；
    // 并集合并语义（save/upsert）仅保留给协议发现类调用方
    void saveReplacingDropsRemovedEndpointsAndSweepsSupersededLegacyRows()
    {
        DeviceRegistry registry;
        DeviceProfile profile = makeProfile(QStringLiteral("控制器-01"),
                                            QStringLiteral("10.6.0.1"), 21,
                                            QStringLiteral("ftp"),
                                            QStringLiteral("ref-ftp"));
        profile.endpoints.push_back({"telnet", "10.6.0.1", 23, {}});
        const auto stored = registry.upsert(profile);
        QVERIFY(registry.save(stored));
        const QString deviceId = QString::fromStdString(stored.deviceId);
        QVERIFY(!deviceId.isEmpty());

        // 旧 device.list 回退行：一行属于本档案被取代的地址，一行属于无关设备
        QVariantMap telnetLegacy;
        telnetLegacy.insert(QStringLiteral("ip"), QStringLiteral("10.6.0.1"));
        telnetLegacy.insert(QStringLiteral("port"), 23);
        telnetLegacy.insert(QStringLiteral("protocol"), QStringLiteral("telnet"));
        telnetLegacy.insert(QStringLiteral("displayName"), QStringLiteral("控制器-01"));
        QVERIFY(ConfigStore::instance().save(QStringLiteral("device.list"),
                                             QStringLiteral("10.6.0.1:23"), telnetLegacy));
        QVariantMap unrelatedLegacy;
        unrelatedLegacy.insert(QStringLiteral("ip"), QStringLiteral("10.6.0.9"));
        unrelatedLegacy.insert(QStringLiteral("port"), 21);
        unrelatedLegacy.insert(QStringLiteral("displayName"), QStringLiteral("别-01"));
        QVERIFY(ConfigStore::instance().save(QStringLiteral("device.list"),
                                             QStringLiteral("10.6.0.9:21"), unrelatedLegacy));

        // 编辑器动作：ftp 地址 ip1→ip2 且删除 telnet 端点（清单未列出即移除）
        DeviceProfile edited = stored;
        edited.endpoints = {{"ftp", "10.6.0.2", 21, "ref-ftp"}};
        DeviceProfile result;
        QVERIFY(registry.saveReplacing(edited, &result));
        QCOMPARE(result.deviceId, stored.deviceId);
        QCOMPARE(result.endpoints.size(), size_t(1));
        QCOMPARE(QString::fromStdString(endpointKey(result.endpoints.front())),
                 QStringLiteral("ftp://10.6.0.2:21"));

        // 内存档案只剩新端点（旧端点未被并集写回）
        const auto found = registry.find(stored.deviceId);
        QVERIFY(found.has_value());
        QCOMPARE(found->endpoints.size(), size_t(1));
        QCOMPARE(QString::fromStdString(found->endpoints.front().ip),
                 QStringLiteral("10.6.0.2"));

        // device.profile 落库内容同样只剩新端点
        const QVariantMap raw = ConfigStore::instance().load(
            QStringLiteral("device.profile"), deviceId);
        const auto decoded = decodeDeviceProfile(raw);
        QVERIFY(decoded.has_value());
        QCOMPARE(decoded->endpoints.size(), size_t(1));
        QCOMPARE(QString::fromStdString(decoded->endpoints.front().ip),
                 QStringLiteral("10.6.0.2"));

        // 被取代的旧 device.list 行已清扫；无关旧行保留（回退读取兼容）
        QVERIFY(!ConfigStore::instance().exists(QStringLiteral("device.list"),
                                                QStringLiteral("10.6.0.1:23")));
        QVERIFY(ConfigStore::instance().exists(QStringLiteral("device.list"),
                                               QStringLiteral("10.6.0.9:21")));

        // 重载：旧地址与旧行均不复活（本档案唯一端点为 ip2，另有无关旧设备一台）
        DeviceRegistry reloaded;
        QVERIFY(reloaded.load());
        QCOMPARE(reloaded.list().size(), size_t(2));
        const auto after = reloaded.find(stored.deviceId);
        QVERIFY(after.has_value());
        QCOMPARE(after->endpoints.size(), size_t(1));
        QCOMPARE(QString::fromStdString(after->endpoints.front().ip),
                 QStringLiteral("10.6.0.2"));
    }

    // 终审 Critical 1 对照：发现类调用方（save/upsert）保持并集合并不受影响
    void discoveryUpsertKeepsUnionMerge()
    {
        DeviceRegistry registry;
        const auto stored = registry.upsert(makeProfile(QStringLiteral("控制器-01"),
                                                        QStringLiteral("10.6.0.1"), 21));
        QVERIFY(registry.save(stored));
        DeviceProfile discovered;
        discovered.deviceId = stored.deviceId;
        discovered.endpoints.push_back({"telnet", "10.6.0.1", 23, {}});
        QVERIFY(registry.save(discovered));
        const auto found = registry.find(stored.deviceId);
        QVERIFY(found.has_value());
        QCOMPARE(found->endpoints.size(), size_t(2));
    }

    // ---- v2.14 #49-7 落盘失败快照回滚 ----
    //
    // 守卫实证（失败注入手段，用例开头现场背书）：`ConfigStore::instance().close()` 后
    // save() 必返回 false——代码面：save 首行 `if (!ensureOpen()) return false`
    // （ConfigStore.cpp:122-126），ensureOpen() = m_open && m_db.isValid() && m_db.isOpen()
    // （:26-29），close() 公开（ConfigStore.h:15）且把三要素全部清掉（ConfigStore.cpp:105-120）。
    // 结论：该注入是确定性手段，无需只读权限等环境配合；下方用例 a 首行运行时复核，
    // 探针行若注入失效（save 仍 true）直接红，用例 b/c 沿用同一注入。
    void saveFailureRollsBackMemory()
    {
        DeviceRegistry registry;
        QVERIFY(registry.save(makeProfile(QStringLiteral("控制器-甲"),
                                          QStringLiteral("10.0.0.1"), 21)));
        QVERIFY(registry.warnings().empty());
        const auto before = registry.list();
        QCOMPARE(before.size(), size_t(1));

        // 入站「乙」与他档重名（甲）：若修复缺失，失败后内存将残留 2 条档案
        // 且 rebuildWarnings 已产出「设备名称重复」——两者都是 RED 取证点
        const DeviceProfile incoming = makeProfile(QStringLiteral("控制器-甲"),
                                                   QStringLiteral("10.0.0.2"), 21);
        DeviceRegistry idProbe;
        const std::string ghostId = idProbe.upsert(incoming).deviceId;
        QVERIFY(!ghostId.empty());

        // 失败注入 + 守卫实证现场复核
        ConfigStore::instance().close();
        QVariantMap probe;
        probe.insert(QStringLiteral("x"), 1);
        QVERIFY2(!ConfigStore::instance().save(QStringLiteral("probe.guard"),
                                               QStringLiteral("probe-key"), probe),
                 "失败注入失效：close() 后 ConfigStore::save 未返回 false");

        QVERIFY(!registry.save(incoming));

        // 回滚：档案表与 warnings 均与调用前一致，乙未残留内存
        QCOMPARE(registry.list().size(), before.size());
        QVERIFY(!registry.find(ghostId).has_value());
        QVERIFY(registry.warnings().empty());

        // 重开临时库复核磁盘：只落过成功那一条，键与内存一致
        QVERIFY(ConfigStore::instance().open(m_dbPath));
        const auto rows = ConfigStore::instance().list(QStringLiteral("device.profile"), 1000);
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().value(QStringLiteral("key")).toString(),
                 QString::fromStdString(before.front().deviceId));
    }

    // saveReplacing 落盘失败：端点替换回滚、stored 不写、磁盘行未受污染
    // （清扫段本就只在成功路径执行——store 关闭期间磁盘不会变动，重开抽查为终态背书）
    void saveReplacingFailureRollsBackEndpointsAndSkipsSweep()
    {
        DeviceRegistry registry;
        DeviceProfile profile = makeProfile(QStringLiteral("控制器-01"),
                                            QStringLiteral("10.7.0.1"), 21,
                                            QStringLiteral("ftp"),
                                            QStringLiteral("ref-ftp"));
        profile.endpoints.push_back({"telnet", "10.7.0.1", 23, {}});
        const auto stored = registry.upsert(profile);
        QVERIFY(registry.save(stored));

        // 旧 device.list 回退行：属即将被替换删除的 telnet 地址
        QVariantMap telnetLegacy;
        telnetLegacy.insert(QStringLiteral("ip"), QStringLiteral("10.7.0.1"));
        telnetLegacy.insert(QStringLiteral("port"), 23);
        telnetLegacy.insert(QStringLiteral("protocol"), QStringLiteral("telnet"));
        telnetLegacy.insert(QStringLiteral("displayName"), QStringLiteral("控制器-01"));
        QVERIFY(ConfigStore::instance().save(QStringLiteral("device.list"),
                                             QStringLiteral("10.7.0.1:23"), telnetLegacy));

        ConfigStore::instance().close();   // 失败注入（守卫实证见上一用例）

        DeviceProfile edited = stored;
        edited.endpoints = {{"ftp", "10.7.0.2", 21, "ref-ftp"}};   // 改 ip 且删 telnet
        DeviceProfile result;
        QVERIFY(!registry.saveReplacing(edited, &result));

        // 内存回滚：档案仍是替换前两端口（未修时残留替换后的单端点 → RED）
        const auto found = registry.find(stored.deviceId);
        QVERIFY(found.has_value());
        QCOMPARE(found->endpoints.size(), size_t(2));
        QCOMPARE(QString::fromStdString(endpointKey(found->endpoints.at(0))),
                 QStringLiteral("ftp://10.7.0.1:21"));
        QVERIFY(registry.warnings().empty());

        // stored 出参失败时未填充——调用方（DeviceBusWidget）据此拿不到合并结果，
        // 「失败不广播 deviceProfileEdited / 不写 legacy 行」在存储层的可见性锚点
        QVERIFY(result.deviceId.empty());
        QVERIFY(result.endpoints.empty());

        // 重开复核磁盘：旧行未被清扫、device.profile 仍是两端点（替换未生效）
        QVERIFY(ConfigStore::instance().open(m_dbPath));
        QVERIFY(ConfigStore::instance().exists(QStringLiteral("device.list"),
                                               QStringLiteral("10.7.0.1:23")));
        const QVariantMap raw = ConfigStore::instance().load(
            QStringLiteral("device.profile"), QString::fromStdString(stored.deviceId));
        const auto decoded = decodeDeviceProfile(raw);
        QVERIFY(decoded.has_value());
        QCOMPARE(decoded->endpoints.size(), size_t(2));

        // 重载后与磁盘一致
        DeviceRegistry reloaded;
        QVERIFY(reloaded.load());
        const auto after = reloaded.find(stored.deviceId);
        QVERIFY(after.has_value());
        QCOMPARE(after->endpoints.size(), size_t(2));
    }

    // 用例 c：stableId/空 deviceId 分支可达性背书 + 可得路径的同等语义覆盖
    void stableIdFailureAlsoRollsBack()
    {
        // 可达性结论（运行时背书，详见任务报告 Round 2）：save :368 与 saveReplacing :226
        // 的 `deviceId.empty()` 分支不可达——upsert :165 与 saveReplacing :200 的新建分支
        // 对空 id 一律先赋 stableId()，而 stableId :134-135 恒非空（无端点且无名称时
        // identity="name:" 仍出哈希）；命中既有档案则继承非空 ID 不变量（load() 走
        // upsert 同保）。实证方式：临时在两个分支体写哨兵文件跑全量 48 目标，
        // 哨兵未出现。不硬凑不可达路径——本用例用 ConfigStore 失败路径覆盖同等语义：
        // 任一 return false 之前内存无半成品残留、stored 不被填充。
        DeviceRegistry registry;
        QVERIFY(registry.save(makeProfile(QStringLiteral("控制器-留存"),
                                          QStringLiteral("10.8.0.1"), 21)));
        const size_t beforeSize = registry.list().size();

        // 极端入站：名称纯空白（normalize 后为空串）、无端点、deviceId 空——
        // 最接近「无法生成稳定 ID」的构造
        DeviceProfile extreme;
        extreme.name = "   ";

        ConfigStore::instance().close();   // 失败注入（守卫实证见 saveFailureRollsBackMemory）

        QVERIFY(!registry.save(extreme));
        QCOMPARE(registry.list().size(), beforeSize);   // 半成品 push_back 已回滚

        DeviceProfile result;
        QVERIFY(!registry.saveReplacing(extreme, &result));
        QCOMPARE(registry.list().size(), beforeSize);   // saveReplacing 新建分支同样回滚
        QVERIFY(result.deviceId.empty());               // stored 仅成功时填充

        // 重开复核磁盘：仍只有成功落库那一条
        QVERIFY(ConfigStore::instance().open(m_dbPath));
        QCOMPARE(ConfigStore::instance().list(QStringLiteral("device.profile"), 1000).size(), 1);

        // 不变量正面背书：同一极端入站在落盘成功时 deviceId 必非空
        // （即空 ID 分支的进入条件永不成立，与哨兵实证互为对照）
        DeviceRegistry idProbe;
        QVERIFY(!idProbe.upsert(extreme).deviceId.empty());
    }

    // ---- v2.15 #49-8 全表语义迁移 ----

    // 清扫曾固定 list(type,1000)——超上限的 legacy 行漏扫残留，
    // load() 回退读取把旧地址复活。全表形态后必须一条不漏。
    void sweepAboveLegacyCapRemovesEverySupersededRow()
    {
        // 夹具：一份档案持有 1005 个旧 device.list 地址（key=ip:port，同 DeviceBusWidget），
        // 编辑器整表替换为不含上述任何地址的 guard 端点后，1005 行全部被取代，须被清扫删净
        DeviceRegistry registry;
        DeviceProfile bulky;
        bulky.name = QStringLiteral("批量旧地址").toStdString();
        for (int i = 0; i < 1005; ++i) {
            const QString ip = QStringLiteral("10.0.%1.%2").arg(i / 256).arg(i % 256);
            bulky.endpoints.push_back({"telnet", ip.toStdString(), 23, {}});
            QVariantMap row;
            row.insert(QStringLiteral("ip"), ip);
            row.insert(QStringLiteral("port"), 23);
            row.insert(QStringLiteral("protocol"), QStringLiteral("telnet"));
            QVERIFY(ConfigStore::instance().save(QStringLiteral("device.list"),
                                                 QStringLiteral("%1:23").arg(ip), row));
        }
        const auto stored = registry.upsert(bulky);
        QCOMPARE(stored.endpoints.size(), size_t(1005));
        QVERIFY(registry.save(stored));

        // 编辑器动作：端点清单整表替换为 guard 地址，触发被取代旧行清扫
        DeviceProfile edited = stored;
        edited.endpoints = {{"telnet", "192.0.2.1", 23, {}}};
        DeviceProfile result;
        QVERIFY(registry.saveReplacing(edited, &result));

        // 全表清扫：被取代的 1005 条旧行一条不漏（未修时截断 1000 → 残留 5 → RED）
        QCOMPARE(ConfigStore::instance().list(QStringLiteral("device.list")).size(), 0);

        // 行为面复证：重载后旧地址不回退复活，只剩 guard 档案一条一端点
        DeviceRegistry reloaded;
        QVERIFY(reloaded.load());
        QCOMPARE(reloaded.list().size(), size_t(1));
        QCOMPARE(reloaded.list().front().endpoints.size(), size_t(1));
    }
};

QTEST_MAIN(TstDeviceRegistryStore)
#include "tst_device_registry_store.moc"
