// TransferScheduler.cpp — 私有线程池、设备队列、目标锁与事件节流实现

#include "transfer/TransferScheduler.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QQueue>
#include <QRecursiveMutex>
#include <QSet>
#include <QThread>
#include <QThreadPool>

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>

namespace {

constexpr int kMinConcurrency = 1;
constexpr int kMaxConcurrency = 8;
constexpr qint64 kProgressIntervalMs = 100;

qint64 steadyMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool isTerminal(TransferState state)
{
    return state == TransferState::Succeeded
        || state == TransferState::PartiallySucceeded
        || state == TransferState::NeedsAttention
        || state == TransferState::Failed
        || state == TransferState::Cancelled;
}

TransferError cancelledError()
{
    return {TransferErrorCode::Cancelled,
            QStringLiteral("传输已取消"),
            QStringLiteral("cancelled"),
            false};
}

TransferError conflictError()
{
    return {TransferErrorCode::TargetChanged,
            QStringLiteral("目标路径已有排队或运行中的任务"),
            QStringLiteral("target path conflict"),
            false};
}

TransferError invalidExecutorResultError()
{
    return {TransferErrorCode::RemoteIo,
            QStringLiteral("执行器未返回有效终态"),
            QStringLiteral("executor returned non-terminal state"),
            false};
}

TransferError executorExceptionError()
{
    return {TransferErrorCode::RemoteIo,
            QStringLiteral("传输执行器异常终止"),
            QStringLiteral("executor exception"),
            false};
}

QString normalizedRemotePath(const QString& path)
{
    const bool absolute = path.startsWith(u'/');
    const QStringList parts = path.split(u'/', Qt::SkipEmptyParts);
    QStringList normalized;
    for (const QString& part : parts) {
        if (part == QStringLiteral("."))
            continue;
        if (part == QStringLiteral("..")) {
            if (!normalized.isEmpty() && normalized.back() != QStringLiteral(".."))
                normalized.removeLast();
            else if (!absolute)
                normalized.push_back(part);
            continue;
        }
        normalized.push_back(part);
    }

    QString result = normalized.join(u'/');
    if (absolute)
        result.prepend(u'/');
    if (result.isEmpty())
        return absolute ? QStringLiteral("/") : QStringLiteral(".");
    return result;
}

QString normalizedLocalPath(const QString& path)
{
    QString normalized = QDir::fromNativeSeparators(
        QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}

} // namespace

class TransferScheduler::Impl
{
public:
    class WorkerThreadScope
    {
    public:
        explicit WorkerThreadScope(Impl& impl)
            : m_impl(impl)
            , m_threadId(QThread::currentThreadId())
        {
            QMutexLocker locker(&m_impl.mutex);
            m_impl.workerThreadIds.insert(m_threadId);
        }

        ~WorkerThreadScope()
        {
            QMutexLocker locker(&m_impl.mutex);
            m_impl.workerThreadIds.remove(m_threadId);
        }

    private:
        Impl& m_impl;
        Qt::HANDLE m_threadId;
    };

    struct Record
    {
        QUuid id;
        TransferTask task;
        QString deviceKey;
        QStringList remoteTargetKeys;
        QStringList localTargetKeys;
        std::atomic_bool cancel{false};
        TransferState state = TransferState::Queued;
        QVector<TransferItemResult> itemResults;
        TransferError error;
        int currentItemIndex = -1;
        int progress = 0;
        qint64 lastProgressMs = 0;
        bool hasPublishedProgress = false;
        bool ready = false;
        bool running = false;
        bool terminal = false;
        bool cancellationRequested = false;
    };

    Impl(int maxConcurrency, Executor executor, Clock clock)
        : maxConcurrency(std::clamp(maxConcurrency, kMinConcurrency, kMaxConcurrency))
        , executor(std::move(executor))
        , clock(clock ? std::move(clock) : Clock(steadyMilliseconds))
    {
        pool.setMaxThreadCount(this->maxConcurrency);
    }

    TransferTaskSnapshot snapshotLocked(const Record& record) const
    {
        return {record.id,
                record.task.displayName,
                record.deviceKey,
                record.task.generation,
                record.state,
                static_cast<int>(record.task.items.size()),
                record.currentItemIndex,
                record.progress,
                record.itemResults,
                record.error,
                record.cancellationRequested};
    }

    TransferEvent eventLocked(TransferEventType type, const Record& record) const
    {
        return {type, snapshotLocked(record), clock()};
    }

    void deliver(const TransferEvent& event)
    {
        EventSink currentSink;
        {
            QMutexLocker locker(&mutex);
            currentSink = sink;
        }
        if (!currentSink)
            return;
        try {
            currentSink(event);
        } catch (const std::exception&) {
            qWarning() << "TransferScheduler event sink threw an exception";
        } catch (...) {
            qWarning() << "TransferScheduler event sink threw a non-standard exception";
        }
    }

    QStringList remoteTargetKeysFor(const TransferTask& task) const
    {
        QSet<QString> keys;
        const QString prefix = TransferScheduler::deviceKey(task) + u'|';
        for (const auto& item : task.items) {
            if (item.direction == TransferDirection::Upload && !item.remotePath.isEmpty())
                keys.insert(prefix + normalizedRemotePath(item.remotePath));
        }
        return keys.values();
    }

    QStringList localTargetKeysFor(const TransferTask& task) const
    {
        QSet<QString> keys;
        for (const auto& item : task.items) {
            if (item.direction == TransferDirection::Download && !item.localPath.isEmpty())
                keys.insert(normalizedLocalPath(item.localPath));
        }
        return keys.values();
    }

    bool hasRemoteConflictLocked(const QStringList& keys) const
    {
        for (const QString& key : keys) {
            if (reservedRemoteTargets.contains(key))
                return true;
        }
        return false;
    }

    bool hasActiveLocalConflictLocked(const Record& record) const
    {
        for (const QString& key : record.localTargetKeys) {
            if (activeLocalTargets.contains(key))
                return true;
        }
        return false;
    }

    void releaseRemoteTargetsLocked(const Record& record)
    {
        for (const QString& key : record.remoteTargetKeys) {
            if (reservedRemoteTargets.value(key) == record.id)
                reservedRemoteTargets.remove(key);
        }
    }

    void removeFromDeviceQueueLocked(const Record& record)
    {
        auto queueIt = deviceQueues.find(record.deviceKey);
        if (queueIt == deviceQueues.end())
            return;
        queueIt->removeAll(record.id);
        if (queueIt->isEmpty())
            deviceQueues.erase(queueIt);
    }

    QVector<std::shared_ptr<Record>> scheduleEligibleLocked()
    {
        QVector<std::shared_ptr<Record>> starts;
        if (!accepting)
            return starts;
        while (activeCount < maxConcurrency) {
            bool scheduled = false;
            const QStringList keys = deviceQueues.keys();
            for (const QString& key : keys) {
                if (activeCount >= maxConcurrency)
                    break;
                if (activeDevices.contains(key))
                    continue;

                auto queueIt = deviceQueues.find(key);
                if (queueIt == deviceQueues.end())
                    continue;
                while (!queueIt->isEmpty() && !records.contains(queueIt->head()))
                    queueIt->dequeue();
                if (queueIt->isEmpty()) {
                    deviceQueues.erase(queueIt);
                    continue;
                }

                const auto record = records.value(queueIt->head());
                if (!record || !record->ready || record->terminal
                    || hasActiveLocalConflictLocked(*record))
                    continue;

                queueIt->dequeue();
                if (queueIt->isEmpty())
                    deviceQueues.erase(queueIt);
                activeDevices.insert(key);
                for (const QString& localKey : record->localTargetKeys)
                    activeLocalTargets.insert(localKey);
                record->running = true;
                ++activeCount;
                starts.push_back(record);
                scheduled = true;
            }
            if (!scheduled)
                break;
        }
        return starts;
    }

    void start(const QVector<std::shared_ptr<Record>>& recordsToStart)
    {
        for (const auto& record : recordsToStart) {
            pool.start([this, record] { run(record); });
        }
    }

    void publishState(const std::shared_ptr<Record>& record, TransferState state)
    {
        QMutexLocker ordering(&eventMutex);
        TransferEvent event;
        {
            QMutexLocker locker(&mutex);
            if (record->terminal || record->cancellationRequested
                || !records.contains(record->id))
                return;
            record->state = state;
            event = eventLocked(TransferEventType::TaskStateChanged, *record);
        }
        deliver(event);
    }

    void publishProgress(const std::weak_ptr<Record>& weakRecord,
                         int itemIndex,
                         int value)
    {
        const auto record = weakRecord.lock();
        if (!record)
            return;

        QMutexLocker ordering(&eventMutex);
        TransferEvent event;
        bool shouldPublish = false;
        {
            QMutexLocker locker(&mutex);
            if (record->terminal || record->cancellationRequested
                || !records.contains(record->id)
                || record->currentItemIndex != itemIndex)
                return;

            record->progress = qMax(record->progress, std::clamp(value, 0, 100));
            const qint64 now = clock();
            if (!record->hasPublishedProgress
                || now - record->lastProgressMs >= kProgressIntervalMs) {
                record->hasPublishedProgress = true;
                record->lastProgressMs = now;
                event = {TransferEventType::ItemProgress, snapshotLocked(*record), now};
                shouldPublish = true;
            }
        }
        if (shouldPublish)
            deliver(event);
    }

    void publishItemFinished(const std::shared_ptr<Record>& record,
                             const TransferItemResult& result)
    {
        QMutexLocker ordering(&eventMutex);
        TransferEvent event;
        {
            QMutexLocker locker(&mutex);
            if (record->terminal || !records.contains(record->id))
                return;
            record->itemResults.push_back(result);
            record->error = result.error;
            event = eventLocked(TransferEventType::ItemFinished, *record);
        }
        deliver(event);
    }

    void finish(const std::shared_ptr<Record>& record,
                TransferState state,
                TransferError error)
    {
        QMutexLocker ordering(&eventMutex);
        TransferEvent terminalEvent;
        QVector<std::shared_ptr<Record>> starts;
        {
            QMutexLocker locker(&mutex);
            if (record->terminal || !records.contains(record->id))
                return;

            if (record->cancellationRequested) {
                state = TransferState::Cancelled;
                error = cancelledError();
            }
            record->terminal = true;
            record->state = state;
            record->error = std::move(error);
            terminalEvent = eventLocked(TransferEventType::TaskFinished, *record);

            if (record->running) {
                activeDevices.remove(record->deviceKey);
                for (const QString& key : record->localTargetKeys)
                    activeLocalTargets.remove(key);
                record->running = false;
                --activeCount;
            } else {
                removeFromDeviceQueueLocked(*record);
            }
            releaseRemoteTargetsLocked(*record);
            records.remove(record->id);
            starts = scheduleEligibleLocked();
        }
        start(starts);
        deliver(terminalEvent);
    }

    void run(const std::shared_ptr<Record>& record)
    {
        WorkerThreadScope workerThread(*this);
        publishState(record, TransferState::Preparing);
        if (record->cancel.load()) {
            finish(record, TransferState::Cancelled, cancelledError());
            return;
        }
        if (record->task.items.isEmpty()) {
            const TransferError error{TransferErrorCode::InvalidPath,
                                      QStringLiteral("传输任务不包含文件"),
                                      QStringLiteral("empty transfer task"),
                                      false};
            finish(record, TransferState::Failed, error);
            return;
        }

        for (int index = 0; index < record->task.items.size(); ++index) {
            if (record->cancel.load()) {
                finish(record, TransferState::Cancelled, cancelledError());
                return;
            }

            bool cancellationRequested = false;
            {
                QMutexLocker locker(&mutex);
                if (record->terminal || !records.contains(record->id))
                    return;
                cancellationRequested = record->cancellationRequested;
                if (!cancellationRequested) {
                    record->currentItemIndex = index;
                    record->progress = 0;
                    record->hasPublishedProgress = false;
                }
            }
            if (cancellationRequested) {
                finish(record, TransferState::Cancelled, cancelledError());
                return;
            }
            publishState(record, TransferState::Transferring);

            TransferItemResult result;
            try {
                if (executor) {
                    const std::weak_ptr<Record> weakRecord(record);
                    result = executor(
                        record->task,
                        index,
                        record->task.items.at(index),
                        record->cancel,
                        [this, weakRecord, index](int value) {
                            publishProgress(weakRecord, index, value);
                        });
                } else {
                    result = {TransferState::Failed,
                              invalidExecutorResultError(),
                              0,
                              false,
                              0};
                }
            } catch (const std::exception&) {
                result = {TransferState::Failed,
                          executorExceptionError(),
                          0,
                          false,
                          0};
            } catch (...) {
                result = {TransferState::Failed,
                          executorExceptionError(),
                          0,
                          false,
                          0};
            }

            if (!isTerminal(result.state)) {
                result.state = TransferState::Failed;
                result.error = invalidExecutorResultError();
            }

            // Executor 返回到终态发布组成一个线性化区间。取消若先取得事件门闸，
            // 即使底层刚完成不可逆提交，调度任务也只能从 Cancelling 收口为
            // Cancelled；若本区间先取得门闸，则成功终态先发布，后续取消为 no-op。
            QMutexLocker completionOrdering(&eventMutex);
            {
                QMutexLocker locker(&mutex);
                if (record->cancellationRequested) {
                    result.state = TransferState::Cancelled;
                    result.error = cancelledError();
                    result.bytes = 0;
                }
            }
            publishItemFinished(record, result);

            bool cancellationWon = false;
            {
                QMutexLocker locker(&mutex);
                cancellationWon = record->cancellationRequested;
            }
            if (cancellationWon) {
                finish(record, TransferState::Cancelled, cancelledError());
                return;
            }

            if (result.state != TransferState::Succeeded) {
                TransferState taskState = result.state;
                if (taskState == TransferState::Failed && !record->itemResults.isEmpty()) {
                    bool hadSuccess = false;
                    for (int i = 0; i + 1 < record->itemResults.size(); ++i)
                        hadSuccess = hadSuccess
                            || record->itemResults.at(i).state == TransferState::Succeeded;
                    if (hadSuccess)
                        taskState = TransferState::PartiallySucceeded;
                }
                finish(record, taskState, result.error);
                return;
            }
        }

        finish(record, TransferState::Succeeded, {});
    }

    QThreadPool pool;
    QRecursiveMutex eventMutex;
    QMutex mutex;
    QHash<QUuid, std::shared_ptr<Record>> records;
    QHash<QString, QQueue<QUuid>> deviceQueues;
    QHash<QString, QUuid> reservedRemoteTargets;
    QSet<QString> activeDevices;
    QSet<QString> activeLocalTargets;
    QSet<Qt::HANDLE> workerThreadIds;
    EventSink sink;
    const int maxConcurrency;
    Executor executor;
    Clock clock;
    int activeCount = 0;
    bool accepting = true;
};

TransferScheduler::TransferScheduler(int maxConcurrency, Executor executor, Clock clock)
    : m_impl(std::make_unique<Impl>(maxConcurrency,
                                    std::move(executor),
                                    std::move(clock)))
{
}

TransferScheduler::~TransferScheduler()
{
    shutdown();
}

QUuid TransferScheduler::submit(TransferTask task)
{
    const QUuid id = QUuid::createUuid();
    auto record = std::make_shared<Impl::Record>();
    record->id = id;
    record->task = std::move(task);
    record->deviceKey = deviceKey(record->task);
    record->remoteTargetKeys = m_impl->remoteTargetKeysFor(record->task);
    record->localTargetKeys = m_impl->localTargetKeysFor(record->task);

    TransferEvent addedEvent;
    TransferEvent conflictEvent;
    bool conflict = false;
    QMutexLocker ordering(&m_impl->eventMutex);
    {
        QMutexLocker locker(&m_impl->mutex);
        if (!m_impl->accepting)
            return {};

        addedEvent = m_impl->eventLocked(TransferEventType::TaskAdded, *record);
        conflict = m_impl->hasRemoteConflictLocked(record->remoteTargetKeys);
        if (conflict) {
            record->terminal = true;
            record->state = TransferState::NeedsAttention;
            record->error = conflictError();
            conflictEvent = m_impl->eventLocked(TransferEventType::TaskFinished, *record);
        } else {
            m_impl->records.insert(id, record);
            m_impl->deviceQueues[record->deviceKey].enqueue(id);
            for (const QString& key : record->remoteTargetKeys)
                m_impl->reservedRemoteTargets.insert(key, id);
        }
    }

    m_impl->deliver(addedEvent);
    if (conflict) {
        m_impl->deliver(conflictEvent);
        return id;
    }

    QVector<std::shared_ptr<Impl::Record>> starts;
    {
        QMutexLocker locker(&m_impl->mutex);
        const auto current = m_impl->records.value(id);
        if (current) {
            current->ready = true;
            starts = m_impl->scheduleEligibleLocked();
        }
    }
    m_impl->start(starts);
    return id;
}

void TransferScheduler::cancel(const QUuid& taskId)
{
    QMutexLocker ordering(&m_impl->eventMutex);
    TransferEvent cancellingEvent;
    TransferEvent terminalEvent;
    bool publishCancelling = false;
    bool publishTerminal = false;
    QVector<std::shared_ptr<Impl::Record>> starts;
    {
        QMutexLocker locker(&m_impl->mutex);
        const auto record = m_impl->records.value(taskId);
        if (!record || record->terminal)
            return;

        if (record->cancellationRequested)
            return;

        record->cancellationRequested = true;
        record->state = TransferState::Cancelling;
        cancellingEvent = m_impl->eventLocked(TransferEventType::TaskStateChanged, *record);
        publishCancelling = true;
    }
    if (publishCancelling)
        m_impl->deliver(cancellingEvent);

    {
        QMutexLocker locker(&m_impl->mutex);
        const auto record = m_impl->records.value(taskId);
        if (!record || record->terminal)
            return;
        record->cancel.store(true);
        if (!record->running) {
            record->terminal = true;
            record->state = TransferState::Cancelled;
            record->error = cancelledError();
            terminalEvent = m_impl->eventLocked(TransferEventType::TaskFinished, *record);
            publishTerminal = true;
            m_impl->removeFromDeviceQueueLocked(*record);
            m_impl->releaseRemoteTargetsLocked(*record);
            m_impl->records.remove(taskId);
            starts = m_impl->scheduleEligibleLocked();
        }
    }
    m_impl->start(starts);
    if (publishTerminal)
        m_impl->deliver(terminalEvent);
}

void TransferScheduler::shutdown()
{
    QMutexLocker ordering(&m_impl->eventMutex);
    QVector<TransferEvent> stateEvents;
    QVector<TransferEvent> terminalEvents;
    bool calledFromWorker = false;
    {
        QMutexLocker locker(&m_impl->mutex);
        calledFromWorker = m_impl->workerThreadIds.contains(QThread::currentThreadId());
        m_impl->accepting = false;

        const auto currentRecords = m_impl->records.values();
        for (const auto& record : currentRecords) {
            if (record->terminal)
                continue;
            if (!record->cancellationRequested) {
                record->cancellationRequested = true;
                record->state = TransferState::Cancelling;
                stateEvents.push_back(
                    m_impl->eventLocked(TransferEventType::TaskStateChanged, *record));
            }
        }
    }

    for (const auto& event : stateEvents)
        m_impl->deliver(event);

    {
        QMutexLocker locker(&m_impl->mutex);
        const auto currentRecords = m_impl->records.values();
        for (const auto& record : currentRecords) {
            if (record->terminal)
                continue;
            record->cancel.store(true);
            if (!record->running) {
                record->terminal = true;
                record->state = TransferState::Cancelled;
                record->error = cancelledError();
                terminalEvents.push_back(
                    m_impl->eventLocked(TransferEventType::TaskFinished, *record));
                m_impl->removeFromDeviceQueueLocked(*record);
                m_impl->releaseRemoteTargetsLocked(*record);
                m_impl->records.remove(record->id);
            }
        }
    }

    for (const auto& event : terminalEvents)
        m_impl->deliver(event);
    ordering.unlock();
    if (calledFromWorker)
        return;
    m_impl->pool.waitForDone();
}

void TransferScheduler::setEventSink(EventSink sink)
{
    QMutexLocker locker(&m_impl->mutex);
    m_impl->sink = std::move(sink);
}

QString TransferScheduler::deviceKey(const TransferTask& task)
{
    QString protocol = task.protocol;
    if (protocol.isEmpty())
        protocol = QString::fromStdString(task.device.protocol);
    return protocol.toCaseFolded() + u'|'
        + QString::fromStdString(task.device.ip).toCaseFolded() + u'|'
        + QString::number(task.device.port) + u'|' + task.userIdentity;
}
