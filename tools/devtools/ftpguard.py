# -*- coding: utf-8 -*-
"""SylixOS FTP 三禁静态守卫（GitHub #59）。

判据来源：根 `AGENTS.md` 反模式「SylixOS FTP 三条勿回退」——
  禁令① `CURLOPT_FTP_USE_EPSV = 0`（强制 PASV）
  禁令② `CURLOPT_FTP_FILEMETHOD = CURLFTPMETHOD_MULTICWD`（逐级 CWD，服务端不支持多级路径）
  禁令③ QUOTE 命令用根 URL + 绝对路径

被检对象：`src/adapter/FtpAdapter.cpp`（可用 `--source` 指定别的文件或合成源码）。

审计单元「腿」的定义：**函数体内出现 `curl_easy_init` 或 `curl_easy_perform`** 即一条腿
（连 `perform` 一起认，是为了兜住不经 `init` 拿到句柄的形态，例如 `curl_easy_duphandle`）。
只委托他腿、自己不建句柄的函数（`uploadFolder`/`clearRemoteDirectory`）不算腿——
它们的禁令由持有句柄的那条腿在本守卫内单点钉住。

禁令①② 的满足形态**只有两种**：调用单源装配函数（默认名 `setupCommonOpts`），
或在本腿内自己把该键设成正确字面值。两者都没有即违规。装配函数自身缺任一键同样是违规
（一处失守会让所有调用方一起失守），此时违规**记在装配函数上**——走装配的调用腿不各自重复报，
免得一改装配函数就刷出十几条同因红（实测：装配缺 EPSV 时那条违规只出现一次）。
显式把键设成错误值（`EPSV=1`、`CURLFTPMETHOD_SINGLECWD/NOCWD`）也判违规，不当作"已满足"。

禁令③ 的静态判据（**窄口径，宁可误报不可误绿**）：
  ③a 设过 `CURLOPT_QUOTE`/`CURLOPT_PREQUOTE`/`CURLOPT_POSTQUOTE` 的腿，其请求 URL 必须由
     根路径字面量装配（`buildUrl("/")` 或 `buildFtpUrl(..., "/")`），不得把远端路径塞进 URL；
  ③b 交给 `curl_slist_append` 的每条命令串，其变量经本腿内赋值展开后必须出现首斜杠绝对化
     惯用法（`"/" + x`，或 `x[0] != '/'` / `x.front() != '/'` 判据，或调用在册的路径助手），
     即不得直接拼裸的远端路径参数。

本守卫**做不到**的事（如实声明，不得当成已覆盖）：三条限制各配了一个"预判抓不到"的
负控用例跑给看（`test_NC1_死代码里的键当前有效_守卫抓不到`、`test_NC2_绝对化做在错的变量上_守卫抓不到`、
`test_NC3_键值取自变量_守卫既不认也不报回退`），另两条走分层用例与保守红兜住
（跨文件那条见 `test_跨文件的同名装配函数按未满足处理_宁误报不误绿`；协议语义本就不在静态判据内，
无对应用例，也不得声称覆盖）：
  · 死代码与预处理分支：写在 `#if 0` 或未编译分支里的 setopt 会被当作存在；
  · 跨文件供能：只读这一个文件。装配函数若被搬去别的翻译单元，本文件内查无定义，
     所有仅"调用装配"的腿会一起判**违规**（保守红，不会把缺键算成满足）；
     代价是搬家的当天必须同步本守卫的目标文件清单；
  · 运行期取值：键值来自变量/常量时按字面量口径判，不追该变量的真实来源与最终值；
  · 语义正确性：③b 只证明"做过首斜杠绝对化"，不证明绝对化用对了变量、也不证明路径对服务端可达；
  · 协议语义：不判断某条腿连的是不是 SylixOS 设备，也不观察 perform 的实际往返。

退出码（供 CI 使用，语义化）：
  0 干净 / 1 命中违规 / 2 目标文件缺失或不可读 / 3 零证据（解析一条腿都没抓到）
零证据一律 FAIL——静默收集 0 条腿却报绿的守卫是最坏形态。
"""
import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOURCE = REPO_ROOT / "src" / "adapter" / "FtpAdapter.cpp"
DEFAULT_ASSEMBLER = "setupCommonOpts"

EXIT_OK = 0
EXIT_VIOLATION = 1
EXIT_TARGET_MISSING = 2
EXIT_NO_EVIDENCE = 3

EPSV_KEY = "CURLOPT_FTP_USE_EPSV"
FILEMETHOD_KEY = "CURLOPT_FTP_FILEMETHOD"
MULTICWD_VALUE = "CURLFTPMETHOD_MULTICWD"
QUOTE_KEYS = ("CURLOPT_PREQUOTE", "CURLOPT_POSTQUOTE", "CURLOPT_QUOTE")
FTP_IO_MARKERS = ("curl_easy_init", "curl_easy_perform")

# ------------------------------------------------------------------ 在册判据（纯逻辑）
_RE_EPSV = re.compile(r"\bCURLOPT_FTP_USE_EPSV\s*,\s*(\d+)\s*L?")
_RE_FILEMETHOD = re.compile(r"\bCURLOPT_FTP_FILEMETHOD\s*,\s*([A-Za-z_]\w*)")
_RE_QUOTE = re.compile(r"\b(" + "|".join(QUOTE_KEYS) + r")\b")
_RE_SLIST = re.compile(r"curl_slist_append\s*\((.*)\)")
_RE_ASSIGN = re.compile(r"\b([A-Za-z_]\w*)\s*=(?!=)\s*([^;]+);")
_RE_IDENT = re.compile(r"[A-Za-z_]\w*")
_RE_FIRST_TOKEN = re.compile(r"\s*([A-Za-z_]\w*)")
_RE_NAME_BEFORE_PAREN = re.compile(r"([A-Za-z_][\w:]*)\s*(?:<[^<>]*>)?\s*$")

ROOT_URL_PATTERNS: Tuple[str, ...] = (
    r'\bbuildUrl\s*\(\s*"/"\s*\)',
    r'\bbuildFtpUrl\s*\([^;]*,\s*"/"\s*\)',
)
ABSOLUTE_IDIOM_PATTERNS: Tuple[str, ...] = (
    r'"/"\s*\+',
    r'\[\s*0\s*\]\s*!=\s*"/"',
    r'\bfront\s*\(\s*\)\s*!=\s*"/"',
    r'\bstarts_with\s*\(\s*"/"',
    r"\badapter_internal::ftpDirectoryPathForListing\b",
)

# 开括号前首个/末个 token 落在这里就不是函数定义（控制流 / 类型 / 访问说明符）
_NON_FUNCTION_HEAD = frozenset({
    "if", "for", "while", "switch", "catch", "return", "else", "do", "struct",
    "class", "union", "enum", "namespace", "public", "private", "protected",
    "case", "default", "try", "template", "requires", "new", "delete",
})


# ------------------------------------------------------------------ 数据结构
@dataclass(frozen=True)
class Violation:
    kind: str
    function: str
    line: Optional[int]
    detail: str


@dataclass
class Leg:
    """一条会做 FTP I/O 的 libcurl 腿。"""
    function: str
    line: Optional[int]                            # 句柄（或 perform）所在行，消息定位用
    start_line: int
    calls_assembler: bool
    epsv_values: Tuple[str, ...] = ()
    filemethod_values: Tuple[str, ...] = ()
    quote_keys: Tuple[str, ...] = ()
    uses_root_url: bool = False
    quote_commands: Tuple[Tuple[str, bool], ...] = ()   # (命令串原文, 是否见过绝对化惯用法)

    @property
    def epsv_ok(self) -> bool:
        return self.calls_assembler or "0" in self.epsv_values

    @property
    def multicwd_ok(self) -> bool:
        return self.calls_assembler or MULTICWD_VALUE in self.filemethod_values


@dataclass
class GuardReport:
    status: str                       # 'ok' | 'violation' | 'no-evidence' | 'target-missing'
    source: str
    legs: List[Leg] = field(default_factory=list)
    violations: List[Violation] = field(default_factory=list)
    assembler: str = DEFAULT_ASSEMBLER
    assembler_found: bool = False
    assembler_line: Optional[int] = None
    functions_parsed: int = 0

    @property
    def exit_code(self) -> int:
        return {
            "ok": EXIT_OK,
            "violation": EXIT_VIOLATION,
            "no-evidence": EXIT_NO_EVIDENCE,
            "target-missing": EXIT_TARGET_MISSING,
        }[self.status]


# ------------------------------------------------------------------ 纯逻辑：源码清洗
def scrub(text: str) -> Tuple[List[str], List[str]]:
    """把源码切成两份**等行号**的视图。

    masked   —— 去注释、字符串/字符字面量的**内容**替换为 ``\\x01``：用于检测 setopt 键与
                句柄 API。这样 ``m_lastError = "curl_easy_init() 失败"`` 与注释里的
                ``CURLOPT_FTP_USE_EPSV`` 都不会被误判成真的调用/真的设置。
    stripped —— 去注释但**保留字面量内容**：用于 ③a 根 URL 与 ③b 绝对路径的字面量判据。
    """
    masked_lines: List[str] = []
    stripped_lines: List[str] = []
    mask: List[str] = []
    strip: List[str] = []
    state = "code"          # code | line_comment | block_comment | string | char
    i = 0
    n = len(text)

    def flush() -> None:
        masked_lines.append("".join(mask))
        stripped_lines.append("".join(strip))
        del mask[:]
        del strip[:]

    while i < n:
        ch = text[i]
        nxt = text[i + 1] if i + 1 < n else ""

        if ch == "\n":
            flush()
            if state == "line_comment":
                state = "code"
            i += 1
            continue

        if state == "line_comment":
            mask.append(" ")
            strip.append(" ")
            i += 1
            continue

        if state == "block_comment":
            if ch == "*" and nxt == "/":
                mask.append("  ")
                strip.append("  ")
                state = "code"
                i += 2
                continue
            mask.append(" ")
            strip.append(" ")
            i += 1
            continue

        if state in ("string", "char"):
            closing = '"' if state == "string" else "'"
            if ch == "\\":
                mask.append("\\\x01" if nxt != "\n" else "\\")
                strip.append("\\" + (nxt if nxt != "\n" else ""))
                if nxt != "\n":
                    i += 2
                else:
                    i += 1
                continue
            if ch == closing:
                mask.append(ch)
                strip.append(ch)
                state = "code"
                i += 1
                continue
            mask.append("\x01")
            strip.append(ch)
            i += 1
            continue

        # state == "code"
        if ch == "/" and nxt == "/":
            state = "line_comment"
            i += 2
            continue
        if ch == "/" and nxt == "*":
            state = "block_comment"
            i += 2
            continue
        mask.append(ch)
        strip.append(ch)
        if ch == '"':
            state = "string"
        elif ch == "'":
            state = "char"
        i += 1

    if mask or strip:
        flush()
    return masked_lines, stripped_lines


def function_name_of(pending: str) -> Optional[str]:
    """由开括号前累计的文本判定是否函数定义头；是则返回函数名，否则 None。"""
    text = pending.strip()
    if "(" not in text:
        return None
    head = _RE_FIRST_TOKEN.match(text)
    if head and head.group(1) in _NON_FUNCTION_HEAD:
        return None
    idx = text.rfind("(")
    m = _RE_NAME_BEFORE_PAREN.search(text[:idx])
    if not m:
        return None
    name = m.group(1)
    parts = [p for p in name.split("::") if p]
    if not parts or parts[-1] in _NON_FUNCTION_HEAD:
        return None
    return name


def parse_function_spans(masked: Sequence[str]) -> List[List]:
    """花括号扫描 → [[函数名, 起始行, 结束行], …]（行号 1 起）。

    非函数作用域（namespace / struct / if / lambda …）只用于配平深度，不进结果；
    函数名取最后一个 `(` 之前的限定标识符，`{` 单独成行的旧式头也能吃到（累计串跨行）。
    """
    spans: List[List] = []
    stack: List[Optional[List]] = []     # 元素=函数作用域的 span 引用；None=非函数作用域
    pending: List[str] = []
    for lineno, line in enumerate(masked, 1):
        for ch in line:
            if ch == "{":
                name = function_name_of("".join(pending))
                if name is None:
                    stack.append(None)
                else:
                    span = [name, lineno, lineno]
                    spans.append(span)
                    stack.append(span)
                pending = []
            elif ch == "}":
                if stack:
                    span = stack.pop()
                    if span is not None:
                        span[2] = lineno
                pending = []
            elif ch == ";":
                pending = []
            else:
                pending.append(ch)
        pending.append(" ")
    return spans


# ------------------------------------------------------------------ 纯逻辑：赋值展开
def collect_assignments(stripped_body: str) -> Dict[str, List[str]]:
    """本函数体内所有 `name = rhs;` 赋值（同一变量可多条，取并集）。"""
    table: Dict[str, List[str]] = {}
    for name, rhs in _RE_ASSIGN.findall(stripped_body):
        table.setdefault(name, []).append(rhs.strip())
    return table


def expand_expression(expr: str, table: Dict[str, List[str]], limit: int = 6) -> str:
    """把命令串里的变量按本腿内赋值 RHS 逐层展开，返回展开后的文本合集。"""
    texts: List[str] = [expr]
    seen = set(_RE_IDENT.findall(expr))
    frontier = list(seen)
    for _ in range(limit):
        if not frontier:
            break
        nxt: List[str] = []
        for cur in frontier:
            for rhs in table.get(cur, ()):
                texts.append(rhs)
                for ident in _RE_IDENT.findall(rhs):
                    if ident not in seen:
                        seen.add(ident)
                        nxt.append(ident)
        frontier = nxt
    return " ; ".join(texts)


def has_pattern(text: str, patterns: Sequence[str]) -> bool:
    return any(re.search(p, text) for p in patterns)


# ------------------------------------------------------------------ 纯逻辑：主扫描
def scan_source(text: str,
                source: str = "<source>",
                assembler: str = DEFAULT_ASSEMBLER) -> GuardReport:
    """纯函数：源码文本 → 守卫结论（不做任何 IO，便于脱盘测试）。"""
    masked, stripped = scrub(text)
    spans = parse_function_spans(masked)
    report = GuardReport(status="ok", source=source, assembler=assembler)
    report.functions_parsed = len(spans)

    def body_of(start: int, end: int) -> Tuple[str, str]:
        return ("\n".join(masked[start - 1:end]),
                "\n".join(stripped[start - 1:end]))

    # 1) 装配函数自检
    assembler_span = None
    for name, start, end in spans:
        if name == assembler or name.split("::")[-1] == assembler:
            assembler_span = (name, start, end)
            break
    if assembler_span is not None:
        report.assembler_found = True
        report.assembler_line = assembler_span[1]
        masked_body, _ = body_of(assembler_span[1], assembler_span[2])
        if "0" not in _RE_EPSV.findall(masked_body):
            report.violations.append(Violation(
                kind="assembler_missing_epsv",
                function=assembler_span[0], line=assembler_span[1],
                detail=(f"单源装配函数 {assembler}（{source}:{assembler_span[1]}）"
                        f"未设 {EPSV_KEY}=0，禁令①无单源保证")))
        if MULTICWD_VALUE not in _RE_FILEMETHOD.findall(masked_body):
            report.violations.append(Violation(
                kind="assembler_missing_filemethod",
                function=assembler_span[0], line=assembler_span[1],
                detail=(f"单源装配函数 {assembler}（{source}:{assembler_span[1]}）"
                        f"未设 {FILEMETHOD_KEY}={MULTICWD_VALUE}，禁令②无单源保证")))

    # 2) 逐腿审计
    for name, start, end in spans:
        masked_body, stripped_body = body_of(start, end)
        if not any(marker in masked_body for marker in FTP_IO_MARKERS):
            continue
        line = None
        for marker in FTP_IO_MARKERS:
            m = re.search(r"\b" + re.escape(marker), masked_body)
            if m:
                line = start + masked_body[:m.start()].count("\n")
                break
        table = collect_assignments(stripped_body)
        quote_keys = tuple(sorted(set(_RE_QUOTE.findall(masked_body))))
        quote_commands: List[Tuple[str, bool]] = []
        if quote_keys:
            for expr in _RE_SLIST.findall(stripped_body):
                expanded = expand_expression(expr, table)
                quote_commands.append((expr.strip(), has_pattern(expanded, ABSOLUTE_IDIOM_PATTERNS)))
        leg = Leg(
            function=name,
            line=line,
            start_line=start,
            calls_assembler=(assembler_span is not None
                             and re.search(r"\b" + re.escape(assembler) + r"\s*\(", masked_body)
                             is not None),
            epsv_values=tuple(_RE_EPSV.findall(masked_body)),
            filemethod_values=tuple(_RE_FILEMETHOD.findall(masked_body)),
            quote_keys=quote_keys,
            uses_root_url=has_pattern(stripped_body, ROOT_URL_PATTERNS),
            quote_commands=tuple(quote_commands),
        )
        report.legs.append(leg)
        where = f"{source}:{line if line else start}"
        if not leg.epsv_ok:
            report.violations.append(Violation(
                kind="leg_missing_epsv", function=name, line=leg.line,
                detail=(f"腿 {name}（{where}）既未调用装配函数 {assembler}，"
                        f"也未自行设 {EPSV_KEY}=0 —— 禁令①失守")))
        elif any(v != "0" for v in leg.epsv_values):
            bad = sorted({v for v in leg.epsv_values if v != "0"})
            report.violations.append(Violation(
                kind="leg_epsv_reverted", function=name, line=leg.line,
                detail=(f"腿 {name}（{where}）把 {EPSV_KEY} 设成非零值 {bad} —— "
                        f"禁令①被显式回退（装配函数设过 0 也会被这一行覆盖）")))
        if not leg.multicwd_ok:
            report.violations.append(Violation(
                kind="leg_missing_filemethod", function=name, line=leg.line,
                detail=(f"腿 {name}（{where}）既未调用装配函数 {assembler}，"
                        f"也未自行设 {FILEMETHOD_KEY}={MULTICWD_VALUE} —— 禁令②失守")))
        elif any(v != MULTICWD_VALUE for v in leg.filemethod_values):
            bad = sorted({v for v in leg.filemethod_values if v != MULTICWD_VALUE})
            report.violations.append(Violation(
                kind="leg_wrong_filemethod", function=name, line=leg.line,
                detail=(f"腿 {name}（{where}）把 {FILEMETHOD_KEY} 设成 {bad} —— 禁令②被显式回退")))
        if leg.quote_keys and not leg.uses_root_url:
            report.violations.append(Violation(
                kind="quote_missing_root_url", function=name, line=leg.line,
                detail=(f"腿 {name}（{where}）设了 {'/'.join(leg.quote_keys)}，"
                        f"但请求 URL 未由根路径字面量装配（禁令③a：QUOTE 必须配根 URL）")))
        for expr, absolutized in leg.quote_commands:
            if not absolutized:
                report.violations.append(Violation(
                    kind="quote_bare_path", function=name, line=leg.line,
                    detail=(f"腿 {name}（{where}）的 QUOTE 命令串 {expr!r} 经赋值展开后"
                            f"未见首斜杠绝对化惯用法（禁令③b：QUOTE 必须用绝对路径）")))

    if not report.legs:
        report.status = "no-evidence"
    elif report.violations:
        report.status = "violation"
    else:
        report.status = "ok"
    return report


# ------------------------------------------------------------------ 呈现（纯逻辑）
def _source_label(ok: bool, via_assembler: bool, via_inline: bool) -> str:
    """键的保证来源：标明是"装配处给的"还是"本腿自设的"，绿格也不许含糊。"""
    if not ok:
        return "缺"
    if via_assembler and via_inline:
        return "有(装配+自设)"
    if via_assembler:
        return "有(装配)"
    return "有(自设)"


def format_report(report: GuardReport) -> List[str]:
    if report.status == "target-missing":
        return [f"[FAIL] 目标文件缺失或不可读: {report.source}"]

    lines: List[str] = [f"SylixOS FTP 三禁静态守卫 — {report.source}"]
    if report.assembler_found:
        lines.append(f"  单源装配函数 {report.assembler}（:{report.assembler_line}）在册")
    else:
        lines.append(f"  未找到装配函数 {report.assembler}（各腿须自行设键）")
    lines.append(f"  审计腿数 {len(report.legs)}（解析到函数 {report.functions_parsed} 个）")
    for leg in report.legs:
        epsv = _source_label(leg.epsv_ok, leg.calls_assembler, "0" in leg.epsv_values)
        cwd = _source_label(leg.multicwd_ok, leg.calls_assembler,
                            MULTICWD_VALUE in leg.filemethod_values)
        quote = "—"
        if leg.quote_keys:
            abs_ok = all(ok for _, ok in leg.quote_commands)
            quote = f"根URL{'✓' if leg.uses_root_url else '✗'}/绝对路径{'✓' if abs_ok else '✗'}"
        lines.append("    {:<40} :{:<5} EPSV={} MULTICWD={} QUOTE={}".format(
            leg.function, leg.line, epsv, cwd, quote))
    for violation in report.violations:
        lines.append(f"[FAIL] {violation.detail}")
    if report.status == "no-evidence":
        lines.append("[FAIL] 零证据：未解析到任何含 "
                     + "/".join(FTP_IO_MARKERS)
                     + " 的函数腿，守卫拒绝把「什么都没查到」报成绿")
    elif report.status == "ok":
        lines.append(f"[OK] SylixOS 三禁由 {report.assembler} 单源保证，"
                     f"{len(report.legs)} 条腿齐备（QUOTE 腿 {sum(1 for l in report.legs if l.quote_keys)} 条）")
    else:
        lines.append(f"[SUMMARY] 违规 {len(report.violations)} 条 / 腿 {len(report.legs)} 条")
    return lines


# ------------------------------------------------------------------ CLI
def main(argv: Optional[Sequence[str]] = None) -> int:
    # 输出码页钉 UTF-8：违规详情含中文，cp1252 一类码页会让 print 抛 UnicodeEncodeError——
    # 按 tools/devtools/AGENTS.md 第 6 条，那会把"检查未通过"冒充成"工具自己崩了"。
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

    ap = argparse.ArgumentParser(description="SylixOS FTP 三禁静态守卫（GitHub #59）")
    ap.add_argument("--source", default=str(DEFAULT_SOURCE),
                    help=f"待检源码（默认 {DEFAULT_SOURCE}）")
    ap.add_argument("--assembler", default=DEFAULT_ASSEMBLER,
                    help=f"单源装配函数名（默认 {DEFAULT_ASSEMBLER}）")
    args = ap.parse_args(list(argv) if argv is not None else None)

    path = Path(args.source)
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        report = GuardReport(status="target-missing", source=str(path), assembler=args.assembler)
        report.violations.append(Violation("target_missing", "-", None, str(exc)))
        print(format_report(report)[0] + f"（{exc.__class__.__name__}）")
        return report.exit_code

    report = scan_source(text, source=str(path), assembler=args.assembler)
    for line in format_report(report):
        print(line)
    return report.exit_code


if __name__ == "__main__":
    sys.exit(main())
