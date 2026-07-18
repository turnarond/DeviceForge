/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: FormatUtils.h
 * Date: 2026-07-18
 * Author: turnarond
 *
 * Description: 共享格式化工具函数
 */

#pragma once

#include <QString>

/// 将字节数转为人类可读格式: B / KB / MB / GB
inline QString formatFileSize(qint64 bytes)
{
    if (bytes < 0) return "?";
    if (bytes < 1024)
        return QString::number(bytes) + " B";
    double val = bytes / 1024.0;
    if (val < 1024.0)
        return QString::number(val, 'f', 1) + " KB";
    val /= 1024.0;
    if (val < 1024.0)
        return QString::number(val, 'f', 1) + " MB";
    val /= 1024.0;
    return QString::number(val, 'f', 2) + " GB";
}
