# -*- coding: utf-8 -*-
"""开发工具集统一命令行入口。

用法（仓库根目录执行）：
    python tools/devtools/cli.py versioncheck [--root <dir>]
    python tools/devtools/cli.py smoke <exe> [--timeout 秒] [--title 标题]
"""
import os
import sys

# 支持直接运行（python tools/devtools/cli.py）与模块运行（python -m devtools.cli）
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import argparse  # noqa: E402

from devtools import smoke, versioncheck  # noqa: E402


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(prog="devtools", description="DeviceForge 开发工具集")
    sub = ap.add_subparsers(dest="command", required=True)

    vc = sub.add_parser("versioncheck", help="版本一致性校验（规范第 2、10 条）")
    vc.add_argument("--root", default=".", help="仓库根目录（默认当前目录）")

    sm = sub.add_parser("smoke", help="UI 冒烟（启动→主窗口→优雅退出，规范第 16 条）")
    sm.add_argument("exe", help="可执行文件路径")
    sm.add_argument("--timeout", type=float, default=30.0, help="等待主窗口超时秒数（默认 30）")
    sm.add_argument("--title", default="DeviceForge", help="主窗口标题包含关键字（默认 DeviceForge）")

    return ap


def main(argv: list = None) -> int:
    args = build_parser().parse_args(argv)
    if args.command == "versioncheck":
        return versioncheck.main(["--root", args.root])
    if args.command == "smoke":
        result = smoke.SmokeRunner(args.exe, timeout=args.timeout, title=args.title).run()
        print(result.message)
        return 0 if result.ok else 1
    return 2


if __name__ == "__main__":
    sys.exit(main())
