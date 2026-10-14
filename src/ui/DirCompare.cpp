/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: DirCompare.cpp
 * Date: 2026-10-14
 * Author: turnarond
 *
 * Description: 目录对比纯逻辑实现。
 */

#include "ui/DirCompare.h"

#include <unordered_map>

namespace {

bool isNavigational(const std::string& name)
{
    return name == "." || name == "..";
}

} // namespace

bool mtimeMinuteEqual(const std::string& a, const std::string& b)
{
    // "yyyy-MM-dd HH:mm" = 16 字符；短于 16 视为不可比
    if (a.size() < 16 || b.size() < 16)
        return false;
    return a.compare(0, 16, b, 0, 16) == 0;
}

CompareSummary compareDirectories(const std::vector<FtpFileInfo>& left,
                                  const std::vector<FtpFileInfo>& right,
                                  QVector<CompareEntry>* alignedOut)
{
    CompareSummary s;
    if (alignedOut)
        alignedOut->clear();

    // 右列表 name → 下标（重复名取首个，与旧着色 map 语义一致）
    std::unordered_map<std::string, size_t> rightIndex;
    for (size_t i = 0; i < right.size(); ++i) {
        if (isNavigational(right[i].name)) continue;
        rightIndex.emplace(right[i].name, i);
    }

    std::vector<bool> rightMatched(right.size(), false);

    // 左优先并集：先遍历左列表，再追加右列表未配对项
    for (size_t i = 0; i < left.size(); ++i) {
        const auto& l = left[i];
        if (isNavigational(l.name)) continue;

        CompareEntry e;
        e.leftRow = static_cast<int>(i);

        auto it = rightIndex.find(l.name);
        if (it == rightIndex.end()) {
            e.cls = CompareClass::OnlyLeft;
            e.rightRow = -1;
            ++s.onlyLeft;
        } else {
            const auto& r = right[it->second];
            rightMatched[it->second] = true;
            e.rightRow = static_cast<int>(it->second);
            if (l.isDir != r.isDir) {
                // 评审 M1：同名一侧目录一侧文件——类型都变了不是"同"
                e.cls = CompareClass::Diff;
                ++s.diff;
            } else if (l.isDir) {
                // 目录只判存在性（同名目录 size 天然不同，不比时间）
                e.cls = CompareClass::Same;
                ++s.same;
            } else if (l.size != r.size) {
                e.cls = CompareClass::Diff;
                ++s.diff;
            } else if (!l.dateTime.empty() && !r.dateTime.empty()
                       && !mtimeMinuteEqual(l.dateTime, r.dateTime)) {
                e.cls = CompareClass::Diff;
                ++s.diff;
            } else {
                e.cls = CompareClass::Same;
                ++s.same;
            }
        }
        if (alignedOut)
            alignedOut->append(e);
    }

    for (size_t j = 0; j < right.size(); ++j) {
        if (isNavigational(right[j].name) || rightMatched[j])
            continue;
        CompareEntry e;
        e.cls = CompareClass::OnlyRight;
        e.leftRow = -1;
        e.rightRow = static_cast<int>(j);
        ++s.onlyRight;
        if (alignedOut)
            alignedOut->append(e);
    }

    return s;
}

QVector<AlignedRow> buildSideRows(const QVector<CompareEntry>& entries,
                                   bool leftSide,
                                   bool diffOnly)
{
    QVector<AlignedRow> rows;
    rows.reserve(entries.size());
    for (const auto& e : entries) {
        if (diffOnly && e.cls == CompareClass::Same)
            continue;
        AlignedRow r;
        r.cls = e.cls;
        const int src = leftSide ? e.leftRow : e.rightRow;
        r.sourceRow = src;   // -1 即对面独有 → 本侧占位行
        rows.append(r);
    }
    return rows;
}
