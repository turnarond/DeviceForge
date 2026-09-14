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
    void cancel(const QUuid& taskId);
    void shutdown();
    void setEventSink(EventSink sink);

    static QString deviceKey(const TransferTask& task);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
