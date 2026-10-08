#include <QtTest>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QUuid>
#include "config/ConfigStore.h"

class TestConfigStore : public QObject {
    Q_OBJECT
private:
    QString m_dbPath;
    QString m_workDir;
private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();
    void roundTrip();
    void uniqueKeyUpsert();
    void listOrder();
    void listPaging();
    void removeWorks();
    void exportImportRoundtrip();
};

void TestConfigStore::initTestCase()
{
    // 进程专属临时目录，避免 CI 残留/并发污染
    m_workDir = QDir::temp().filePath(
        QStringLiteral("tst_config_store_%1")
            .arg(QUuid::createUuid().toString(QUuid::Id128)));
    QVERIFY(QDir().mkpath(m_workDir));
    m_dbPath = QDir(m_workDir).filePath(QStringLiteral("config.db"));
}

void TestConfigStore::cleanupTestCase()
{
    ConfigStore::instance().close();
    QDir(m_workDir).removeRecursively();
}

void TestConfigStore::init()
{
    ConfigStore::instance().close();
    QFile::remove(m_dbPath);
    QFile::remove(m_dbPath + QStringLiteral("-wal"));
    QFile::remove(m_dbPath + QStringLiteral("-shm"));
    QVERIFY2(ConfigStore::instance().open(m_dbPath),
             qPrintable(QStringLiteral("open failed path=%1").arg(m_dbPath)));
}

void TestConfigStore::cleanup()
{
    ConfigStore::instance().close();
    QFile::remove(m_dbPath);
    QFile::remove(m_dbPath + QStringLiteral("-wal"));
    QFile::remove(m_dbPath + QStringLiteral("-shm"));
}

void TestConfigStore::roundTrip()
{
    QVariantMap v{{QStringLiteral("ip"), QStringLiteral("10.0.0.1")},
                  {QStringLiteral("port"), 22}};
    QVERIFY(ConfigStore::instance().save(QStringLiteral("device.list"),
                                         QStringLiteral("10.0.0.1:22"), v));
    auto got = ConfigStore::instance().load(QStringLiteral("device.list"),
                                            QStringLiteral("10.0.0.1:22"));
    QCOMPARE(got.value(QStringLiteral("ip")).toString(), QStringLiteral("10.0.0.1"));
    QCOMPARE(got.value(QStringLiteral("port")).toInt(), 22);
}

void TestConfigStore::uniqueKeyUpsert()
{
    QVariantMap v1{{QStringLiteral("x"), 1}};
    QVariantMap v2{{QStringLiteral("x"), 2}};
    QVERIFY(ConfigStore::instance().save(QStringLiteral("t"), QStringLiteral("k"), v1));
    QVERIFY(ConfigStore::instance().save(QStringLiteral("t"), QStringLiteral("k"), v2));
    QCOMPARE(ConfigStore::instance().load(QStringLiteral("t"), QStringLiteral("k"))
                 .value(QStringLiteral("x"))
                 .toInt(),
             2);
}

void TestConfigStore::listOrder()
{
    QVERIFY(ConfigStore::instance().save(QStringLiteral("t"), QStringLiteral("a"),
                                         QVariantMap{{QStringLiteral("n"), 1}}));
    QTest::qWait(20);
    QVERIFY(ConfigStore::instance().save(QStringLiteral("t"), QStringLiteral("b"),
                                         QVariantMap{{QStringLiteral("n"), 2}}));
    auto l = ConfigStore::instance().list(QStringLiteral("t"));
    QCOMPARE(l.size(), 2);
    QCOMPARE(l.at(0).value(QStringLiteral("key")).toString(), QStringLiteral("b"));
}

void TestConfigStore::listPaging()
{
    // 12 行按插入顺序保存（updated_at 严格递增），list 以 updated_at 倒序返回：
    // 最新在前。offset 分页必须能完整遍历超出单页窗口的历史。
    for (int i = 0; i < 12; ++i) {
        QVERIFY(ConfigStore::instance().save(
            QStringLiteral("page.t"), QStringLiteral("k%1").arg(i, 2, 10, QLatin1Char('0')),
            QVariantMap{{QStringLiteral("n"), i}}));
        QTest::qWait(15);
    }
    // 默认调用（不带 offset）与旧行为一致：从最新开始、受 limit 约束
    const auto head = ConfigStore::instance().list(QStringLiteral("page.t"), 5);
    QCOMPARE(head.size(), 5);
    QCOMPARE(head.at(0).value(QStringLiteral("key")).toString(), QStringLiteral("k11"));
    QCOMPARE(head.at(4).value(QStringLiteral("key")).toString(), QStringLiteral("k07"));
    // 后续页逐页向后移动
    const auto p1 = ConfigStore::instance().list(QStringLiteral("page.t"), 5, 5);
    QCOMPARE(p1.size(), 5);
    QCOMPARE(p1.at(0).value(QStringLiteral("key")).toString(), QStringLiteral("k06"));
    QCOMPARE(p1.at(4).value(QStringLiteral("key")).toString(), QStringLiteral("k02"));
    const auto p2 = ConfigStore::instance().list(QStringLiteral("page.t"), 5, 10);
    QCOMPARE(p2.size(), 2);
    QCOMPARE(p2.at(0).value(QStringLiteral("key")).toString(), QStringLiteral("k01"));
    QCOMPARE(p2.at(1).value(QStringLiteral("key")).toString(), QStringLiteral("k00"));
    // 越界页为空
    QVERIFY(ConfigStore::instance().list(QStringLiteral("page.t"), 5, 12).isEmpty());
    // 分页拼接 = 全量倒序（无重复、无遗漏）
    QStringList joined;
    for (int offset = 0;; offset += 5) {
        const auto page = ConfigStore::instance().list(QStringLiteral("page.t"), 5, offset);
        for (const QVariantMap& row : page)
            joined << row.value(QStringLiteral("key")).toString();
        if (page.size() < 5) break;
    }
    QStringList expected;
    for (int i = 11; i >= 0; --i)
        expected << QStringLiteral("k%1").arg(i, 2, 10, QLatin1Char('0'));
    QCOMPARE(joined, expected);
}

void TestConfigStore::removeWorks()
{
    QVERIFY(ConfigStore::instance().save(QStringLiteral("t"), QStringLiteral("k"),
                                         QVariantMap{{QStringLiteral("x"), 1}}));
    QVERIFY(ConfigStore::instance().remove(QStringLiteral("t"), QStringLiteral("k")));
    QVERIFY(!ConfigStore::instance().exists(QStringLiteral("t"), QStringLiteral("k")));
}

void TestConfigStore::exportImportRoundtrip()
{
    QVERIFY(ConfigStore::instance().save(QStringLiteral("t"), QStringLiteral("k1"),
                                         QVariantMap{{QStringLiteral("v"), 1}}));
    const QString path = QDir(m_workDir).filePath(QStringLiteral("export.json"));
    QVERIFY(ConfigStore::instance().exportTo(path));
    ConfigStore::instance().close();
    QFile::remove(m_dbPath);
    QFile::remove(m_dbPath + QStringLiteral("-wal"));
    QFile::remove(m_dbPath + QStringLiteral("-shm"));
    QVERIFY(ConfigStore::instance().open(m_dbPath));
    QVERIFY(ConfigStore::instance().importFrom(path));
    QCOMPARE(ConfigStore::instance()
                 .load(QStringLiteral("t"), QStringLiteral("k1"))
                 .value(QStringLiteral("v"))
                 .toInt(),
             1);
    QFile::remove(path);
}

QTEST_MAIN(TestConfigStore)
#include "tst_config_store.moc"
