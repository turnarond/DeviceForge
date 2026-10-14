/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: DirCompare.h
 * Date: 2026-10-14
 * Author: turnarond
 *
 * Description: 目录对比纯逻辑（v2.12 文件管理增强）— 左右两份 FtpFileInfo
 *              列表的元数据口径分类、行对齐展开与分钟精度时间比较。
 *              无 Qt Widgets / 网络依赖，可独立单测（tests/dir_compare）。
 */

#pragma once
#include <QVector>
#include <vector>
#include "tools/FtpDeployTool/FtpFileInfo.h"

enum class CompareClass { Same, Diff, OnlyLeft, OnlyRight };

// 并集对齐条目：行下标指向各自输入列表；无对面 = -1
struct CompareEntry {
    CompareClass cls = CompareClass::Same;
    int leftRow = -1;
    int rightRow = -1;
};

struct CompareSummary {
    int same = 0;
    int diff = 0;
    int onlyLeft = 0;
    int onlyRight = 0;
};

// 单侧展开行：sourceRow=-1 表示占位行（该条目只存在于对面）
struct AlignedRow {
    int sourceRow = -1;
    CompareClass cls = CompareClass::Same;
};

// ISO "yyyy-MM-dd HH:mm:ss" 前 16 字符相等 → 分钟一致；任一侧空/畸形 → 不相等（不可比）
bool mtimeMinuteEqual(const std::string& a, const std::string& b);

// 判定契约（规格 §3.2）：
//  - "." / ".." 不参与配对；目录只判存在性（同名即 Same，缺一侧按 Only* 计）
//  - 文件：size 主判据；两侧 mtime 均可得时分钟相等作辅助（任一不可得仅比 size）
// alignedOut 非空则输出左优先并集序（左侧原序在前，右侧独有条目按右原序追加）
CompareSummary compareDirectories(const std::vector<FtpFileInfo>& left,
                                  const std::vector<FtpFileInfo>& right,
                                  QVector<CompareEntry>* alignedOut = nullptr);

// 将对齐条目展开为某一侧的行序列；diffOnly=true 时 Same 条目整对收缩不入列
QVector<AlignedRow> buildSideRows(const QVector<CompareEntry>& entries,
                                  bool leftSide,
                                  bool diffOnly = false);
