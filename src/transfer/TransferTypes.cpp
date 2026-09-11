// TransferTypes.cpp — 可靠传输领域的基础状态与错误分类实现

#include "transfer/TransferTypes.h"

#include <initializer_list>

namespace {

bool containsAny(const QString& text, std::initializer_list<const char*> needles)
{
    for (const char* needle : needles) {
        if (text.contains(QLatin1String(needle), Qt::CaseInsensitive))
            return true;
    }
    return false;
}

TransferError makeError(TransferErrorCode code, const QString& message, bool retryable)
{
    return {code, message, message, retryable};
}

} // namespace

TransferError classifyTransferError(QString message, int nativeCode)
{
    if (nativeCode == 28 || containsAny(message, {"timed out", "timeout"}))
        return makeError(TransferErrorCode::Timeout, message, true);

    if (nativeCode == 67 || containsAny(message, {"authentication", "login denied"}))
        return makeError(TransferErrorCode::Authentication, message, false);

    if (containsAny(message, {"permission denied", "access denied"}))
        return makeError(TransferErrorCode::Permission, message, false);

    if (containsAny(message, {"invalid path", "no such file", "not found"}))
        return makeError(TransferErrorCode::InvalidPath, message, false);

    if (containsAny(message, {"connection", "network", "resolve host", "couldn't connect"}))
        return makeError(TransferErrorCode::ConnectionLost, message, true);

    return makeError(TransferErrorCode::RemoteIo, message, false);
}

QVector<int> retryDelaysMs()
{
    return {1000, 3000};
}

bool canTransition(TransferState from, TransferState to)
{
    switch (from) {
    case TransferState::Queued:
        return to == TransferState::Preparing || to == TransferState::Cancelling
            || to == TransferState::Cancelled;
    case TransferState::Preparing:
        return to == TransferState::Transferring || to == TransferState::NeedsAttention
            || to == TransferState::Failed || to == TransferState::Cancelling
            || to == TransferState::Cancelled;
    case TransferState::Transferring:
        return to == TransferState::Verifying || to == TransferState::Reconnecting
            || to == TransferState::RetryWaiting || to == TransferState::NeedsAttention
            || to == TransferState::Failed || to == TransferState::Cancelling
            || to == TransferState::Cancelled;
    case TransferState::Verifying:
        return to == TransferState::Committing || to == TransferState::Reconnecting
            || to == TransferState::RetryWaiting || to == TransferState::NeedsAttention
            || to == TransferState::Failed || to == TransferState::Cancelling
            || to == TransferState::Cancelled;
    case TransferState::Committing:
        return to == TransferState::Succeeded || to == TransferState::PartiallySucceeded
            || to == TransferState::NeedsAttention || to == TransferState::Failed
            || to == TransferState::Cancelling || to == TransferState::Cancelled;
    case TransferState::Reconnecting:
        return to == TransferState::Transferring || to == TransferState::RetryWaiting
            || to == TransferState::Failed || to == TransferState::Cancelling
            || to == TransferState::Cancelled;
    case TransferState::RetryWaiting:
        return to == TransferState::Preparing || to == TransferState::Reconnecting
            || to == TransferState::Failed || to == TransferState::Cancelling
            || to == TransferState::Cancelled;
    case TransferState::NeedsAttention:
        return to == TransferState::Preparing || to == TransferState::Failed
            || to == TransferState::Cancelling || to == TransferState::Cancelled;
    case TransferState::Cancelling:
        return to == TransferState::Cancelled;
    case TransferState::Succeeded:
    case TransferState::PartiallySucceeded:
    case TransferState::Failed:
    case TransferState::Cancelled:
        return false;
    }

    return false;
}
