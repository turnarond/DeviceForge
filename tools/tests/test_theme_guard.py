# -*- coding: utf-8 -*-
"""theme_guard 旧色值静态守卫自测（TDD 红-绿）。

守卫语义：src/ 下 .qss/.cpp/.h 中出现任何旧色值 → 违规（退出码 1）；
干净 → 0；根目录/被检目录缺失 → 2；零证据（未扫到任何文件）→ 3。
"""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import devtools.theme_guard as tg  # noqa: E402


def _tree(files: dict) -> Path:
    """在临时目录下按 rel_path -> content 建树，返回临时根。"""
    root = Path(tempfile.mkdtemp())
    for rel, content in files.items():
        p = root / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(content, encoding="utf-8")
    return root


class ThemeGuardTest(unittest.TestCase):

    def test_旧色值命中判违规(self):
        root = _tree({"src/app/darkstyle.qss": "QWidget { color: #F0A030; }"})
        self.assertEqual(tg.main(["--root", str(root)]), 1)

    def test_新token值不判违规(self):
        root = _tree({"src/app/darkstyle.qss": "QWidget { color: #0E766E; border: 1px solid #D3DAE0; }"})
        self.assertEqual(tg.main(["--root", str(root)]), 0)

    def test_大小写不敏感(self):
        root = _tree({"src/app/x.cpp": 'auto c = "#f0a030";'})
        self.assertEqual(tg.main(["--root", str(root)]), 1)

    def test_注释中的旧色值也判违规(self):
        root = _tree({"src/app/x.cpp": "// 旧琴色 #F0A030 已废弃\n"})
        self.assertEqual(tg.main(["--root", str(root)]), 1)

    def test_非白名单扩展名不扫描(self):
        root = _tree({"docs/x.md": "旧色值 #F0A030 只是文档历史", "src/app/x.qss": "color: #E9EDF0;"})
        self.assertEqual(tg.main(["--root", str(root)]), 0)

    def test_空目录零证据(self):
        root = _tree({"src/README.txt": "no scannable files"})
        self.assertEqual(tg.main(["--root", str(root)]), 3)

    def test_根目录缺失(self):
        self.assertEqual(tg.main(["--root", str(Path(tempfile.mkdtemp()) / "nope")]), 2)

    def test_SVG编码旧色也判违规(self):
        root = _tree({"src/app/x.qss": "image: url(\"data:image/svg+xml;utf8,<svg fill='%23D48820'/>\");"})
        self.assertEqual(tg.main(["--root", str(root)]), 1)

    def test_扫描目录缺失返回2(self):
        root = _tree({"src/app/x.qss": "color: #E9EDF0;"})
        self.assertEqual(tg.main(["--root", str(root), "--scan-dirs", "src,nope"]), 2)

    def test_违规输出含文件与行号(self):
        root = _tree({"src/app/x.cpp": "line1\nline2 color #40C8A0\n"})
        import io
        buf = io.StringIO()
        old = sys.stdout
        sys.stdout = buf
        try:
            code = tg.main(["--root", str(root)])
        finally:
            sys.stdout = old
        self.assertEqual(code, 1)
        self.assertIn("x.cpp", buf.getvalue())
        self.assertIn(":2", buf.getvalue())


if __name__ == "__main__":
    unittest.main()
