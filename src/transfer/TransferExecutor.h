// TransferExecutor.h — 单文件可靠传输执行器

#pragma once

#include "transfer/ITransferChannel.h"

#include <atomic>
#include <functional>

enum class TransferDirection {
    Upload,
    Download
};

enum class OverwritePolicy {
    Overwrite,
    Skip
};

struct TransferItemRequest
{
    QString localPath;
    QString remotePath;
    TransferDirection direction = TransferDirection::Upload;
    OverwritePolicy overwrite = OverwritePolicy::Overwrite;
};

struct TransferItemResult
{
    TransferState state = TransferState::Failed;
    TransferError error;
    int attempts = 0;
    bool nonAtomic = false;
    qint64 bytes = 0;
};

class TransferExecutor
{
public:
    using Sleeper = std::function<void(int)>;

    explicit TransferExecutor(ITransferChannel& channel, Sleeper sleeper = {});

    // credentials 必须是任务私有对象，且生命周期覆盖 execute()；execute() 在所有
    // 终态安全擦除它，避免密码副本长驻任务对象。
    TransferExecutor(ITransferChannel& channel,
                     DeviceInfo device,
                     AuthInfo& credentials,
                     Sleeper sleeper = {});

    TransferItemResult execute(const TransferItemRequest& request, std::atomic_bool& cancel);

private:
    TransferItemResult executeUpload(const TransferItemRequest& request,
                                     const TransferCapabilities& capabilities,
                                     std::atomic_bool& cancel,
                                     int firstAttemptIndex);
    TransferItemResult executeDownload(const TransferItemRequest& request,
                                       const TransferCapabilities& capabilities,
                                       std::atomic_bool& cancel,
                                       int firstAttemptIndex);
    bool prepareRemoteStat(const QString& path,
                           TransferFileStat& stat,
                           std::atomic_bool& cancel,
                           int& attemptIndex,
                           TransferItemResult& terminalResult);
    bool waitBeforeRetry(int milliseconds, std::atomic_bool& cancel) const;

    ITransferChannel& m_channel;
    Sleeper m_sleeper;
    DeviceInfo m_device;
    AuthInfo* m_credentials = nullptr;
};
