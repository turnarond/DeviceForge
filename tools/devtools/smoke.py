# -*- coding: utf-8 -*-
"""UI 冒烟验证工具（开发规范第 16 条，交付评审强制项）。

流程：启动可执行文件 → 轮询主窗口标题（EnumWindows 包含匹配）→
找到窗口后发送 WM_CLOSE 优雅退出 → 退出码 0 即通过。
超时 / 提前崩溃 / 退出码非 0 均视为失败。
"""
import os
import subprocess
import sys
import time
from dataclasses import dataclass
from typing import Callable, Optional, Tuple

WM_CLOSE = 0x0010
GRACE_SECONDS = 5.0
POLL_INTERVAL = 0.2

FindWindowFn = Callable[[str], Optional[int]]
SleepFn = Callable[[float], None]
TerminateFn = Callable[[subprocess.Popen, Optional[int]], Tuple[bool, str]]


def _default_find_window(title: str) -> Optional[int]:
    """Windows：EnumWindows 扫描标题包含 title 的顶层窗口，返回句柄；非 Windows 恒 None。"""
    if os.name != "nt":
        return None
    import ctypes
    from ctypes import wintypes

    user32 = ctypes.windll.user32
    found: list = [None]

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def _enum_cb(hwnd, _lparam):  # noqa: ANN001
        length = user32.GetWindowTextLengthW(hwnd)
        if length:
            buf = ctypes.create_unicode_buffer(length + 1)
            user32.GetWindowTextW(hwnd, buf, length + 1)
            if title in buf.value:
                found[0] = hwnd
                return False  # 停止枚举
        return True

    user32.EnumWindows(_enum_cb, 0)
    return found[0]


def _default_terminate(proc: subprocess.Popen, hwnd: Optional[int]) -> Tuple[bool, str]:
    """发送 WM_CLOSE 优雅退出；等待退出码 0；超时强制终止。"""
    if os.name == "nt" and hwnd:
        import ctypes
        ctypes.windll.user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
        try:
            proc.wait(timeout=GRACE_SECONDS)
            if proc.returncode == 0:
                return True, f"主窗口关闭，进程正常退出（退出码 0）"
            return False, f"进程退出码非 0: {proc.returncode}"
        except subprocess.TimeoutExpired:
            pass
    proc.kill()
    proc.wait()
    return False, "优雅退出超时，已强制终止"


@dataclass
class SmokeResult:
    ok: bool
    message: str
    returncode: Optional[int] = None


class SmokeRunner:
    """冒烟执行器。find_window / sleep / terminate 可注入以便测试。"""

    def __init__(self, exe_path: str, timeout: float = 30.0, title: str = "DeviceForge",
                 find_window: Optional[FindWindowFn] = None,
                 sleep: Optional[SleepFn] = None,
                 terminate: Optional[TerminateFn] = None):
        self.exe_path = exe_path
        self.timeout = timeout
        self.title = title
        self.find_window = find_window or _default_find_window
        self.sleep = sleep or time.sleep
        self.terminate = terminate or _default_terminate

    def _spawn(self) -> subprocess.Popen:
        return subprocess.Popen([self.exe_path])

    def run(self) -> SmokeResult:
        if not os.path.isfile(self.exe_path):
            return SmokeResult(False, f"可执行文件不存在: {self.exe_path}")

        proc = self._spawn()
        try:
            hwnd = None
            deadline = time.monotonic() + self.timeout
            while time.monotonic() < deadline:
                if proc.poll() is not None:
                    return SmokeResult(False, f"进程提前退出，退出码 {proc.returncode}",
                                       proc.returncode)
                hwnd = self.find_window(self.title)
                if hwnd:
                    break
                self.sleep(POLL_INTERVAL)

            if hwnd is None:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()
                return SmokeResult(
                    False,
                    f"超时({self.timeout:.1f}s)未找到主窗口（标题含“{self.title}”），进程已终止",
                    proc.returncode)

            ok, message = self.terminate(proc, hwnd)
            return SmokeResult(ok, message, proc.returncode)
        finally:
            # 兜底：任何异常路径下都不遗留僵尸进程
            if proc.poll() is None:
                proc.kill()
                proc.wait()


def main(argv: Optional[list[str]] = None) -> int:
    arguments = sys.argv[1:] if argv is None else argv
    if len(arguments) != 1:
        print("用法: python smoke.py <exe路径>")
        return 2
    result = SmokeRunner(arguments[0]).run()
    print(result.message)
    return 0 if result.ok else 1


if __name__ == "__main__":
    sys.exit(main())
