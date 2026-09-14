// TransferScheduler.h — 按设备串行、跨设备并行的可靠传输调度器

#pragma once

#include "transfer/TransferEvents.h"

#include <QString>
#include <QUuid>

#include <atomic>
#include <functional>
#include <memory>

class TransferScheduler
{
public:
    using ProgressSink = std::function<void(int)>;
    using Executor = std::function<TransferItemResult(
        const TransferTask&,
        int,
        const TransferItemRequest&,
        std::atomic_bool&,
        const ProgressSink&)>;
    using EventSink = std::function<void(const TransferEvent&)>;
    using Clock = std::function<qint64()>;

    explicit TransferScheduler(int maxConcurrency,
                               Executor executor,
                               Clock clock = {});
    ~TransferScheduler();

    TransferScheduler(const TransferScheduler&) = delete;
    TransferScheduler& operator=(const TransferScheduler&) = delete;

    QUuid submit(TransferTask task);

    // 取消以 Cancelling 事件为线性化点：若它先于 Executor 终态发布，任务只会
    // 收口为 Cancelled；若不可逆提交的成功终态已先发布，后续 cancel 为 no-op。
    void cancel(const QUuid& taskId);
    // 外部线程调用会等待全部 worker 收口；若由本调度器 worker 的 EventSink 同步调用，
    // 仅发起拒绝提交与取消并立即返回，避免 waitForDone() 等待自身。拥有者随后应从
    // 非 worker 线程调用 shutdown()（或析构）完成等待。
    void shutdown();
    void setEventSink(EventSink sink);

    static QString deviceKey(const TransferTask& task);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
