# -*- coding: utf-8 -*-
"""versioncheck 工具的单元测试（TDD 红-绿）。"""
import sys
import unittest
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import devtools.versioncheck as vc  # noqa: E402

# 沙箱限制：临时目录放工作区内（tools/.test-tmp），且不用 mkdtemp（其随机目录被沙箱拒绝）
_TMP_ROOT = Path(__file__).resolve().parent.parent / ".test-tmp"


def _make_tmp() -> Path:
    p = _TMP_ROOT / uuid.uuid4().hex
    p.mkdir(parents=True, exist_ok=True)
    return p


def _write(tmp: Path, rel: str, content: str) -> Path:
    p = tmp / rel
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(content, encoding="utf-8")
    return p


class ExtractTest(unittest.TestCase):
    """各来源提取函数解析测试。"""

    def test_cmake_权威版本解析(self):
        p = _write(_make_tmp(), "CMakeLists.txt",
                   "project(DeviceForge VERSION 2.7.0 LANGUAGES C CXX)\n")
        self.assertEqual(vc.extract_cmake(p), "2.7.0")

    def test_cmake_无版本返回None(self):
        p = _write(_make_tmp(), "CMakeLists.txt",
                   "project(DeviceForge LANGUAGES C CXX)\n")
        self.assertIsNone(vc.extract_cmake(p))

    def test_rc_四段转三段(self):
        p = _write(_make_tmp(), "DeviceForge.rc",
                   'FILEVERSION 2,7,0,0\nVALUE "FileVersion", "2.7.0.0"\n')
        self.assertEqual(vc.extract_rc(p), "2.7.0")

    def test_readme_版本解析(self):
        p = _write(_make_tmp(), "README.md",
                   "**版本**：2.7.0 | **许可**：MIT License\n")
        self.assertEqual(vc.extract_readme(p), "2.7.0")

    def test_changelog_首个标题解析(self):
        p = _write(_make_tmp(), "CHANGELOG.md",
                   "# Changelog\n\n## [2.7.0] — 2026-08-19\n\n### 说明\n")
        self.assertEqual(vc.extract_changelog(p), "2.7.0")

    def test_claude_当前版本解析(self):
        p = _write(_make_tmp(), "CLAUDE.md",
                   "### 架构状态：当前版本 2.7.0\n")
        self.assertEqual(vc.extract_claude(p), "2.7.0")

    def test_白皮书_版本解析(self):
        p = _write(_make_tmp(), "白皮书.md",
                   "版本：2.7.0 | 日期：2026-08-18 | 状态：草案\n")
        self.assertEqual(vc.extract_whitepaper(p), "2.7.0")

    def test_文件缺失返回None(self):
        self.assertIsNone(vc.extract_cmake(_make_tmp() / "nope.txt"))


def _build_tree(version: str = "2.7.0", omit=None) -> Path:
    """构造一个全仓版本一致的假仓库（可指定缺省文件）。"""
    tmp = _make_tmp()
    files = {
        "CMakeLists.txt": f"project(DeviceForge VERSION {version} LANGUAGES C CXX)\n",
        "src/app/DeviceForge.rc": f'FILEVERSION {version.replace(".", ",")},0\n',
        "README.md": f"**版本**：{version} | **许可**：MIT License\n",
        "CHANGELOG.md": f"# Changelog\n\n## [{version}] — 2026-08-19\n",
        "CLAUDE.md": f"### 当前版本 {version}\n",
        "docs/01-白皮书/白皮书.md": f"版本：{version} | 日期：2026-08-18 | 状态：草案\n",
    }
    for rel, content in files.items():
        if omit and rel == omit:
            continue
        _write(tmp, rel, content)
    return tmp


class CheckVersionsTest(unittest.TestCase):
    """全仓一致性校验测试。"""

    def test_全部一致通过(self):
        result = vc.check_versions(_build_tree())
        self.assertTrue(result.consistent)
        self.assertEqual(result.authority, "2.7.0")
        self.assertEqual(len(result.mismatches), 0)

    def test_单处不一致被检出(self):
        tmp = _build_tree()
        _write(tmp, "README.md", "**版本**：2.6.0 | **许可**：MIT License\n")
        result = vc.check_versions(tmp)
        self.assertFalse(result.consistent)
        self.assertIn("readme", result.mismatches)
        self.assertEqual(result.reports["readme"].found, "2.6.0")

    def test_缺失来源不阻断但报告(self):
        result = vc.check_versions(_build_tree(omit="README.md"))
        self.assertTrue(result.consistent)
        self.assertIsNone(result.reports["readme"].found)
        self.assertIn("readme", result.missing)

    def test_权威缺失视为不一致(self):
        tmp = _build_tree(omit="CMakeLists.txt")
        result = vc.check_versions(tmp)
        self.assertFalse(result.consistent)


class CliTest(unittest.TestCase):
    def test_不一致时退出码非零(self):
        tmp = _build_tree()
        _write(tmp, "README.md", "**版本**：9.9.9\n")
        self.assertNotEqual(vc.main(["--root", str(tmp)]), 0)


if __name__ == "__main__":
    unittest.main()
