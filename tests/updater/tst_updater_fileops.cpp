// tst_updater_fileops.cpp — Updater 文件操作原语单元测试
//
// issue #22：WinMain 备份守卫的前提假设是 copyDirectory 的失败返回语义可信。
// 本目标锁定四条契约：
//   1. copyDirectory 成功路径：平铺文件 + 子目录递归复制完整
//   2. copyDirectory 源目录不存在 → false（备份源头异常必须暴露）
//   3. copyDirectory 目标被普通文件占用 → false（CreateDirectoryA/CopyFileA 双失败路径）
//   4. removeDirectory 删树成功 / 路径缺失返回 false

#include <QtTest>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QFile>
#include <QDir>
#include <QFileInfo>

#include "updater/UpdaterFileOps.h"

namespace {

std::string nativePath(const QString& p)
{
    return QDir::toNativeSeparators(p).toStdString();
}

void writeFile(const QString& path, const char* content)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(content);
}

QByteArray readAll(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

} // namespace

class TestUpdaterFileOps : public QObject
{
    Q_OBJECT

private slots:
    void copyDirectory_success()
    {
        QTemporaryDir src, dst;
        QVERIFY(src.isValid());
        QVERIFY(dst.isValid());

        writeFile(src.filePath("a.txt"), "hello");
        QVERIFY(QDir(src.path()).mkpath(QStringLiteral("sub/deep")));
        writeFile(src.filePath("sub/b.bin"), "world");
        writeFile(src.filePath("sub/deep/c.txt"), "!");

        QVERIFY(copyDirectory(nativePath(src.path()).c_str(),
                              nativePath(dst.filePath("backup")).c_str()));

        QCOMPARE(readAll(dst.filePath("backup/a.txt")), QByteArray("hello"));
        QCOMPARE(readAll(dst.filePath("backup/sub/b.bin")), QByteArray("world"));
        QCOMPARE(readAll(dst.filePath("backup/sub/deep/c.txt")), QByteArray("!"));
    }

    void copyDirectory_srcMissing_returnsFalse()
    {
        QTemporaryDir dst;
        QVERIFY(dst.isValid());

        const std::string ghost = nativePath(dst.filePath("ghost-src"));
        QVERIFY(!QFileInfo::exists(QString::fromStdString(ghost)));

        QVERIFY(!copyDirectory(ghost.c_str(), nativePath(dst.filePath("bk")).c_str()));
    }

    void copyDirectory_dstOccupiedByFile_returnsFalse()
    {
        QTemporaryDir src, dstRoot;
        QVERIFY(src.isValid());
        QVERIFY(dstRoot.isValid());

        writeFile(src.filePath("x.txt"), "data");
        const QString blocked = dstRoot.filePath("blocked");
        writeFile(blocked, "occupied-by-plain-file");
        QVERIFY(QFileInfo(blocked).isFile());

        QVERIFY(!copyDirectory(nativePath(src.path()).c_str(), nativePath(blocked).c_str()));
    }

    void removeDirectory_removesTree()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("sub/deep")));
        writeFile(dir.filePath("a.txt"), "1");
        writeFile(dir.filePath("sub/deep/c.txt"), "2");

        QVERIFY(removeDirectory(nativePath(dir.path()).c_str()));
        QVERIFY(!QFileInfo::exists(dir.path()));   // 整棵树连同根一起消失
    }

    void removeDirectory_missingPath_returnsFalse()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const std::string ghost = nativePath(dir.filePath("no-such-dir"));
        QVERIFY(!removeDirectory(ghost.c_str()));
    }
};

QTEST_MAIN(TestUpdaterFileOps)
#include "tst_updater_fileops.moc"
