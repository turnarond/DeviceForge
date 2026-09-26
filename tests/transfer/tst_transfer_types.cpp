// tst_transfer_types.cpp — 可靠传输基础类型的纯逻辑契约测试

#include <QtTest>

#include "transfer/TransferTypes.h"

class TstTransferTypes : public QObject
{
    Q_OBJECT

private slots:
    void stateAndRetryPolicy()
    {
        QVERIFY(canTransition(TransferState::Queued, TransferState::Preparing));
        QVERIFY(canTransition(TransferState::Preparing, TransferState::Transferring));
        QVERIFY(canTransition(TransferState::Transferring, TransferState::Verifying));
        QVERIFY(canTransition(TransferState::Verifying, TransferState::Committing));
        QVERIFY(canTransition(TransferState::Committing, TransferState::Succeeded));
        QVERIFY(canTransition(TransferState::Transferring, TransferState::Reconnecting));
        QVERIFY(canTransition(TransferState::Reconnecting, TransferState::RetryWaiting));
        QVERIFY(canTransition(TransferState::RetryWaiting, TransferState::Preparing));
        QCOMPARE(retryDelaysMs(), QVector<int>({1000, 3000}));
    }

    void terminalStatesCannotTransition_data()
    {
        QTest::addColumn<int>("terminalState");

        QTest::newRow("succeeded") << static_cast<int>(TransferState::Succeeded);
        QTest::newRow("partiallySucceeded") << static_cast<int>(TransferState::PartiallySucceeded);
        QTest::newRow("failed") << static_cast<int>(TransferState::Failed);
        QTest::newRow("cancelled") << static_cast<int>(TransferState::Cancelled);
    }

    void terminalStatesCannotTransition()
    {
        QFETCH(int, terminalState);
        const auto from = static_cast<TransferState>(terminalState);
        QVERIFY(!canTransition(from, TransferState::Queued));
        QVERIFY(!canTransition(from, TransferState::Transferring));
        QVERIFY(!canTransition(from, TransferState::Cancelled));
    }

    void errorClassification()
    {
        const auto timeout = classifyTransferError(QStringLiteral("operation timed out"), 28);
        QCOMPARE(timeout.code, TransferErrorCode::Timeout);
        QVERIFY(timeout.retryable);

        const auto authentication =
            classifyTransferError(QStringLiteral("authentication failed"), 67);
        QCOMPARE(authentication.code, TransferErrorCode::Authentication);
        QVERIFY(!authentication.retryable);

        // native code 67 的认证语义必须压过消息中的“timed out”。
        const auto conflicting =
            classifyTransferError(QStringLiteral("authentication request timed out"), 67);
        QCOMPARE(conflicting.code, TransferErrorCode::Authentication);
        QVERIFY(!conflicting.retryable);

        const auto transient = classifyTransferError(QStringLiteral("connection reset by peer"), 0);
        QCOMPARE(transient.code, TransferErrorCode::ConnectionLost);
        QVERIFY(transient.retryable);

        const auto quoteWrite = classifyTransferError(
            QStringLiteral("重命名失败: Failed writing received data to disk/application"), 0);
        QCOMPARE(quoteWrite.code, TransferErrorCode::RemoteIo);
        QVERIFY(quoteWrite.retryable);

        const auto unsupported =
            classifyTransferError(QStringLiteral("unsupported connection protocol"), 0);
        QCOMPARE(unsupported.code, TransferErrorCode::Unsupported);
        QVERIFY(!unsupported.retryable);
    }
};

QTEST_MAIN(TstTransferTypes)
#include "tst_transfer_types.moc"
