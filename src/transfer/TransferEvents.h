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
