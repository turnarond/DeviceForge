# -*- coding: utf-8 -*-
"""版本一致性校验工具（开发规范第 2、10 条）。

以 CMakeLists.txt 的 ``project(DeviceForge VERSION x.y.z ...)`` 为唯一权威，
校验 rc / README / CHANGELOG / CLAUDE / 白皮书 的版本号是否全仓一致。
发布前必须通过（退出码 0），见 ``docs/01-白皮书/版本发布与Tag规范.md``。
"""
import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Dict, List, Optional, Tuple

Extractor = Callable[[Path], Optional[str]]


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def _read_bytes(path: Path) -> str:
    """读取文本文件，自动识别 UTF-16 BOM（Windows .rc 文件常为 UTF-16 LE）。"""
    data = path.read_bytes()
    if data[:2] in (b"\xff\xfe", b"\xfe\xff"):
        return data.decode("utf-16", errors="replace")
    return data.decode("utf-8", errors="replace")


# ---------------------------------------------------------------- 提取函数

def extract_cmake(path: Path) -> Optional[str]:
    """project(DeviceForge VERSION x.y.z ...) —— 唯一权威。"""
    if not path.is_file():
        return None
    m = re.search(r"project\(\s*DeviceForge\s+VERSION\s+(\d+\.\d+\.\d+)", _read(path))
    return m.group(1) if m else None


def extract_rc(path: Path) -> Optional[str]:
    """FILEVERSION x,y,z,0 —— 四段转三段（末尾 0 为 Windows 惯例）。"""
    if not path.is_file():
        return None
    m = re.search(r"FILEVERSION\s+(\d+,\d+,\d+,\d+)", _read_bytes(path))
    if not m:
        return None
    parts = m.group(1).split(",")
    return ".".join(parts[:3]) if parts[3] == "0" else ".".join(parts)


def extract_readme(path: Path) -> Optional[str]:
    """**版本**：x.y.z"""
    if not path.is_file():
        return None
    m = re.search(r"\*\*版本\*\*：(\d+\.\d+\.\d+)", _read(path))
    return m.group(1) if m else None


def extract_changelog(path: Path) -> Optional[str]:
    """首个 `## [x.y.z]` 标题。"""
    if not path.is_file():
        return None
    m = re.search(r"^##\s*\[(\d+\.\d+\.\d+)\]", _read(path), re.MULTILINE)
    return m.group(1) if m else None


def extract_claude(path: Path) -> Optional[str]:
    """“当前版本 x.y.z”（CLAUDE.md 架构状态行）。"""
    if not path.is_file():
        return None
    m = re.search(r"当前版本\s+(\d+\.\d+\.\d+)", _read(path))
    return m.group(1) if m else None


def extract_whitepaper(path: Path) -> Optional[str]:
    """“版本：x.y.z”（白皮书版本头）。"""
    if not path.is_file():
        return None
    m = re.search(r"版本：(\d+\.\d+\.\d+)", _read(path))
    return m.group(1) if m else None


# ---------------------------------------------------------------- 来源表

# (名称, 相对路径, 提取函数, 是否权威)
DEFAULT_SOURCES: Tuple[Tuple[str, str, Extractor, bool], ...] = (
    ("cmake",    "CMakeLists.txt",                 extract_cmake,      True),
    ("rc",       "src/app/DeviceForge.rc",         extract_rc,         False),
    ("readme",   "README.md",                      extract_readme,     False),
    ("changelog","CHANGELOG.md",                   extract_changelog,  False),
    ("claude",   "CLAUDE.md",                      extract_claude,     False),
    ("白皮书",    "docs/01-白皮书/白皮书.md",        extract_whitepaper, False),
)


@dataclass
class SourceReport:
    name: str
    path: str
    found: Optional[str]


@dataclass
class CheckResult:
    reports: Dict[str, SourceReport]
    authority: Optional[str]
    consistent: bool
    mismatches: List[str]
    missing: List[str]


def check_versions(root: str = ".", sources: Tuple = DEFAULT_SOURCES) -> CheckResult:
    root_path = Path(root)
    reports: Dict[str, SourceReport] = {}
    authority: Optional[str] = None
    for name, rel, extractor, is_authority in sources:
        found = extractor(root_path / rel)
        reports[name] = SourceReport(name, rel, found)
        if is_authority:
            authority = found

    if authority is None:
        return CheckResult(reports, None, False, ["cmake(权威来源) 缺失"], [])

    mismatches: List[str] = []
    missing: List[str] = []
    for name, rep in reports.items():
        if name == "cmake":
            continue
        if rep.found is None:
            missing.append(name)
        elif rep.found != authority:
            mismatches.append(name)
    return CheckResult(reports, authority, not mismatches, mismatches, missing)


# ---------------------------------------------------------------- CLI

def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(description="版本一致性校验（以 CMakeLists.txt 为唯一权威）")
    ap.add_argument("--root", default=".", help="仓库根目录（默认当前目录）")
    args = ap.parse_args(argv)

    result = check_versions(args.root)
    for name, rep in result.reports.items():
        status = rep.found if rep.found else "缺失/未解析"
        print(f"  {name:<10} {rep.path:<36} -> {status}")

    if not result.consistent:
        print(f"[FAIL] 版本不一致（权威 {result.authority}）: {result.mismatches}")
        return 1
    if result.missing:
        print(f"[WARN] 以下来源缺失/未解析（不阻断）: {result.missing}")
    print(f"[OK] 全仓版本一致: {result.authority}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
