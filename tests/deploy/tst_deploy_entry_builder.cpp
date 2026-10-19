// tst_deploy_entry_builder.cpp — DeployEntryBuilder.h 部署入口条目装配纯头守卫测试
// （v2.15 波2 任务9 评审修复轮 1：I-1 上限绕过 + I-2 单发装配腿可测缝）。
//
// 覆盖意图（规格 §3.1 不变式「三入口同口径 + 失败不静默 + 镜像 + 5000 上限整体拒绝」）：
// ① 混合选择（文件+目录）→ 顶层文件产扁平项 remoteDir/<文件名>、目录内容产镜像项
//   remoteDir/<目录名>/…，两输出向量同长且顺序与输入 selections 一致；
// ② 超上限 → 返回 false 且 errorOut **逐字等于** FolderExpand 纯头拒绝文案
//   （断言用同模板构造的期望串，非转述副本），输出三容器零写入；
// ③ 全空（仅空目录被选）→ 中文「所选目录为空…」拒绝，输出零写入；
// ④ 无任何目录 → v2.14 逐字节扁平口径（files=原始清单、远端向量与展开条目留空）；
// ⑤ 评审 I-1 回归核心：分类取**点击时刻实况**（QFileInfo::isDir），面板快照过期
//   （文件行实为目录）不得绕过 5000 上限、不得漏掉镜像映射；目录行实为文件则回
//   扁平口径正常交付。
//
// 夹具溯源：目录扫描经 DirScanner 注入缝脱盘（同 tst_folder_expand 模式，超上限
// 5001 条为注入假路径）；条目**类型判定**依赖 QTemporaryDir 真盘实况（I-1 修复的
// 语义前提就是"以磁盘为准"，此处不可脱盘）。
#include <QtTest>

#include <QFileInfo>
#include <QTemporaryDir>

#include "deploy/DeployEntryBuilder.h"

class TstDeployEntryBuilder : public QObject {
    Q_OBJECT
private slots:
    // ① 混合选择：a.txt（文件）+ bin/（目录，注入两条内容）+ b.txt（文件）。
    //    期望：files 与 remotePaths 同长（4）；顶层文件=remoteDir/文件名扁平项、
    //    目录内容=remoteDir/bin/… 镜像项；顺序严格按输入序。
    void mixedSelectionProducesFlatAndMirrorEntriesInOrder() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = QDir(dir.path()).absolutePath();   // 规范化绝对路径
        const QString fileA = root + "/a.txt";
        const QString fileB = root + "/b.txt";
        const QString binDir = root + "/bin";
        QVERIFY(QDir(root).mkpath("bin"));
        { QFile f(fileA); QVERIFY(f.open(QIODevice::WriteOnly)); }
        { QFile f(fileB); QVERIFY(f.open(QIODevice::WriteOnly)); }
        // 注入 scanner：脱盘返回 bin 内两条固定路径（目录本身为真盘实况目录）
        const folderexpand::DirScanner scanner =
            [](const QString& d) { return QStringList{ d + "/c.txt", d + "/sub/d.txt" }; };

        const QList<folderexpand::Selection> selections = {
            {fileA, true},    // 快照故意过期（实为文件），验证以实况为准
            {binDir, true},
            {fileB, false},
        };
        std::vector<std::string> files;
        std::vector<std::string> remotePaths;
        QList<folderexpand::ExpandedFile> expandedItems;
        QString error;
        const bool ok = deployentry::build(selections, QStringLiteral("/opt/app"),
                                           files, remotePaths, expandedItems, error, scanner);
        QVERIFY2(ok, qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(files.size(), remotePaths.size());
        QCOMPARE(files.size(), std::size_t(4));
        // 顺序=输入序：a.txt（扁平）、bin/c.txt、bin/sub/d.txt（镜像）、b.txt（扁平）
        QCOMPARE(QString::fromStdString(files[0]), fileA);
        QCOMPARE(QString::fromStdString(files[1]), binDir + "/c.txt");
        QCOMPARE(QString::fromStdString(files[2]), binDir + "/sub/d.txt");
        QCOMPARE(QString::fromStdString(files[3]), fileB);
        QCOMPARE(QString::fromStdString(remotePaths[0]), QStringLiteral("/opt/app/a.txt"));
        QCOMPARE(QString::fromStdString(remotePaths[1]), QStringLiteral("/opt/app/bin/c.txt"));
        QCOMPARE(QString::fromStdString(remotePaths[2]), QStringLiteral("/opt/app/bin/sub/d.txt"));
        QCOMPARE(QString::fromStdString(remotePaths[3]), QStringLiteral("/opt/app/b.txt"));
        // expandedItems 原样条目（保存为模板腿消费）：顶层文件 rel 空、目录项带镜像 rel
        QCOMPARE(expandedItems.size(), 4);
        QCOMPARE(expandedItems[0].remoteRel, QString());
        QCOMPARE(expandedItems[1].remoteRel, QStringLiteral("bin/c.txt"));
        QCOMPARE(expandedItems[2].remoteRel, QStringLiteral("bin/sub/d.txt"));
        QCOMPARE(expandedItems[3].remoteRel, QString());
    }

    // ② 超上限：注入 scanner 返回 5001 条（> kMaxExpandedItems）→ 整体拒绝，
    //    errorOut 与 FolderExpand 纯头文案**逐字相等**（同模板构造期望串），
    //    三个输出容器零写入。
    void overCapRejectsWithPureHeaderErrorVerbatim() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString bigDir = QDir(dir.path()).absolutePath() + "/big";
        QVERIFY(QDir(dir.path()).mkpath("big"));
        QStringList scanned;
        scanned.reserve(folderexpand::kMaxExpandedItems + 1);
        for (int i = 0; i <= folderexpand::kMaxExpandedItems; ++i) {
            scanned.append(bigDir + "/f" + QString::number(i) + ".txt");
        }
        QCOMPARE(scanned.size(), folderexpand::kMaxExpandedItems + 1);
        const folderexpand::DirScanner scanner = [&scanned](const QString&) { return scanned; };

        const QList<folderexpand::Selection> selections = { {bigDir, true} };
        std::vector<std::string> files;
        std::vector<std::string> remotePaths;
        QList<folderexpand::ExpandedFile> expandedItems;
        QString error;
        const bool ok = deployentry::build(selections, QStringLiteral("/opt/app"),
                                           files, remotePaths, expandedItems, error, scanner);
        QVERIFY(!ok);
        // 逐字相等：期望串用与纯头同一模板构造（评审要求，不得转述）
        const QString expected = QStringLiteral("目录展开后共 %1 条文件，超过单次部署上限 %2，"
                                                "请精简目录或分批部署")
                                     .arg(folderexpand::kMaxExpandedItems + 1)
                                     .arg(folderexpand::kMaxExpandedItems);
        QCOMPARE(error, expected);
        QVERIFY(files.empty());
        QVERIFY(remotePaths.empty());
        QVERIFY(expandedItems.isEmpty());
    }

    // ⑤ 评审 I-1 回归核心：面板快照把目录行记成文件（isDirectory=false），磁盘
    //    实况为目录——装配必须按**点击时刻实况**分类：小目录→展开为镜像条目；
    //    超上限目录→拒绝（快照口径下会产扁平清单绕过 5000 上限走 Job 无上限遍历）。
    void staleFileSnapshotOfDirectoryClassifiedByReality() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString binDir = QDir(dir.path()).absolutePath() + "/bin";
        QVERIFY(QDir(dir.path()).mkpath("bin"));
        const folderexpand::DirScanner scanner =
            [](const QString& d) { return QStringList{ d + "/c.txt" }; };
        {
            // 快照 false + 实况目录 → 展开镜像（而非扁平单条下传）
            const QList<folderexpand::Selection> selections = { {binDir, false} };
            std::vector<std::string> files;
            std::vector<std::string> remotePaths;
            QList<folderexpand::ExpandedFile> expandedItems;
            QString error;
            QVERIFY(deployentry::build(selections, QStringLiteral("/opt/app"),
                                       files, remotePaths, expandedItems, error, scanner));
            QCOMPARE(files.size(), std::size_t(1));
            QCOMPARE(remotePaths.size(), std::size_t(1));
            QCOMPARE(QString::fromStdString(remotePaths[0]), QStringLiteral("/opt/app/bin/c.txt"));
        }
        {
            // 快照 false + 实况目录且超上限 → 拒绝，绝不再产无上限扁平清单
            QStringList scanned;
            scanned.reserve(folderexpand::kMaxExpandedItems + 1);
            for (int i = 0; i <= folderexpand::kMaxExpandedItems; ++i) {
                scanned.append(binDir + "/f" + QString::number(i) + ".txt");
            }
            const folderexpand::DirScanner bigScanner =
                [&scanned](const QString&) { return scanned; };
            const QList<folderexpand::Selection> selections = { {binDir, false} };
            std::vector<std::string> files;
            std::vector<std::string> remotePaths;
            QList<folderexpand::ExpandedFile> expandedItems;
            QString error;
            const bool ok = deployentry::build(selections, QStringLiteral("/opt/app"),
                                               files, remotePaths, expandedItems, error, bigScanner);
            QVERIFY(!ok);
            const QString expected = QStringLiteral("目录展开后共 %1 条文件，超过单次部署上限 %2，"
                                                    "请精简目录或分批部署")
                                         .arg(folderexpand::kMaxExpandedItems + 1)
                                         .arg(folderexpand::kMaxExpandedItems);
            QCOMPARE(error, expected);
            QVERIFY(files.empty());
            QVERIFY(remotePaths.empty());
        }
        {
            // 反向漂移：快照 true + 实况文件 → 按实况归文件；全体无目录 → v2.14 扁平口径
            const QString aFile = QDir(dir.path()).absolutePath() + "/a.txt";
            { QFile f(aFile); QVERIFY(f.open(QIODevice::WriteOnly)); }
            const QList<folderexpand::Selection> selections = { {aFile, true} };
            std::vector<std::string> files;
            std::vector<std::string> remotePaths;
            QList<folderexpand::ExpandedFile> expandedItems;
            QString error;
            QVERIFY(deployentry::build(selections, QStringLiteral("/opt/app"),
                                       files, remotePaths, expandedItems, error, scanner));
            QCOMPARE(files.size(), std::size_t(1));
            QCOMPARE(QString::fromStdString(files[0]), aFile);
            QVERIFY(remotePaths.empty());     // 扁平口径：远端向量留空
            QVERIFY(expandedItems.isEmpty());
        }
    }

    // ③ 全空：仅一个真空目录被选（缺省 scanDir 实扫）→ 特定中文拒绝，输出零写入。
    void onlyEmptyDirectoriesRejectedWithChineseError() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString emptyDir = QDir(dir.path()).absolutePath() + "/empty";
        QVERIFY(QDir(dir.path()).mkpath("empty"));
        const QList<folderexpand::Selection> selections = { {emptyDir, true} };
        std::vector<std::string> files;
        std::vector<std::string> remotePaths;
        QList<folderexpand::ExpandedFile> expandedItems;
        QString error;
        const bool ok = deployentry::build(selections, QStringLiteral("/opt/app"),
                                           files, remotePaths, expandedItems, error);
        QVERIFY(!ok);
        QCOMPARE(error, QStringLiteral("所选目录为空，没有可部署的文件，请检查左侧选择"));
        QVERIFY(files.empty());
        QVERIFY(remotePaths.empty());
        QVERIFY(expandedItems.isEmpty());
    }

    // ④ 无目录：v2.14 逐字节扁平口径——files=原始清单（输入序、原样路径），
    //    remotePaths/expandedItems 留空（空=后端 legacy/basename 分支逐字不变）。
    void noDirectoriesKeepsV214FlatBehaviorByteIdentical() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = QDir(dir.path()).absolutePath();
        const QString fileA = root + "/a.txt";
        const QString fileB = root + "/b.txt";
        { QFile f(fileA); QVERIFY(f.open(QIODevice::WriteOnly)); }
        { QFile f(fileB); QVERIFY(f.open(QIODevice::WriteOnly)); }
        const QList<folderexpand::Selection> selections = {
            {fileA, false}, {fileB, false},
        };
        std::vector<std::string> files;
        std::vector<std::string> remotePaths;
        QList<folderexpand::ExpandedFile> expandedItems;
        QString error;
        const bool ok = deployentry::build(selections, QStringLiteral("/opt/app"),
                                           files, remotePaths, expandedItems, error);
        QVERIFY2(ok, qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(files.size(), std::size_t(2));
        QCOMPARE(QString::fromStdString(files[0]), fileA);
        QCOMPARE(QString::fromStdString(files[1]), fileB);
        QVERIFY(remotePaths.empty());
        QVERIFY(expandedItems.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstDeployEntryBuilder)
#include "tst_deploy_entry_builder.moc"
