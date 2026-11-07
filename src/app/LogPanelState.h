/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: LogPanelState.h
 *
 * Date: 2026-10-08
 *
 * Author: turnarond
 *
 * Description: 综合日志面板折叠/比例状态的纯逻辑（可测性收敛）。
 *              ConfigStore 读写由 DeviceForge 外壳调用，本头不触 IO。
 */

#pragma once

#include <QPair>
#include <QString>
#include <QVariantMap>

namespace logpanel {

struct State {
    bool collapsed = true;   // 默认折叠（日志区是辅助信息，不占主界面）
    int expandedHeight = 180;
};

constexpr const char* kRecordType = "app.panel";
constexpr const char* kRecordKey = "logPanel";
constexpr int kMinLogHeight = 80;
// 状态版本：v1 时代默认展开、旧记录里 collapsed=false 是"当时的默认"而非用户选择。
// v2 起默认折叠——旧记录（无 version 或 <2）忽略其 collapsed，统一按折叠处理。
constexpr int kStateVersion = 2;

inline State fromRecord(const QVariantMap& record)
{
    State s;
    const int version = record.value(QStringLiteral("version")).toInt();   // 缺失=0
    const QVariant collapsed = record.value(QStringLiteral("collapsed"));
    // 仅当记录带新版本号时才信任其折叠态；旧记录按默认折叠
    if (version >= kStateVersion && collapsed.typeId() == QMetaType::Bool)
        s.collapsed = collapsed.toBool();
    const QVariant height = record.value(QStringLiteral("height"));
    if (height.typeId() == QMetaType::Int && height.toInt() >= kMinLogHeight)
        s.expandedHeight = height.toInt();
    return s;
}

inline QVariantMap toRecord(const State& state)
{
    QVariantMap rec;
    rec.insert(QStringLiteral("version"), kStateVersion);
    rec.insert(QStringLiteral("collapsed"), state.collapsed);
    rec.insert(QStringLiteral("height"), state.expandedHeight);
    return rec;
}

// 返回 {工具区高度, 日志区高度}：展开态把日志高度钳制在
// [kMinLogHeight, total-100]（工具区至少保留 100px）；
// 折叠态日志区收为 0（折叠条是 splitter 外的常驻兄弟节点，不随面板隐藏）。
inline QPair<int, int> splitSizes(const State& state, int totalHeight)
{
    if (state.collapsed)
        return qMakePair(qMax(totalHeight, 0), 0);
    const int upper = qMax(kMinLogHeight, totalHeight - 100);
    const int log = qBound(kMinLogHeight, state.expandedHeight, upper);
    return qMakePair(qMax(totalHeight - log, 0), log);
}

} // namespace logpanel
