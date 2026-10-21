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

    // #46：保存前同键检测——他档「协议+ip+port」全同端点须报冲突（含归一化口径）
    void endpointConflicts_flagSameKeyInOtherProfile()
    {
        DeviceRegistry registry;
        DeviceProfile existing;
        existing.name = "PLC-A";
        existing.endpoints.push_back({"ftp", "192.168.20.123", 21, {}});
        registry.upsert(existing);

        DeviceProfile incoming;
        incoming.deviceId = "device-other";            // 他档（编辑自身之外的档案）
        incoming.name = "PLC-B";
        // 归一化口径必须一致命中：协议大写、ip 带空格、port 0 → 默认 21
        incoming.endpoints.push_back({" FTP ", " 192.168.20.123 ", 0, {}});
        const auto conflicts = registry.endpointConflicts(incoming);
        QCOMPARE(conflicts.size(), size_t(1));
        QVERIFY(QString::fromStdString(conflicts.front()).contains(QStringLiteral("ftp://192.168.20.123:21")));
        QVERIFY(QString::fromStdString(conflicts.front()).contains(QStringLiteral("PLC-A")));
    }

    // #46：编辑自身不算冲突（deviceId 排除）
    void endpointConflicts_excludesSelfByDeviceId()
    {
        DeviceRegistry registry;
        DeviceProfile profile;
        profile.name = "PLC-A";
        profile.endpoints.push_back({"ftp", "192.168.20.123", 21, {}});
        const auto saved = registry.upsert(profile);

        DeviceProfile editing = saved;                  // 同 deviceId，端点原样保留
        editing.endpoints.push_back({"telnet", "192.168.20.123", 23, {}});
        QVERIFY(registry.endpointConflicts(editing).empty());
    }

    // #46：同 ip:port 不同协议不算冲突
    void endpointConflicts_ignoresDifferentProtocol()
    {
        DeviceRegistry registry;
        DeviceProfile profile;
        profile.name = "PLC-A";
        profile.endpoints.push_back({"ftp", "192.168.20.123", 21, {}});
        registry.upsert(profile);

        DeviceProfile incoming;
        incoming.deviceId = "device-other";
        incoming.endpoints.push_back({"sftp", "192.168.20.123", 21, {}});
        QVERIFY(registry.endpointConflicts(incoming).empty());
    }

    // #46：多个冲突端点逐条报告；无冲突返回空
    void endpointConflicts_reportsEachCollision()
    {
        DeviceRegistry registry;
        DeviceProfile a;
        a.name = "PLC-A";
        a.endpoints.push_back({"ftp", "192.168.20.123", 21, {}});
        registry.upsert(a);
        DeviceProfile b;
        b.name = "PLC-B";
        b.endpoints.push_back({"telnet", "192.168.20.124", 23, {}});
        registry.upsert(b);

        DeviceProfile incoming;
        incoming.deviceId = "device-new";
        incoming.endpoints.push_back({"ftp", "192.168.20.123", 21, {}});
        incoming.endpoints.push_back({"telnet", "192.168.20.124", 23, {}});
        incoming.endpoints.push_back({"ssh", "192.168.20.125", 22, {}});
        const auto conflicts = registry.endpointConflicts(incoming);
        QCOMPARE(conflicts.size(), size_t(2));
    }

    // v2.16-B §4：DeviceRegistry::normalize 的标签分支改用 devicebus::normalizeTag
    // （经 upsert 路径）。钉住等值口径——去首尾空白、丢空串、去重、保序，与旧
    // trim+appendUnique 一致；「大小写与内部空格不动」由 selector 侧守卫用例（
    // tst_task_center_widget）与 normalizeTag 纯头用例共同守住，此处不重复。
    // " / "（纯分隔符）按 §2.1 判空不入库——这正是单源化收口的口径。
    void normalizeTagSurroundingSpacesTrimmedAndEmptiesDropped()
    {
        DeviceRegistry registry;
        DeviceProfile profile;
        profile.endpoints.push_back({"ftp", "192.168.20.150", 21, {}});
        profile.tags = {" 车间A ", "", "   ", "车间A", " / ", "夜班 "};
        const auto saved = registry.upsert(profile);

        const std::vector<std::string> expected{"车间A", "夜班"};
        QCOMPARE(saved.tags.size(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
            QCOMPARE(QString::fromStdString(saved.tags[i]),
                     QString::fromStdString(expected[i]));
    }
};

QTEST_MAIN(TstDeviceRegistry)
#include "tst_device_registry.moc"
