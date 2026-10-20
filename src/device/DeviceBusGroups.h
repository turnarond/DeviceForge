/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: DeviceBusGroups.h
 *
 * Date: 2026-10-20
 *
 * Author: turnarond
 *
 * Description: v2.16-B 设备栏标签分组唯一判定真源纯头（规格 §2.1，参照
 *              FolderExpand.h / WsEventTypes.h 纯头 include 模式，
 *              无 QWidget、无 ConfigStore、折叠态随重画作废）。
 *              规则（逐条可测）：
 *              ① 一台设备 N 个标签 → N 个分组各出现一次；无标签或规范化后
 *                全空 → 只进「未分组」（默认分组，绝不写回 profile.tags）；
 *              ② 组序：标签组按 tag 升序（区分大小写，与 SylixOS/POSIX 目录
 *                口径一致），「未分组」永远最后；同组内按 registry 顺序；
 *              ③ filterText 非空：profileIndexes 只留命中，total 仍是组内全量；
 *                命中为 0 的组不进结果（UI 侧不再判空组）；
 *              ④ 全不命中：结果为空，「无匹配设备（N 台）」说明行由 UI 侧给；
 *              ⑤ 零成员的分组永不出现在结果里。
 */
#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <vector>

#include "device/DeviceProfile.h"

namespace devicebus {

// 去首尾空白；空/纯分隔符 → 空串（=不算标签）。两侧匹配共用（规格 §4）。
inline QString normalizeTag(const QString& raw)
{
    static const QString separators = QStringLiteral("/|,;、");
    QString out = raw.trimmed();
    bool allSeparators = !out.isEmpty();
    for (const QChar ch : out) {
        if (!separators.contains(ch)) { allSeparators = false; break; }
    }
    return allSeparators ? QString() : out;
}

// 档案标签的规范化视图（去空白/丢空/去重——§2.4 同组去重的根）；绝不写回 profile.tags（规格 §4）。
inline QStringList normalizedTags(const DeviceProfile& p)
{
    QStringList out;
    for (const auto& tag : p.tags) {
        const QString normalized = normalizeTag(QString::fromStdString(tag));
        if (!normalized.isEmpty() && !out.contains(normalized)) out.append(normalized);
    }
    return out;
}

// 模板圈选复用（规格 §4）：selector 与档案两侧都先规范化。
inline bool hasNormalizedTag(const DeviceProfile& p, const QString& selector)
{
    const QString wanted = normalizeTag(selector);
    if (wanted.isEmpty()) return false;
    return normalizedTags(p).contains(wanted);
}

// 分组段（规格 §2.1 载体裁定变更，任务 4 同步）：成员载体为 **QList<int> profileIndexes**
// 而非 QStringList deviceIds——deviceId 反查在 legacy 合成路径的空/重复 id 下会把后一台
// 错认成前一台（tst emptyDeviceIdProfilesStillGroup 钉桩）。索引指向 segments 入参
// profiles 的同一位置，任务 4 绑定 QAction 时直接取 profiles[idx]，杜绝错绑。
struct Segment {
    QString tag;
    QList<int> profileIndexes;   // 命中过滤后的成员，按 registry 顺序
    int total = 0;               // 该组成员总数（不过滤时 == profileIndexes.size()）
};

// 「未分组」是函数内 static，不用命名空间级 inline 变量：静态 QString 的动态初始化
// 时机早于 QCoreApplication 存在，跨编译单元顺序不可控（规格 §2.1）。绝不写回 profile.tags。
inline const QString& ungroupedTag()
{
    static const QString kUngrouped = QStringLiteral("未分组");
    return kUngrouped;
}

// 名称/IP/标签三源、子串、大小写不敏感；空过滤 = 全命中（规则 3 的前提）。
// 定义必须早于 segments（后者调用前者），别按字母序排。
inline bool matchesFilter(const DeviceProfile& p, const QString& filterText)
{
    const QString needle = filterText.trimmed();
    if (needle.isEmpty()) return true;                    // 空过滤 = 全命中（规则 3 的前提）
    if (QString::fromStdString(p.name).contains(needle, Qt::CaseInsensitive)) return true;
    for (const auto& endpoint : p.endpoints)
        if (QString::fromStdString(endpoint.ip).contains(needle, Qt::CaseInsensitive)) return true;
    for (const auto& tag : normalizedTags(p))
        if (tag.contains(needle, Qt::CaseInsensitive)) return true;
    return false;
}

// 唯一分类真源（规则 1：N 标签 → N 组各一次；无标签/规范化后全空 → 只进「未分组」）。
inline QList<Segment> segments(const std::vector<DeviceProfile>& profiles,
                               const QString& filterText)
{
    QMap<QString, QList<int>> byTag;   // 标签 → 成员下标（含「未分组」）；按 registry 序追加
    for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
        const QStringList tags = normalizedTags(profiles[i]);
        if (tags.isEmpty()) {
            byTag[ungroupedTag()].append(i);
            continue;
        }
        for (const auto& tag : tags)
            byTag[tag].append(i);
    }

    // 规则 2 唯一真源：显式排序，绝不依赖 QMap 键序（其键序会把「未分组」排在
    // 高位码点标签如「车间X」之前）。
    QStringList order;
    for (auto it = byTag.cbegin(); it != byTag.cend(); ++it)
        if (it.key() != ungroupedTag()) order.append(it.key());
    order.sort(Qt::CaseSensitive);           // 与 SylixOS/POSIX 目录口径一致
    if (byTag.contains(ungroupedTag())) order.append(ungroupedTag());

    const bool filtering = !filterText.trimmed().isEmpty();
    QList<Segment> out;
    for (const auto& tag : order) {
        Segment seg;
        seg.tag = tag;
        const QList<int> members = byTag.value(tag);
        seg.total = members.size();          // total = 组内全量（不过滤）
        for (const int index : members) {
            // 逐成员按自己的档案判定命中——无任何按 deviceId 的反查（验证项 1 裁定）
            if (filtering && !matchesFilter(profiles[index], filterText)) continue;
            seg.profileIndexes.append(index);
        }
        // 规则 3/5 在纯头里就收敛（单源）：命中为 0 的组不进结果——UI 侧不再判空组，
        // 也就不可能出现"点了个空壳子菜单"。不过滤时空默认分组压根不会成为键（byTag 只在插入时建键）。
        if (seg.profileIndexes.isEmpty())
            continue;
        out.append(seg);
    }
    return out;
}

} // namespace devicebus
