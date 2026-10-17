/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: DeployTemplateMap.h
 *
 * Date: 2026-10-17
 *
 * Author: turnarond
 *
 * Description: 单次部署配置 → 任务模板的纯映射层（v2.14 #49-2 第一腿，参照
 *              WsEventTypes.h 可测性收敛模式）。任务 3 的"保存为模板"按钮槽
 *              消费本函数；本头只负责字段映射，不落库、不触网、不依赖 Widget。
 */

#pragma once

#include "task/TaskTemplateTypes.h"

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <vector>
#include <string>

// v2.14 #49-2：单次部署配置 → 任务模板的纯映射（兑现 v2.11 spec §4「将当前单次
// 部署配置保存为新模板」承诺的可测性收敛层，参照 WsEventTypes.h 模式）。
// 只映射模板体系已能表达的字段：文件清单+目标目录+设备绑定+（可选）重启步骤；
// 部署前清空/FTPS/端口/并发度是工具级连接配置，刻意不进模板。
namespace deploymap {

struct OnceConfig {
    QString protocol;                    // "ftp"/"sftp"（currentProtocol() 同口径）
    QStringList localFiles;              // 左面板本地选中文件（调用方保证非空）
    QString remoteDirectory;             // 右面板当前目录（空回退 "/" 由调用方处理）
    std::vector<std::string> deviceIds;  // 勾选档案；空=未绑定
    bool rebootAfter = false;
    QString rebootCommand;               // 重启命令行（空回退 "reboot"）
    QString rebootProtocol;              // "telnet"/"ssh"（m_rebootProtocolCombo currentData）
    int rebootTimeoutSec = 10;
    int rebootRetryCount = 1;
};

inline TaskTemplate buildTemplateFromOnceConfig(const OnceConfig& cfg)
{
    TaskTemplate tmpl;
    tmpl.name = QStringLiteral("单次部署模板").toStdString();   // 占位名，编辑器可改
    tmpl.version = 1;
    tmpl.deviceIds = cfg.deviceIds;
    TaskStep deploy;
    deploy.type = TaskStepType::DeployFiles;
    QVariantMap params;
    if (!cfg.protocol.isEmpty())
        params.insert(QStringLiteral("protocol"), cfg.protocol);
    params.insert(QStringLiteral("files"), cfg.localFiles);
    params.insert(QStringLiteral("remoteDirectory"),
                  cfg.remoteDirectory.isEmpty() ? QStringLiteral("/") : cfg.remoteDirectory);
    deploy.parameters = params;
    tmpl.steps.push_back(deploy);
    if (cfg.rebootAfter) {
        TaskStep reboot;
        reboot.type = TaskStepType::Reboot;
        QVariantMap rp;
        QString command = cfg.rebootCommand.trimmed();
        if (command.isEmpty()) command = QStringLiteral("reboot");   // 引擎缺省同口径
        rp.insert(QStringLiteral("command"), command);
        if (!cfg.rebootProtocol.isEmpty())
            rp.insert(QStringLiteral("protocol"), cfg.rebootProtocol);
        rp.insert(QStringLiteral("timeoutSec"), cfg.rebootTimeoutSec);
        rp.insert(QStringLiteral("retryCount"), cfg.rebootRetryCount);
        reboot.parameters = rp;
        tmpl.steps.push_back(reboot);
    }
    return tmpl;
}

} // namespace deploymap
