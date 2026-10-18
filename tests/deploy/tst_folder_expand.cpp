// tst_folder_expand.cpp — FolderExpand.h 目录展开纯头守卫测试（v2.15 波2 第一腿，任务 7）。
//
// 覆盖意图：部署页文件夹支持的展开核心口径（规格 §3.1）——目录展开以**目录名自身**
// 为远端相对路径首段（D:/pkg/bin → "bin/app.conf"）；顶层普通文件 rel 为空（扁平）且
// 保持输入序；混选两类同现；展开总数超 kMaxExpandedItems(5000) → 整体拒绝，中文 error
// 同时含实际数与上限数；空目录 0 条目不报错；composeRemotePath 三分支（rel 空防御分支
// /常规目录去尾斜杠拼接/根目录 "/" 不叠斜杠）；根目录退化（契约 ⑧）整体拒绝并给中文 error。
// 夹具=注入 fake DirScanner，全程不触真盘（脱盘纯逻辑）；目录内顺序只依赖注入 scanner
// 的返回序，不依赖真实 QDirIterator 自然序。

#include <QtTest>

#include "deploy/FolderExpand.h"

class TstFolderExpand : public QObject {
    Q_OBJECT
private slots:
    // 目录展开：目录名为相对首段，多级子目录镜像完整相对路径。
    void dirExpandsWithSelfAsFirstSegment() {
        const QStringList scanned = {
            QStringLiteral("D:/pkg/bin/app.conf"),
            QStringLiteral("D:/pkg/bin/lib/a.so"),
        };
        const folderexpand::DirScanner scanner = [&scanned](const QString&) { return scanned; };
        const QList<folderexpand::Selection> selections = {
            {QStringLiteral("D:/pkg/bin"), true},
        };
        const folderexpand::ScanResult res = folderexpand::expand(selections, scanner);
        QVERIFY(res.ok);
        QVERIFY(res.error.isEmpty());
        QCOMPARE(res.files.size(), 2);
        // 逐条断言字段实值：localPath 原样透传 scanner 返回，remoteRel 目录名打头。
        QCOMPARE(res.files[0].localPath, QStringLiteral("D:/pkg/bin/app.conf"));
        QCOMPARE(res.files[0].remoteRel, QStringLiteral("bin/app.conf"));
        QCOMPARE(res.files[1].localPath, QStringLiteral("D:/pkg/bin/lib/a.so"));
        QCOMPARE(res.files[1].remoteRel, QStringLiteral("bin/lib/a.so"));
    }

    // 顶层普通文件：rel 全空（扁平语义），且严格按输入顺序输出。
    void topLevelFilesKeepEmptyRelAndOrder() {
        const folderexpand::DirScanner scanner = [](const QString&) { return QStringList{}; };
        const QList<folderexpand::Selection> selections = {
            {QStringLiteral("D:/top/f2.txt"), false},
            {QStringLiteral("D:/top/f1.txt"), false},
        };
        const folderexpand::ScanResult res = folderexpand::expand(selections, scanner);
        QVERIFY(res.ok);
        QCOMPARE(res.files.size(), 2);
        // 顺序=输入序（f2 在前），rel 空串。
        QCOMPARE(res.files[0].localPath, QStringLiteral("D:/top/f2.txt"));
        QCOMPARE(res.files[0].remoteRel, QString());
        QCOMPARE(res.files[1].localPath, QStringLiteral("D:/top/f1.txt"));
        QCOMPARE(res.files[1].remoteRel, QString());
    }

    // 文件+目录混选：两类条目同现于 files，selections 输入序处理。
    void mixedSelectionCoexists() {
        const QStringList scanned = {
            QStringLiteral("D:/pkg/bin/app.conf"),
        };
        const folderexpand::DirScanner scanner = [&scanned](const QString&) { return scanned; };
        const QList<folderexpand::Selection> selections = {
            {QStringLiteral("D:/top/readme.md"), false},
            {QStringLiteral("D:/pkg/bin"), true},
        };
        const folderexpand::ScanResult res = folderexpand::expand(selections, scanner);
        QVERIFY(res.ok);
        QCOMPARE(res.files.size(), 2);
        QCOMPARE(res.files[0].localPath, QStringLiteral("D:/top/readme.md"));
        QCOMPARE(res.files[0].remoteRel, QString());            // 顶层文件=扁平
        QCOMPARE(res.files[1].localPath, QStringLiteral("D:/pkg/bin/app.conf"));
        QCOMPARE(res.files[1].remoteRel, QStringLiteral("bin/app.conf")); // 目录=名打头
    }

    // 超上限整体拒绝：5001 条 > 5000 → ok=false，error 中文且同时含实际数与上限数，
    // files 全量丢弃（不留半份清单）。
    void overCapRejectedWithBothNumbers() {
        QStringList scanned;
        scanned.reserve(folderexpand::kMaxExpandedItems + 1);
        for (int i = 0; i <= folderexpand::kMaxExpandedItems; ++i) {
            scanned.append(QStringLiteral("D:/big/f%1.txt").arg(i));
        }
        QCOMPARE(scanned.size(), 5001);
        const folderexpand::DirScanner scanner = [&scanned](const QString&) { return scanned; };
        const QList<folderexpand::Selection> selections = {
            {QStringLiteral("D:/big"), true},
        };
        const folderexpand::ScanResult res = folderexpand::expand(selections, scanner);
        QVERIFY(!res.ok);
        QVERIFY(res.files.isEmpty());   // 整体拒绝
        QVERIFY(res.error.contains(QStringLiteral("5001")));
        QVERIFY(res.error.contains(QStringLiteral("5000")));
        QVERIFY(res.error.contains(QStringLiteral("目录展开后共")));
    }

    // 空目录：0 条目、ok=true、无 error——不算错。
    void emptyDirSucceedsWithZeroEntries() {
        const folderexpand::DirScanner scanner = [](const QString&) { return QStringList{}; };
        const QList<folderexpand::Selection> selections = {
            {QStringLiteral("D:/empty"), true},
        };
        const folderexpand::ScanResult res = folderexpand::expand(selections, scanner);
        QVERIFY(res.ok);
        QVERIFY(res.files.isEmpty());
        QVERIFY(res.error.isEmpty());
    }

    // composeRemotePath 三分支：
    // ① rel 空 → 直挂 remoteDir 根（防御分支——顶层文件实走 files 键简写，不走本函数）；
    // ② 常规目录带尾斜杠 → 规范化去尾斜杠后拼 "/" + rel；
    // ③ 根目录 "/" → 不叠双斜杠。
    void composeRemotePathThreeBranches() {
        // ① rel 空：返回规范化 remoteDir（去尾斜杠；根 "/" 保留单斜杠）。
        QCOMPARE(folderexpand::composeRemotePath(QStringLiteral("/opt/app/"), QString()),
                 QStringLiteral("/opt/app"));
        QCOMPARE(folderexpand::composeRemotePath(QStringLiteral("/"), QString()),
                 QStringLiteral("/"));
        // ② 常规：去尾斜杠 + "/" + rel。
        QCOMPARE(folderexpand::composeRemotePath(QStringLiteral("/opt/app/"),
                                                 QStringLiteral("bin/a")),
                 QStringLiteral("/opt/app/bin/a"));
        // 无尾斜杠输入同样成立（幂等）。
        QCOMPARE(folderexpand::composeRemotePath(QStringLiteral("/opt/app"),
                                                 QStringLiteral("bin/a")),
                 QStringLiteral("/opt/app/bin/a"));
        // ③ 根目录："/" + rel，不得出现 "//bin/a"。
        QCOMPARE(folderexpand::composeRemotePath(QStringLiteral("/"),
                                                 QStringLiteral("bin/a")),
                 QStringLiteral("/bin/a"));
    }

    // 根目录退化防御（契约 ⑧，评审 I-2）：文件系统根作为目录 Selection 时
    // QFileInfo::fileName() 返回空串，若不拦 rel 会退化成 "/x.txt"、下游
    // composeRemotePath 叠 "//"。expand 须整体拒绝：ok=false、中文 error、files 全空。
    void rootDirSelectionRejectedWithChineseError() {
        // 前置自证：确认所选根形态在当前平台确会退化成空 fileName——
        // QDir::rootPath()（Windows "C:/"、Unix "/"）的末段即空串（最后一个 '/' 之后
        // 无字符），故此用例的 dirName 触发条件成立；若此断言变红说明平台语义漂移，需换根形态。
        const QString root = QDir::rootPath();
        QVERIFY2(QFileInfo(root).fileName().isEmpty(),
                 qPrintable(QStringLiteral("前置不成立：QFileInfo(%1).fileName() 非空").arg(root)));
        // 注入返回非空清单的 scanner：证明 dirName 空时在扫描前即整体拒绝、
        // 且 scanner 结果绝不落入 files（不产 "/x.txt" 坏 rel）。
        const QStringList scanned = {
            QStringLiteral("D:/should/not/appear.txt"),
        };
        const folderexpand::DirScanner scanner = [&scanned](const QString&) { return scanned; };
        const QList<folderexpand::Selection> selections = {
            {root, true},
        };
        const folderexpand::ScanResult res = folderexpand::expand(selections, scanner);
        QVERIFY(!res.ok);
        QVERIFY(res.files.isEmpty());   // 整体拒绝：不留半份清单，无坏 rel
        QVERIFY(!res.error.isEmpty());
        QVERIFY(res.error.contains(QStringLiteral("根目录")));   // 中文拒绝文案在册
    }
};

QTEST_GUILESS_MAIN(TstFolderExpand)
#include "tst_folder_expand.moc"
