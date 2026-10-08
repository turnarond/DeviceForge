/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: tst_log_panel_state.cpp
 *
 * Date: 2026-10-08
 *
 * Author: turnarond
 *
 * Description: 综合日志面板状态纯逻辑回归（v2.11.1 折叠面板 + 初始比例）。
 */

#include <QtTest>
#include <QVariantMap>

#include "app/LogPanelState.h"

using logpanel::State;

class TstLogPanelState : public QObject {
    Q_OBJECT

private slots:
    void emptyRecordYieldsDefaults();
    void recordParsesStoredState();
    void invalidValuesFallBackToDefaults();
    void splitSizesExpandedClampsWithinWindow();
    void splitSizesCollapsedKeepsTitleStrip();
    void recordRoundTrip();
};

void TstLogPanelState::emptyRecordYieldsDefaults()
{
    const State s = logpanel::fromRecord(QVariantMap{});
    QCOMPARE(s.collapsed, false);
    QCOMPARE(s.expandedHeight, 180);
}

void TstLogPanelState::recordParsesStoredState()
{
    QVariantMap rec;
    rec.insert(QStringLiteral("collapsed"), true);
    rec.insert(QStringLiteral("height"), 240);
    const State s = logpanel::fromRecord(rec);
    QCOMPARE(s.collapsed, true);
    QCOMPARE(s.expandedHeight, 240);
}

void TstLogPanelState::invalidValuesFallBackToDefaults()
{
    QVariantMap rec;
    rec.insert(QStringLiteral("collapsed"), QStringLiteral("nope"));
    rec.insert(QStringLiteral("height"), 0);
    const State s = logpanel::fromRecord(rec);
    QCOMPARE(s.collapsed, false);
    QCOMPARE(s.expandedHeight, 180);

    rec.insert(QStringLiteral("height"), -50);
    QCOMPARE(logpanel::fromRecord(rec).expandedHeight, 180);
}

void TstLogPanelState::splitSizesExpandedClampsWithinWindow()
{
    const auto sizes = logpanel::splitSizes(State{false, 180}, 800);
    QCOMPARE(sizes.second, 180);
    QCOMPARE(sizes.first, 620);

    // 超过窗口可用空间时钳制：总高 300，工具区至少保留 100
    const auto tight = logpanel::splitSizes(State{false, 180}, 260);
    QCOMPARE(tight.second, 160);
    QCOMPARE(tight.first, 100);

    // 过小则抬到最小日志高度
    const auto small = logpanel::splitSizes(State{false, 10}, 800);
    QCOMPARE(small.second, logpanel::kMinLogHeight);
}

void TstLogPanelState::splitSizesCollapsedKeepsTitleStrip()
{
    // 折叠态：日志区收为 0；折叠条在 splitter 外常驻可见（v2.11.1 修复旧折叠陷阱）
    const auto sizes = logpanel::splitSizes(State{true, 180}, 800);
    QCOMPARE(sizes.second, 0);
    QCOMPARE(sizes.first, 800);
}

void TstLogPanelState::recordRoundTrip()
{
    const State s{true, 260};
    QCOMPARE(logpanel::fromRecord(logpanel::toRecord(s)).collapsed, true);
    QCOMPARE(logpanel::fromRecord(logpanel::toRecord(s)).expandedHeight, 260);
}

QTEST_MAIN(TstLogPanelState)
#include "tst_log_panel_state.moc"
