/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: tst_updatechecker.cpp
 * Date: 2026-07-15
 * Author: turnarond
 * Description: 在线更新模块单元测试 — 版本解析 + 比较 + ReleaseInfo 默认值
 */
#include <QtTest/QtTest>
#include "updater/UpdateTypes.h"

class TstUpdateChecker : public QObject {
    Q_OBJECT
private slots:
    void sanity() { QVERIFY(true); }

    // 版本解析
    void parseVersion_vPrefix() {
        Version v = parseVersion("v2.1.0");
        QCOMPARE(v.major, 2);
        QCOMPARE(v.minor, 1);
        QCOMPARE(v.patch, 0);
    }

    void parseVersion_noPrefix() {
        Version v = parseVersion("3.14.5");
        QCOMPARE(v.major, 3);
        QCOMPARE(v.minor, 14);
        QCOMPARE(v.patch, 5);
    }

    void parseVersion_badFormat() {
        Version v = parseVersion("not-a-version");
        QCOMPARE(v.major, 0);
        QCOMPARE(v.minor, 0);
        QCOMPARE(v.patch, 0);
    }

    void parseVersion_uppercaseV_excluded() {
        // spec: 仅小写 v 前缀，大写 V 应保留原样 → atoi("V2.1.0") = 0
        Version v = parseVersion("V2.1.0");
        QCOMPARE(v.major, 0);
        QCOMPARE(v.minor, 0);
        QCOMPARE(v.patch, 0);
    }

    // 版本比较
    void compareVersion_newer() {
        Version a = parseVersion("v2.2.0");
        Version b = parseVersion("v2.1.0");
        QVERIFY(compareVersion(a, b) > 0);
    }

    void compareVersion_older() {
        Version a = parseVersion("v2.0.0");
        Version b = parseVersion("v2.1.0");
        QVERIFY(compareVersion(a, b) < 0);
    }

    void compareVersion_equal() {
        Version a = parseVersion("v2.1.0");
        Version b = parseVersion("v2.1.0");
        QCOMPARE(compareVersion(a, b), 0);
    }

    void compareVersion_patchBump() {
        Version a = parseVersion("v2.1.1");
        Version b = parseVersion("v2.1.0");
        QVERIFY(compareVersion(a, b) > 0);
    }

    void compareVersion_majorBump() {
        Version a = parseVersion("v3.0.0");
        Version b = parseVersion("v2.9.9");
        QVERIFY(compareVersion(a, b) > 0);
    }

    // toString
    void versionToString() {
        Version v;
        v.major = 2;
        v.minor = 1;
        v.patch = 0;
        QCOMPARE(v.toString(), std::string("2.1.0"));
    }

    // ReleaseInfo 默认值
    void releaseInfo_defaults() {
        ReleaseInfo info;
        QVERIFY(info.tagName.empty());
        QVERIFY(info.downloadUrl.empty());
        QCOMPARE(info.assetSize, (int64_t)0);
        QVERIFY(!info.isNewer);  // 默认 false
    }
};

QTEST_MAIN(TstUpdateChecker)
#include "tst_updatechecker.moc"
