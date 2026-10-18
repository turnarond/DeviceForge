/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: FolderExpand.h
 *
 * Date: 2026-10-18
 *
 * Author: turnarond
 *
 * Description: 部署页文件夹支持的目录展开共享纯头（v2.15 波2 第一腿，任务 7，
 *              参照 WsEventTypes.h / TaskStepValidation.h 纯头 include 模式，
 *              无 src 级 CMake 清单改动）。
 *              领域规则（规格 §3.1，约束性）：
 *              ① 目录展开以**目录自身名**为镜像相对路径首段——D:/pkg/bin 内的
 *                app.conf 得 rel "bin/app.conf"，lib/a.so 得 "bin/lib/a.so"；
 *              ② 顶层普通文件 rel 为空（扁平落 remoteDir 根）；
 *              ③ 展开后文件条目总数 > kMaxExpandedItems(5000) → 整体拒绝，
 *                中文 error 同时携带实际条数与上限条数；
 *              ④ 空目录 → 0 条目、ok=true，不算错误；
 *              ⑤ 递归全深度、不追符号链接——缺省 scanDir 用 QDirIterator 默认
 *                旗标（刻意不含 QDirIterator::FollowSymlinks），链接目录不入；
 *              ⑥ 顺序：selections 按输入序处理，目录内沿用 scanner 返回序
 *                （缺省实现即 QDirIterator 自然序）；
 *              ⑦ 相对路径大小写冲突不检测（现场文件系统互斥，YAGNI）。
 *              DirScanner 依赖注入以脱盘测试；composeRemotePath 为远端路径
 *              拼接单源（任务 9 三入口共用）。
 */

#pragma once

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace folderexpand {

// 单次部署展开文件条目总数上限（超限整体拒绝，规格 §3.1）。
constexpr int kMaxExpandedItems = 5000;

// 顶层选中条目：localPath 为文件或目录，isDirectory 由调用方判定
// （QFileInfo 或面板行属性），本头不再触盘复核（信任调用方，YAGNI）。
struct Selection {
    QString localPath;   // 顶层条目（文件或目录）
    bool isDirectory;    // 调用方判定（QFileInfo 或面板行属性）
};

// 展开后的单个普通文件：localPath 绝对路径 + remoteRel 远端相对路径
// （目录内=目录名/子路径；顶层文件=空，扁平）。
struct ExpandedFile {
    QString localPath;   // 普通文件绝对路径
    QString remoteRel;   // 远端相对路径：目录内=目录名/子路径；顶层文件=空（扁平）
};

// 展开结果：ok=false 时 error 为完整中文文案、files 已整体丢弃。
struct ScanResult { bool ok = false; QList<ExpandedFile> files; QString error; };

// 目录扫描器注入缝：输入目录绝对路径，返回目录内全部普通文件绝对路径
// （递归全深度）。缺省实现 scanDir 用 QDirIterator：Filters 仅 QDir::Files、
// 旗标仅 Subdirectories——QDirIterator 默认旗标刻意不含 FollowSymlinks，
// 即不跟随符号链接目录（防环游/越界），链接指向的文件也不递归。
using DirScanner = std::function<QStringList(const QString& dir)>;

inline QStringList scanDir(const QString& dir)
{
    QStringList files;
    // 默认旗标 = Subdirectories（无 FollowSymlinks）：递归全深度、不追符号链接。
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        files.append(it.next());
    }
    return files;
}

// 展开主循环：selections 按输入序处理——文件条目直接以空 rel 落入；
// 目录条目经 scanner 取回普通文件清单后以「目录名/相对路径」为镜像 rel。
// 计数闸：累计条数 > kMaxExpandedItems → 整体拒绝（files 清空）+ 中文 error
// 「目录展开后共 %1 条文件，超过单次部署上限 %2，请精简目录或分批部署」。
inline ScanResult expand(const QList<Selection>& selections,
                         const DirScanner& scanner = scanDir)
{
    ScanResult result;
    for (const Selection& selection : selections) {
        if (!selection.isDirectory) {
            // 顶层普通文件：扁平 rel=空串，localPath 原样透传。
            result.files.append(ExpandedFile{selection.localPath, QString()});
        } else {
            // 目录展开：目录自身名（QFileInfo::fileName）为相对路径首段。
            const QString dirName = QFileInfo(selection.localPath).fileName();
            const QStringList scanned = scanner(selection.localPath);
            for (const QString& file : scanned) {
                // 目录内相对路径：QDir::relativeFilePath 产出 '/' 分隔子路径，
                // 与首段目录名拼成镜像 rel（如 "bin/lib/a.so"）。
                const QString childRel = QDir(selection.localPath).relativeFilePath(file);
                result.files.append(ExpandedFile{file, dirName + QLatin1Char('/') + childRel});
            }
        }
        // 计数闸按每个 selection 处理完后判定，报错时实际数=当前累计总数。
        if (result.files.size() > kMaxExpandedItems) {
            const int actualCount = result.files.size();   // 先取实际数再清
            result.ok = false;
            result.files.clear();   // 整体拒绝：不留半份清单
            result.error = QStringLiteral("目录展开后共 %1 条文件，超过单次部署上限 %2，"
                                          "请精简目录或分批部署")
                               .arg(actualCount)
                               .arg(kMaxExpandedItems);
            return result;
        }
    }
    result.ok = true;
    return result;
}

// 远端路径拼接单源（任务 9 三入口共用）：
// ① rel 空 → 直挂 remoteDir 根（防御分支：顶层扁平文件实走 files 键简写，
//    正常不经此函数；返回规范化 remoteDir，去尾斜杠、根 "/" 保留）；
// ② 否则 remoteDir 规范化去尾斜杠 + "/" + rel。根目录 "/" 因 size>1 守卫
//    不被削空，且走 TaskStepValidation.h parseDeployItems 同款 base=="/"
//    特判直接拼 "/"+rel，杜绝 "//xxx" 双斜杠形态。
inline QString composeRemotePath(const QString& remoteDir, const QString& rel)
{
    QString base = remoteDir;
    while (base.size() > 1 && base.endsWith(u'/')) {
        base.chop(1);
    }
    if (rel.isEmpty()) {
        return base;   // 防御分支：仅规范化 remoteDir 本身
    }
    return base == QLatin1String("/")
        ? QStringLiteral("/") + rel
        : base + QLatin1Char('/') + rel;
}

} // namespace folderexpand
