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

#include "deploy/FolderExpand.h"
#include "task/TaskTemplateTypes.h"

#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <vector>
#include <string>

// v2.14 #49-2：单次部署配置 → 任务模板的纯映射（兑现 v2.11 spec §4「将当前单次
// 部署配置保存为新模板」承诺的可测性收敛层，参照 WsEventTypes.h 模式）。
// 只映射模板体系已能表达的字段：文件清单+目标目录+设备绑定+（可选）重启步骤；
// 部署前清空/FTPS/端口/并发度是工具级连接配置，刻意不进模板。
// v2.15 任务9（规格 §3.1 三入口同口径）：OnceConfig 增 expandedItems——Widget 层
// 已用 folderexpand::expand 展开完成的条目清单（本映射层纯搬运、不触文件系统，
// QFileInfo::fileName 仅路径字符串运算）；非空时 parameters 只产 items 全量数组
// 且不写 files/remoteDirectory 两键（parseDeployItems 契约：items 非空即完全
// 忽略 files，两键并写必静默丢顶层文件）。
namespace deploymap {

struct OnceConfig {
    QString protocol;                    // "ftp"/"sftp"（currentProtocol() 同口径）
    QStringList localFiles;              // 左面板本地选中文件（调用方保证非空）
    QString remoteDirectory;             // 右面板当前目录（空则由本映射就地回退 '/'（调用方亦可先行回退，幂等））
    // v2.15 任务9：入口目录展开产物（含目录选择时由 Widget 填入，rel 空=顶层扁平文件）。
    // 空=纯文件选择/旧口径调用方，简写组装（files+remoteDirectory）逐字不变。
    QList<folderexpand::ExpandedFile> expandedItems;
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
    const QString remoteDirectory =
        cfg.remoteDirectory.isEmpty() ? QStringLiteral("/") : cfg.remoteDirectory;
    if (!cfg.expandedItems.isEmpty()) {
        // 全量承载（规格 §3.1）：只产 items 数组、不写 files/remoteDirectory——
        // 顶层扁平文件项 = composeRemotePath(remoteDir, 文件名)、目录镜像项 =
        // composeRemotePath(remoteDir, rel)；方向/覆盖写引擎规范缺省值
        // （upload/overwrite，与编辑器 deployItemsFromTable 同口径）。
        QVariantList items;
        for (const folderexpand::ExpandedFile& entry : cfg.expandedItems) {
            const QString rel = entry.remoteRel.isEmpty()
                ? QFileInfo(entry.localPath).fileName()   // 顶层文件 → 扁平远端名
                : entry.remoteRel;                          // 目录镜像 → 相对段
            QVariantMap item;
            item.insert(QStringLiteral("localPath"), entry.localPath);
            item.insert(QStringLiteral("remotePath"),
                        folderexpand::composeRemotePath(remoteDirectory, rel));
            item.insert(QStringLiteral("direction"), QStringLiteral("upload"));
            item.insert(QStringLiteral("overwrite"), QStringLiteral("overwrite"));
            items.push_back(item);
        }
        params.insert(QStringLiteral("items"), items);
    } else {
        // v2.14 简写组装逐字不动（expandedItems 空 = 纯文件选择/旧口径调用方）
        params.insert(QStringLiteral("files"), cfg.localFiles);
        params.insert(QStringLiteral("remoteDirectory"), remoteDirectory);
    }
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
