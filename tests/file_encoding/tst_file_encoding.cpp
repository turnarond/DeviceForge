#include <QtTest/QtTest>
#include "ui/FileEncoding.h"
#include "tools/FtpDeployTool/FtpFileInfo.h"

// 编码探测/转换纯逻辑（v2.12 F4 预览编辑器内核）：
// BOM → 严格 UTF-8 → GBK 试探 → ASCII 默认；二进制启发；行尾探测与保持；冲突谓词。
// 禁用 QTextCodec（项目未链 Qt6::Core5Compat），GBK 走 Win32 CP936。

namespace {

QByteArray gbk(const char* s)   // "中文" 的 GBK 字节 D6 D0 CE C4
{
    QByteArray b;
    b.append('\xD6'); b.append('\xD0'); b.append('\xCE'); b.append('\xC4');
    if (s) b.append(s);
    return b;
}

FtpFileInfo entry(uint64_t size, const char* mtime)
{
    FtpFileInfo f;
    f.name = "a.txt";
    f.size = size;
    f.dateTime = mtime;
    return f;
}

} // namespace

class TstFileEncoding : public QObject {
    Q_OBJECT
private slots:
    void probe_utf8Bom()
    {
        QByteArray raw;
        raw.append('\xEF'); raw.append('\xBB'); raw.append('\xBF');
        raw.append("abc");
        TextProbe p = probeTextBuffer(raw);
        QVERIFY(!p.looksBinary);
        QCOMPARE(p.codec, QStringLiteral("UTF-8"));
        QVERIFY(p.fromBom);
    }

    void probe_utf16Boms()
    {
        QByteArray le; le.append('\xFF'); le.append('\xFE'); le.append("a\x00", 2);
        QCOMPARE(probeTextBuffer(le).codec, QStringLiteral("UTF-16LE"));
        QVERIFY(probeTextBuffer(le).fromBom);
        QVERIFY(!probeTextBuffer(le).looksBinary);   // UTF-16 的 0x00 不得误判二进制

        QByteArray be; be.append('\xFE'); be.append('\xFF'); be.append("\x00""a", 2);
        QCOMPARE(probeTextBuffer(be).codec, QStringLiteral("UTF-16BE"));
    }

    void probe_strictUtf8Accepted()
    {
        QByteArray raw = QByteArray::fromHex("E4B8ADE69687");   // "中文" UTF-8
        TextProbe p = probeTextBuffer(raw);
        QVERIFY(!p.looksBinary);
        QCOMPARE(p.codec, QStringLiteral("UTF-8"));
        QVERIFY(!p.fromBom);
    }

    void probe_invalidUtf8FallsToGbk()
    {
        TextProbe p = probeTextBuffer(gbk(nullptr));            // D6 D0 CE C4：UTF-8 非法、GBK 合法
        QCOMPARE(p.codec, QStringLiteral("GBK"));
    }

    void probe_utf8ValidatorRejectsLoneContinuation()
    {
        const char bad[] = { '\xC3', '\x28' };
        QVERIFY(!utf8StrictValidate(bad, 2));
        const char ok[] = { 'a', 'b' };
        QVERIFY(utf8StrictValidate(ok, 2));
    }

    void probe_pureAsciiDefaultsUtf8()
    {
        QCOMPARE(probeTextBuffer("port=8090\n").codec, QStringLiteral("UTF-8"));
        QCOMPARE(probeTextBuffer("").codec, QStringLiteral("UTF-8"));
    }

    void probe_binaryByNul()
    {
        QByteArray raw("ab");
        raw.append('\x00');
        raw.append("cd");
        QVERIFY(probeTextBuffer(raw).looksBinary);
    }

    void probe_binaryByControlRatio()
    {
        QByteArray raw(80, '\x01');   // 无 NUL 但控制字符占比过高
        QVERIFY(probeTextBuffer(raw).looksBinary);
        QByteArray txt("level=info\npath=/export/app/alarm.log\n");
        QVERIFY(!probeTextBuffer(txt).looksBinary);
    }

    void probe_eolDetection()
    {
        QCOMPARE(probeTextBuffer("a\r\nb\r\n").eol, QStringLiteral("CRLF"));
        QCOMPARE(probeTextBuffer("a\nb\n").eol, QStringLiteral("LF"));
        QCOMPARE(probeTextBuffer("a\r\nb\nc").eol, QStringLiteral("mixed"));
        QCOMPARE(probeTextBuffer("no-newline").eol, QStringLiteral("LF"));   // 默认 LF
    }

    void gbkDecodeable_semantics()
    {
        QVERIFY(gbkDecodeable(gbk(nullptr)));
        QVERIFY(!gbkDecodeable(QByteArray::fromHex("FF40")));   // 非法 GBK 双字节组合
        QVERIFY(gbkDecodeable(QByteArray()));                    // 空=平凡可解
    }

    void decode_encode_roundtripGbkCrlf()
    {
        QByteArray raw = gbk(nullptr);
        raw.append("\r\nx");
        QString text, err;
        QVERIFY(decodeText(raw, QStringLiteral("GBK"), text, err));
        QVERIFY(err.isEmpty());
        QCOMPARE(text, QStringLiteral("中文\nx"));            // 编辑器内一律 LF
        QByteArray back = encodeText(text, QStringLiteral("GBK"), QStringLiteral("CRLF"), false);
        QCOMPARE(back, raw);                                   // 行尾与字节完全还原
    }

    void decode_gbkInvalid_returnsError()
    {
        QString text, err;
        QVERIFY(!decodeText(QByteArray::fromHex("FF40"), QStringLiteral("GBK"), text, err));
        QVERIFY(!err.isEmpty());
    }

    void encode_utf16leKeepsBomFlag()
    {
        QByteArray enc = encodeText(QStringLiteral("ab"), QStringLiteral("UTF-16LE"),
                                    QStringLiteral("LF"), true);
        QVERIFY(enc.left(2) == QByteArray("\xFF\xFE", 2));
        TextProbe p = probeTextBuffer(enc);
        QCOMPARE(p.codec, QStringLiteral("UTF-16LE"));
        QCOMPARE(p.fromBom, true);
    }

    void remoteChanged_predicates()
    {
        ConflictSnapshot snap;
        snap.size = 100; snap.mtime = "2026-10-12 10:00:00";

        QVERIFY(remoteChanged(snap, entry(101, "2026-10-12 10:00:00")));   // size 变
        QVERIFY(remoteChanged(snap, entry(100, "2026-10-12 10:05:00")));   // 分钟变
        QVERIFY(!remoteChanged(snap, entry(100, "2026-10-12 10:00:59")));  // 同分钟噪声
        QVERIFY(!remoteChanged(snap, entry(100, "2026-10-12 10:00:00")));  // 未变

        ConflictSnapshot sizeOnly; sizeOnly.size = 100; sizeOnly.mtime.clear();
        QVERIFY(!remoteChanged(sizeOnly, entry(100, "2026-10-12 10:00:00")));  // 一侧无 mtime 仅比 size
        QVERIFY(remoteChanged(sizeOnly, entry(999, "")));

        ConflictSnapshot none; none.hasSize = false; none.mtime.clear();
        QVERIFY(!remoteChanged(none, entry(10, "2026-01-01 01:01:01")));       // 皆不可比 → 放行
    }

    void limitsAndCodecs_contract()
    {
        QCOMPARE(kMaxEditBytes, uint64_t(2 * 1024 * 1024));
        QStringList cs = suggestedCodecs(QStringLiteral("GBK"));
        QVERIFY(cs.contains(QStringLiteral("UTF-8")));
        QVERIFY(cs.contains(QStringLiteral("GBK")));
        QVERIFY(cs.contains(QStringLiteral("System")));
        QVERIFY(cs.contains(QStringLiteral("Latin-1")));
        QVERIFY(cs.indexOf(QStringLiteral("GBK")) < cs.indexOf(QStringLiteral("UTF-8"))); // 探测结果置首
    }
};
QTEST_MAIN(TstFileEncoding)
#include "tst_file_encoding.moc"
