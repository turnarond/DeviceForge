# -*- coding: utf-8 -*-
"""旧色值静态守卫（Instrument 视觉语言重构）。

用途：确保 src/ 下 .qss/.cpp/.h 不再出现 Instrument 之前的旧色值
（设计文档 §Global Constraints 清单）。旧色值散落在代码内嵌 setStyleSheet /
模型前景 / 富文本链接等处，QSS 重写不会自动清掉它们——本守卫是整改清单与 CI 门禁。

判据（窄口径，注释里的旧色值也算违规——注释同样会误导后续维护者）：
  文本小写化后包含任一旧色值即命中。
  名单：见 LEGACY_COLORS（与设计文档 Global Constraints 逐字一致）。

扫描范围：默认 根/src 下 *.qss/*.cpp/*.h（可用 --scan-dirs 覆盖，逗号分隔多目录）。

退出码（供 CI 使用，语义化，与 ftpguard 同口径）：
  0 干净 / 1 命中违规 / 2 根目录缺失 / 3 零证据（未扫描到任何文件）

本守卫做不到的：动态拼接出的颜色字符串（如 "#" + "F0A030"）、二进制资源内的
颜色、通过字符串拼接的路径；这些不在静态判据内，不得声称覆盖。
"""
import argparse
import sys
from pathlib import Path

# 与设计文档 Global Constraints 逐字一致（旧「琴色是动词」体系 + Instrument 前的深/浅色板）
LEGACY_COLORS = (
    "#F0A030", "#D48820", "#F0B840",   # 琴色系
    "#40C8A0", "#2FA88A",              # 青绿系（旧进度/成功）
    "#E85848", "#D6453A",              # 错误系
    "#7B8494", "#6B7480",              # 次要文字
    "#C8CCD4",                          # 主文字（旧暗）
    "#0B0E14", "#141820", "#0E1219",   # 旧暗色底
    "#232A36", "#252A33", "#333B48", "#3A4250",  # 旧石墨结构色
    "#1A1200",                          # 旧深色主按钮文字
)

EXTS = (".qss", ".cpp", ".h")
DEFAULT_SCAN_DIRS = "src"


def scan(root: Path, scan_dirs):
    hits = []
    files = 0
    for d in scan_dirs:
        base = root / d
        if not base.is_dir():
            continue
        for p in sorted(base.rglob("*")):
            if not p.is_file() or p.suffix.lower() not in EXTS:
                continue
            files += 1
            try:
                text = p.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            for lineno, line in enumerate(text.splitlines(), 1):
                low = line.lower()
                for legacy in LEGACY_COLORS:
                    if legacy.lower() in low:
                        hits.append((p, lineno, legacy))
    return files, hits


def main(argv=None) -> int:
    # 输出契约：UTF-8，与控制台码页无关（AGENTS.md 约定）
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass

    ap = argparse.ArgumentParser(description="旧色值静态守卫（Instrument 视觉语言）")
    ap.add_argument("--root", default=".", help="仓库根目录（默认当前目录）")
    ap.add_argument("--scan-dirs", default=DEFAULT_SCAN_DIRS,
                    help="相对根目录的扫描目录，逗号分隔（默认 src）")
    args = ap.parse_args(argv)

    root = Path(args.root)
    if not root.is_dir():
        print(f"[FAIL] 根目录不存在: {root}")
        return 2

    scan_dirs = [s.strip() for s in args.scan_dirs.split(",") if s.strip()]
    files, hits = scan(root, scan_dirs)
    if files == 0:
        print(f"[FAIL] 零证据：{root} 下未扫描到 .qss/.cpp/.h 文件")
        return 3

    for p, lineno, legacy in hits:
        print(f"{p}:{lineno}: {legacy}")

    if hits:
        print(f"[FAIL] 旧色值残留 {len(hits)} 处（扫描 {files} 文件）")
        return 1
    print(f"[OK] 旧色值清零（扫描 {files} 文件）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
