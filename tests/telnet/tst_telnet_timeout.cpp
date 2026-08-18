#include <QtTest/QtTest>
#include <QTcpServer>
#include "adapter/TelnetAdapter.h"

// Telnet 请求-响应超时语义测试（本地 QTcpServer 接受连接但从不回包）：
// 覆盖 #7 — 超时零字节必须报告失败（"响应超时"），而非假成功；
// 同时覆盖免认证路径（无 "login:" 提示 → 跳过认证直接可用，SylixOS telnetd 兼容）
class TstTelnetTimeout : public QObject {
    Q_OBJECT
private slots:
    void requestEmptyTimeoutIsFailure() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));

        TelnetAdapter adapter;
        adapter.setLoginPromptTimeoutMs(500);  // 测试加速：无提示服务器快速跳过登录等待

        DeviceInfo dev;
        dev.ip = "127.0.0.1";
        dev.port = server.serverPort();
        AuthInfo auth;
        auth.user = "tester";
        auth.password = "secret";

        // 免认证服务器（无 login: 提示）→ 连接成功
        QVERIFY2(adapter.connect(dev, auth), adapter.lastError().c_str());

        // 服务端从不回包 → 命令必须失败并带"响应超时"
        Request req;
        req.path = "uname -a";
        req.timeoutMs = 500;
        auto resp = adapter.request(req).get();
        QVERIFY(!resp.success);
        QCOMPARE(QString::fromStdString(resp.errorMessage), QStringLiteral("响应超时"));

        adapter.disconnect();
    }
};
QTEST_MAIN(TstTelnetTimeout)
#include "tst_telnet_timeout.moc"
