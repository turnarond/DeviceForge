/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: WindowPlacement.h
 *
 * Date: 2026-10-18
 *
 * Author: turnarond
 *
 * Description: 主窗口初始几何钳制的纯决策逻辑（v2.14 双屏「看不全」修复，
 *              可测性收敛参照 NetRelayTypes.h 模式）。背景：主窗口工具栏
 *              单行布局的最小宽度可超过单块屏幕可用宽，且 OS 默认放置可能
 *              把窗口落在装不下它的屏上，右侧被裁。决策只依赖「需求尺寸 +
 *              各屏可用几何」，屏幕枚举与 setGeometry 胶水留在 DeviceForge。
 */

#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>

#include <algorithm>

namespace winplace {

// 主窗口初始几何：
// 1) 选**列表中第一块**能完整容纳 needed 的屏（调用方按主屏优先传），窗口居中其上；
// 2) 没有任何屏能完整容纳 → 选可用面积最大者，尺寸逐轴压缩进该屏可用区并居中
//    （最终仍会被 Qt 按窗口最小尺寸钳制，本函数只保证不主动超出屏幕）；
// 3) 屏列表为空（异常环境）→ 需求原样置于原点。
inline QRect initialFrameGeometry(const QSize& needed, const QList<QRect>& availableScreens)
{
    if (availableScreens.isEmpty())
        return QRect(QPoint(0, 0), needed);

    for (const QRect& screen : availableScreens) {
        if (screen.width() >= needed.width() && screen.height() >= needed.height()) {
            return QRect(screen.topLeft()
                             + QPoint((screen.width() - needed.width()) / 2,
                                      (screen.height() - needed.height()) / 2),
                         needed);
        }
    }

    QRect largest = availableScreens.first();
    for (const QRect& screen : availableScreens) {
        if (screen.width() * screen.height() > largest.width() * largest.height())
            largest = screen;
    }
    const QSize fitted((std::min)(needed.width(), largest.width()),
                       (std::min)(needed.height(), largest.height()));
    return QRect(largest.topLeft()
                     + QPoint((largest.width() - fitted.width()) / 2,
                              (largest.height() - fitted.height()) / 2),
                 fitted);
}

} // namespace winplace
