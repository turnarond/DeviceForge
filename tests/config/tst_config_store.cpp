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
