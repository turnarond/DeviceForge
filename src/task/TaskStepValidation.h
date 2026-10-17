/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: TaskStepValidation.h
 *
 * Date: 2026-10-17
 *
 * Author: turnarond
 *
 * Description: 任务步骤**参数级**校验共享纯函数（v2.14 #49-3，可测性收敛，
 *              参照 NetRelayTypes.h / WsEventTypes.h 模式）。引擎 Validating
 *              阶段（TaskExecutionEngine::validateDeviceSteps）与编辑器
 *              （TaskTemplateEditorDialog::validate）此前两处并行实现参数校验，
 *              commands 分支文案与判定口径双双分叉（编辑器更严、引擎更宽）；
 *              本头把文案收敛为单一来源。
 *              分工裁定：端点/设备存在性校验**刻意留在引擎执行期**——模板可跨机
 *              迁移，导入/编辑期本机端点未必存在（v2.13 悬空引用语义的延伸），
 *              故本头只做参数形态判定，不接触 DeviceProfile。
 *              commands 判定采编辑器语义「trim 后判空」，比引擎旧裸列表 isEmpty
 *              更严（纯空白命令行视为无效）——引擎随之获得更严校验，属刻意收紧。
 */

#pragma once

#include "task/TaskExecutionTypes.h"    // taskStepTypeToChineseName
#include "task/TaskTemplateTypes.h"     // TaskStep / TaskStepType
#include "transfer/TransferExecutor.h"  // TransferItemRequest / TransferDirection / OverwritePolicy

#include <QFileInfo>
#include <QString>
#include <QVariant>
#include <QVector>

#include <cstddef>

namespace taskstep {

// 部署步骤参数解析（items 数组，或 files+remoteDirectory 简写）。
// 自引擎 cpp 匿名 ns 整体搬移，行为零变化：items 数组优先；简写展开去尾斜杠；
// 空路径拒；全部错误文案逐字保留。
inline bool parseDeployItems(const QVariantMap& params,
                             QVector<TransferItemRequest>& items,
                             QString& error)
{
    const QVariant itemsValue = params.value(QStringLiteral("items"));
    if (itemsValue.typeId() == QMetaType::QVariantList && !itemsValue.toList().isEmpty()) {
        const QVariantList list = itemsValue.toList();
        for (const QVariant& value : list) {
            if (value.typeId() != QMetaType::QVariantMap) {
                error = QStringLiteral("文件部署步骤条目结构非法（应为对象）");
                return false;
            }
            const QVariantMap map = value.toMap();
            TransferItemRequest item;
            item.localPath = map.value(QStringLiteral("localPath")).toString();
            item.remotePath = map.value(QStringLiteral("remotePath")).toString();
            if (item.localPath.isEmpty() || item.remotePath.isEmpty()) {
                error = QStringLiteral("文件部署步骤存在缺少 localPath/remotePath 的条目");
                return false;
            }
            item.direction = map.value(QStringLiteral("direction")).toString() == QLatin1String("download")
                ? TransferDirection::Download : TransferDirection::Upload;
            item.overwrite = map.value(QStringLiteral("overwrite")).toString() == QLatin1String("skip")
                ? OverwritePolicy::Skip : OverwritePolicy::Overwrite;
            items.push_back(item);
        }
        return true;
    }

    const QStringList files = params.value(QStringLiteral("files")).toStringList();
    const QString remoteDirectory = params.value(QStringLiteral("remoteDirectory")).toString();
    if (!files.isEmpty() && !remoteDirectory.isEmpty()) {
        QString base = remoteDirectory;
        while (base.size() > 1 && base.endsWith(u'/')) {
            base.chop(1);
        }
        for (const QString& file : files) {
            if (file.trimmed().isEmpty()) {
                error = QStringLiteral("文件部署清单存在空路径");
                return false;
            }
            TransferItemRequest item;
            item.localPath = file;
            const QString name = QFileInfo(file).fileName();
            item.remotePath = base == QLatin1String("/")
                ? QStringLiteral("/") + name
                : base + QLatin1Char('/') + name;
            items.push_back(item);
        }
        return true;
    }

    error = QStringLiteral("文件部署步骤缺少文件清单（items 数组，或 files + remoteDirectory）");
    return false;
}

// 单步参数级校验：空串=通过；非空=完整中文文案（「步骤 %1（…）…」整句，
// 引擎与编辑器共用此唯一口径）。deploy→「步骤 %1（文件部署）参数非法：%2」；
// run_commands/recover→「步骤 %1（%2）缺少命令列表」（%2=类型中文名，
// 批量命令/恢复检查）。Reboot 无参数级校验恒通过（端点检查留引擎执行期，
// 执行期 runCommandStep 的 command 缺省 reboot 兜底是运行防线，不属校验层）。
inline QString stepParamsValidationError(const TaskStep& step, std::size_t index)
{
    switch (step.type) {
    case TaskStepType::DeployFiles: {
        QVector<TransferItemRequest> items;
        QString parseError;
        if (!parseDeployItems(step.parameters, items, parseError)) {
            return QStringLiteral("步骤 %1（文件部署）参数非法：%2")
                .arg(index + 1)
                .arg(parseError);
        }
        return QString();
    }
    case TaskStepType::RunCommands:
    case TaskStepType::Recover: {
        // trim 后判空（原编辑器 commandsEmptyAfterTrim 语义）：纯空白命令行视为无效。
        const QStringList commands =
            step.parameters.value(QStringLiteral("commands")).toStringList();
        bool emptyAfterTrim = true;
        for (const QString& command : commands) {
            if (!command.trimmed().isEmpty()) {
                emptyAfterTrim = false;
                break;
            }
        }
        if (emptyAfterTrim) {
            return QStringLiteral("步骤 %1（%2）缺少命令列表")
                .arg(index + 1)
                .arg(taskStepTypeToChineseName(step.type));
        }
        return QString();
    }
    case TaskStepType::Reboot:
        break;   // 无参数级校验；协议过滤可选，端点存在性由引擎执行期自查
    }
    return QString();
}

} // namespace taskstep
