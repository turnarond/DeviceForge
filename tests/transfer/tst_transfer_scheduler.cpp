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
};

QTEST_APPLESS_MAIN(TstTransferScheduler)
#include "tst_transfer_scheduler.moc"
