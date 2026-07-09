#include <QtTest/QtTest>
#include <QDir>
#include <QTemporaryFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSignalSpy>
#include <QElapsedTimer>

#include "RelayRecorder.h"
#include "RelayRecording.h"
#include "RelayPlayer.h"

class TstNrec : public QObject {
    Q_OBJECT
private slots:
    void sanity() { QVERIFY(true); }

    void recorderRoundTrip() {
        QString path = QDir::temp().filePath("tst_roundtrip.nrec");
        RelayRecorder rec;
        QVERIFY(rec.open(path, RelayProtocol::Tcp, 1000));
        rec.append(RelayDirection::Upstream,   1, 0,   QByteArray("hello"));
        rec.append(RelayDirection::Downstream, 1, 15,  QByteArray("\x00\x01\x02", 3));
        rec.append(RelayDirection::Upstream,   1, 40,  QByteArray("world"));
        rec.close();

        NrecFile f; QString err;
        QVERIFY2(RelayRecording::load(path, f, err), qPrintable(err));
        QCOMPARE(f.protocol, RelayProtocol::Tcp);
        QCOMPARE(f.startEpochMs, qint64(1000));
        QCOMPARE(f.records.size(), 3);
        QCOMPARE(f.records[0].dir, RelayDirection::Upstream);
        QCOMPARE(f.records[0].tsOffsetMs, qint64(0));
        QCOMPARE(f.records[0].payload, QByteArray("hello"));
        QCOMPARE(f.records[1].payload, QByteArray("\x00\x01\x02", 3));
        QCOMPARE(f.records[2].tsOffsetMs, qint64(40));
        QFile::remove(path);
    }

    void loadRejectsBadMagic() {
        QString path = QDir::temp().filePath("tst_bad.nrec");
        QFile bad(path); QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("XXXX garbage"); bad.close();
        NrecFile f; QString err;
        QVERIFY(!RelayRecording::load(path, f, err));   // 坏 magic 必须被拒
        QVERIFY(!err.isEmpty());
        QFile::remove(path);
    }

    void playerReplaysUpstreamOnly() {
        // 造一个含上/下行混合的 .nrec：上行 "A","B"，下行 "x"
        QString path = QDir::temp().filePath("tst_replay.nrec");
        { RelayRecorder rec;
          QVERIFY(rec.open(path, RelayProtocol::Tcp, 0));
          rec.append(RelayDirection::Upstream,   1, 0,  QByteArray("A"));
          rec.append(RelayDirection::Downstream, 1, 5,  QByteArray("x")); // 应被回放忽略
          rec.append(RelayDirection::Upstream,   1, 20, QByteArray("B"));
          rec.close(); }

        // 本地消费者：QTcpServer 收字节
        QByteArray received;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        quint16 port = server.serverPort();
        QObject::connect(&server, &QTcpServer::newConnection, [&]() {
            QTcpSocket* c = server.nextPendingConnection();
            QObject::connect(c, &QTcpSocket::readyRead, [&, c]() { received += c->readAll(); });
        });

        RelayPlayer player;
        bool finished = false;
        player.setFinishedCallback([&]() { finished = true; });
        QVERIFY(player.start(path, "127.0.0.1", port, 1.0));

        // 事件循环推进直到 finished 且收到期望字节，或超时。
        // 说明：即使生产端已修复 flush，环回投递到消费者 readyRead 仍是异步的，
        // finished 触发时 received 未必已等于 "AB"，需一并等待字节到齐。
        QElapsedTimer t; t.start();
        while ((!finished || received != QByteArray("AB")) && t.elapsed() < 3000) {
            QCoreApplication::processEvents(); QTest::qWait(5);
        }
        QVERIFY(finished);
        QCOMPARE(received, QByteArray("AB"));   // 只上行、按序，忽略下行 "x"
        QFile::remove(path);
    }
};

QTEST_MAIN(TstNrec)
#include "tst_nrec.moc"
