// v2.12 MODBUS 重构 Task 1：目标数据模型、持久化与旧配置迁移测试。
// 覆盖：地址标签格式化、编解码 round-trip、畸形记录防御、
// ConfigStore 保存/读取/删除、modbus.slave → modbus.target 迁移与失败回退。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QUuid>
#include <QVariantList>

#include "config/ConfigStore.h"
#include "device/DeviceRegistry.h"
#include "tools/ModbusTool/ModbusTargetStore.h"
#include "tools/ModbusTool/ModbusTargetTypes.h"

namespace {

// 构造一条含指定协议端点的设备档案（migration 用例的注册表数据源）
DeviceProfile makeProfile(const QString& deviceId, const QString& protocol,
                          const QString& ip, int port)
{
    DeviceProfile profile;
    profile.deviceId = deviceId.toStdString();
    profile.name = deviceId.toStdString();
    profile.endpoints.push_back({protocol.toStdString(), ip.toStdString(), port, std::string()});
    return profile;
}

// 种一条旧 modbus.slave 记录（regType 为旧界面上拉索引，与 ModbusWidget 写入口径一致）
void seedLegacySlaveRow(const QString& key, int regTypeComboIndex, int startAddr, int count,
                        int slaveId, int intervalMs)
{
    const QVariantMap row{
        {QStringLiteral("regType"), regTypeComboIndex},
        {QStringLiteral("startAddr"), startAddr},
        {QStringLiteral("count"), count},
        {QStringLiteral("slaveId"), slaveId},
        {QStringLiteral("intervalMs"), intervalMs},
    };
    QVERIFY(ConfigStore::instance().save(QStringLiteral("modbus.slave"), key, row));
}

// 在 targets 清单中按 slaveId 查找
const modbus::SlaveTarget* findTarget(const modbus::DeviceTargets& dt, int slaveId)
{
    for (const auto& t : dt.targets) {
        if (t.slaveId == slaveId) return &t;
    }
    return nullptr;
}

void comparePoll(const modbus::PollSpec& actual, QModbusDataUnit::RegisterType regType,
                 int startAddr, int count, int intervalMs)
{
    QCOMPARE(int(actual.regType), int(regType));
    QCOMPARE(actual.startAddr, startAddr);
    QCOMPARE(actual.count, count);
    QCOMPARE(actual.intervalMs, intervalMs);
}

} // namespace

class TstModbusTargetStore : public QObject
{
    Q_OBJECT

private:
    QString m_dbPath;
    QString m_workDir;

private slots:
    void initTestCase()
    {
        // 进程专属临时目录，避免 CI 残留/并发污染（同 tst_device_registry_store）
        m_workDir = QDir::temp().filePath(
            QStringLiteral("tst_modbus_target_store_%1")
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

    // 地址标签：4xxxx/3xxxx/1xxxx 基址 + 5 位零填充（简报数值逐字）
    void addressLabelFormatting_data()
    {
        QTest::addColumn<int>("regTypeRaw");
        QTest::addColumn<int>("addr");
        QTest::addColumn<QString>("expected");
        QTest::newRow("保持寄存器0") << int(QModbusDataUnit::HoldingRegisters) << 0
                                    << QStringLiteral("40001");
        QTest::newRow("输入寄存器0") << int(QModbusDataUnit::InputRegisters) << 0
                                    << QStringLiteral("30001");
        QTest::newRow("离散输入0") << int(QModbusDataUnit::DiscreteInputs) << 0
                                  << QStringLiteral("10001");
        QTest::newRow("线圈5") << int(QModbusDataUnit::Coils) << 5
                              << QStringLiteral("00005");
    }

    void addressLabelFormatting()
    {
        QFETCH(int, regTypeRaw);
        QFETCH(int, addr);
        QFETCH(QString, expected);
        const auto type = QModbusDataUnit::RegisterType(regTypeRaw);
        QCOMPARE(modbus::addressLabel(type, addr), expected);
    }

    // 目标 ID 组成：<deviceId>#<slaveId>
    void makeTargetIdFormatting()
    {
        QCOMPARE(modbus::makeTargetId("plc-01", 3), QStringLiteral("plc-01#3"));
    }

    // 编解码 round-trip：多从站、enabled、tagSchema=0
    void codecRoundTrip()
    {
        modbus::DeviceTargets source;
        source.deviceId = "plc-01";
        source.tagSchema = 0;
        modbus::SlaveTarget t1;
        t1.slaveId = 1;
        t1.enabled = true;
        t1.poll = {QModbusDataUnit::HoldingRegisters, 0, 10, 500};
        modbus::SlaveTarget t2;
        t2.slaveId = 2;
        t2.enabled = false;
        t2.poll = {QModbusDataUnit::Coils, 5, 8, 1000};
        source.targets.push_back(t1);
        source.targets.push_back(t2);

        const QVariantMap record = modbus::encodeDeviceTargets(source);
        const auto decoded = modbus::decodeDeviceTargets(record);
        QVERIFY(decoded.has_value());
        QCOMPARE(decoded->deviceId, source.deviceId);
        QCOMPARE(decoded->tagSchema, 0);
        QCOMPARE(decoded->targets.size(), 2);
        QCOMPARE(decoded->targets[0].slaveId, 1);
        QCOMPARE(decoded->targets[0].enabled, true);
        comparePoll(decoded->targets[0].poll, QModbusDataUnit::HoldingRegisters, 0, 10, 500);
        QCOMPARE(decoded->targets[1].slaveId, 2);
        QCOMPARE(decoded->targets[1].enabled, false);
        comparePoll(decoded->targets[1].poll, QModbusDataUnit::Coils, 5, 8, 1000);
    }

    // 畸形记录防御：缺 deviceId/targets/非法 regType 越界 → nullopt；
    // 缺可选数值字段 → 安全默认（结构体内联初值）
    void decodeMalformedFallsBack()
    {
        // 空记录 → nullopt
        QVERIFY(!modbus::decodeDeviceTargets(QVariantMap()).has_value());

        // 缺 deviceId → nullopt
        QVariantMap noId;
        noId.insert(QStringLiteral("targets"), QVariantList());
        QVERIFY(!modbus::decodeDeviceTargets(noId).has_value());

        // 缺 targets → nullopt
        QVariantMap noTargets;
        noTargets.insert(QStringLiteral("deviceId"), QStringLiteral("plc-01"));
        QVERIFY(!modbus::decodeDeviceTargets(noTargets).has_value());

        // regType 越界（99 与 0=Invalid）→ nullopt
        const auto makeWithRegType = [](int regTypeRaw) {
            QVariantMap item;
            item.insert(QStringLiteral("slaveId"), 1);
            item.insert(QStringLiteral("regType"), regTypeRaw);
            QVariantList list;
            list.append(item);
            QVariantMap record;
            record.insert(QStringLiteral("deviceId"), QStringLiteral("plc-01"));
            record.insert(QStringLiteral("targets"), list);
            return record;
        };
        QVERIFY(!modbus::decodeDeviceTargets(makeWithRegType(99)).has_value());
        QVERIFY(!modbus::decodeDeviceTargets(makeWithRegType(0)).has_value());

        // 缺可选数值字段 → 安全默认（HoldingRegisters/0/1/1000、enabled=true）
        QVariantMap item;
        item.insert(QStringLiteral("slaveId"), 7);
        QVariantList list;
        list.append(item);
        QVariantMap record;
        record.insert(QStringLiteral("deviceId"), QStringLiteral("plc-02"));
        record.insert(QStringLiteral("targets"), list);
        const auto decoded = modbus::decodeDeviceTargets(record);
        QVERIFY(decoded.has_value());
        QCOMPARE(decoded->targets.size(), 1);
        QCOMPARE(decoded->targets[0].slaveId, 7);
        QCOMPARE(decoded->targets[0].enabled, true);
        comparePoll(decoded->targets[0].poll, QModbusDataUnit::HoldingRegisters, 0, 1, 1000);
    }

    // ConfigStore 持久化 round-trip：save → load → allDeviceIds → remove
    void saveLoadRemove()
    {
        modbus::ModbusTargetStore store;
        modbus::DeviceTargets dt;
        dt.deviceId = "dev-9";
        dt.tagSchema = 0;
        modbus::SlaveTarget t;
        t.slaveId = 3;
        t.enabled = true;
        t.poll = {QModbusDataUnit::InputRegisters, 12, 4, 250};
        dt.targets.push_back(t);

        QVERIFY(store.save(dt));
        modbus::DeviceTargets got;
        QVERIFY(store.load("dev-9", &got));
        QCOMPARE(got.deviceId, std::string("dev-9"));
        QCOMPARE(got.targets.size(), 1);
        QCOMPARE(got.targets[0].slaveId, 3);
        comparePoll(got.targets[0].poll, QModbusDataUnit::InputRegisters, 12, 4, 250);

        const auto ids = store.allDeviceIds();
        QCOMPARE(ids.size(), std::size_t(1));
        QCOMPARE(ids.at(0), std::string("dev-9"));

        QVERIFY(store.remove("dev-9"));
        QVERIFY(!store.load("dev-9", &got));
        QVERIFY(store.allDeviceIds().empty());
    }

    // 迁移：旧 modbus.slave 两行 + 一个含 modbus 端点档案 → 回填该设备的 targets
    void migrateLegacySlavesBackfills()
    {
        // 旧口径 regType 为下拉索引：0=保持寄存器、1=输入寄存器
        seedLegacySlaveRow(QStringLiteral("slave:1"), 0, 4, 8, 1, 250);
        seedLegacySlaveRow(QStringLiteral("slave:3"), 1, 0, 2, 3, 1000);

        DeviceRegistry registry;
        QVERIFY(registry.save(makeProfile(QStringLiteral("plc-77"), QStringLiteral("modbus"),
                                          QStringLiteral("10.0.0.7"), 502)));

        modbus::ModbusTargetStore store;
        QCOMPARE(store.migrateLegacySlaves(&registry), 1);

        modbus::DeviceTargets out;
        QVERIFY(store.load("plc-77", &out));
        QCOMPARE(out.deviceId, std::string("plc-77"));
        QCOMPARE(out.tagSchema, 0);
        QCOMPARE(out.targets.size(), 2);
        const auto* t1 = findTarget(out, 1);
        QVERIFY(t1 != nullptr);
        QCOMPARE(t1->enabled, true);
        comparePoll(t1->poll, QModbusDataUnit::HoldingRegisters, 4, 8, 250);
        const auto* t3 = findTarget(out, 3);
        QVERIFY(t3 != nullptr);
        QCOMPARE(t3->enabled, true);
        comparePoll(t3->poll, QModbusDataUnit::InputRegisters, 0, 2, 1000);

        // 旧记录不删除
        QCOMPARE(ConfigStore::instance().list(QStringLiteral("modbus.slave")).size(), 2);
    }

    // 迁移失败回退：nullptr / 无 modbus 设备 → 返回 0、不崩、旧行仍可读且可回退合成
    void migrationFailureFallback()
    {
        seedLegacySlaveRow(QStringLiteral("slave:1"), 0, 0, 10, 1, 1000);
        modbus::ModbusTargetStore store;

        // registry 空指针 → 0 且旧行仍可读
        QCOMPARE(store.migrateLegacySlaves(nullptr), 0);
        QVERIFY(!ConfigStore::instance().load(QStringLiteral("modbus.slave"),
                                              QStringLiteral("slave:1")).isEmpty());

        // 注册表无 modbus 端点设备 → 0 且不落任何 modbus.target
        DeviceRegistry registry;
        QVERIFY(registry.save(makeProfile(QStringLiteral("ftp-01"), QStringLiteral("ftp"),
                                          QStringLiteral("10.0.0.9"), 21)));
        QCOMPARE(store.migrateLegacySlaves(&registry), 0);
        QVERIFY(store.allDeviceIds().empty());

        // 新结构缺失时 load 回退读旧行合成临时 targets（仍不写库）
        modbus::DeviceTargets dt;
        QVERIFY(store.load("ftp-01", &dt));
        QCOMPARE(dt.deviceId, std::string("ftp-01"));
        QCOMPARE(dt.targets.size(), 1);
        QCOMPARE(dt.targets[0].slaveId, 1);
        comparePoll(dt.targets[0].poll, QModbusDataUnit::HoldingRegisters, 0, 10, 1000);
        QVERIFY(store.allDeviceIds().empty());
    }
};

QTEST_GUILESS_MAIN(TstModbusTargetStore)
#include "tst_modbus_target_store.moc"
