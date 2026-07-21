#include <QtTest>
#include "config/DpapiCrypto.h"

class TestDpapiCrypto : public QObject {
    Q_OBJECT
private slots:
    void roundTrip();
    void protectIsNotPlaintext();
};

void TestDpapiCrypto::roundTrip() {
    const QString plain = QString::fromUtf8("secret-123!@#中文密码");
    const QString cipher = DpapiCrypto::protect(plain);
    QCOMPARE(DpapiCrypto::unprotect(cipher), plain);
}

void TestDpapiCrypto::protectIsNotPlaintext() {
    const QString plain = QStringLiteral("secret-123");
    const QString cipher = DpapiCrypto::protect(plain);
    QVERIFY2(cipher != plain, "密文不能等于明文");
}

QTEST_MAIN(TestDpapiCrypto)
#include "tst_dpapi_crypto.moc"
