// TransferEvents.h — 调度器对外发布的值语义任务与事件快照

#pragma once

#include "framework/DeviceInfo.h"
#include "transfer/TransferExecutor.h"

#include <QString>
#include <QUuid>
#include <QVector>

struct TransferTask
{
    QString displayName;
    QString protocol;
    DeviceInfo device;
    QString userIdentity;
    QString credentialKey;
    quint64 generation = 0;
    bool useFtps = false;
    // F6 业务语义：仅当目标通过原子提交成功后，由 Scheduler 的 Executor 编排
    // 删除源文件；TransferExecutor 本身仍保持“只交付、不删源”的单一职责。
    bool removeSourceAfterCommit = false;
    QVector<TransferItemRequest> items;
};

enum class TransferEventType {
    TaskAdded,
    TaskStateChanged,
    ItemProgress,
    ItemFinished,
    TaskFinished
};

struct TransferTaskSnapshot
{
    QUuid id;
    QString displayName;
    QString deviceKey;
    quint64 generation = 0;
    TransferState state = TransferState::Queued;
    int itemCount = 0;
    int currentItemIndex = -1;
    int progress = 0;
    QVector<TransferItemResult> itemResults;
    TransferError error;
    bool cancelRequested = false;
};

struct TransferEvent
{
    TransferEventType type = TransferEventType::TaskAdded;
    TransferTaskSnapshot snapshot;
    qint64 timestampMs = 0;
};
