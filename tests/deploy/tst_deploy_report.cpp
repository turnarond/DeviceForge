// tst_deploy_report.cpp — 部署报告数据结构与 CSV/HTML 渲染纯函数测试
//
// v2.8 并行批量部署 Task 1：锁定报告渲染的对外契约——
//   CSV 固定表头与逐字段转义（逗号/引号翻倍/换行）、失败文件分号连接整体
//   引号包裹、HTML 表格结构与 Cancelled 态着色 class、<>& 转义、空结果
//   的合法空表头。全部为纯函数直测，零网络依赖。

#include <QtTest>
#include <QString>
#include <QStringList>
#include <string>
#include <vector>

#include "tools/FtpDeployTool/DeployReport.h"

namespace {

// 按行切分（容忍尾部无换行）；用于逐行断言 CSV 记录
QStringList splitLines(const std::string& text)
{
    QStringList lines;
    std::string cur;
    for (char c : text) {
        if (c == '\n') {
            lines << QString::fromStdString(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty())
        lines << QString::fromStdString(cur);
    return lines;
}

int countOccurrences(const QString& text, const QString& needle)
{
    int count = 0;
    for (qsizetype pos = text.indexOf(needle); pos >= 0;
         pos = text.indexOf(needle, pos + needle.size()))
        ++count;
    return count;
}

// 单台设备包装成报告（多数用例只关注单条记录的渲染）
DeployReport wrapOne(const DeviceResult& r, const char* protocol = "ftp", int concurrency = 4)
{
    DeployReport report;
    report.protocol = protocol;
    report.concurrency = concurrency;
    report.results.push_back(r);
    return report;
}

DeviceResult makeOk(const char* key)
{
    DeviceResult r;
    r.deviceKey = key;
    r.state = DeviceResult::Ok;
    r.durationMs = 1234;
    return r;
}

DeviceResult makeFailed(const char* key)
{
    DeviceResult r;
    r.deviceKey = key;
    r.state = DeviceResult::Failed;
    r.lastError = "553 Could not put file";
    r.durationMs = 5678;
    return r;
}

} // namespace

class TestDeployReport : public QObject
{
    Q_OBJECT

private slots:
    // 断言 1：两台设备一好一坏 → 首行为固定表头，其后每台设备一行
    void csv_headerAndTwoDevices()
    {
        DeployReport report;
        report.protocol = "ftp";
        report.concurrency = 4;
        report.results.push_back(makeOk("192.168.1.10:21"));
        report.results.push_back(makeFailed("192.168.1.11:21"));

        const QStringList lines = splitLines(renderReportCsv(report));
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines.at(0),
                 QStringLiteral("device,result,failed_files,last_error,duration_ms,started_at"));
        QVERIFY(lines.at(1).startsWith(QStringLiteral("192.168.1.10:21,ok,")));
        QVERIFY(lines.at(2).startsWith(QStringLiteral("192.168.1.11:21,failed,")));
    }

    // 断言 2：多个失败文件 → 字段内分号连接且整体引号包裹；清单内引号同样翻倍
    void csv_failedFilesJoinedAndQuoted()
    {
        DeviceResult r = makeFailed("10.0.0.1:22");
        r.failedFiles = {"app/config.ini", "app/main.bin", "we\"ird.log"};

        const QString csv = QString::fromStdString(renderReportCsv(wrapOne(r)));
        // 整体一个字段："app/config.ini;app/main.bin;we""ird.log"
        QVERIFY(csv.contains(QStringLiteral("\"app/config.ini;app/main.bin;we\"\"ird.log\"")));
    }

    // 断言 3：deviceKey 含逗号/引号 → 字段双引号转义（内部 " 翻倍）；
    //         last_error 含逗号与换行 → 引号包裹且换行保留在字段内
    void csv_specialCharsEscaped()
    {
        DeviceResult r;
        r.deviceKey = "dev,\"x\"";
        r.state = DeviceResult::Failed;
        r.lastError = "553 put failed\ncheck disk";
        r.durationMs = 1500;
        // startedAt 缺省 0 → 时间列留空（未开始不虚构时间）

        const QString csv = QString::fromStdString(renderReportCsv(wrapOne(r, "sftp", 2)));
        QCOMPARE(csv,
                 QStringLiteral(
                     "device,result,failed_files,last_error,duration_ms,started_at\n"
                     "\"dev,\"\"x\"\"\",failed,,\"553 put failed\ncheck disk\",1500,\n"));
    }

    // 断言 4：html 输出含 <table> 表格结构；Cancelled 态行着色 class="cancelled"；
    //         字段值中 <>& 必须转义，不出现原始尖括号
    void html_tableAndCancelledClass()
    {
        DeviceResult cancelled;
        cancelled.deviceKey = "10.1.1.1:21";
        cancelled.state = DeviceResult::Cancelled;

        DeviceResult failed;
        failed.deviceKey = "10.1.1.2:21";
        failed.state = DeviceResult::Failed;
        failed.lastError = "err <a> & <b>";
        failed.failedFiles = {"x.bin"};

        DeployReport report;
        report.protocol = "ftps";
        report.concurrency = 8;
        report.results.push_back(makeOk("10.1.1.0:21"));
        report.results.push_back(cancelled);
        report.results.push_back(failed);

        const QString html = QString::fromStdString(renderReportHtml(report));
        QVERIFY(html.contains(QStringLiteral("<table>")));
        QVERIFY(html.contains(QStringLiteral("</table>")));
        QVERIFY(html.contains(QStringLiteral("<tr><th>device</th>")));   // 首列表头
        QCOMPARE(countOccurrences(html, QStringLiteral("<th>")), 6);     // 六列
        QVERIFY(html.contains(QStringLiteral("class=\"cancelled\"")));   // 取消态着色类
        QVERIFY(html.contains(QStringLiteral("&lt;a&gt; &amp; &lt;b&gt;")));
        QVERIFY(!html.contains(QStringLiteral("<a>")));                  // 原始尖括号禁止出现
    }

    // 断言 5：空 results → CSV 仍输出合法空表头；HTML 仍输出合法空表格
    void emptyResults_stillValidHeader()
    {
        DeployReport report;

        const QString csv = QString::fromStdString(renderReportCsv(report));
        QCOMPARE(csv,
                 QStringLiteral("device,result,failed_files,last_error,duration_ms,started_at\n"));

        const QString html = QString::fromStdString(renderReportHtml(report));
        QVERIFY(html.contains(QStringLiteral("<table>")));
        QVERIFY(html.contains(QStringLiteral("</table>")));
        QCOMPARE(countOccurrences(html, QStringLiteral("<th>")), 6);
        QVERIFY(!html.contains(QStringLiteral("<td>")));                 // 无数据行
    }
};

QTEST_MAIN(TestDeployReport)
#include "tst_deploy_report.moc"
