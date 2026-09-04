# -*- coding: utf-8 -*-
"""smoke 工具的单元测试（TDD 红-绿，Windows 平台）。"""
import subprocess
import sys
import unittest
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import devtools.smoke as sm  # noqa: E402

# 沙箱限制：临时目录放工作区内（tools/.test-tmp），且不用 mkdtemp（其随机目录被沙箱拒绝）
_TMP_ROOT = Path(__file__).resolve().parent.parent / ".test-tmp"


def _make_tmp() -> Path:
    p = _TMP_ROOT / uuid.uuid4().hex
    p.mkdir(parents=True, exist_ok=True)
    return p


def _spawn_sleeper():
    """真实子进程：睡眠 30s 的 python 进程，用于验证进程管理。"""
    return subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)"])


class SmokeRunnerTest(unittest.TestCase):

    def tearDown(self):
        # 清理可能残留的子进程
        for p in getattr(self, "_procs", []):
            if p.poll() is None:
                p.kill()
                p.wait()

    def _track(self, proc):
        self._procs = getattr(self, "_procs", []) + [proc]
        return proc

    def test_exe不存在返回失败(self):
        runner = sm.SmokeRunner(str(Path(_make_tmp()) / "no_such.exe"), timeout=1)
        result = runner.run()
        self.assertFalse(result.ok)
        self.assertIn("不存在", result.message)

    def test_窗口超时失败且进程被清理(self):
        proc = self._track(_spawn_sleeper())
        runner = sm.SmokeRunner(
            proc.args[0], timeout=0.5,
            find_window=lambda title: None,  # 永远找不到窗口
            sleep=lambda s: None,            # 不真睡，加速测试
        )
        runner._spawn = lambda: proc  # 复用已启动进程
        result = runner.run()
        self.assertFalse(result.ok)
        self.assertIn("超时", result.message)
        self.assertIsNotNone(proc.poll(), "超时后进程应已被清理")

    def test_窗口出现则优雅关闭成功(self):
        proc = self._track(_spawn_sleeper())
        calls = []

        def fake_find(title):
            # 第一次 None，第二次返回窗口句柄（模拟窗口出现）
            calls.append(title)
            return None if len(calls) == 1 else 0x1234

        runner = sm.SmokeRunner(
            proc.args[0], timeout=5,
            find_window=fake_find,
            sleep=lambda s: None,
            terminate=lambda p, hwnd: (True, "mock 优雅关闭成功"),
        )
        runner._spawn = lambda: proc
        result = runner.run()
        self.assertTrue(result.ok, result.message)

    def test_进程提前崩溃失败(self):
        proc = self._track(subprocess.Popen([sys.executable, "-c", "pass"]))
        runner = sm.SmokeRunner(
            proc.args[0], timeout=1,
            find_window=lambda title: None,
            sleep=lambda s: None,
        )
        runner._spawn = lambda: proc
        result = runner.run()
        self.assertFalse(result.ok)
        self.assertIn("提前退出", result.message)


class SmokeCliTest(unittest.TestCase):

    def test_direct_cli_help_only_documents_supported_exe_syntax(self):
        script = Path(sm.__file__).resolve()
        completed = subprocess.run(
            [sys.executable, str(script)],
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(completed.returncode, 2)
        self.assertEqual(completed.stdout.strip(), "用法: python smoke.py <exe路径>")
        self.assertEqual(completed.stderr, "")


if __name__ == "__main__":
    unittest.main()
