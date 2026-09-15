// tst_transfer_scheduler.cpp — 设备 FIFO、跨设备并发、取消与事件快照契约

#include <QtTest>

#include "transfer/TransferScheduler.h"

#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QSemaphore>
#include <QThread>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace {

TransferTask uploadTask(const QString& label,
                        const QString& host,
                        const QString& remotePath,
                        quint64 generation = 1)
{
    TransferTask task;
    task.displayName = label;
    task.protocol = QStringLiteral("ftp");
    task.device = {host.toStdString(), 21, "ftp", label.toStdString(), ""};
    task.userIdentity = QStringLiteral("operator");
    task.credentialKey = QStringLiteral("credential/") + label;
    task.generation = generation;
    task.items.push_back({QStringLiteral("C:/input/") + label,
                          remotePath,
                          TransferDirection::Upload,
                          OverwritePolicy::Overwrite});
    return task;
}

TransferTask downloadTask(const QString& label,
                          const QString& host,
                          const QString& remotePath,
                          const QString& localPath)
{
    TransferTask task = uploadTask(label, host, remotePath);
    task.items.front().direction = TransferDirection::Download;
    task.items.front().localPath = localPath;
    return task;
}

TransferItemResult succeededResult()
{
    return {TransferState::Succeeded, {}, 1, false, 10};
}

TransferItemResult cancelledResult()
{
    return {TransferState::Cancelled,
            {TransferErrorCode::Cancelled,
             QStringLiteral("传输已取消"),
             QStringLiteral("cancelled"),
             false},
            1,
            false,
            0};
}

class EventLog
{
public:
    void append(const TransferEvent& event)
    {
        QMutexLocker locker(&m_mutex);
        m_events.push_back(event);
    }

    QVector<TransferEvent> events() const
    {
        QMutexLocker locker(&m_mutex);
        return m_events;
    }

    int count(TransferEventType type, const QUuid& taskId = {}) const
    {
        int result = 0;
        const auto copy = events();
        for (const auto& event : copy) {
            if (event.type == type && (taskId.isNull() || event.snapshot.id == taskId))
                ++result;
        }
        return result;
    }

    TransferEvent last(TransferEventType type, const QUuid& taskId) const
    {
        const auto copy = events();
        for (auto it = copy.crbegin(); it != copy.crend(); ++it) {
            if (it->type == type && it->snapshot.id == taskId)
                return *it;
        }
        return {};
    }

private:
    mutable QMutex m_mutex;
    QVector<TransferEvent> m_events;
};

} // namespace

class TstTransferScheduler : public QObject
{
    Q_OBJECT

private slots:
    void sameDeviceIsFifoWhileDifferentDevicesRunUpToLimit()
    {
        QMutex statsMutex;
        QHash<QString, int> activeByDevice;
        QHash<QString, int> maxByDevice;
        QStringList startOrder;
        int globalActive = 0;
        int globalMax = 0;
        QSemaphore release;

        auto executor = [&](const TransferTask& task,
                            int,
                            const TransferItemRequest&,
                            std::atomic_bool& cancel,
                            const TransferScheduler::ProgressSink&) {
            const QString key = TransferScheduler::deviceKey(task);
            {
                QMutexLocker locker(&statsMutex);
                ++activeByDevice[key];
                maxByDevice[key] = qMax(maxByDevice.value(key), activeByDevice.value(key));
                globalMax = qMax(globalMax, ++globalActive);
                startOrder.push_back(task.displayName);
            }
            while (!release.tryAcquire(1, 10)) {
                if (cancel.load())
                    return cancelledResult();
            }
            {
                QMutexLocker locker(&statsMutex);
                --activeByDevice[key];
                --globalActive;
            }
            return succeededResult();
        };

        EventLog events;
        TransferScheduler scheduler(2, executor);
        scheduler.setEventSink([&events](const TransferEvent& event) { events.append(event); });

        const auto first = scheduler.submit(
            uploadTask(QStringLiteral("first"), QStringLiteral("10.0.0.1"),
                       QStringLiteral("/opt/first.bin")));
        const auto second = scheduler.submit(
            uploadTask(QStringLiteral("second"), QStringLiteral("10.0.0.1"),
                       QStringLiteral("/opt/second.bin")));
        const auto other = scheduler.submit(
            uploadTask(QStringLiteral("other"), QStringLiteral("10.0.0.2"),
                       QStringLiteral("/opt/other.bin")));

        QTRY_VERIFY_WITH_TIMEOUT([&] {
            QMutexLocker locker(&statsMutex);
            return globalMax == 2;
        }(), 2000);
        {
            QMutexLocker locker(&statsMutex);
            QCOMPARE(maxByDevice.value(QStringLiteral("ftp|10.0.0.1|21|operator")), 1);
            QCOMPARE(globalMax, 2);
            QVERIFY(globalActive <= 2);
            QCOMPARE(startOrder.filter(QStringLiteral("first")).size(), 1);
            QVERIFY(!startOrder.contains(QStringLiteral("second")));
        }

        release.release(2);
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            QMutexLocker locker(&statsMutex);
            return startOrder.contains(QStringLiteral("second"));
        }(), 2000);
        release.release(1);
        QTRY_COMPARE_WITH_TIMEOUT(events.count(TransferEventType::TaskFinished), 3, 2000);

        QStringList sameDeviceStarts;
        {
            QMutexLocker locker(&statsMutex);
            for (const QString& label : startOrder) {
                if (label == QStringLiteral("first") || label == QStringLiteral("second"))
                    sameDeviceStarts.push_back(label);
            }
        }
        QCOMPARE(sameDeviceStarts,
                 QStringList({QStringLiteral("first"), QStringLiteral("second")}));
        QCOMPARE(events.count(TransferEventType::TaskFinished, first), 1);
        QCOMPARE(events.count(TransferEventType::TaskFinished, second), 1);
        QCOMPARE(events.count(TransferEventType::TaskFinished, other), 1);
    }

    void queuedAndRunningCancellationAndUnknownIdAreSafe()
    {
        QMutex mutex;
        QStringList starts;
        QSemaphore release;
        auto executor = [&](const TransferTask& task,
                            int,
                            const TransferItemRequest&,
                            std::atomic_bool& cancel,
                            const TransferScheduler::ProgressSink&) {
            {
                QMutexLocker locker(&mutex);
                starts.push_back(task.displayName);
            }
            while (!release.tryAcquire(1, 10)) {
                if (cancel.load())
                    return cancelledResult();
            }
            return cancel.load() ? cancelledResult() : succeededResult();
        };

        EventLog events;
        TransferScheduler scheduler(1, executor);
        scheduler.setEventSink([&events](const TransferEvent& event) { events.append(event); });

        const QUuid running = scheduler.submit(
            uploadTask(QStringLiteral("running"), QStringLiteral("10.0.0.3"),
                       QStringLiteral("/running.bin")));
        const QUuid queued = scheduler.submit(
            uploadTask(QStringLiteral("queued"), QStringLiteral("10.0.0.3"),
                       QStringLiteral("/queued.bin")));
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            QMutexLocker locker(&mutex);
            return starts.contains(QStringLiteral("running"));
        }(), 2000);

        scheduler.cancel(queued);
        QTRY_COMPARE_WITH_TIMEOUT(events.count(TransferEventType::TaskFinished, queued), 1, 1000);
        QCOMPARE(events.last(TransferEventType::TaskFinished, queued).snapshot.state,
                 TransferState::Cancelled);
        {
            QMutexLocker locker(&mutex);
            QVERIFY(!starts.contains(QStringLiteral("queued")));
        }

        // 被取消的排队任务必须同时释放设备队列节点与远端目标保留；同一路径
        // 可立即重新提交，并在当前运行任务收口后成为该设备的新队首。
        const QUuid replacement = scheduler.submit(
            uploadTask(QStringLiteral("replacement"), QStringLiteral("10.0.0.3"),
                       QStringLiteral("/queued.bin")));

        const int beforeUnknown = events.events().size();
        scheduler.cancel(QUuid::createUuid());
        QTest::qWait(20);
        QCOMPARE(events.events().size(), beforeUnknown);

        scheduler.cancel(running);
        QTRY_COMPARE_WITH_TIMEOUT(events.count(TransferEventType::TaskFinished, running), 1, 2000);
        QCOMPARE(events.last(TransferEventType::TaskFinished, running).snapshot.state,
                 TransferState::Cancelled);
        const auto runningEvents = events.events();
        int terminalIndex = -1;
        int cancellingIndex = -1;
        for (int i = 0; i < runningEvents.size(); ++i) {
            const auto& event = runningEvents.at(i);
            if (event.snapshot.id != running)
                continue;
            if (event.type == TransferEventType::TaskStateChanged
                && event.snapshot.state == TransferState::Cancelling)
                cancellingIndex = i;
            if (event.type == TransferEventType::TaskFinished)
                terminalIndex = i;
        }
        QVERIFY(cancellingIndex >= 0);
        QVERIFY(terminalIndex > cancellingIndex);
        for (int i = terminalIndex + 1; i < runningEvents.size(); ++i)
            QVERIFY(runningEvents.at(i).snapshot.id != running);
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            QMutexLocker locker(&mutex);
            return starts.contains(QStringLiteral("replacement"));
        }(), 2000);
        release.release(1);
        QTRY_COMPARE_WITH_TIMEOUT(events.count(TransferEventType::TaskFinished, replacement),
                                  1, 2000);
        QCOMPARE(events.last(TransferEventType::TaskFinished, replacement).snapshot.state,
                 TransferState::Succeeded);
    }

    void progressPublishesLatestSnapshotAtMostEveryHundredMilliseconds()
    {
        std::atomic<qint64> now{0};
        TransferScheduler::ProgressSink staleProgress;
        auto executor = [&](const TransferTask&,
                            int,
                            const TransferItemRequest&,
                            std::atomic_bool&,
                            const TransferScheduler::ProgressSink& progress) {
            staleProgress = progress;
            now.store(0);
            progress(10);
            now.store(99);
            progress(20);
            now.store(100);
            progress(30);
            now.store(199);
            progress(40);
            now.store(200);
            progress(50);
            return succeededResult();
        };

        EventLog events;
        TransferScheduler scheduler(1, executor, [&now] { return now.load(); });
        scheduler.setEventSink([&events](const TransferEvent& event) { events.append(event); });
        const QUuid id = scheduler.submit(
            uploadTask(QStringLiteral("progress"), QStringLiteral("10.0.0.4"),
                       QStringLiteral("/progress.bin"), 42));

        QTRY_COMPARE_WITH_TIMEOUT(events.count(TransferEventType::TaskFinished, id), 1, 2000);
        QVector<int> values;
        for (const auto& event : events.events()) {
            if (event.type == TransferEventType::ItemProgress && event.snapshot.id == id)
                values.push_back(event.snapshot.progress);
        }
        QCOMPARE(values, QVector<int>({10, 30, 50}));
        const TransferEvent terminal = events.last(TransferEventType::TaskFinished, id);
        QCOMPARE(terminal.snapshot.generation, quint64(42));
        QCOMPARE(terminal.snapshot.progress, 50);
        QCOMPARE(terminal.snapshot.state, TransferState::Succeeded);

        const int beforeStale = events.events().size();
        staleProgress(5);
        scheduler.cancel(id);
        QTest::qWait(20);
        QCOMPARE(events.events().size(), beforeStale);
        QCOMPARE(events.count(TransferEventType::TaskFinished, id), 1);
    }

    void cancellationBeforeFirstWorkerStateIsMonotonic()
    {
        std::atomic_int executorCalls{0};
        auto executor = [&](const TransferTask&,
                            int,
                            const TransferItemRequest&,
                            std::atomic_bool&,
                            const TransferScheduler::ProgressSink&) {
            ++executorCalls;
            return succeededResult();
        };

        EventLog events;
        TransferScheduler scheduler(1, executor);
        scheduler.setEventSink([&](const TransferEvent& event) {
            events.append(event);
            if (event.type == TransferEventType::TaskAdded)
                scheduler.cancel(event.snapshot.id);
        });

        const QUuid id = scheduler.submit(
            uploadTask(QStringLiteral("early-cancel"), QStringLiteral("10.0.0.41"),
                       QStringLiteral("/early-cancel.bin")));

        QCOMPARE(events.count(TransferEventType::TaskFinished, id), 1);
        QCOMPARE(events.last(TransferEventType::TaskFinished, id).snapshot.state,
                 TransferState::Cancelled);
        QCOMPARE(executorCalls.load(), 0);
        for (const auto& event : events.events()) {
            if (event.snapshot.id != id || event.type != TransferEventType::TaskStateChanged)
                continue;
            QVERIFY(event.snapshot.state != TransferState::Preparing);
            QVERIFY(event.snapshot.state != TransferState::Transferring);
        }
    }

    void cancellationWinsWhenExecutorReturnsSuccessAfterCancelling()
    {
        QSemaphore entered;
        QSemaphore allowSuccess;
        auto executor = [&](const TransferTask&,
                            int,
                            const TransferItemRequest&,
                            std::atomic_bool&,
                            const TransferScheduler::ProgressSink& progress) {
            entered.release();
            allowSuccess.acquire();
            progress(100);
            return succeededResult();
        };

        EventLog events;
        TransferScheduler scheduler(1, executor);
        scheduler.setEventSink([&events](const TransferEvent& event) { events.append(event); });
        const QUuid id = scheduler.submit(
            uploadTask(QStringLiteral("late-success"), QStringLiteral("10.0.0.42"),
                       QStringLiteral("/late-success.bin")));
        QVERIFY(entered.tryAcquire(1, 2000));

        scheduler.cancel(id);
        allowSuccess.release();
        QTRY_COMPARE_WITH_TIMEOUT(events.count(TransferEventType::TaskFinished, id), 1, 2000);

        const auto copy = events.events();
        int cancellingIndex = -1;
        int terminalIndex = -1;
        for (int i = 0; i < copy.size(); ++i) {
            const auto& event = copy.at(i);
            if (event.snapshot.id != id)
                continue;
            if (event.type == TransferEventType::TaskStateChanged
                && event.snapshot.state == TransferState::Cancelling)
                cancellingIndex = i;
            if (event.type == TransferEventType::TaskFinished)
                terminalIndex = i;
            if (cancellingIndex >= 0) {
                QVERIFY(event.type != TransferEventType::ItemProgress);
                QVERIFY(event.snapshot.state != TransferState::Succeeded);
            }
        }
        QVERIFY(cancellingIndex >= 0);
        QVERIFY(terminalIndex > cancellingIndex);
        QCOMPARE(copy.at(terminalIndex).snapshot.state, TransferState::Cancelled);
        for (int i = terminalIndex + 1; i < copy.size(); ++i)
            QVERIFY(copy.at(i).snapshot.id != id);
    }

    // 原子提交已完成后，取消只能中止后续清理；不得把已经存在的目标
    // 回报为 Cancelled，否则恢复会重复传输该文件。
    void cancellationAfterAtomicCommitPreservesCleanupAttention()
    {
        QSemaphore entered;
        QSemaphore returnCommittedResult;
        auto executor = [&](const TransferTask&,
                            int,
                            const TransferItemRequest&,
                            std::atomic_bool&,
                            const TransferScheduler::ProgressSink&) {
            entered.release();
            returnCommittedResult.acquire();
            return TransferItemResult{
                TransferState::PartiallySucceeded,
                {TransferErrorCode::Cancelled,
                 QStringLiteral("目标已提交，源清理已取消"),
                 QStringLiteral("cancelled before source cleanup"),
                 false},
                1,
                false,
                10,
                true};
        };

        EventLog events;
        TransferScheduler scheduler(1, executor);
        scheduler.setEventSink([&events](const TransferEvent& event) { events.append(event); });
        const QUuid id = scheduler.submit(
            uploadTask(QStringLiteral("committed-cancel"), QStringLiteral("10.0.0.43"),
                       QStringLiteral("/committed-cancel.bin")));
        QVERIFY(entered.tryAcquire(1, 2000));

        scheduler.cancel(id);
        returnCommittedResult.release();
        QTRY_COMPARE_WITH_TIMEOUT(events.count(TransferEventType::TaskFinished, id), 1, 2000);

        const TransferEvent terminal = events.last(TransferEventType::TaskFinished, id);
        QCOMPARE(terminal.snapshot.state, TransferState::PartiallySucceeded);
        QCOMPARE(terminal.snapshot.itemResults.size(), 1);
        QVERIFY(terminal.snapshot.itemResults.front().atomicCommitSucceeded);
    }

    void cancellationAfterEarlierAtomicCommitIsPartiallySucceeded()
    {
        QSemaphore secondItemEntered;
        QSemaphore releaseSecondItem;
        auto executor = [&](const TransferTask&,
                            int itemIndex,
                            const TransferItemRequest&,
                            std::atomic_bool&,
                            const TransferScheduler::ProgressSink&) {
            if (itemIndex == 0) {
                return TransferItemResult{
                    TransferState::Succeeded, {}, 1, false, 10, true};
            }
            secondItemEntered.release();
            releaseSecondItem.acquire();
            return cancelledResult();
        };

        EventLog events;
        TransferScheduler scheduler(1, executor);
        scheduler.setEventSink([&events](const TransferEvent& event) { events.append(event); });
        TransferTask task = uploadTask(QStringLiteral("multi-commit-cancel"),
                                       QStringLiteral("10.0.0.44"),
                                       QStringLiteral("/multi-commit-cancel.bin"));
        task.items.push_back({QStringLiteral("C:/input/second.bin"),
                              QStringLiteral("/second.bin"),
                              TransferDirection::Upload,
                              OverwritePolicy::Overwrite});
        const QUuid id = scheduler.submit(std::move(task));
        QVERIFY(secondItemEntered.tryAcquire(1, 2000));

        scheduler.cancel(id);
        releaseSecondItem.release();
        QTRY_COMPARE_WITH_TIMEOUT(events.count(TransferEventType::TaskFinished, id), 1, 2000);

        const TransferEvent terminal = events.last(TransferEventType::TaskFinished, id);
        QCOMPARE(terminal.snapshot.state, TransferState::PartiallySucceeded);
        QCOMPARE(terminal.snapshot.itemResults.size(), 2);
        QVERIFY(terminal.snapshot.itemResults.at(0).atomicCommitSucceeded);
        QCOMPARE(terminal.snapshot.itemResults.at(1).state, TransferState::Cancelled);
    }

    void normalizedRemoteConflictTerminatesOnceAndReservationIsReleased()
    {
        QMutex mutex;
        QStringList starts;
        QSemaphore release;
        auto executor = [&](const TransferTask& task,
                            int,
                            const TransferItemRequest&,
                            std::atomic_bool& cancel,
                            const TransferScheduler::ProgressSink&) {
            {
                QMutexLocker locker(&mutex);
                starts.push_back(task.displayName);
            }
            while (!release.tryAcquire(1, 10)) {
                if (cancel.load())
                    return cancelledResult();
            }
            return succeededResult();
        };

        EventLog events;
        TransferScheduler scheduler(2, executor);
        scheduler.setEventSink([&events](const TransferEvent& event) { events.append(event); });
        const QUuid first = scheduler.submit(
            uploadTask(QStringLiteral("owner"), QStringLiteral("10.0.0.5"),
                       QStringLiteral("/opt/firmware.bin")));
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            QMutexLocker locker(&mutex);
            return starts.contains(QStringLiteral("owner"));
        }(), 2000);

        const QUuid conflict = scheduler.submit(
            uploadTask(QStringLiteral("conflict"), QStringLiteral("10.0.0.5"),
                       QStringLiteral("/opt/tmp/../firmware.bin")));
        QTRY_COMPARE_WITH_TIMEOUT(events.count(TransferEventType::TaskFinished, conflict), 1, 1000);
        const auto conflictEvent = events.last(TransferEventType::TaskFinished, conflict);
        QCOMPARE(conflictEvent.snapshot.state, TransferState::NeedsAttention);
        QCOMPARE(conflictEvent.snapshot.error.code, TransferErrorCode::TargetChanged);

        release.release(1);
        QTRY_COMPARE_WITH_TIMEOUT(events.count(TransferEventType::TaskFinished, first), 1, 2000);
        const QUuid reused = scheduler.submit(
            uploadTask(QStringLiteral("reused"), QStringLiteral("10.0.0.5"),
                       QStringLiteral("/opt/firmware.bin")));
        release.release(1);
        QTRY_COMPARE_WITH_TIMEOUT(events.count(TransferEventType::TaskFinished, reused), 1, 2000);
        QCOMPARE(events.last(TransferEventType::TaskFinished, reused).snapshot.state,
                 TransferState::Succeeded);
        {
            QMutexLocker locker(&mutex);
            QVERIFY(!starts.contains(QStringLiteral("conflict")));
            QVERIFY(starts.contains(QStringLiteral("reused")));
        }
    }

    void downloadsToSameNormalizedLocalTargetAreSerializedAcrossDevices()
    {
        QMutex mutex;
        QStringList starts;
        int active = 0;
        int maximum = 0;
        QSemaphore release;
        auto executor = [&](const TransferTask& task,
                            int,
                            const TransferItemRequest&,
                            std::atomic_bool& cancel,
                            const TransferScheduler::ProgressSink&) {
            {
                QMutexLocker locker(&mutex);
                starts.push_back(task.displayName);
                maximum = qMax(maximum, ++active);
            }
            while (!release.tryAcquire(1, 10)) {
                if (cancel.load())
                    return cancelledResult();
            }
            {
                QMutexLocker locker(&mutex);
                --active;
            }
            return succeededResult();
        };

        EventLog events;
        TransferScheduler scheduler(2, executor);
        scheduler.setEventSink([&events](const TransferEvent& event) { events.append(event); });
        const QString target = QStringLiteral("C:/downloads/firmware.bin");
        const QUuid first = scheduler.submit(
            downloadTask(QStringLiteral("download-a"), QStringLiteral("10.0.0.6"),
                         QStringLiteral("/a.bin"), target));
        const QUuid second = scheduler.submit(
            downloadTask(QStringLiteral("download-b"), QStringLiteral("10.0.0.7"),
                         QStringLiteral("/b.bin"),
                         QStringLiteral("C:/downloads/tmp/../firmware.bin")));

        QTRY_VERIFY_WITH_TIMEOUT([&] {
            QMutexLocker locker(&mutex);
            return starts.size() == 1;
        }(), 2000);
        {
            QMutexLocker locker(&mutex);
            QCOMPARE(maximum, 1);
        }
        release.release(1);
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            QMutexLocker locker(&mutex);
            return starts.size() == 2;
        }(), 2000);
        release.release(1);
        QTRY_COMPARE_WITH_TIMEOUT(events.count(TransferEventType::TaskFinished), 2, 2000);
        QCOMPARE(events.count(TransferEventType::TaskFinished, first), 1);
        QCOMPARE(events.count(TransferEventType::TaskFinished, second), 1);
        {
            QMutexLocker locker(&mutex);
            QCOMPARE(maximum, 1);
        }
    }

    void shutdownRejectsSubmissionsAndDestructorWaitsForRunningExecutor()
    {
        std::atomic_bool started{false};
        std::atomic_bool exited{false};
        auto executor = [&](const TransferTask&,
                            int,
                            const TransferItemRequest&,
                            std::atomic_bool& cancel,
                            const TransferScheduler::ProgressSink&) {
            started.store(true);
            while (!cancel.load())
                QThread::msleep(5);
            exited.store(true);
            return cancelledResult();
        };

        QElapsedTimer elapsed;
        {
            auto scheduler = std::make_unique<TransferScheduler>(1, executor);
            scheduler->submit(uploadTask(QStringLiteral("lifecycle"),
                                         QStringLiteral("10.0.0.8"),
                                         QStringLiteral("/lifecycle.bin")));
            QTRY_VERIFY_WITH_TIMEOUT(started.load(), 2000);
            elapsed.start();
        }
        QVERIFY(exited.load());
        QVERIFY(elapsed.elapsed() < 1000);

        TransferScheduler stopped(1, executor);
        stopped.shutdown();
        QVERIFY(stopped.submit(uploadTask(QStringLiteral("rejected"),
                                          QStringLiteral("10.0.0.9"),
                                          QStringLiteral("/rejected.bin")))
                    .isNull());
    }


    void concurrencyConfigurationIsClampedToSupportedRange_data()
    {
        QTest::addColumn<int>("configured");
        QTest::addColumn<int>("submitted");
        QTest::addColumn<int>("expectedMaximum");

        QTest::newRow("zero-to-one") << 0 << 2 << 1;
        QTest::newRow("negative-to-one") << -4 << 2 << 1;
        QTest::newRow("above-eight-to-eight") << 99 << 9 << 8;
    }

    void concurrencyConfigurationIsClampedToSupportedRange()
    {
        QFETCH(int, configured);
        QFETCH(int, submitted);
        QFETCH(int, expectedMaximum);

        QMutex mutex;
        int active = 0;
        int maximum = 0;
        QSemaphore release;
        auto executor = [&](const TransferTask&,
                            int,
                            const TransferItemRequest&,
                            std::atomic_bool& cancel,
                            const TransferScheduler::ProgressSink&) {
            {
                QMutexLocker locker(&mutex);
                maximum = qMax(maximum, ++active);
            }
            while (!release.tryAcquire(1, 5)) {
                if (cancel.load())
                    return cancelledResult();
            }
            {
                QMutexLocker locker(&mutex);
                --active;
            }
            return succeededResult();
        };

        EventLog events;
        TransferScheduler scheduler(configured, executor);
        scheduler.setEventSink([&events](const TransferEvent& event) { events.append(event); });
        for (int i = 0; i < submitted; ++i) {
            scheduler.submit(uploadTask(QStringLiteral("limit-%1").arg(i),
                                        QStringLiteral("10.1.0.%1").arg(i + 1),
                                        QStringLiteral("/limit-%1.bin").arg(i)));
        }
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            QMutexLocker locker(&mutex);
            return maximum == expectedMaximum;
        }(), 3000);
        release.release(submitted);
        QTRY_COMPARE_WITH_TIMEOUT(events.count(TransferEventType::TaskFinished),
                                  submitted, 3000);
        QMutexLocker locker(&mutex);
        QCOMPARE(maximum, expectedMaximum);
    }

    void submitAndShutdownRaceNeverStartsWorkAfterShutdownReturns()
    {
        for (int round = 0; round < 100; ++round) {
            std::atomic_bool shutdownReturned{false};
            std::atomic_bool lateStart{false};
            auto executor = [&](const TransferTask&,
                                int,
                                const TransferItemRequest&,
                                std::atomic_bool& cancel,
                                const TransferScheduler::ProgressSink&) {
                if (shutdownReturned.load())
                    lateStart.store(true);
                return cancel.load() ? cancelledResult() : succeededResult();
            };

            TransferScheduler scheduler(8, executor);
            QSemaphore go;
            std::thread submitter([&] {
                go.acquire();
                for (int i = 0; i < 16; ++i) {
                    scheduler.submit(uploadTask(
                        QStringLiteral("race-%1-%2").arg(round).arg(i),
                        QStringLiteral("10.2.%1.%2").arg(round % 200).arg(i + 1),
                        QStringLiteral("/race-%1.bin").arg(i)));
                }
            });
            std::thread stopper([&] {
                go.acquire();
                scheduler.shutdown();
                shutdownReturned.store(true);
            });
            go.release(2);
            submitter.join();
            stopper.join();
            QThread::msleep(1);

            QVERIFY(!lateStart.load());
            QVERIFY(scheduler.submit(uploadTask(QStringLiteral("after-shutdown"),
                                                 QStringLiteral("10.3.0.1"),
                                                 QStringLiteral("/after.bin")))
                        .isNull());
        }
    }

    void workerEventSinkCanRequestShutdownWithoutWaitingForItself()
    {
        std::atomic_bool shutdownReturned{false};
        EventLog events;
        auto executor = [](const TransferTask&,
                           int,
                           const TransferItemRequest&,
                           std::atomic_bool& cancel,
                           const TransferScheduler::ProgressSink&) {
            return cancel.load() ? cancelledResult() : succeededResult();
        };

        TransferScheduler scheduler(1, executor);
        scheduler.setEventSink([&](const TransferEvent& event) {
            events.append(event);
            if (event.type == TransferEventType::TaskStateChanged
                && event.snapshot.state == TransferState::Preparing) {
                scheduler.shutdown();
                shutdownReturned.store(true);
            }
        });
        const QUuid id = scheduler.submit(
            uploadTask(QStringLiteral("worker-shutdown"), QStringLiteral("10.0.0.43"),
                       QStringLiteral("/worker-shutdown.bin")));

        QTRY_VERIFY_WITH_TIMEOUT(shutdownReturned.load(), 2000);
        QTRY_COMPARE_WITH_TIMEOUT(events.count(TransferEventType::TaskFinished, id), 1, 2000);
        QCOMPARE(events.last(TransferEventType::TaskFinished, id).snapshot.state,
                 TransferState::Cancelled);
        QVERIFY(scheduler.submit(uploadTask(QStringLiteral("rejected-after-worker-stop"),
                                             QStringLiteral("10.0.0.44"),
                                             QStringLiteral("/rejected.bin")))
                    .isNull());
    }
};

QTEST_APPLESS_MAIN(TstTransferScheduler)
#include "tst_transfer_scheduler.moc"
