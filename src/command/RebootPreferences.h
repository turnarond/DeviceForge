#pragma once

#include "command/CommandTypes.h"
#include <QVariantMap>

inline QVariantMap rebootOptionsToMap(const RebootOptions& options)
{
    QVariantMap map;
    map.insert(QStringLiteral("protocol"), QString::fromStdString(options.protocol));
    map.insert(QStringLiteral("command"), QString::fromStdString(options.command));
    map.insert(QStringLiteral("timeoutSec"), options.timeoutSec);
    map.insert(QStringLiteral("retryCount"), options.retryCount);
    return map;
}

inline RebootOptions rebootOptionsFromMap(const QVariantMap& map)
{
    RebootOptions options;
    const QString protocol = map.value(QStringLiteral("protocol"), QStringLiteral("telnet")).toString().trimmed().toLower();
    options.protocol = protocol == QStringLiteral("ssh") ? "ssh" : "telnet";
    const QString command = map.value(QStringLiteral("command"), QStringLiteral("reboot")).toString().trimmed();
    options.command = command.isEmpty() ? "reboot" : command.toStdString();
    options.timeoutSec = qBound(1, map.value(QStringLiteral("timeoutSec"), 10).toInt(), 120);
    options.retryCount = qBound(0, map.value(QStringLiteral("retryCount"), 1).toInt(), 3);
    return options;
}