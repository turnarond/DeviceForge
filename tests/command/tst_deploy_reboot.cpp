#include <QtTest>

#include "command/RebootSelection.h"

namespace {
DeviceInfo device(const char* ip, int port)
{
    DeviceInfo result;
    result.ip = ip;
    result.port = port;
    return result;
}
}

class TestDeployReboot : public QObject
{
    Q_OBJECT
private slots:
    void rebootOnlySuccessfulDevices();
};

void TestDeployReboot::rebootOnlySuccessfulDevices()
{
    const std::vector<DeviceInfo> devices = {
        device("192.168.1.10", 21),
        device("192.168.1.11", 21),
        device("192.168.1.12", 21)
    };
    const auto selected = selectRebootDevices(
        devices, {"192.168.1.10:21", "192.168.1.12:21"});
    QCOMPARE(selected.size(), size_t{2});
    QCOMPARE(QString::fromStdString(selected[0].ip), QStringLiteral("192.168.1.10"));
    QCOMPARE(QString::fromStdString(selected[1].ip), QStringLiteral("192.168.1.12"));
}

QTEST_MAIN(TestDeployReboot)
#include "tst_deploy_reboot.moc"
