/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: tst_device_bus_groups.cpp
 *
 * Date: 2026-10-20
 *
 * Author: turnarond
 *
 * Description: v2.16-B 设备栏分组与打开路由纯头回归：devicebus::{normalizeTag,
 *              normalizedTags, hasNormalizedTag, ungroupedTag, matchesFilter, segments}
 *              覆盖规格 §2.1 五条规则（N 标签 N 组/未分组兜底、组序与未分组永远最后、
 *              命中过滤 + total、全不命中出空表、零成员组不出结果）、§2.4 同组去重，
 *              以及 Segment 成员载体 profileIndexes 的空 deviceId 钉桩；
 *              deviceopen::{routeTo, Page, Route} 覆盖规格 §2.2（五类协议映射、
 *              扫描跳过不可用端点取首个可解析者且 ip/port 与路由同源、旧行空协议
 *              ftp 兜底、无可用端点 Page::None + 文案；opcua 仅纯头级，UI 不可达）。
 *              纯逻辑、无 QWidget、无 ConfigStore。
 */
#include <QtTest>

#include <QSet>
#include <QStringList>
#include <vector>

#include "device/DeviceProfile.h"
#include "device/DeviceBusGroups.h"
#include "ui/DeviceOpenRouter.h"

namespace {

DeviceProfile makeProfile(const QString& deviceId, const QString& name,
                          const QStringList& tags,
                          const QString& protocol = QStringLiteral("ftp"),
                          const QString& ip = QStringLiteral("10.0.0.1"),
                          int port = 21)
{
    DeviceProfile p;
    p.deviceId = deviceId.toStdString();
    p.name = name.toStdString();
    for (const auto& tag : tags) p.tags.push_back(tag.toStdString());
    if (!protocol.isEmpty())
        p.endpoints.push_back({protocol.toStdString(), ip.toStdString(), port, {}});
    return p;
}

} // namespace

namespace {

// 断言辅助：按标签找段（避免测试依赖段序——段序由专门的规则 2 用例钉死）
int indexOfTag(const QList<devicebus::Segment>& segs, const QString& tag)
{
    for (int i = 0; i < segs.size(); ++i)
        if (segs[i].tag == tag) return i;
    return -1;
}

} // namespace

class TstDeviceBusGroups : public QObject {
    Q_OBJECT

private slots:
    // normalizeTag 边界（规格 §5）：空格/空/纯分隔符/大小写不动/内部空格不动
    void normalizeTagTrimsOnly()
    {
        QVERIFY(devicebus::normalizeTag(QStringLiteral(" 车间A ")) == QStringLiteral("车间A"));
        QVERIFY(devicebus::normalizeTag(QString()).isEmpty());                          // 空进空出
        QVERIFY(devicebus::normalizeTag(QStringLiteral("   ")).isEmpty());              // 纯空白 → 空
        QVERIFY(devicebus::normalizeTag(QStringLiteral("/|,;、")).isEmpty());           // 纯分隔符 → 空
        QVERIFY(devicebus::normalizeTag(QStringLiteral("A b ")) == QStringLiteral("A b")); // 内部空格不动
        QVERIFY(devicebus::normalizeTag(QStringLiteral(" Line2 ")) == QStringLiteral("Line2")); // 大小写不动
    }

    // hasNormalizedTag（规格 §4，任务 3 圈选复用的面）：selector 与档案两侧都先规范化；
    // selector 规范化后为空 ⇒ 恒 false，绝不与任何档案互相命中——含无标签档案与
    // 「标签规范化后全空」档案（这两类正是 segments 里落「未分组」的那两型）
    void hasNormalizedTagNormalizesBothSidesAndRejectsEmptySelector()
    {
        // 档案标签自带首尾空白——只有 selector 侧也走 normalizeTag 才对得上
        const DeviceProfile tagged = makeProfile(QStringLiteral("d1"), QStringLiteral("plc1"),
                                                 {QStringLiteral(" 车间A "), QStringLiteral("夜班")});
        QVERIFY(devicebus::hasNormalizedTag(tagged, QStringLiteral(" 车间A ")));  // 匹配臂（空白 selector 规范化后命中）
        QVERIFY(devicebus::hasNormalizedTag(tagged, QStringLiteral("车间A")));    // 档案侧规范化后命中
        QVERIFY(!devicebus::hasNormalizedTag(tagged, QStringLiteral("车间B")));   // 对照组：真不命中
        const DeviceProfile untagged = makeProfile(QStringLiteral("d2"), QStringLiteral("plc2"), {});
        const DeviceProfile junkTags = makeProfile(QStringLiteral("d3"), QStringLiteral("plc3"),
                                                   {QStringLiteral(" / "), QStringLiteral(",;、")});
        // 空 selector 臂（DeviceBusGroups.h 的 `if (wanted.isEmpty()) return false;` 分支）：
        // 三类档案（有可用标签/无标签/标签规范化后全空）× 三种空写法（纯空白/纯分隔符/空串）⇒ 全 false
        QVERIFY(!devicebus::hasNormalizedTag(tagged, QStringLiteral("   ")));
        QVERIFY(!devicebus::hasNormalizedTag(tagged, QStringLiteral(" / ")));
        QVERIFY(!devicebus::hasNormalizedTag(tagged, QString()));
        QVERIFY(!devicebus::hasNormalizedTag(untagged, QStringLiteral("   ")));
        QVERIFY(!devicebus::hasNormalizedTag(untagged, QStringLiteral("、|;")));
        QVERIFY(!devicebus::hasNormalizedTag(junkTags, QStringLiteral("   ")));     // 空 selector 绝不命中空标签档案
        QVERIFY(!devicebus::hasNormalizedTag(junkTags, QStringLiteral(" / ")));
        QVERIFY(!devicebus::hasNormalizedTag(junkTags, QString()));
        // 夹具形状核对：junkTags 规范化后确实一个标签都不剩（否则上面的 false 断言是空转）；
        // 而 tagged 用同一个函数、同一个"带空白"写法能返回 true ⇒ 这批 false 不是恒假（防恒真）
        const QStringList noTagsExpected;
        QCOMPARE(devicebus::normalizedTags(junkTags), noTagsExpected);
        QVERIFY(devicebus::hasNormalizedTag(tagged, QStringLiteral(" 夜班 ")));  // 同函数真分支对照
    }

    // 规则 1：一台设备 2 个标签 → 两个分组各出现一次
    void multiTagDeviceAppearsInEachSegment()
    {
        std::vector<DeviceProfile> profiles;
        profiles.push_back(makeProfile(QStringLiteral("d1"), QStringLiteral("plc1"),
                                       {QStringLiteral("车间A"), QStringLiteral("夜班")}));
        const QList<devicebus::Segment> segs = devicebus::segments(profiles, QString());
        QCOMPARE(segs.size(), 2);
        const int a = indexOfTag(segs, QStringLiteral("车间A"));
        const int b = indexOfTag(segs, QStringLiteral("夜班"));
        QVERIFY(a >= 0 && b >= 0);
        const QList<int> onlyD1{0};
        QCOMPARE(segs[a].profileIndexes, onlyD1);
        QCOMPARE(segs[a].total, 1);
        QCOMPARE(segs[b].profileIndexes, onlyD1);
        QCOMPARE(segs[b].total, 1);
    }

    // 规则 1：无标签 / 规范化后全空 → 只进「未分组」（默认分组）
    void untaggedDeviceGoesToUngrouped()
    {
        std::vector<DeviceProfile> profiles;
        profiles.push_back(makeProfile(QStringLiteral("d1"), QStringLiteral("plc1"), {}));
        profiles.push_back(makeProfile(QStringLiteral("d2"), QStringLiteral("plc2"),
                                       {QStringLiteral(" / "), QStringLiteral(",;、|")}));
        const QList<devicebus::Segment> segs = devicebus::segments(profiles, QString());
        QCOMPARE(segs.size(), 1);
        QCOMPARE(segs[0].tag, devicebus::ungroupedTag());
        const QList<int> both{0, 1};
        QCOMPARE(segs[0].profileIndexes, both);   // registry 顺序：d1 先 d2 后
        QCOMPARE(segs[0].total, 2);
    }

    // 规则 2：标签组按 tag 升序（区分大小写），「未分组」永远最后（不依赖 QMap 键序）
    void segmentsSortedCaseSensitiveUngroupedLast()
    {
        std::vector<DeviceProfile> profiles;
        profiles.push_back(makeProfile(QStringLiteral("d1"), QStringLiteral("n1"),
                                       {QStringLiteral("车间X")}));
        profiles.push_back(makeProfile(QStringLiteral("d2"), QStringLiteral("n2"),
                                       {QStringLiteral("b")}));
        profiles.push_back(makeProfile(QStringLiteral("d3"), QStringLiteral("n3"), {}));
        profiles.push_back(makeProfile(QStringLiteral("d4"), QStringLiteral("n4"),
                                       {QStringLiteral("A")}));
        const QList<devicebus::Segment> segs = devicebus::segments(profiles, QString());
        QCOMPARE(segs.size(), 4);
        QCOMPARE(segs[0].tag, QStringLiteral("A"));      // A(0x41) < b(0x62)：区分大小写升序
        QCOMPARE(segs[1].tag, QStringLiteral("b"));
        QCOMPARE(segs[2].tag, QStringLiteral("车间X"));   // 未分组(0x672A) 必须排在它后面
        QCOMPARE(segs[3].tag, devicebus::ungroupedTag()); // 永远最后
    }

    // 规则 3：profileIndexes 只留命中，total 仍是组内全量；0 命中的组不进结果
    void filteredSegmentReportsHitOverTotal()
    {
        std::vector<DeviceProfile> profiles;
        profiles.push_back(makeProfile(QStringLiteral("d1"), QStringLiteral("plc-one"),
                                       {QStringLiteral("车间A")}));
        profiles.push_back(makeProfile(QStringLiteral("d2"), QStringLiteral("plc-two"),
                                       {QStringLiteral("车间A")}));
        profiles.push_back(makeProfile(QStringLiteral("d3"), QStringLiteral("other"),
                                       {QStringLiteral("夜班")}));
        const QList<devicebus::Segment> segs = devicebus::segments(profiles, QStringLiteral("plc"));
        QCOMPARE(segs.size(), 1);                        // 「夜班」1 成员 0 命中 → 不列
        QCOMPARE(segs[0].tag, QStringLiteral("车间A"));
        const QList<int> hits{0, 1};                     // d1/d2 命中，d3 不命中
        QCOMPARE(segs[0].profileIndexes, hits);          // 只留命中
        QCOMPARE(segs[0].total, 2);                      // 命中/总 = 2/2
    }

    // 规则 5：没有成员（或命中为 0）的「未分组」不进结果——菜单里不该有空壳组
    void emptyUngroupedSegmentNeverAppears()
    {
        std::vector<DeviceProfile> profiles;
        profiles.push_back(makeProfile(QStringLiteral("d1"), QStringLiteral("plc-one"),
                                       {QStringLiteral("车间A")}));
        profiles.push_back(makeProfile(QStringLiteral("d2"), QStringLiteral("router-plc"), {}));
        // ① 不过滤：d2 落「未分组」，该组非空 → 必须出现（对照组，防 ②假绿）
        const QList<devicebus::Segment> all = devicebus::segments(profiles, QString());
        QCOMPARE(all.size(), 2);
        QVERIFY(indexOfTag(all, devicebus::ungroupedTag()) >= 0);
        // ② 过滤 "one"：只有 d1("plc-one") 命中（"plc" 会同时命中 d2("router-plc")，
        //    故选 "one"）；「未分组」唯一成员 d2 不命中 ⇒ 0 命中组不出现在结果
        const QList<devicebus::Segment> hit = devicebus::segments(profiles, QStringLiteral("one"));
        QCOMPARE(hit.size(), 1);
        QCOMPARE(hit[0].tag, QStringLiteral("车间A"));
        QVERIFY(indexOfTag(hit, devicebus::ungroupedTag()) < 0);
    }

    // 规则 4：全不命中 ⇒ 结果为空列表；「无匹配设备（N 台）」说明行由 UI 侧给
    //（N 取设备总数 = profiles.size()，纯头不参与文案）
    void allMissProducesNoSegmentsOnlyHint()
    {
        std::vector<DeviceProfile> profiles;
        profiles.push_back(makeProfile(QStringLiteral("d1"), QStringLiteral("plc-one"),
                                       {QStringLiteral("车间A")}));
        profiles.push_back(makeProfile(QStringLiteral("d2"), QStringLiteral("plc-two"),
                                       {QStringLiteral("车间A"), QStringLiteral("夜班")}));
        profiles.push_back(makeProfile(QStringLiteral("d3"), QStringLiteral("router"), {}));
        const QList<devicebus::Segment> segs = devicebus::segments(profiles, QStringLiteral("zzz-miss"));
        QVERIFY(segs.isEmpty());
    }

    // matchesFilter：三源（名称/IP/标签）、子串、大小写不敏感；空过滤 = 全命中
    void matchesFilterAcrossNameIpTag()
    {
        const DeviceProfile p = makeProfile(QStringLiteral("d1"), QStringLiteral("PLC-One"),
                                            {QStringLiteral("车间A"), QStringLiteral("夜班")},
                                            QStringLiteral("ftp"), QStringLiteral("192.168.7.31"), 21);
        QVERIFY(devicebus::matchesFilter(p, QStringLiteral("plc-one")));  // 名称，大小写不敏感
        QVERIFY(devicebus::matchesFilter(p, QStringLiteral("168.7")));    // IP 子串
        QVERIFY(devicebus::matchesFilter(p, QStringLiteral("车间")));      // 规范化标签子串
        QVERIFY(devicebus::matchesFilter(p, QStringLiteral(" 夜班 ")));    // 过滤词先 trim
        QVERIFY(devicebus::matchesFilter(p, QString()));                  // 空过滤 = 全命中
        QVERIFY(!devicebus::matchesFilter(p, QStringLiteral("modbus")));   // 协议名不是匹配源
        QVERIFY(!devicebus::matchesFilter(p, QStringLiteral("192.168.8")));
    }

    // §2.4：同一台设备在同组内不重复（规范化后同名的多写法去重）；跨组各一次是对的
    void deviceIdAppearsOncePerSegmentOnly()
    {
        std::vector<DeviceProfile> profiles;
        profiles.push_back(makeProfile(QStringLiteral("d1"), QStringLiteral("plc"),
                                       {QStringLiteral("车间A"), QStringLiteral(" 车间A "),
                                        QStringLiteral("夜班")}));
        const QList<devicebus::Segment> segs = devicebus::segments(profiles, QString());
        QCOMPARE(segs.size(), 2);                          // 跨组各一次：车间A + 夜班
        const int a = indexOfTag(segs, QStringLiteral("车间A"));
        QVERIFY(a >= 0);
        QCOMPARE(segs[a].profileIndexes.size(), 1);        // 同组内不重复
        QCOMPARE(segs[a].total, 1);                        // total 是成员数不是标签数
        const int b = indexOfTag(segs, QStringLiteral("夜班"));
        QVERIFY(b >= 0);
        QCOMPARE(segs[b].profileIndexes.size(), 1);
    }

    // 验证项 1（v2.16-B 任务 1 裁定）：legacy 合成路径可能给空 deviceId；两台空 id 设备
    // 必须各归各组成员。载体为 profileIndexes——按 deviceId 反查的形态（初稿 find_if）会把
    // 第二条成员错认成第一条，此用例当场红出（emptyDeviceIdProfilesStillGroup 即该裁定证据）
    void emptyDeviceIdProfilesStillGroup()
    {
        std::vector<DeviceProfile> profiles;
        profiles.push_back(makeProfile(QString(), QStringLiteral("alpha"),
                                       {QStringLiteral("车间A")}));
        profiles.push_back(makeProfile(QString(), QStringLiteral("beta"),
                                       {QStringLiteral("车间A")}));
        // 不过滤：空 deviceId 也照常入组，两台都在
        const QList<devicebus::Segment> all = devicebus::segments(profiles, QString());
        QCOMPARE(all.size(), 1);
        QCOMPARE(all[0].total, 2);
        QCOMPARE(all[0].profileIndexes.size(), 2);
        // 过滤只命中 p2("beta")：命中成员必须是 1 个、且是 p2（下标 1）
        const QList<devicebus::Segment> hit = devicebus::segments(profiles, QStringLiteral("beta"));
        QCOMPARE(hit.size(), 1);
        QCOMPARE(hit[0].total, 2);
        const QList<int> onlyP2{1};
        QCOMPARE(hit[0].profileIndexes, onlyP2);
    }

    // §2.2 路由：ftp/ftps/sftp → FtpDeploy，且 Route 必须同携带命中端点的 ip/port
    //（部署页下拉的条目键就是纯 ip——路由与选中必须同源，FtpDeployWidget.cpp:1544/1563）
    void routeFtpFamilyGoesToDeployPage()
    {
        const auto ftpProfile = makeProfile(QStringLiteral("dev-f"), QStringLiteral("甲"),
                                            QStringList{}, QStringLiteral("ftp"),
                                            QStringLiteral("10.0.0.5"), 21);
        const auto ftpRoute = deviceopen::routeTo(ftpProfile);
        QCOMPARE(ftpRoute.page, deviceopen::Page::FtpDeploy);
        QCOMPARE(ftpRoute.ip, QStringLiteral("10.0.0.5"));
        QCOMPARE(ftpRoute.port, 21);

        const auto sftpProfile = makeProfile(QStringLiteral("dev-s"), QStringLiteral("乙"),
                                             QStringList{}, QStringLiteral("sftp"),
                                             QStringLiteral("10.0.0.7"), 22);
        const auto sftpRoute = deviceopen::routeTo(sftpProfile);
        QCOMPARE(sftpRoute.page, deviceopen::Page::FtpDeploy);
        QCOMPARE(sftpRoute.ip, QStringLiteral("10.0.0.7"));   // 部署页下拉的键就是纯 ip
        QCOMPARE(sftpRoute.port, 22);

        // 协议比对口径：trim + 小写（" FtpS " 也必须命中）
        const auto ftpsProfile = makeProfile(QStringLiteral("dev-x"), QStringLiteral("丙"),
                                             QStringList{}, QStringLiteral(" FtpS "),
                                             QStringLiteral("10.0.0.9"), 990);
        const auto ftpsRoute = deviceopen::routeTo(ftpsProfile);
        QCOMPARE(ftpsRoute.page, deviceopen::Page::FtpDeploy);
        QCOMPARE(ftpsRoute.ip, QStringLiteral("10.0.0.9"));
        QCOMPARE(ftpsRoute.port, 990);
    }

    // §2.2 路由：telnet/ssh → BatchCommand（该页 §2.2 实测"只切页"，但 Route 的
    // ip/port 仍须随命中端点带出——日志与后续能力都取用同源值）
    void routeTelnetSshGoesToBatchCommand()
    {
        const auto telnetProfile = makeProfile(QStringLiteral("dev-t"), QStringLiteral("丁"),
                                               QStringList{}, QStringLiteral("telnet"),
                                               QStringLiteral("172.16.0.8"), 23);
        const auto telnetRoute = deviceopen::routeTo(telnetProfile);
        QCOMPARE(telnetRoute.page, deviceopen::Page::BatchCommand);
        QCOMPARE(telnetRoute.ip, QStringLiteral("172.16.0.8"));
        QCOMPARE(telnetRoute.port, 23);

        const auto sshProfile = makeProfile(QStringLiteral("dev-h"), QStringLiteral("戊"),
                                            QStringList{}, QStringLiteral("ssh"),
                                            QStringLiteral("172.16.0.9"), 22);
        const auto sshRoute = deviceopen::routeTo(sshProfile);
        QCOMPARE(sshRoute.page, deviceopen::Page::BatchCommand);
        QCOMPARE(sshRoute.ip, QStringLiteral("172.16.0.9"));
        QCOMPARE(sshRoute.port, 22);
    }

    // §2.2 路由：modbus → Modbus（MODBUS 页未接总线，§2.2 实测表"只切页"；
    // Route 仍同源携带命中端点 ip/port）
    void routeModbusGoesToModbusPage()
    {
        const auto profile = makeProfile(QStringLiteral("dev-m"), QStringLiteral("己"),
                                         QStringList{}, QStringLiteral("modbus"),
                                         QStringLiteral("10.2.3.4"), 502);
        const auto route = deviceopen::routeTo(profile);
        QCOMPARE(route.page, deviceopen::Page::Modbus);
        QCOMPARE(route.ip, QStringLiteral("10.2.3.4"));
        QCOMPARE(route.port, 502);
    }

    // §2.2 路由：opcua → OpcUa —— 纯头可判；UI 不可达（端点协议白名单
    // DeviceBusWidget.cpp:40-44 不含 opcua，规格 §2.2 边界自检出），不得谎称 UI 级验证
    void routeOpcUaGoesToOpcUaPage()
    {
        const auto profile = makeProfile(QStringLiteral("dev-o"), QStringLiteral("庚"),
                                         QStringList{}, QStringLiteral("opcua"),
                                         QStringLiteral("10.9.9.9"), 4840);
        const auto route = deviceopen::routeTo(profile);
        QCOMPARE(route.page, deviceopen::Page::OpcUa);
        QCOMPARE(route.ip, QStringLiteral("10.9.9.9"));
        QCOMPARE(route.port, 4840);
    }

    // §2.2 判定口径（2026-10-20 裁定）：逐端点扫描，空 ip/协议不认识的端点跳过继续找，
    // 首个可解析端点胜出——且路由页与选中键 ip 必须来自同一个端点（路由选 A、选中选 B 即自相矛盾）
    void routePicksFirstResolvableEndpoint()
    {
        DeviceProfile profile = makeProfile(QStringLiteral("dev-multi"), QStringLiteral("辛"),
                                            QStringList{}, QStringLiteral("telnet"),
                                            QString() /*空 ip 端点*/, 23);
        profile.endpoints.push_back({QStringLiteral("modbus").toStdString(),
                                     QStringLiteral("10.2.3.4").toStdString(), 1502, {}});
        const auto route = deviceopen::routeTo(profile);
        QCOMPARE(route.page, deviceopen::Page::Modbus);          // 跳过了 [{ip:"", telnet}]
        QCOMPARE(route.ip, QStringLiteral("10.2.3.4"));          // 命中端点自己的 ip，非首端点
        QCOMPARE(route.port, 1502);                              // port 同源
        QVERIFY(route.statusHint.isEmpty());                     // 有可用端点 ⇒ 不给提示文案
    }

    // §2.2 旧行兜底：device.list 合成档案协议列为空 ⇒ 按 ftp 处理
    //（与编辑器 DeviceBusWidget.cpp:254「未知/空 → ftp」同口径——注意仅指"协议为空串"，
    // 协议写了但不认识的是不可用端点，走扫描跳过口径，不是兜底）
    void routeLegacyEmptyProtocolDefaultsToFtp()
    {
        DeviceProfile profile;
        profile.deviceId = QStringLiteral("legacy-1").toStdString();
        profile.name = QStringLiteral("旧行设备").toStdString();
        profile.endpoints.push_back({QString().toStdString() /*协议列为空*/,
                                     QStringLiteral("192.168.5.9").toStdString(), 21, {}});
        const auto route = deviceopen::routeTo(profile);
        QCOMPARE(route.page, deviceopen::Page::FtpDeploy);
        QCOMPARE(route.protocol, QStringLiteral("ftp"));         // 兜底后的归一协议
        QCOMPARE(route.ip, QStringLiteral("192.168.5.9"));       // 部署页下拉选中键
        QCOMPARE(route.port, 21);
    }

    // §2.2 末行：有档案但整条扫不出一个可用端点（无端点/协议不认识/只剩空 ip）
    // ⇒ Page::None + 状态栏文案「该设备没有可用的协议端点」（不切页，UI 编排归任务 6）
    void routeWithoutUsableEndpointIsNoneWithHint()
    {
        // 臂 1：无端点
        const auto empty = makeProfile(QStringLiteral("dev-e1"), QStringLiteral("空"),
                                        QStringList{}, QString() /*makeProfile 协议空 ⇒ 不建端点*/);
        const auto emptyRoute = deviceopen::routeTo(empty);
        QCOMPARE(emptyRoute.page, deviceopen::Page::None);
        QCOMPARE(emptyRoute.statusHint, QStringLiteral("该设备没有可用的协议端点"));

        // 臂 2：协议写了但不认识（全扫无命中才 None——扫描跳过口径的收口臂）
        const auto junk = makeProfile(QStringLiteral("dev-e2"), QStringLiteral("噪"),
                                      QStringList{}, QStringLiteral("quantum"),
                                      QStringLiteral("1.2.3.4"), 7);
        const auto junkRoute = deviceopen::routeTo(junk);
        QCOMPARE(junkRoute.page, deviceopen::Page::None);
        QCOMPARE(junkRoute.statusHint, QStringLiteral("该设备没有可用的协议端点"));
        QVERIFY(junkRoute.ip.isEmpty());                         // 不可路由就不给半截选中键

        // 臂 3：有端点但 ip 全空/纯空白——trim 后判空，同样跳过收口
        DeviceProfile blank;
        blank.deviceId = QStringLiteral("dev-e3").toStdString();
        blank.endpoints.push_back({QStringLiteral("ftp").toStdString(),
                                   QStringLiteral("   ").toStdString(), 21, {}});
        const auto blankRoute = deviceopen::routeTo(blank);
        QCOMPARE(blankRoute.page, deviceopen::Page::None);
        QCOMPARE(blankRoute.statusHint, QStringLiteral("该设备没有可用的协议端点"));
    }
};

QTEST_MAIN(TstDeviceBusGroups)
#include "tst_device_bus_groups.moc"
