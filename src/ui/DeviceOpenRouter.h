/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: DeviceOpenRouter.h
 *
 * Date: 2026-10-20
 *
 * Author: turnarond
 *
 * Description: v2.16-B 「打开这台设备」档案→目标工具页唯一判定真源纯头（规格 §2.2，
 *              参照 DeviceBusGroups.h / FolderExpand.h 纯头 include 模式，
 *              无 QWidget、无 ConfigStore）。
 *              Route 必须带命中端点的 ip——部署页设备下拉以纯 ip 为条目键
 *              （FtpDeployWidget.cpp:1544/1563），选中键与路由必须同源，
 *              否则会出现"路由选 A 端点、下拉选 B 端点"。
 */
#pragma once

#include <QString>

#include "device/DeviceProfile.h"

namespace deviceopen {

enum class Page { FtpDeploy, BatchCommand, Modbus, OpcUa, None };

struct Route {
    Page page = Page::None;
    QString protocol;
    QString ip;
    int port = 0;
    QString statusHint;   // Page::None 时的状态栏文案（中文常量用 QStringLiteral）
};

inline Page pageFor(const QString& protocol)
{
    if (protocol == QStringLiteral("ftp") || protocol == QStringLiteral("ftps")
        || protocol == QStringLiteral("sftp"))
        return Page::FtpDeploy;
    if (protocol == QStringLiteral("telnet") || protocol == QStringLiteral("ssh"))
        return Page::BatchCommand;
    if (protocol == QStringLiteral("modbus"))
        return Page::Modbus;
    if (protocol == QStringLiteral("opcua"))
        return Page::OpcUa;
    return Page::None;
}

inline Route routeTo(const DeviceProfile& profile)
{
    Route route;
    for (const auto& endpoint : profile.endpoints) {
        const QString ip = QString::fromStdString(endpoint.ip).trimmed();
        if (ip.isEmpty()) continue;                       // 空 ip 端点不可用，继续找下一个
        QString protocol = QString::fromStdString(endpoint.protocol).trimmed().toLower();
        if (protocol.isEmpty())
            protocol = QStringLiteral("ftp");             // 旧 device.list 行兜底
        const Page page = pageFor(protocol);
        if (page == Page::None) continue;                 // 协议不认识 ⇒ 不算命中
        route.page = page;
        route.protocol = protocol;
        route.ip = ip;
        route.port = endpoint.port;
        return route;
    }
    route.statusHint = QStringLiteral("该设备没有可用的协议端点");
    return route;
}

} // namespace deviceopen
