#include <QtTest/QtTest>

class TstNrec : public QObject {
    Q_OBJECT
private slots:
    void sanity() { QVERIFY(true); }
};

QTEST_MAIN(TstNrec)
#include "tst_nrec.moc"
