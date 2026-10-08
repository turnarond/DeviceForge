#include <QtTest>

#include "device/DeviceRegistry.h"

class TstDeviceRegistry : public QObject
{
    Q_OBJECT

private slots:
    void generatesStableIdForNormalizedEndpoint()
    {
        DeviceRegistry registry;
        DeviceProfile first;
        first.endpoints.push_back({" FTP ", " 192.168.20.123 ", 21, "ftp-main"});

        DeviceProfile second;
        second.endpoints.push_back({"ftp", "192.168.20.123", 21, "ftp-main"});

        const auto savedFirst = registry.upsert(first);
        const auto savedSecond = registry.upsert(second);

        QVERIFY(!savedFirst.deviceId.empty());
        QCOMPARE(QString::fromStdString(savedFirst.deviceId),
                 QString::fromStdString(savedSecond.deviceId));
        QCOMPARE(registry.list().size(), size_t(1));
    }

    void mergesEndpointsAcrossProtocolsForSameDevice()
    {
        DeviceRegistry registry;
        DeviceProfile profile;
        profile.name = "控制器-01";
        profile.endpoints.push_back({"ftp", "192.168.20.123", 21, "ftp-main"});
        const auto saved = registry.upsert(profile);

        DeviceProfile update;
        update.deviceId = saved.deviceId;
        update.endpoints.push_back({"SSH", "192.168.20.123", 22, "shell-main"});
        const auto merged = registry.upsert(update);

        QCOMPARE(merged.endpoints.size(), size_t(2));
        QCOMPARE(QString::fromStdString(merged.name), QStringLiteral("控制器-01"));
        QCOMPARE(QString::fromStdString(endpointKey(merged.endpoints.at(1))),
                 QStringLiteral("ssh://192.168.20.123:22"));
    }

    void mapsAliasAndFallsBackToAddress()
    {
        DeviceInfo legacy{"192.168.20.128", 23, "telnet", "现场柜-02", "A线"};
        const auto profile = fromDeviceInfo(legacy);

        QCOMPARE(QString::fromStdString(profile.name), QStringLiteral("现场柜-02"));
        QCOMPARE(QString::fromStdString(profile.note), QStringLiteral("A线"));
        QCOMPARE(profile.endpoints.size(), size_t(1));

        const auto mapped = toDeviceInfo(profile, profile.endpoints.front());
        QCOMPARE(QString::fromStdString(mapped.alias), QStringLiteral("现场柜-02"));

        DeviceProfile unnamed;
        unnamed.endpoints.push_back({"telnet", "192.168.20.128", 23, {}});
        const auto fallback = toDeviceInfo(unnamed, unnamed.endpoints.front());
        QCOMPARE(QString::fromStdString(fallback.alias), QStringLiteral("192.168.20.128:23"));
    }

    void recordsDuplicateDisplayNameWarning()
    {
        DeviceRegistry registry;
        DeviceProfile first;
        first.name = "产线A";
        first.endpoints.push_back({"ftp", "192.168.20.121", 21, {}});
        registry.upsert(first);

        DeviceProfile second;
        second.name = "产线A";
        second.endpoints.push_back({"ftp", "192.168.20.122", 21, {}});
        registry.upsert(second);

        QCOMPARE(registry.list().size(), size_t(2));
        QCOMPARE(registry.warnings().size(), size_t(1));
        QVERIFY(QString::fromStdString(registry.warnings().front()).contains(QStringLiteral("产线A")));
    }
};

QTEST_MAIN(TstDeviceRegistry)
#include "tst_device_registry.moc"
