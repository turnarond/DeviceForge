#include <QtTest/QtTest>
#include <QDir>
#include <QFileInfo>
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

    // 写一个完整 32 字节头，magic 非 "NREC"（"XXXX" + 28 个 0）。
    // 区别于 loadRejectsBadMagic：文件足够大能越过 size<kHeaderSize 守卫，
    // 从而真正命中 memcmp magic 校验分支。
    void loadRejectsBadMagicFullHeader() {
        QString path = QDir::temp().filePath("tst_badmagic_full.nrec");
        QFile bad(path); QVERIFY(bad.open(QIODevice::WriteOnly));
        QByteArray hdr(32, '\0');
        hdr[0] = 'X'; hdr[1] = 'X'; hdr[2] = 'X'; hdr[3] = 'X';
        QCOMPARE(bad.write(hdr), qint64(32));
        bad.close();
        NrecFile f; QString err;
        QVERIFY(!RelayRecording::load(path, f, err));   // 全头坏 magic 必须被拒
        QVERIFY(!err.isEmpty());
        QFile::remove(path);
    }

    // 有效 magic "NREC" + version=999，其余凑满 32 字节头，必须因版本不符被拒。
    void loadRejectsBadVersion() {
        QString path = QDir::temp().filePath("tst_badver.nrec");
        QFile bad(path); QVERIFY(bad.open(QIODevice::WriteOnly));
        QDataStream out(&bad);
        out.setByteOrder(QDataStream::LittleEndian);
        out.writeRawData(nrec::kMagic, 4);
        out << quint16(999);              // 非法版本
        out << quint8(0) << quint8(0);    // proto + rsv
        out << qint64(0);                 // epoch
        for (int i = 0; i < 16; ++i) out << quint8(0);  // reserved[16]
        bad.close();
        QCOMPARE(QFileInfo(path).size(), qint64(32));    // 头长度自校验
        NrecFile f; QString err;
        QVERIFY(!RelayRecording::load(path, f, err));    // 版本不符必须被拒
        QVERIFY(!err.isEmpty());
        QFile::remove(path);
    }

    // 用 RelayRecorder 写一条小记录的有效文件，再把记录头 length 字段
    // 篡改为 0x7FFFFFFF（offset 46，小端），必须命中 len>remaining 越界拒绝。
    void loadRejectsOversizeLength() {
        QString path = QDir::temp().filePath("tst_oversize.nrec");
        { RelayRecorder rec;
          QVERIFY(rec.open(path, RelayProtocol::Tcp, 0));
          rec.append(RelayDirection::Upstream, 1, 0, QByteArray("hi"));
          rec.close(); }

        // 先确认原文件可正常加载，排除构造错误
        { NrecFile ok; QString e; QVERIFY2(RelayRecording::load(path, ok, e), qPrintable(e)); }

        // length 字段偏移 = 32(头) + 1(dir) + 1(rsv) + 4(sid) + 8(ts) = 46
        QFile f(path); QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.seek(46));
        QDataStream out(&f);
        out.setByteOrder(QDataStream::LittleEndian);
        out << quint32(0x7FFFFFFF);       // 篡改为超大长度
        f.close();

        NrecFile bad; QString err;
        QVERIFY(!RelayRecording::load(path, bad, err));  // 超大 length 必须被拒
        QVERIFY(!err.isEmpty());
        QFile::remove(path);
    }

    // 有效 32 字节头 + 仅追加 5 字节（不足 18 字节记录头），
    // 必须因 QDataStream 读到不完整记录头（status != Ok）被拒。
    void loadRejectsTruncatedRecordHeader() {
        QString path = QDir::temp().filePath("tst_trunc.nrec");
        QFile bad(path); QVERIFY(bad.open(QIODevice::WriteOnly));
        QDataStream out(&bad);
        out.setByteOrder(QDataStream::LittleEndian);
        out.writeRawData(nrec::kMagic, 4);
        out << quint16(nrec::kVersion);   // 合法版本
        out << quint8(0) << quint8(0);    // proto + rsv
        out << qint64(0);                 // epoch
        for (int i = 0; i < 16; ++i) out << quint8(0);  // reserved[16]
        // 追加不完整的记录头（仅 5 字节，< 18）
        const char frag[5] = { 0, 0, 1, 0, 0 };
        out.writeRawData(frag, 5);
        bad.close();
        QCOMPARE(QFileInfo(path).size(), qint64(37));    // 32 头 + 5 碎片

        NrecFile f; QString err;
        QVERIFY(!RelayRecording::load(path, f, err));    // 截断记录头必须被拒
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

    void multicastRoundTrip() {
        QString path = QDir::temp().filePath("tst_mcast.nrec");
        RelayRecorder rec;
        QVERIFY(rec.open(path, RelayProtocol::Multicast, 5000, "239.1.2.3", 6000));
        rec.append(RelayDirection::Upstream, 1, 0,  QByteArray("radar-a"));
        rec.append(RelayDirection::Upstream, 1, 33, QByteArray("radar-b"));
        rec.close();

        NrecFile f; QString err;
        QVERIFY2(RelayRecording::load(path, f, err), qPrintable(err));
        QCOMPARE(f.protocol, RelayProtocol::Multicast);
        QCOMPARE(f.groupAddr, QString("239.1.2.3"));
        QCOMPARE(f.groupPort, quint16(6000));
        QCOMPARE(f.records.size(), 2);
        QCOMPARE(f.records[0].payload, QByteArray("radar-a"));
        QCOMPARE(f.records[1].tsOffsetMs, qint64(33));
        QFile::remove(path);
    }

    void udpFileStillLoadsAfterMulticastExtension() {
        // 旧式 UDP 录制（不传组参数）读取应正常，组字段为空
        QString path = QDir::temp().filePath("tst_udp_compat.nrec");
        RelayRecorder rec;
        QVERIFY(rec.open(path, RelayProtocol::Udp, 1000));
        rec.append(RelayDirection::Upstream, 1, 0, QByteArray("x"));
        rec.close();
        NrecFile f; QString err;
        QVERIFY2(RelayRecording::load(path, f, err), qPrintable(err));
        QCOMPARE(f.protocol, RelayProtocol::Udp);
        QVERIFY(f.groupAddr.isEmpty());
        QCOMPARE(f.groupPort, quint16(0));
        QCOMPARE(f.records.size(), 1);
        QFile::remove(path);
    }
};

QTEST_MAIN(TstNrec)
#include "tst_nrec.moc"
