#include <QtTest/QtTest>
#include "tools/FtpDeployTool/RemoteFileModel.h"
#include "tools/FtpDeployTool/FtpFileInfo.h"
#include "ui/DirCompare.h"

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
    void sort_descending_equalsSafe() {
        RemoteFileModel model;
        std::vector<FtpFileInfo> files;
        auto mk = [](std::string n, bool dir, uint64_t sz = 0) {
            FtpFileInfo f; f.name = n; f.isDir = dir; f.size = sz; return f;
        };
        // 等名/等键文件（含大小写不敏感等名）：旧实现降序时 comp(a,b) 与 comp(b,a) 均 true，
        // 违反严格弱序不可反身性（UB，std::sort 断言/崩溃/乱序），修复后应稳定成组且不崩溃
        files.push_back(mk("dup.txt", false, 100));
        files.push_back(mk("dup.txt", false, 100));
        files.push_back(mk("a.txt", false, 50));
        files.push_back(mk("Dup.txt", false, 100));
        files.push_back(mk("c.txt", false, 200));
        model.setFileList(files);
        model.sort(RemoteFileModel::ColName, Qt::DescendingOrder);  // 不崩溃

        QCOMPARE(model.rowCount(), 5);
        // 降序（d > c > a）：等名组（dup.txt/dup.txt/Dup.txt）在最前且相邻，
        // 其后 c.txt、a.txt 递减；组内顺序任意——std::sort 非稳定，仅锁"成组"语义
        int dups = 0;
        for (int i = 0; i < 3; ++i) {
            const QString n = QString::fromStdString(model.fileAt(i).name);
            QVERIFY(QString::compare(n, QStringLiteral("dup.txt"), Qt::CaseInsensitive) == 0);
            ++dups;
        }
        QCOMPARE(dups, 3);
        QCOMPARE(QString::fromStdString(model.fileAt(3).name), QStringLiteral("c.txt"));
        QCOMPARE(QString::fromStdString(model.fileAt(4).name), QStringLiteral("a.txt"));

        // 大小列降序 + 等大小键（ColSize 相等分支同样受 SWO 修复保护）
        RemoteFileModel m2;
        std::vector<FtpFileInfo> f2;
        f2.push_back(mk("a.bin", false, 10));
        f2.push_back(mk("b.bin", false, 10));   // 与 a.bin 等大小
        f2.push_back(mk("c.bin", false, 30));
        m2.setFileList(f2);
        m2.sort(RemoteFileModel::ColSize, Qt::DescendingOrder);  // 不崩溃
        QCOMPARE(m2.rowCount(), 3);
        QCOMPARE(QString::fromStdString(m2.fileAt(0).name), QStringLiteral("c.bin"));
        // 等大小组（10/10）相邻且完整
        int szDups = 0;
        for (int i = 1; i <= 2; ++i) {
            QCOMPARE(m2.fileAt(i).size, uint64_t(10));
            ++szDups;
        }
        QCOMPARE(szDups, 2);
    }
    void ownerColumn_endToEnd() {
        RemoteFileModel model;
        std::vector<FtpFileInfo> files;
        FtpFileInfo withOwner; withOwner.name = "a.txt"; withOwner.owner = "0";
        FtpFileInfo noOwner;   noOwner.name = "b.txt";   // owner 留空
        files.push_back(withOwner);
        files.push_back(noOwner);
        model.setFileList(files);

        // 列序扩展：所有者列在权限之后，ColCount 随之 +1
        QCOMPARE(RemoteFileModel::ColCount, 6);
        QCOMPARE(model.headerData(RemoteFileModel::ColOwner, Qt::Horizontal, Qt::DisplayRole)
                     .toString(), QStringLiteral("所有者"));
        QCOMPARE(model.columnCount(), 6);

        const int ownerCol = RemoteFileModel::ColOwner;
        QCOMPARE(model.data(model.index(0, ownerCol), Qt::DisplayRole).toString(),
                 QStringLiteral("0"));
        // 不可得统一显示 —（U+2014）
        QCOMPARE(model.data(model.index(1, ownerCol), Qt::DisplayRole).toString(),
                 QString(QChar(0x2014)));
    }

    void sort_ownerColumn_ordersByOwner() {
        RemoteFileModel model;
        std::vector<FtpFileInfo> files;
        auto mk = [](std::string n, std::string o) {
            FtpFileInfo f; f.name = n; f.owner = o; return f;
        };
        files.push_back(mk("a.txt", "root"));
        files.push_back(mk("b.txt", "admin"));
        model.setFileList(files);
        model.sort(RemoteFileModel::ColOwner, Qt::AscendingOrder);
        QCOMPARE(QString::fromStdString(model.fileAt(0).name), QStringLiteral("b.txt"));
    }
    void compareView_placeholderAndColors() {
        RemoteFileModel model;
        std::vector<FtpFileInfo> files;
        FtpFileInfo a; a.name = "a.txt"; a.size = 10;
        FtpFileInfo l; l.name = "l.txt"; l.size = 5;
        files.push_back(a); files.push_back(l);
        model.setFileList(files);

        // 左栏视角：a=Same，l=OnlyLeft（金中），对面 OnlyRight 条目展开为占位行
        QVector<AlignedRow> rows;
        rows.append(AlignedRow{0, CompareClass::Same});
        rows.append(AlignedRow{1, CompareClass::OnlyLeft});
        rows.append(AlignedRow{-1, CompareClass::OnlyRight});
        model.setCompareView(files, rows, QStringLiteral("远端独有"));

        QCOMPARE(model.rowCount(), 3);
        QVERIFY(!model.isPlaceholderAt(0));
        QVERIFY(!model.isPlaceholderAt(1));
        QVERIFY(model.isPlaceholderAt(2));
        // 占位行不可选不可用（双击/复制移动必须跳过）
        QVERIFY(!(model.flags(model.index(2, 0)) & Qt::ItemIsSelectable));
        QVERIFY(!!(model.flags(model.index(1, 0)) & Qt::ItemIsSelectable));
        // 名称列：占位行显示对面独有提示
        QCOMPARE(model.data(model.index(2, RemoteFileModel::ColName), Qt::DisplayRole).toString(),
                 QStringLiteral("远端独有"));
        // 对比态行色（深色主题）：Same 不着色 / OnlyLeft 金中 / 占位石墨
        QVERIFY(!model.data(model.index(0, RemoteFileModel::ColName), Qt::BackgroundRole).isValid());
        QCOMPARE(model.data(model.index(1, RemoteFileModel::ColName), Qt::BackgroundRole).value<QColor>(),
                 QColor(0x2A, 0x22, 0x14));
        QCOMPARE(model.data(model.index(2, RemoteFileModel::ColName), Qt::BackgroundRole).value<QColor>(),
                 QColor(0x08, 0x0B, 0x10));
        // Diff 行色
        model.setCompareView(files, {AlignedRow{0, CompareClass::Diff}}, QStringLiteral("—"));
        QCOMPARE(model.data(model.index(0, RemoteFileModel::ColName), Qt::BackgroundRole).value<QColor>(),
                 QColor(0x1F, 0x1A, 0x10));

        // 退出对比态：恢复普通视图与旧绿/黄自动着色路径
        model.exitCompareView();
        QCOMPARE(model.rowCount(), 2);
        QVERIFY(!model.isPlaceholderAt(0));
        LocalFileInfo li; li.name = "a.txt"; li.size = 99;
        model.setLocalFilesForCompare({li});   // 旧行为：同名异 size → 黄
        QCOMPARE(model.data(model.index(0, RemoteFileModel::ColName), Qt::BackgroundRole).value<QColor>(),
                 QColor(80, 70, 30));
    }
};
QTEST_MAIN(TstRemoteModel)
#include "tst_remote_model.moc"
