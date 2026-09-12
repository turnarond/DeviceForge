#include <QtTest/QtTest>
#include <string>
#include <vector>
#include "adapter/SshAdapter.h"

// planFolderUpload 纯逻辑测试（不连服务器）：
// 本地 {root/a.txt, root/sub/b.txt, root/sub/} → 远程路径映射
class TstSftpPlan : public QObject {
    Q_OBJECT
private slots:
    void plan_singleFile() {
        // 本地临时目录：root/a.txt
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile f(dir.filePath("a.txt"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x"); f.close();
        auto items = SshAdapter::planFolderUpload(dir.path().toStdString(), "/home/user/apps");
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].isDirectory, false);
        QCOMPARE(QString::fromStdString(items[0].remotePath), "/home/user/apps/a.txt");
    }
    void plan_nestedDir() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir().mkpath(dir.filePath("sub"));
        QFile f(dir.filePath("sub/b.txt"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("y"); f.close();
        auto items = SshAdapter::planFolderUpload(dir.path().toStdString(), "/apps");
        // sub/ 目录项 + sub/b.txt 文件项
        QCOMPARE(items.size(), 2);
        QVERIFY(items[0].isDirectory);
        QCOMPARE(QString::fromStdString(items[0].remotePath), "/apps/sub");
        QCOMPARE(items[1].isDirectory, false);
        QCOMPARE(QString::fromStdString(items[1].remotePath), "/apps/sub/b.txt");
    }
    void plan_emptyDir() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto items = SshAdapter::planFolderUpload(dir.path().toStdString(), "/apps");
        QCOMPARE(items.size(), 0);  // 空目录：无传输项
    }
    void plan_unicodeNestedNamesUseUtf8() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString subdirectory = dir.filePath(QStringLiteral("子目录"));
        QVERIFY(QDir().mkpath(subdirectory));
        const QString filePath = QDir(subdirectory).filePath(QStringLiteral("固件.bin"));
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("z"), qint64(1));
        file.close();

        const auto items = SshAdapter::planFolderUpload(
            dir.path().toUtf8().toStdString(), "/apps");

        QCOMPARE(items.size(), 2);
        QVERIFY(items[0].isDirectory);
        QCOMPARE(QString::fromUtf8(items[0].remotePath), QStringLiteral("/apps/子目录"));
        QVERIFY(!items[1].isDirectory);
        QCOMPARE(QDir::fromNativeSeparators(QString::fromUtf8(items[1].localPath)),
                 QDir::fromNativeSeparators(filePath));
        QCOMPARE(QString::fromUtf8(items[1].remotePath),
                 QStringLiteral("/apps/子目录/固件.bin"));
    }
};
QTEST_MAIN(TstSftpPlan)
#include "tst_sftp_plan.moc"
