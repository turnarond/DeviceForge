#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "framework/DeviceInfo.h"   // connect({}, {}) 需要完整类型（IFileSource.h 仅前向声明）
#include "ui/LocalFileSource.h"

class TstFileSource : public QObject {
    Q_OBJECT
private slots:
    void local_list_returnsEntries() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile f(dir.filePath("a.txt"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("hello"); f.close();
        QDir().mkpath(dir.filePath("sub"));
        LocalFileSource src;
        auto entries = src.list(dir.path());
        QCOMPARE(entries.size(), 2);            // a.txt + sub
        QVERIFY(entries[0].name == "a.txt" || entries[0].name == "sub");
        for (const auto& e : entries) {
            if (e.name == "a.txt") { QCOMPARE(e.size, 5u); QVERIFY(!e.isDir); }
            if (e.name == "sub")   { QVERIFY(e.isDir); }
        }
    }
    void local_mkdir_rename_remove() {
        QTemporaryDir dir;
        LocalFileSource src;
        QVERIFY(src.mkdir(dir.filePath("newdir")));
        QVERIFY(QDir(dir.filePath("newdir")).exists());
        QVERIFY(src.mkdir(dir.filePath("newdir")));   // 已存在 → 幂等成功（契约语义）
        QVERIFY(src.lastError().isEmpty());
        QFile f(dir.filePath("x.txt"));
        f.open(QIODevice::WriteOnly); f.write("x"); f.close();
        QVERIFY(src.rename(dir.filePath("x.txt"), dir.filePath("y.txt")));
        QVERIFY(!QFile::exists(dir.filePath("x.txt")));
        QVERIFY(QFile::exists(dir.filePath("y.txt")));
        QVERIFY(src.remove(dir.filePath("y.txt"), false));
        QVERIFY(!QFile::exists(dir.filePath("y.txt")));
        QVERIFY(src.remove(dir.filePath("newdir"), true));   // 递归删除目录
        QVERIFY(!QDir(dir.filePath("newdir")).exists());
    }
    void local_clearDirectory_keepsRoot() {
        QTemporaryDir dir;
        LocalFileSource src;
        QFile f(dir.filePath("keep.txt"));
        f.open(QIODevice::WriteOnly); f.write("k"); f.close();
        QDir().mkpath(dir.filePath("inner"));
        QVERIFY(src.clearDirectory(dir.path()));
        QCOMPARE(QDir(dir.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).size(), 0);
        QVERIFY(QDir(dir.path()).exists());      // 目录本身保留
    }
    void local_connectIsNoop() {
        LocalFileSource src;
        QVERIFY(src.connect({}, {}));
        QVERIFY(src.isConnected());
    }
    // v2.12 文件管理增强：本地条目填充 owner（与 QFileInfo::owner 口径一致；
    // 平台取不到时允许为空串——面板显示 —，但不得崩溃）
    void local_list_fillsOwner() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile f(dir.filePath("o.txt"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x"); f.close();
        LocalFileSource src;
        auto entries = src.list(dir.path());
        QCOMPARE(entries.size(), 1);
        const QFileInfo qi(dir.filePath("o.txt"));
        QCOMPARE(QString::fromStdString(entries[0].owner), qi.owner());
    }
};
QTEST_MAIN(TstFileSource)
#include "tst_file_source.moc"
