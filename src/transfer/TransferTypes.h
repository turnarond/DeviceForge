// TransferTypes.h — 可靠传输领域的基础状态与错误类型

#pragma once

#include <QString>
#include <QVector>

enum class TransferState {
    Queued,
    Preparing,
    Transferring,
    Verifying,
    Committing,
    Reconnecting,
    RetryWaiting,
    NeedsAttention,
    Succeeded,
    PartiallySucceeded,
    Failed,
    Cancelling,
    Cancelled
};

enum class TransferErrorCode {
    None,
    Timeout,
    ConnectionLost,
    Authentication,
    Permission,
    InvalidPath,
    LocalIo,
    RemoteIo,
    TargetChanged,
    Cancelled,
    Unsupported
};

struct TransferError
{
    TransferErrorCode code = TransferErrorCode::None;
    QString message;
    QString detail;
    bool retryable = false;
};

TransferError classifyTransferError(QString message, int nativeCode);
QVector<int> retryDelaysMs();
bool canTransition(TransferState from, TransferState to);
