/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: DeployEntryBuilder.h
 *
 * Date: 2026-10-18
 *
 * Author: turnarond
 *
 * Description: 部署入口条目装配共享纯头（v2.15 波2 任务9 评审修复轮 1，
 *              I-1 上限绕过 + I-2 单发装配腿可测缝；参照 FolderExpand.h /
 *              TaskStepValidation.h 纯头自由函数模式，无 Widget/QtWidgets 依赖，
 *              无 src 级 CMake 清单改动）。
 *              把 FtpDeployWidget::buildDeployEntries 的单发/保存为模板共用装配腿
 *              （hasDirectory 判定 → folderexpand::expand → composeRemotePath 逐条
 *              组装 → 拒绝文案逐字直传）收敛为本头单一实现，Widget 退化为薄调用方。
 *              评审 I-1 修复核心：条目分类以**点击时刻磁盘实况**
 *              （QFileInfo(localPath).isDir()）为准，Selection.isDirectory 面板行
 *              快照仅作输入信息一律丢弃——快照过期把目录记成文件时，旧实现产扁平
 *              清单+空远端向量落入 DeployJob legacy 无上限遍历，绕过 kMaxExpandedItems
 *              整体拒绝与远端镜像不变式（规格 §3.1）；实况重分类后目录必走
 *              expand 受闸腿。
 *              口径不变项：无目录（实况）→ v2.14 逐字扁平行为（files=原始清单、
 *              remotePaths/expandedItems 留空）；超上限/根退化拒绝 = 纯头 error
 *              逐字直传（失败不静默）；全空目录 → 中文「所选目录为空…」拒绝；
 *              rel 空顶层扁平项以文件名作远端末段（控制器裁定：composeRemotePath
 *              rel 空返回裸 base，不可直用）。
 */

#pragma once

#include "deploy/FolderExpand.h"

#include <QFileInfo>
#include <QList>
#include <QString>
#include <QStringList>

#include <string>
#include <vector>

namespace deployentry {

// 入口装配主函数（三入口同口径中的单发/保存为模板共用腿；编辑器 teAddDir 腿经
// TaskTemplateEditorDialog::addDeployDirectoryEntries 同用 expand+composeRemotePath）。
// 输出契约：
// ① 返回 true 且 remotePathsOut 为空 ⇔ selections 实况无任何目录——filesOut=原始
//    本地清单（输入序逐字），expandedItemsOut 留空（v2.14 扁平口径与 requestKey 键
//    形态零漂移，DeployJob 走 basename/legacy 分支）；
// ② 返回 true 且 remotePathsOut 非空 ⇔ 含目录展开成功——filesOut=展开后普通文件
//    本地路径、remotePathsOut=每条目远端全路径（顶层文件=remoteDir/文件名 扁平项、
//    目录内容=remoteDir/rel 镜像项）、expandedItemsOut=展开条目原样（保存为模板
//    映射层消费），两向量同长、顺序与展开序一致；
// ③ 返回 false ⇔ 拒绝——errorOut 为完整中文文案（超上限/根退化=纯头逐字直传；
//    全空目录=本头「所选目录为空…」）。三输出容器（filesOut/remotePathsOut/
//    expandedItemsOut）在函数入口即清空，故拒绝返回时它们**为空**（不写任何条目），
//    与调用方「拒绝即不产出半份清单」的期望一致（tst_deploy_entry_builder 已钉）。
// scanner 注入缝沿用 FolderExpand 口径以脱盘测试上限拒绝分支；类型判定不脱盘
// （实况分类正是本函数的修复语义）。
inline bool build(const QList<folderexpand::Selection>& selections,
                  const QString& remoteDirectory,
                  std::vector<std::string>& filesOut,
                  std::vector<std::string>& remotePathsOut,
                  QList<folderexpand::ExpandedFile>& expandedItemsOut,
                  QString& errorOut,
                  const folderexpand::DirScanner& scanner = folderexpand::scanDir)
{
    filesOut.clear();
    remotePathsOut.clear();
    expandedItemsOut.clear();
    errorOut.clear();

    // 评审 I-1：分类以点击时刻实况为准，面板行快照 isDirectory 一律丢弃重算——
    // 目录行实为文件 → 归扁平（该文件正常交付，不再 0 条目丢失）；
    // 文件行实为目录 → 归展开（受 5000 闸 + 镜像映射），杜绝快照口径的无上限绕过。
    QList<folderexpand::Selection> reality;
    reality.reserve(selections.size());
    bool hasDirectory = false;
    for (const folderexpand::Selection& selection : selections) {
        const bool dirNow = QFileInfo(selection.localPath).isDir();
        reality.append({selection.localPath, dirNow});
        hasDirectory = hasDirectory || dirNow;
    }

    if (!hasDirectory) {
        // v2.14 逐字扁平口径：files=原始清单（输入序）、远端向量与展开条目留空，
        // 后端 requestKey 键形态零漂移。
        for (const folderexpand::Selection& selection : selections)
            filesOut.push_back(selection.localPath.toStdString());
        return true;
    }

    const folderexpand::ScanResult expanded = folderexpand::expand(reality, scanner);
    if (!expanded.ok) {
        // 超上限/根目录退化：纯头中文文案逐字直传（调用方日志闸拒绝，不静默放行）。
        errorOut = expanded.error;
        return false;
    }
    if (expanded.files.isEmpty()) {
        errorOut = QStringLiteral("所选目录为空，没有可部署的文件，请检查左侧选择");
        return false;
    }
    for (const folderexpand::ExpandedFile& entry : expanded.files) {
        filesOut.push_back(entry.localPath.toStdString());
        // rel 空=顶层扁平文件 → 文件名作远端末段（控制器裁定的正确口径；与映射层
        // items 组装、编辑器 addDeployDirectoryEntries 同规则）。
        const QString rel = entry.remoteRel.isEmpty()
            ? QFileInfo(entry.localPath).fileName()
            : entry.remoteRel;
        remotePathsOut.push_back(
            folderexpand::composeRemotePath(remoteDirectory, rel).toStdString());
    }
    expandedItemsOut = expanded.files;
    return true;
}

} // namespace deployentry
