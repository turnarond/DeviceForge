# -*- coding: utf-8 -*-
"""`ftpguard` 静态守卫自测（GitHub #59，TDD 红-绿）。

三类用例：
  ① 合成源码分层用例——合规样绿、每种违规形态红（缺 EPSV / 缺 MULTICWD / 绕过装配处 /
     显式回退键值 / QUOTE 非根 URL / QUOTE 裸相对路径 / 注释与字符串诱饵 / 零证据 / 目标缺失）；
  ② 真码闸门——对仓内 `src/adapter/FtpAdapter.cpp` 断言零违规（CI 的 `python -m unittest
     discover -s tools/tests` 就是靠这条把不变式钉进流水线），并配"摘掉装配处的键必红"
     的非虚设性取证；
  ③ **预判抓不到**的负控——跑给看、如实绿，写在 `NegativeControlTest` 里，防止把守卫
     能力说成比实际更大（docstring 声明的每条限制都有对应用例）。
"""
import io
import re
import subprocess
import sys
import unittest
import uuid
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import devtools.ftpguard as fg  # noqa: E402

# 沙箱限制：临时目录放工作区内（tools/.test-tmp），且不用 mkdtemp（其随机目录被沙箱拒绝）
_TMP_ROOT = Path(__file__).resolve().parent.parent / ".test-tmp"
REAL_SOURCE = Path(__file__).resolve().parents[2] / "src" / "adapter" / "FtpAdapter.cpp"


def _make_tmp() -> Path:
    p = _TMP_ROOT / uuid.uuid4().hex
    p.mkdir(parents=True, exist_ok=True)
    return p


def _write(tmp: Path, rel: str, content: str) -> Path:
    p = tmp / rel
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(content, encoding="utf-8")
    return p


# ---------------------------------------------------------------- 合成源码夹具
def assembler(epsv=True, filemethod=True) -> str:
    lines = [
        "struct FtpAdapter::Impl {",
        "    void setupCommonOpts(CURL* curl, const std::string& url, long timeoutSec = 30) {",
        '        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());',
        "        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSec);",
    ]
    if epsv:
        lines.append("        curl_easy_setopt(curl, CURLOPT_FTP_USE_EPSV, 0L);")
    if filemethod:
        lines.append("        curl_easy_setopt(curl, CURLOPT_FTP_FILEMETHOD, CURLFTPMETHOD_MULTICWD);")
    lines += ["    }", "};"]
    return "\n".join(lines) + "\n"


def leg(name: str, *, calls_assembler=True, epsv=None, filemethod=None,
        quote=False, root_url=False, absolute_path=True, extra="") -> str:
    body = [
        f"bool FtpAdapter::{name}(const std::string& remotePath) {{",
        "    CURL* curl = curl_easy_init();",
        "    if (!curl) {",
        '        m_impl->m_lastError = "curl_easy_init() 失败";',
        "        return false;",
        "    }",
    ]
    body.append('    std::string url = ' +
                ('m_impl->buildUrl("/");' if root_url else 'm_impl->buildUrl(remotePath);'))
    if calls_assembler:
        body.append("    m_impl->setupCommonOpts(curl, url);")
    if epsv is not None:
        body.append(f"    curl_easy_setopt(curl, CURLOPT_FTP_USE_EPSV, {epsv});")
    if filemethod is not None:
        body.append(f"    curl_easy_setopt(curl, CURLOPT_FTP_FILEMETHOD, {filemethod});")
    if quote:
        if absolute_path:
            body += [
                '    std::string path = remotePath;',
                "    if (!path.empty() && path[0] != '/') path = \"/\" + path;",
                '    std::string cmd = "DELE " + path;',
            ]
        else:
            # 裸的远端路径参数直接拼进 QUOTE 命令（禁令③b 的违规形态）
            body += ['    std::string cmd = "DELE " + remotePath;']
        body += [
            "    struct curl_slist* commands = nullptr;",
            "    commands = curl_slist_append(commands, cmd.c_str());",
            "    curl_easy_setopt(curl, CURLOPT_QUOTE, commands);",
        ]
    body.append("    CURLcode res = curl_easy_perform(curl);")
    if extra:
        body.append(extra)
    body += ["    return res == 0;", "}"]
    return "\n".join(body) + "\n"


def source(*parts: str) -> str:
    return "\n".join(parts)


def kinds(report: fg.GuardReport) -> list:
    return [v.kind for v in report.violations]


# ---------------------------------------------------------------- ① 合成源码分层用例
class SyntheticSourceTest(unittest.TestCase):
    def test_装配处齐键且腿走装配_为绿(self):
        report = fg.scan_source(source(assembler(), leg("downloadFile")))
        self.assertEqual(report.status, "ok")
        self.assertEqual(report.exit_code, fg.EXIT_OK)
        self.assertEqual(len(report.legs), 1)
        self.assertTrue(report.legs[0].epsv_ok)
        self.assertTrue(report.legs[0].multicwd_ok)

    def test_装配处缺EPSV_红(self):
        report = fg.scan_source(source(assembler(epsv=False), leg("downloadFile")))
        self.assertEqual(report.status, "violation")
        self.assertIn("assembler_missing_epsv", kinds(report))

    def test_装配处缺MULTICWD_红(self):
        report = fg.scan_source(source(assembler(filemethod=False), leg("downloadFile")))
        self.assertEqual(report.status, "violation")
        self.assertIn("assembler_missing_filemethod", kinds(report))

    def test_腿绕过装配处_两条禁令一起红(self):
        report = fg.scan_source(source(assembler(), leg("downloadFile", calls_assembler=False)))
        self.assertEqual(report.status, "violation")
        self.assertIn("leg_missing_epsv", kinds(report))
        self.assertIn("leg_missing_filemethod", kinds(report))

    def test_腿自行设两键_算满足(self):
        """装配处之外的合法形态：本腿把键设成正确字面值（守卫认它，但真码要求收敛到装配处）。"""
        text = source(
            assembler(),
            leg("downloadFile", calls_assembler=False, epsv="0L",
                filemethod="CURLFTPMETHOD_MULTICWD"),
        )
        report = fg.scan_source(text)
        self.assertEqual(report.status, "ok", msg=str(report.violations))

    def test_腿把EPSV设成1_按显式回退判红(self):
        text = source(assembler(), leg("downloadFile", epsv="1L"))
        report = fg.scan_source(text)
        self.assertIn("leg_epsv_reverted", kinds(report))

    def test_腿把FILEMETHOD设成SINGLECWD_按显式回退判红(self):
        text = source(assembler(), leg("downloadFile", filemethod="CURLFTPMETHOD_SINGLECWD"))
        report = fg.scan_source(text)
        self.assertIn("leg_wrong_filemethod", kinds(report))

    def test_键只出现在注释里_不得算满足(self):
        """真码同形态：makeDirectory 的块注释把三条禁令讲了一遍却没设 EPSV——注释不是代码。"""
        text = source(
            "struct FtpAdapter::Impl {\n"
            "    void setupCommonOpts(CURL* curl, const std::string& url) {\n"
            "        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());\n"
            "        // 已经设过 CURLOPT_FTP_USE_EPSV 与 CURLFTPMETHOD_MULTICWD 了\n"
            "    }\n"
            "};\n",
            leg("downloadFile"),
        )
        report = fg.scan_source(text)
        self.assertEqual(report.status, "violation")
        self.assertIn("assembler_missing_epsv", kinds(report))
        self.assertIn("assembler_missing_filemethod", kinds(report))

    def test_键只出现在字符串里_不得算满足(self):
        """错误文案 `\"curl_easy_init() 失败\"` 不得被当成句柄 API：字符串内容一律掩掉。"""
        text = ('void FtpAdapter::noop() {\n'
                '    m_lastError = "curl_easy_init() 失败: CURLOPT_FTP_USE_EPSV";\n'
                '}\n')
        report = fg.scan_source(text)
        self.assertEqual(report.legs, [])
        self.assertEqual(report.status, "no-evidence")

    def test_QUOTE腿合规_根URL加绝对路径_为绿(self):
        text = source(assembler(), leg("deleteFile", quote=True, root_url=True, absolute_path=True))
        report = fg.scan_source(text)
        self.assertEqual(report.status, "ok", msg=str(report.violations))
        self.assertEqual(report.legs[0].quote_keys, ("CURLOPT_QUOTE",))
        self.assertTrue(report.legs[0].uses_root_url)

    def test_QUOTE腿用远端路径当URL_红(self):
        text = source(assembler(), leg("deleteFile", quote=True, root_url=False))
        report = fg.scan_source(text)
        self.assertIn("quote_missing_root_url", kinds(report))

    def test_QUOTE命令拼裸远端路径_红(self):
        text = source(assembler(),
                      leg("deleteFile", quote=True, root_url=True, absolute_path=False))
        report = fg.scan_source(text)
        self.assertIn("quote_bare_path", kinds(report))

    def test_零证据一律FAIL(self):
        """解析不到任何腿（文件里没有 FTP I/O）不得当成绿。"""
        report = fg.scan_source("int main() { return 0; }\n")
        self.assertEqual(report.status, "no-evidence")
        self.assertEqual(report.exit_code, fg.EXIT_NO_EVIDENCE)

    def test_不经init的perform腿也在审计范围内(self):
        """duphandle 形态兜底：只 perform 不 init 的腿照样要求两键。"""
        text = source(
            "struct FtpAdapter::Impl {\n"
            "    void setupCommonOpts(CURL* curl) {\n"
            "        curl_easy_setopt(curl, CURLOPT_URL, curl);\n"
            "    }\n"
            "};\n",
            "bool FtpAdapter::dupLeg() {\n"
            "    CURL* curl = curl_easy_duphandle(g_pool);\n",
            "    CURLcode res = curl_easy_perform(curl);\n    return res == 0;\n}\n",
        )
        report = fg.scan_source(text)
        self.assertIn("leg_missing_epsv", kinds(report))
        self.assertIn("leg_missing_filemethod", kinds(report))

    def test_跨文件的同名装配函数按未满足处理_宁误报不误绿(self):
        """只读本文件：装配函数不在本文件里定义时，调用它不算拿到键（保守侧）。"""
        text = source(leg("downloadFile"))
        report = fg.scan_source(text)
        self.assertFalse(report.assembler_found)
        self.assertIn("leg_missing_epsv", kinds(report))


class ExitCodeAndIoTest(unittest.TestCase):
    def _run(self, text: str) -> int:
        path = _write(_make_tmp(), "Sample.cpp", text)
        with redirect_stdout(io.TextIOWrapper(io.BytesIO(), encoding="utf-8")):
            return fg.main(["--source", str(path)])

    def test_绿_退出码0(self):
        self.assertEqual(self._run(source(assembler(), leg("downloadFile"))), fg.EXIT_OK)

    def test_违规_退出码1(self):
        code = self._run(source(assembler(epsv=False), leg("downloadFile")))
        self.assertEqual(code, fg.EXIT_VIOLATION)

    def test_目标缺失_退出码2(self):
        missing = _make_tmp() / "NoSuchFile.cpp"
        with redirect_stdout(io.TextIOWrapper(io.BytesIO(), encoding="utf-8")):
            code = fg.main(["--source", str(missing)])
        self.assertEqual(code, fg.EXIT_TARGET_MISSING)

    def test_零证据_退出码3(self):
        self.assertEqual(self._run("int main() { return 0; }\n"), fg.EXIT_NO_EVIDENCE)

    def test_默认目标是仓内FtpAdapter(self):
        self.assertEqual(fg.DEFAULT_SOURCE, REAL_SOURCE.resolve())

    def test_main在非UTF8码页下不崩且保留语义退出码(self):
        """输出契约=UTF-8（tools/devtools/AGENTS.md 第 6 条）：详情含中文，cp1252 码页不得让 print 抛。"""
        path = _write(_make_tmp(), "Sample.cpp", source(assembler(epsv=False), leg("downloadFile")))
        original = sys.stdout
        sys.stdout = io.TextIOWrapper(io.BytesIO(), encoding="cp1252")
        try:
            code = fg.main(["--source", str(path)])
        finally:
            sys.stdout = original
        self.assertEqual(code, fg.EXIT_VIOLATION)

    def test_cli子命令挂载(self):
        """`python tools/devtools/cli.py ftpguard …` 与独立运行同退出码口径。"""
        from devtools import cli
        good = _write(_make_tmp(), "Good.cpp", source(assembler(), leg("downloadFile")))
        bad = _write(_make_tmp(), "Bad.cpp", source(assembler(epsv=False), leg("downloadFile")))
        buffer = io.TextIOWrapper(io.BytesIO(), encoding="utf-8")
        original = sys.stdout
        sys.stdout = buffer
        try:
            ok_code = cli.main(["ftpguard", "--source", str(good)])
            bad_code = cli.main(["ftpguard", "--source", str(bad)])
        finally:
            sys.stdout = original
        self.assertEqual(ok_code, fg.EXIT_OK)
        self.assertEqual(bad_code, fg.EXIT_VIOLATION)


class NegativeControlTest(unittest.TestCase):
    """**预判抓不到**的形态：跑给看，如实绿。docstring 声明的限制逐条对应一个用例。"""

    def test_NC1_死代码里的键当前有效_守卫抓不到(self):
        text = source(
            "struct FtpAdapter::Impl {\n"
            "    void setupCommonOpts(CURL* curl, const std::string& url) {\n"
            "        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());\n"
            "#if 0\n"
            "        curl_easy_setopt(curl, CURLOPT_FTP_USE_EPSV, 0L);\n"
            "        curl_easy_setopt(curl, CURLOPT_FTP_FILEMETHOD, CURLFTPMETHOD_MULTICWD);\n"
            "#endif\n"
            "    }\n"
            "};\n",
            leg("downloadFile"),
        )
        report = fg.scan_source(text)
        self.assertEqual(report.status, "ok")   # 如实绿：静态文本看不到预处理取舍

    def test_NC2_绝对化做在错的变量上_守卫抓不到(self):
        """禁令③b 只证明"做过首斜杠绝对化"，不证明绝对化用在了**对的**变量上。

        这里 QUOTE 删的是本地缓存路径（语义错），但该变量确实被绝对化过，形态判据吃不下
        "路径语义对不对"这层——守卫如实绿。
        """
        text = source(
            assembler(),
            "bool FtpAdapter::deleteFile(const std::string& remotePath,\n"
            "                            const std::string& localCache) {\n"
            "    CURL* curl = curl_easy_init();\n"
            '    std::string url = m_impl->buildUrl("/");\n'
            "    m_impl->setupCommonOpts(curl, url);\n"
            "    std::string path = localCache;\n"
            "    if (!path.empty() && path[0] != '/') path = \"/\" + path;\n"
            '    std::string cmd = "DELE " + path;\n'          # 用错了变量：本地缓存而非远端路径
            "    struct curl_slist* commands = nullptr;\n"
            "    commands = curl_slist_append(commands, cmd.c_str());\n"
            "    curl_easy_setopt(curl, CURLOPT_QUOTE, commands);\n"
            "    CURLcode res = curl_easy_perform(curl);\n"
            "    return res == 0;\n"
            "}\n",
        )
        report = fg.scan_source(text)
        self.assertEqual(report.status, "ok")   # 如实绿

    def test_NC3_键值取自变量_守卫既不认也不报回退(self):
        """字面量口径的限制：装配处之外把 EPSV 设成运行期变量时，守卫看不到真实值。"""
        text = source(
            assembler(),
            leg("downloadFile", extra="    curl_easy_setopt(curl, CURLOPT_FTP_USE_EPSV, m_epsvFlag);"),
        )
        report = fg.scan_source(text)
        self.assertEqual(report.status, "ok")   # 腿仍显示"有(装配)"；m_epsvFlag=true 时守卫不会红
        self.assertIn("有(装配)", "\n".join(fg.format_report(report)))


class RealSourceGateTest(unittest.TestCase):
    """真码闸门：CI 靠这条把 SylixOS 三禁的不变式钉进流水线（ci.yml:21）。"""

    def test_真码零违规且腿集合非空(self):
        text = REAL_SOURCE.read_text(encoding="utf-8")
        report = fg.scan_source(text, source=str(REAL_SOURCE))
        self.assertEqual(report.status, "ok",
                         msg="\n".join(fg.format_report(report)))
        self.assertEqual(report.exit_code, fg.EXIT_OK)
        # 非虚设性①：腿集合必须真是 11 条建句柄腿，不是"什么都没查到"
        self.assertEqual(len(report.legs), 11)
        names = {leg.function.split("::")[-1] for leg in report.legs}
        self.assertSetEqual(names, {
            "connect", "uploadFile", "downloadFile", "readFileToMemory",
            "writeFileFromMemory", "listDirectory", "listDirectoryParsed",
            "deleteFile", "deleteDirectory", "renameFile", "makeDirectory",
        })
        for leg in report.legs:
            with self.subTest(leg=leg.function):
                self.assertTrue(leg.epsv_ok, msg=f"{leg.function} 缺禁令①")
                self.assertTrue(leg.multicwd_ok, msg=f"{leg.function} 缺禁令②")
            if leg.quote_keys:
                self.assertTrue(leg.uses_root_url, msg=f"{leg.function} 违反禁令③a")
                self.assertTrue(all(ok for _, ok in leg.quote_commands),
                                msg=f"{leg.function} 违反禁令③b")
        quoted = {leg.function.split("::")[-1] for leg in report.legs if leg.quote_keys}
        self.assertSetEqual(quoted, {"deleteFile", "deleteDirectory", "renameFile",
                                     "makeDirectory"})

    def test_真码装配处单源持有两键(self):
        """禁令①② 的"单源"落点：装配函数在册且自身不缺任一键。"""
        text = REAL_SOURCE.read_text(encoding="utf-8")
        report = fg.scan_source(text, source=str(REAL_SOURCE))
        self.assertTrue(report.assembler_found)
        self.assertNotIn("assembler_missing_epsv", kinds(report))
        self.assertNotIn("assembler_missing_filemethod", kinds(report))

    def test_真码摘掉装配处的EPSV行必须变红(self):
        """非虚设性②：整改后的绿必须是"键真在装配处"换来的，删一行就得红。"""
        text = REAL_SOURCE.read_text(encoding="utf-8")
        mutated = text.replace(
            "        curl_easy_setopt(curl, CURLOPT_FTP_USE_EPSV, 0L);\n", "", 1)
        self.assertNotEqual(mutated, text, "夹具失效：装配处未找到 EPSV 设置行")
        report = fg.scan_source(mutated, source=str(REAL_SOURCE))
        self.assertEqual(report.status, "violation")
        self.assertIn("assembler_missing_epsv", kinds(report))

    def test_真码把上传腿改回绕过装配处必须变红(self):
        """非虚设性③：把上传腿的装配调用改名（等价于绕过装配处），两键必须立刻报红。

        用正则改名而非整段文本替换，免得判据绑死缩进。
        """
        text = REAL_SOURCE.read_text(encoding="utf-8")
        mutated, n = re.subn(r"m_impl->setupCommonOpts\(\s*curl,\s*url,\s*Impl::kBulkTransferTimeoutSec",
                             "m_impl->bypassingTheAssembler(curl, url, Impl::kBulkTransferTimeoutSec",
                             text, count=1)
        self.assertEqual(n, 1, "夹具失效：未找到上传腿的装配调用")
        report = fg.scan_source(mutated, source=str(REAL_SOURCE))
        self.assertEqual(report.status, "violation")
        self.assertIn("leg_missing_epsv", kinds(report))
        self.assertIn("leg_missing_filemethod", kinds(report))
        self.assertIn("FtpAdapter::uploadFile",
                      [v.function for v in report.violations])

    def test_命令行闸门按UTF8读退出码0(self):
        proc = subprocess.run(
            [sys.executable, str(Path(__file__).resolve().parents[2]
                                 / "tools" / "devtools" / "ftpguard.py")],
            capture_output=True, encoding="utf-8", cwd=str(REAL_SOURCE.parents[2]))
        self.assertEqual(proc.returncode, fg.EXIT_OK, msg=proc.stdout + proc.stderr)
        self.assertIn("[OK]", proc.stdout)


if __name__ == "__main__":
    unittest.main()
