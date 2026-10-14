#include <QtTest/QtTest>
#include "ui/DirCompare.h"

// 目录对比纯逻辑（v2.12 元数据口径）：分类判定 / 对齐行展开 / 分钟精度时间比较
// 判定契约（规格 §3.2）：size 主判据；两侧 mtime 均可得时分钟相等作辅助；
// isDir 只判存在性；. / .. 不参与配对。

namespace {

FtpFileInfo mk(const char* name, uint64_t size, const char* mtime = "", bool isDir = false)
{
    FtpFileInfo f;
    f.name = name;
    f.size = size;
    f.dateTime = mtime;
    f.isDir = isDir;
    return f;
}

} // namespace

class TstDirCompare : public QObject {
    Q_OBJECT
private slots:
    void compare_sameWhenSizeEqual()
    {
        QVector<CompareEntry> aligned;
        auto s = compareDirectories({mk("a", 10, "2026-10-12 10:00:00")},
                                    {mk("a", 10, "2026-10-12 10:00:00")}, &aligned);
        QCOMPARE(s.same, 1);
        QCOMPARE(s.diff, 0);
        QCOMPARE(aligned.size(), 1);
        QCOMPARE(int(aligned[0].cls), int(CompareClass::Same));
        QCOMPARE(aligned[0].leftRow, 0);
        QCOMPARE(aligned[0].rightRow, 0);
    }

    void compare_diffWhenSizeDiffers()
    {
        auto s = compareDirectories({mk("a", 10, "2026-10-12 10:00:00")},
                                    {mk("a", 12, "2026-10-12 10:00:00")});
        QCOMPARE(s.diff, 1);
        QCOMPARE(s.same, 0);
    }

    void compare_diffWhenMtimeDiffersSameSize()
    {
        auto s = compareDirectories({mk("a", 10, "2026-10-12 10:00:00")},
                                    {mk("a", 10, "2026-10-13 23:59:00")});
        QCOMPARE(s.diff, 1);
    }

    void compare_missingMtimeFallsBackToSize()
    {
        // Windows 格式 FTP 常拿不到时间：一侧 mtime 空、size 等 → 判 Same
        auto s = compareDirectories({mk("a", 10, "")}, {mk("a", 10, "2026-10-13 08:00:00")});
        QCOMPARE(s.same, 1);
    }

    void compare_mtimeComparedAtMinutePrecision()
    {
        // 秒位不同、分钟位相同 → Same（Unix LIST 只有分钟粒度，跨秒噪声不判差异）
        auto s = compareDirectories({mk("a", 10, "2026-10-12 10:00:59")},
                                    {mk("a", 10, "2026-10-12 10:00:01")});
        QCOMPARE(s.same, 1);
    }

    void compare_dirsOnlyPresence()
    {
        // 同名目录：不比 size/mtime（两侧目录 size 天然不同）
        auto s = compareDirectories({mk("etc", 4096, "2026-01-01 00:00:00", true)},
                                    {mk("etc", 8192, "2026-05-05 05:05:05", true)});
        QCOMPARE(s.same, 1);
        QCOMPARE(s.diff, 0);
    }

    void compare_dotsSkipped()
    {
        auto s = compareDirectories({mk("..", 0, "", true), mk("a", 1)},
                                    {mk("a", 1)});
        // 左多出的 ".." 不得计入 onlyLeft
        QCOMPARE(s.onlyLeft, 0);
        QCOMPARE(s.same, 1);
    }

    void compare_onlySidesCounted()
    {
        auto s = compareDirectories({mk("l", 1), mk("x", 2, "2026-10-10 10:10:10")},
                                    {mk("x", 5, "2026-10-10 10:10:10"), mk("r", 3)});
        QCOMPARE(s.diff, 1);      // x 大小不同
        QCOMPARE(s.onlyLeft, 1);  // l
        QCOMPARE(s.onlyRight, 1); // r
        QCOMPARE(s.same, 0);
    }

    void summary_matchesAlignedEntries()
    {
        QVector<CompareEntry> aligned;
        auto s = compareDirectories({mk("a", 1), mk("l", 2)},
                                    {mk("a", 1), mk("r", 3)}, &aligned);
        QCOMPARE(aligned.size(), 3);
        int same = 0, onlyL = 0, onlyR = 0;
        for (const auto& e : aligned) {
            if (e.cls == CompareClass::Same) ++same;
            if (e.cls == CompareClass::OnlyLeft) ++onlyL;
            if (e.cls == CompareClass::OnlyRight) ++onlyR;
        }
        QCOMPARE(same, s.same);
        QCOMPARE(onlyL, s.onlyLeft);
        QCOMPARE(onlyR, s.onlyRight);
    }

    void buildSideRows_gapInsertion()
    {
        QVector<CompareEntry> aligned;
        compareDirectories({mk("a", 1), mk("l", 2)},
                           {mk("a", 1), mk("r", 3)}, &aligned);
        const auto leftRows  = buildSideRows(aligned, /*leftSide=*/true);
        const auto rightRows = buildSideRows(aligned, /*leftSide=*/false);
        // 两侧展开行数一致（并集），只有对面存在的条目产生占位（sourceRow=-1）
        QCOMPARE(leftRows.size(), aligned.size());
        QCOMPARE(rightRows.size(), aligned.size());
        int leftGaps = 0, rightGaps = 0;
        for (const auto& r : leftRows)  if (r.sourceRow < 0) ++leftGaps;
        for (const auto& r : rightRows) if (r.sourceRow < 0) ++rightGaps;
        QCOMPARE(leftGaps, 1);   // r 在左栏是占位
        QCOMPARE(rightGaps, 1);  // l 在右栏是占位
    }

    void buildSideRows_diffOnlyShrink()
    {
        QVector<CompareEntry> aligned;
        compareDirectories({mk("a", 1), mk("x", 2, "2026-01-01 01:01:01")},
                           {mk("a", 1), mk("x", 9, "2026-01-01 01:01:01")}, &aligned);
        const auto rows = buildSideRows(aligned, true, /*diffOnly=*/true);
        QCOMPARE(rows.size(), 1);                 // Same 行（含对侧）全部收缩
        QCOMPARE(rows[0].cls, CompareClass::Diff);
    }

    void mtimeMinuteEqual_semantics()
    {
        QVERIFY(mtimeMinuteEqual("2026-10-12 10:00:59", "2026-10-12 10:00:01"));
        QVERIFY(!mtimeMinuteEqual("2026-10-12 10:01:00", "2026-10-12 10:00:59"));
        QVERIFY(!mtimeMinuteEqual("", "2026-10-12 10:00:00"));   // 空=不可得，不相等
        QVERIFY(!mtimeMinuteEqual("short", "2026-10-12 10:00"));// 畸形串不可比
    }
};
QTEST_MAIN(TstDirCompare)
#include "tst_dir_compare.moc"
