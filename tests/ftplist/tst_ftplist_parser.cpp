// tst_ftplist_parser.cpp — FtpListParser LIST 解析格式矩阵回归测试
//
// issue #21：FTP LIST 输出是出了名的易碎部件（各服务器格式差异大），本目标锁定：
//   格式检测（Unix/Windows/无法识别/total 头）、Unix 矩阵（含单数字日右对齐、
//   多空格日期、含空格文件名、符号链接箭头、中文文件名）、Windows 矩阵
//   （千分位逗号/<DIR>/PM 换算/12AM|PM 边界/无前导零/icase）、垃圾行鲁棒性。
// 断言全部基于当前实现的真实行为；unix_singleDigitDay / unix_multiSpaceDate
// 为缺陷复现用例（TDD 红→修复→绿）。

#include <QtTest>
#include <QDate>
#include <string>
#include <vector>

#include "tools/FtpDeployTool/FtpListParser.h"

namespace {

const FtpFileInfo* findEntry(const std::vector<FtpFileInfo>& v, const std::string& n)
{
    for (const auto& f : v)
        if (f.name == n) return &f;
    return nullptr;
}

QString curYear() { return QDate::currentDate().toString(QStringLiteral("yyyy")); }

} // namespace

class TestFtpListParser : public QObject
{
    Q_OBJECT

private slots:
    // --- 格式检测 ---
    void detect_unixFormat()
    {
        auto v = FtpListParser::parse(
            "-rw-r--r-- 1 user group 100 Jul 25 10:00 a.txt\n"
            "drwxr-xr-x 2 user group 4096 Jul 26 11:00 sub\n");
        QCOMPARE(v.size(), size_t{2});
    }

    void detect_windowsFormat()
    {
        auto v = FtpListParser::parse(
            "07/20/2026  02:30 PM         1,234,567 report.pdf\n"
            "07/21/2026  09:00 AM    <DIR>          logs\n");
        QCOMPARE(v.size(), size_t{2});
    }

    void detect_emptyInput()
    {
        QVERIFY(FtpListParser::parse("").empty());
        QVERIFY(FtpListParser::parse("\n\n").empty());
    }

    void detect_unrecognizable()
    {
        // 't'/'h' 开头既非 Unix 类型位也非数字 → 无法判定格式，返回空而非崩溃
        QVERIFY(FtpListParser::parse("total 5\nhello world\n").empty());
    }

    void detect_totalHeaderSkipped()
    {
        auto v = FtpListParser::parse("total 3\n-rw-r--r-- 1 u g 100 May 1 12:00 x.bin\n");
        QCOMPARE(v.size(), size_t{1});
        QVERIFY(findEntry(v, "x.bin") != nullptr);
    }

    // --- Unix 矩阵 ---
    void unix_basicFile()
    {
        auto v = FtpListParser::parse("-rw-r--r-- 1 user group 4096 Jul 20 14:30 file.txt\n");
        QCOMPARE(v.size(), size_t{1});
        const auto* e = findEntry(v, "file.txt");
        QVERIFY(e != nullptr);
        QCOMPARE(e->isDir, false);
        QCOMPARE(e->size, uint64_t{4096});
        QCOMPARE(QString::fromStdString(e->permissions), QStringLiteral("rw-r--r--"));
        QCOMPARE(QString::fromStdString(e->dateTime), curYear() + QStringLiteral("-07-20 14:30:00"));
    }

    void unix_directory()
    {
        auto v = FtpListParser::parse("drwxr-xr-x 5 ftp ftp 4096 Aug 15 09:05 pub\n");
        const auto* e = findEntry(v, "pub");
        QVERIFY(e != nullptr);
        QCOMPARE(e->isDir, true);
        QCOMPARE(QString::fromStdString(e->permissions), QStringLiteral("rwxr-xr-x"));
        QCOMPARE(QString::fromStdString(e->dateTime), curYear() + QStringLiteral("-08-15 09:05:00"));
    }

    // 标准 ls -l 对单数字日右对齐补空格（"Jul  5"）——修复前该行被静默丢弃
    void unix_singleDigitDay()
    {
        auto v = FtpListParser::parse("-rw-r--r-- 1 user group 512 Jul  5 08:01 x.txt\n");
        const auto* e = findEntry(v, "x.txt");
        QVERIFY(e != nullptr);
        QCOMPARE(e->size, uint64_t{512});
        QCOMPARE(QString::fromStdString(e->dateTime), curYear() + QStringLiteral("-07-05 08:01:00"));
    }

    // 日期字段多空格（部分服务器输出形态）——修复前该行被静默丢弃
    void unix_multiSpaceDate()
    {
        auto v = FtpListParser::parse("-rw-r--r-- 1 u g 123 Jan  8 2020 old.log\n");
        const auto* e = findEntry(v, "old.log");
        QVERIFY(e != nullptr);
        QCOMPARE(QString::fromStdString(e->dateTime), QStringLiteral("2020-01-08 00:00:00"));
    }

    void unix_nameWithSpaces()
    {
        auto v = FtpListParser::parse("-rw-r--r-- 1 u g 123 Mar 8 2023 my file.txt\n");
        const auto* e = findEntry(v, "my file.txt");
        QVERIFY(e != nullptr);
        QCOMPARE(QString::fromStdString(e->dateTime), QStringLiteral("2023-03-08 00:00:00"));
    }

    // 符号链接 "link -> target" 只取 link 名；'l' 类型统一非目录（实现注释文档化行为）
    void unix_symlinkArrow()
    {
        auto v = FtpListParser::parse("lrwxrwxrwx 1 u g 7 Jun 2 10:00 latest -> data\n");
        const auto* e = findEntry(v, "latest");
        QVERIFY(e != nullptr);
        QCOMPARE(e->isDir, false);
        QVERIFY(findEntry(v, "data") == nullptr);
    }

    // 中文文件名逐字节保真（输入与期望同源编码）
    void unix_chineseFilename()
    {
        auto v = FtpListParser::parse("-rw-r--r-- 1 u g 88 Sep 30 2025 \xe5\x9b\xba\xe4\xbb\xb6.bin\n");
        const auto* e = findEntry(v, "\xe5\x9b\xba\xe4\xbb\xb6.bin");
        QVERIFY(e != nullptr);
        QCOMPARE(QString::fromStdString(e->dateTime), QStringLiteral("2025-09-30 00:00:00"));
    }

    // --- Windows 矩阵 ---
    void win_commaSize()
    {
        auto v = FtpListParser::parse("03/15/2026 02:30 PM    1,234 a.pdf\n");
        const auto* e = findEntry(v, "a.pdf");
        QVERIFY(e != nullptr);
        QCOMPARE(e->size, uint64_t{1234});
        QCOMPARE(e->isDir, false);
        QCOMPARE(QString::fromStdString(e->dateTime), QStringLiteral("2026-03-15 14:30:00"));
    }

    void win_dirEntry()
    {
        auto v = FtpListParser::parse("07/21/2026  09:00 AM    <DIR>          logs\n");
        const auto* e = findEntry(v, "logs");
        QVERIFY(e != nullptr);
        QCOMPARE(e->isDir, true);
        QCOMPARE(e->size, uint64_t{0});
        QCOMPARE(QString::fromStdString(e->dateTime), QStringLiteral("2026-07-21 09:00:00"));
    }

    void win_pmConversion()
    {
        auto v = FtpListParser::parse("01/02/2026 03:07 PM    55 d.txt\n");
        const auto* e = findEntry(v, "d.txt");
        QVERIFY(e != nullptr);
        QCOMPARE(QString::fromStdString(e->dateTime), QStringLiteral("2026-01-02 15:07:00"));
    }

    // 12 点边界：12:xx AM → 00:xx；12:xx PM 保持 12
    // 注意：findEntry 返回的指针指向传入 vector 内部——必须先用具名局部量承接
    // parse() 的返回值，避免内联临时 vector 立即析构导致悬垂读取。
    void win_12AmPmEdges()
    {
        auto vMid = FtpListParser::parse("06/01/2026 12:05 AM    10 midnight.txt\n");
        const auto* midnight = findEntry(vMid, "midnight.txt");
        QVERIFY(midnight != nullptr);
        QCOMPARE(QString::fromStdString(midnight->dateTime), QStringLiteral("2026-06-01 00:05:00"));

        auto vNoon = FtpListParser::parse("06/01/2026 12:05 PM    10 noon.txt\n");
        const auto* noon = findEntry(vNoon, "noon.txt");
        QVERIFY(noon != nullptr);
        QCOMPARE(QString::fromStdString(noon->dateTime), QStringLiteral("2026-06-01 12:05:00"));
    }

    void win_noLeadingZero()
    {
        auto v = FtpListParser::parse("7/5/2026 8:01 AM    99 f.txt\n");
        const auto* e = findEntry(v, "f.txt");
        QVERIFY(e != nullptr);
        QCOMPARE(e->size, uint64_t{99});
        QCOMPARE(QString::fromStdString(e->dateTime), QStringLiteral("2026-07-05 08:01:00"));
    }

    // 正则 icase：小写 am/pm 同样识别
    void win_lowercaseAmpm()
    {
        auto v = FtpListParser::parse("04/04/2024 09:15 pm    33 n.md\n");
        const auto* e = findEntry(v, "n.md");
        QVERIFY(e != nullptr);
        QCOMPARE(QString::fromStdString(e->dateTime), QStringLiteral("2024-04-04 21:15:00"));
    }

    // --- 鲁棒性 ---
    // 权限位缺失行/纯数字噪声行：不崩溃、不混入结果（首行为 Unix 定格式）
    void junkLines_skipped()
    {
        auto v = FtpListParser::parse(
            "-rw-r--r-- 1 u g 100 May 2 10:00 good.bin\n"
            "xyz garbage line\n"
            "123456\n");
        QCOMPARE(v.size(), size_t{1});
        QVERIFY(findEntry(v, "good.bin") != nullptr);
    }

    // 首条可判定行决定整份列表格式；异格式行被安全过滤
    void mixedFormats_firstWins()
    {
        auto v = FtpListParser::parse(
            "-rw-r--r-- 1 u g 200 Jun 3 09:30 unix_only.txt\n"
            "07/20/2026  02:30 PM         500 win.txt\n");
        QCOMPARE(v.size(), size_t{1});
        QVERIFY(findEntry(v, "unix_only.txt") != nullptr);
        QVERIFY(findEntry(v, "win.txt") == nullptr);
    }
};

QTEST_MAIN(TestFtpListParser)
#include "tst_ftplist_parser.moc"
