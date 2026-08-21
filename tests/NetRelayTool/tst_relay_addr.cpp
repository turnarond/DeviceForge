#include <QtTest/QtTest>
#include "NetRelayTypes.h"

// 组播地址 / 转发目标地址校验纯逻辑测试（TDD：NetRelay 组播转发功能）
class TstRelayAddr : public QObject {
    Q_OBJECT
private slots:
    void multicastValidAddresses();
    void multicastRejectsNonD();
    void forwardTargetAcceptsUnicastAndMulticast();
    void forwardTargetRejectsInvalid();
};

void TstRelayAddr::multicastValidAddresses()
{
    // IPv4 D 类 224.0.0.0 ~ 239.255.255.255
    QVERIFY(isValidMulticastAddress("224.0.0.0"));
    QVERIFY(isValidMulticastAddress("224.0.0.1"));
    QVERIFY(isValidMulticastAddress("239.1.2.3"));
    QVERIFY(isValidMulticastAddress("239.255.255.255"));
}

void TstRelayAddr::multicastRejectsNonD()
{
    QVERIFY(!isValidMulticastAddress("255.255.255.255"));  // 有限广播（非组播）
    QVERIFY(!isValidMulticastAddress("192.168.1.255"));    // 子网定向广播（非组播）
    QVERIFY(!isValidMulticastAddress("192.168.1.100"));    // 普通单播
    QVERIFY(!isValidMulticastAddress("240.0.0.1"));        // 保留段（非 D 类）
    QVERIFY(!isValidMulticastAddress("256.1.1.1"));        // 越界段
    QVERIFY(!isValidMulticastAddress(""));                 // 空
    QVERIFY(!isValidMulticastAddress("not-an-ip"));        // 非 IP
    QVERIFY(!isValidMulticastAddress("239.1.2"));          // 段数不足
}

void TstRelayAddr::forwardTargetAcceptsUnicastAndMulticast()
{
    // M→U：单播目标
    QVERIFY(isValidForwardTarget("192.168.1.100"));
    QVERIFY(isValidForwardTarget("127.0.0.1"));   // 回环单播（本机调试转发）
    QVERIFY(isValidForwardTarget("10.0.0.2"));
    // M→M：组播目标
    QVERIFY(isValidForwardTarget("224.0.0.1"));
    QVERIFY(isValidForwardTarget("239.1.2.3"));
}

void TstRelayAddr::forwardTargetRejectsInvalid()
{
    QVERIFY(!isValidForwardTarget("255.255.255.255"));  // 有限广播（非转发目标）
    QVERIFY(!isValidForwardTarget("256.1.1.1"));
    QVERIFY(!isValidForwardTarget(""));
    QVERIFY(!isValidForwardTarget("foo"));
    QVERIFY(!isValidForwardTarget("239.1.2"));
}

QTEST_MAIN(TstRelayAddr)
#include "tst_relay_addr.moc"
