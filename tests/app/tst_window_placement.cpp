/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: tst_window_placement.cpp
 *
 * Date: 2026-10-18
 *
 * Author: turnarond
 *
 * Description: WindowPlacement.h 主窗口初始几何钳制纯函数直测（v2.14 双屏
 *              「看不全」修复）：可完整容纳的屏优先、否则最大可用屏压缩居中。
 */

#include <QtTest>

#include "app/WindowPlacement.h"

class TstWindowPlacement : public QObject {
    Q_OBJECT

private slots:
    // 主屏装不下（1920 宽 < 需求 2010）、副屏装得下 → 选副屏居中
    void picksFirstScreenThatFitsEntirely()
    {
        const QList<QRect> screens{
            QRect(0, 0, 1920, 1040),                 // 主屏（任务栏占 25 高）
            QRect(-2560, 0, 2560, 1400)};            // 左副屏
        const QRect got = winplace::initialFrameGeometry(QSize(2010, 910), screens);
        QCOMPARE(got.size(), QSize(2010, 910));
        // 副屏左上 (-2560,0)，居中：x=-2560+(2560-2010)/2=-2285，y=0+(1400-910)/2=245
        QCOMPARE(got.topLeft(), QPoint(-2285, 245));
    }
    // 第一块屏就能完整容纳 → 直接用第一块（主屏优先，不跳屏）
    void prefersFirstFittingScreen()
    {
        const QList<QRect> screens{
            QRect(0, 0, 2560, 1400),
            QRect(-2560, 0, 2560, 1400)};
        const QRect got = winplace::initialFrameGeometry(QSize(1200, 800), screens);
        QCOMPARE(got.topLeft(), QPoint(680, 300));   // 主屏居中
        QCOMPARE(got.size(), QSize(1200, 800));
    }
    // 没有任何屏能完整容纳 → 选可用面积最大者，尺寸压缩进可用区并居中
    void fallsBackToLargestScreenAndShrinks()
    {
        const QList<QRect> screens{
            QRect(0, 0, 1280, 720),
            QRect(1280, 0, 1920, 1040)};
        const QRect got = winplace::initialFrameGeometry(QSize(2010, 910), screens);
        QCOMPARE(got.size(), QSize(1920, 910));      // 宽压缩到屏宽，高装得下保持
        QCOMPARE(got.topLeft(), QPoint(1280, 65));   // 大屏内居中 x=1280+0, y=(1040-910)/2
    }
    // 无屏信息（异常环境）→ 需求原样置于原点，不得崩溃
    void emptyScreensFallsBackToNeededAtOrigin()
    {
        const QRect got = winplace::initialFrameGeometry(QSize(1000, 700), {});
        QCOMPARE(got, QRect(0, 0, 1000, 700));
    }
};

QTEST_APPLESS_MAIN(TstWindowPlacement)
#include "tst_window_placement.moc"
