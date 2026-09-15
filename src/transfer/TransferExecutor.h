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
    // 目标已完成不可回退的原子提交。取消只能阻止提交之后的清理，
    // 调度器必须保留这一事实，恢复也必须跳过该项。
    bool atomicCommitSucceeded = false;
    // 仅表示覆盖策略明确跳过了已存在的目标；零次传输尝试不等于跳过。
    // 字段置于聚合末尾，保持既有聚合初始化参数顺序兼容。
    bool skipped = false;
    // F6 只在源实际删除后置位；目标交付与源清理是两个独立事实。
    bool sourceRemoved = false;
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
