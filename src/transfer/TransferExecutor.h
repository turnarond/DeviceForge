// TransferExecutor.h — 单文件可靠传输执行器

#pragma once

#include "transfer/ITransferChannel.h"

#include <atomic>
#include <functional>

namespace xfer {
class EnsuredDirCache;
}

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
    // 目标内容已校验并完成交付（包含明确降级的非原子交付）。
    // nonAtomic 只表示提交方式，不得单独作为交付事实。
    bool deliverySucceeded = false;
};

inline bool hasDeliveredTarget(const TransferItemResult& result)
{
    const bool successfulState = result.state == TransferState::Succeeded
        || result.state == TransferState::PartiallySucceeded;
    return !result.skipped && successfulState
        && (result.atomicCommitSucceeded || result.deliverySucceeded);
}

class TransferExecutor
{
public:
    using Sleeper = std::function<void(int)>;

    // v2.15 波3 任务11：ensuredDirs 为末位可选参（缺省 nullptr=父目录确保步逐字
    // 保持波2 现状序列）；非空时跨执行器实例复用「已确认存在」的远端目录层，
    // 生命周期须覆盖 execute()（DeployJob 以成员持有，每设备一份）。
    explicit TransferExecutor(ITransferChannel& channel,
                              Sleeper sleeper = {},
                              xfer::EnsuredDirCache* ensuredDirs = nullptr);

    // credentials 必须是任务私有对象，且生命周期覆盖 execute()；execute() 在所有
    // 终态安全擦除它，避免密码副本长驻任务对象。
    TransferExecutor(ITransferChannel& channel,
                     DeviceInfo device,
                     AuthInfo& credentials,
                     Sleeper sleeper = {},
                     xfer::EnsuredDirCache* ensuredDirs = nullptr);

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
    // v2.15 波2：上传前确保远端目标父目录存在。仅当目标不存在时被调用（目标存在
    // 即证明其父目录存在）。返回 false 时 terminalResult 为确定性终局结果：
    // 建目录失败/通道不支持自动创建不进入重试环（确定性失败族）。
    // v2.15 波3 任务11：m_ensuredDirs 非空时，本运行内已确认存在的层跳过 stat 探测
    // （仅省探测，缺失判定/建目录/复检/取消检查点语义不变；nullptr=波2 现状逐字）。
    bool ensureRemoteParentDirectories(const QString& remotePath,
                                       const TransferCapabilities& capabilities,
                                       std::atomic_bool& cancel,
                                       TransferItemResult& terminalResult);
    bool waitBeforeRetry(int milliseconds, std::atomic_bool& cancel) const;

    ITransferChannel& m_channel;
    Sleeper m_sleeper;
    DeviceInfo m_device;
    AuthInfo* m_credentials = nullptr;
    // v2.15 波3 任务11：运行级已确保目录缓存的借用指针（非持有；nullptr=禁用，
    // 父目录确保步维持波2 逐字序列）
    xfer::EnsuredDirCache* m_ensuredDirs = nullptr;
};
