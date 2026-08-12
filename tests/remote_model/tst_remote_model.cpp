#include <QtTest/QtTest>
#include "tools/FtpDeployTool/RemoteFileModel.h"
#include "tools/FtpDeployTool/FtpFileInfo.h"

// RemoteFileModel::sort 语义锁定：
// . / .. 置顶 → 目录优先 → 名称不区分大小写排序
class TstRemoteModel : public QObject {
    Q_OBJECT
private slots:
    void sort_name_dirsFirst_dotsOnTop() {
        RemoteFileModel model;
        std::vector<FtpFileInfo> files;
        auto mk = [](std::string n, bool dir) {
            FtpFileInfo f; f.name = n; f.isDir = dir; return f;
        };
        // 乱序输入：文件/目录/./.. 混合
        files.push_back(mk("b.txt", false));
        files.push_back(mk("A_DIR", true));
        files.push_back(mk("..", true));
        files.push_back(mk("Z.txt", false));
        files.push_back(mk(".", true));
        files.push_back(mk("Cdir", true));
        model.setFileList(files);
        model.sort(RemoteFileModel::ColName, Qt::AscendingOrder);

        QCOMPARE(model.rowCount(), 6);
        // . 和 .. 置顶（保持输入相对顺序：.. 在 . 之前输入）
        QCOMPARE(QString::fromStdString(model.fileAt(0).name), QStringLiteral(".."));
        QCOMPARE(QString::fromStdString(model.fileAt(1).name), QStringLiteral("."));
        // 目录优先（A_DIR / Cdir），名称不区分大小写
        QCOMPARE(QString::fromStdString(model.fileAt(2).name), QStringLiteral("A_DIR"));
        QCOMPARE(QString::fromStdString(model.fileAt(3).name), QStringLiteral("Cdir"));
        // 文件在后（b.txt / Z.txt——不区分大小写：'b' < 'z'）
        QCOMPARE(QString::fromStdString(model.fileAt(4).name), QStringLiteral("b.txt"));
        QCOMPARE(QString::fromStdString(model.fileAt(5).name), QStringLiteral("Z.txt"));
    }
};
QTEST_MAIN(TstRemoteModel)
#include "tst_remote_model.moc"
