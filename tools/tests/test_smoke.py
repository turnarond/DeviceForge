# -*- coding: utf-8 -*-
"""smoke 工具的单元测试（TDD 红-绿，Windows 平台）。"""
import os
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
            find_window=lambda title, pid: None,  # 永远找不到窗口
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

        def fake_find(title, pid):
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
            find_window=lambda title, pid: None,
            sleep=lambda s: None,
        )
        runner._spawn = lambda: proc
        result = runner.run()
        self.assertFalse(result.ok)
        self.assertIn("提前退出", result.message)

    def test_窗口查找只针对自身进程pid(self):
        # #51：标题匹配必须按本次启动的 PID 过滤，外部同名窗口不得劫持
        proc = self._track(_spawn_sleeper())
        seen = []

        def fake_find(title, pid):
            seen.append(pid)
            return 0x1234 if pid == proc.pid else None

        runner = sm.SmokeRunner(
            proc.args[0], timeout=2,
            find_window=fake_find,
            sleep=lambda s: None,
            terminate=lambda p, hwnd: (True, "ok"),
        )
        runner._spawn = lambda: proc
        result = runner.run()
        self.assertTrue(result.ok, result.message)
        self.assertTrue(seen and all(p == proc.pid for p in seen))

    def test_超时消息给出外部残留实例归因(self):
        # #51：脏环境下明确归因（PID+路径），只报告绝不杀用户进程
        proc = self._track(_spawn_sleeper())
        runner = sm.SmokeRunner(
            proc.args[0], timeout=0.3,
            find_window=lambda title, pid: None,
            sleep=lambda s: None,
            foreign_matches=lambda title, pid: [(4321, r"D:\tools\DeviceForge.exe")],
        )
        runner._spawn = lambda: proc
        result = runner.run()
        self.assertFalse(result.ok)
        self.assertIn("外部实例", result.message)
        self.assertIn("4321", result.message)
        self.assertIn("D:\\tools\\DeviceForge.exe", result.message)


class SmokePrecheckTest(unittest.TestCase):
    """#51 环境预检：Qt DLL 目录探测前置注入 PATH，缺失时归因拒跑。"""

    def setUp(self):
        self.tmp = _make_tmp()

    def _mk_qt(self, name):
        d = self.tmp / name
        d.mkdir(parents=True, exist_ok=True)
        (d / "Qt6Core.dll").touch()
        return d

    def test_find_qt_bin_dir_命中含_Qt6Core_dll_的候选(self):
        miss = self.tmp / "no_qt"
        miss.mkdir()
        hit = self._mk_qt("msvc2022_64_bin")
        self.assertEqual(sm.find_qt_bin_dir([miss, hit]), hit)

    def test_find_qt_bin_dir_无候选返回_none(self):
        miss = self.tmp / "no_qt"
        miss.mkdir()
        self.assertIsNone(sm.find_qt_bin_dir([miss]))

    def test_ensure_qt_on_path_前置注入并报告(self):
        hit = self._mk_qt("qtbin")
        env = {"PATH": str(self.tmp)}
        ok, msg = sm.ensure_qt_on_path(env=env, candidates=[hit])
        self.assertTrue(ok)
        self.assertTrue(env["PATH"].startswith(str(hit)))
        self.assertIn(str(hit), msg)

    def test_ensure_qt_on_path_找不到时失败归因(self):
        env = {"PATH": ""}
        ok, msg = sm.ensure_qt_on_path(env=env, candidates=[])
        self.assertFalse(ok)
        self.assertIn("Qt", msg)


class SmokeCliTest(unittest.TestCase):

    def test_direct_cli_help_only_documents_supported_exe_syntax(self):
        script = Path(sm.__file__).resolve()
        completed = subprocess.run(
            [sys.executable, str(script)],
            capture_output=True,
            text=True,
            # smoke 侧 main() 现把输出钉成 UTF-8（跨码页可用），读它的人必须显式声明编码，
            # 否则父进程按本地码页（GBK）解 UTF-8 中文会在读线程抛 UnicodeDecodeError。
            encoding="utf-8",
            check=False,
        )

        self.assertEqual(completed.returncode, 2)
        self.assertEqual(completed.stdout.strip(), "用法: python smoke.py <exe路径>")
        self.assertEqual(completed.stderr, "")

    def test_cli_rejects_extra_arguments_instead_of_ignoring_them(self):
        script = Path(sm.__file__).resolve()
        completed = subprocess.run(
            [sys.executable, str(script), "DeviceForge.exe", "unexpected"],
            capture_output=True,
            text=True,
            # smoke 侧 main() 现把输出钉成 UTF-8（跨码页可用），读它的人必须显式声明编码，
            # 否则父进程按本地码页（GBK）解 UTF-8 中文会在读线程抛 UnicodeDecodeError。
            encoding="utf-8",
            check=False,
        )

        self.assertEqual(completed.returncode, 2)
        self.assertEqual(completed.stdout.strip(), "用法: python smoke.py <exe路径>")
        self.assertEqual(completed.stderr, "")

    def test_cli_usage_under_legacy_console_codepage_still_exits_two(self):
        """CI 取证用红-绿用例：cp1252 控制台下打印中文用法行曾抛 UnicodeEncodeError，
        于是退出码从约定的 2 变成 1（CI 实况：AssertionError: 1 != 2）。"""
        script = Path(sm.__file__).resolve()
        env = dict(os.environ, PYTHONIOENCODING="cp1252")
        completed = subprocess.run(
            [sys.executable, str(script)],
            capture_output=True,
            text=True,
            encoding="utf-8",
            env=env,
            check=False,
        )

        self.assertNotIn("UnicodeEncodeError", completed.stderr)
        self.assertEqual(completed.returncode, 2)


if __name__ == "__main__":
    unittest.main()
