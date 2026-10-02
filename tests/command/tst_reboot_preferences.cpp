#include "command/RebootPreferences.h"
#include <QtTest/QtTest>
class TestRebootPreferences final : public QObject {
    Q_OBJECT
private slots:
    void roundTrip();
    void sanitizeDefaults();
};
void TestRebootPreferences::roundTrip()
{
    RebootOptions expected;
    expected.protocol = "ssh";
    expected.command = "systemctl reboot";
    expected.timeoutSec = 30;
    expected.retryCount = 2;
    const RebootOptions actual = rebootOptionsFromMap(rebootOptionsToMap(expected));
    QCOMPARE(QString::fromStdString(actual.protocol), QStringLiteral("ssh"));
    QCOMPARE(QString::fromStdString(actual.command), QStringLiteral("systemctl reboot"));
    QCOMPARE(actual.timeoutSec, 30);
    QCOMPARE(actual.retryCount, 2);
}
void TestRebootPreferences::sanitizeDefaults()
{
    const RebootOptions options = rebootOptionsFromMap({
        {QStringLiteral("protocol"), QStringLiteral("invalid")},
        {QStringLiteral("command"), QStringLiteral(" ")},
        {QStringLiteral("timeoutSec"), 999},
        {QStringLiteral("retryCount"), -1},
    });
    QCOMPARE(QString::fromStdString(options.protocol), QStringLiteral("telnet"));
    QCOMPARE(QString::fromStdString(options.command), QStringLiteral("reboot"));
    QCOMPARE(options.timeoutSec, 120);
    QCOMPARE(options.retryCount, 0);
}
QTEST_MAIN(TestRebootPreferences)
#include "tst_reboot_preferences.moc"