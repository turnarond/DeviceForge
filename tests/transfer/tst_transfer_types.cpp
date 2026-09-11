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
        QVERIFY(!canTransition(TransferState::Succeeded, TransferState::Transferring));
        QCOMPARE(retryDelaysMs(), QVector<int>({1000, 3000}));
        QVERIFY(classifyTransferError(QStringLiteral("operation timed out"), 28).retryable);
        QVERIFY(!classifyTransferError(QStringLiteral("authentication failed"), 67).retryable);
    }
};

QTEST_MAIN(TstTransferTypes)
#include "tst_transfer_types.moc"
