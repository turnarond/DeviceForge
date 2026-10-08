/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: BatchResultTable.h
 *
 * Date: 2026-10-08
 *
 * Author: turnarond
 *
 * Description: 批量命令结果表头配置的纯逻辑（可测性收敛，参照 NetRelayTypes.h 模式）。
 *              ResizeToContents 会使列宽不可手动拖动，必须用 Interactive + 合理初始宽度。
 */

#pragma once

#include <QHeaderView>

inline void configureBatchResultHeader(QHeaderView* header)
{
    header->setStretchLastSection(true);
    // Interactive 才允许用户拖动列宽；ResizeToContents 会把拖动弹回内容宽度
    for (int col = 0; col < 3; ++col)
        header->setSectionResizeMode(col, QHeaderView::Interactive);
    header->resizeSection(0, 140);   // IP
    header->resizeSection(1, 100);   // 状态
    header->resizeSection(2, 90);    // 耗时(ms)
}
