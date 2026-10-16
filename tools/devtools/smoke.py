# -*- coding: utf-8 -*-
"""UI 冒烟验证工具（开发规范第 16 条，交付评审强制项）。

流程：环境预检（Qt DLL 目录注入 PATH）→ 启动可执行文件 → 轮询主窗口
（标题包含匹配且限定本次启动进程的 PID）→ WM_CLOSE 优雅退出 →
退出码 0 即通过。超时 / 提前崩溃 / 退出码非 0 均视为失败。

两类已知环境坑的自动化对策（#51）：
- 子进程 PATH 缺 Qt bin 导致 0xC0000005 "进程提前退出"假崩：启动前
  ensure_qt_on_path 探测并前置注入，探测失败直接归因拒跑；
- 外部同名标题窗口劫持（EnumWindows 包含匹配命中别的实例）：窗口查找
  按自身 PID 过滤，绝不向外部实例发消息、绝不杀用户进程；超时时把
  外部同名窗口实例（PID+路径）写进失败消息供人工分诊。
"""
import ctypes
import os
import subprocess
import sys
import time
from ctypes import wintypes
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Optional, Tuple

WM_CLOSE = 0x0010
GRACE_SECONDS = 5.0
POLL_INTERVAL = 0.2

FindWindowFn = Callable[[str, int], Optional[int]]
SleepFn = Callable[[float], None]
TerminateFn = Callable[[subprocess.Popen, Optional[int]], Tuple[bool, str]]
ForeignMatchesFn = Callable[[str, int], list]


def find_qt_bin_dir(candidates: Optional[list] = None) -> Optional[Path]:
    """返回第一个含 Qt6Core.dll 的候选目录；candidates=None 时按惯例探测
    （QT_DIR/bin → D:/Qt 与 C:/Qt 的 6.x msvc bin，版本号降序）。"""
    if candidates is None:
        candidates = []
        qt_dir = os.environ.get("QT_DIR")
        if qt_dir:
            candidates.append(Path(qt_dir) / "bin")
        for root in (Path("D:/Qt"), Path("C:/Qt")):
            try:
                for bin_dir in sorted(root.glob("6*/msvc*/bin"), reverse=True):
                    candidates.append(bin_dir)
            except OSError:
                continue
    for cand in candidates:
        try:
            if (Path(cand) / "Qt6Core.dll").is_file():
                return Path(cand)
        except OSError:
            continue
    return None


def ensure_qt_on_path(env: Optional[dict] = None,
                      candidates: Optional[list] = None) -> Tuple[bool, str]:
    """把 Qt bin 目录前置注入 env（默认 os.environ，影响子进程继承）。
    PATH 已能找到 Qt6Core.dll 则原样通过；探测失败返回 (False, 归因)。"""
    if env is None:
        env = os.environ
    path_sep = os.pathsep
    existing = env.get("PATH", "")
    for part in existing.split(path_sep):
        try:
            if part and (Path(part) / "Qt6Core.dll").is_file():
                return True, "Qt DLL 已在 PATH: " + part
        except OSError:
            continue
    found = find_qt_bin_dir(candidates)
    if found is None:
        return False, ("未找到 Qt bin 目录（PATH 与 D:/Qt、C:/Qt、QT_DIR 探测均未命中 "
                       "Qt6Core.dll）；直接运行会以 0xC0000005 假崩，已拒绝启动。"
                       "请安装 Qt 或设置 QT_DIR。")
    env["PATH"] = str(found) + path_sep + existing
    return True, "Qt bin 已前置注入 PATH: " + str(found)


def _enum_title_windows(title: str) -> list:
    """返回 [(hwnd, pid, 标题)]，Windows 且标题包含匹配；其他平台空表。"""
    if os.name != "nt":
        return []
    user32 = ctypes.windll.user32
    out: list = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def _enum_cb(hwnd, _lparam):  # noqa: ANN001
        length = user32.GetWindowTextLengthW(hwnd)
        if length:
            buf = ctypes.create_unicode_buffer(length + 1)
            user32.GetWindowTextW(hwnd, buf, length + 1)
            if title in buf.value:
                pid = wintypes.DWORD()
                user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                out.append((hwnd, int(pid.value), buf.value))
        return True

    user32.EnumWindows(_enum_cb, 0)
    return out


def _process_path(pid: int) -> str:
    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    kernel32 = ctypes.windll.kernel32
    h = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not h:
        return "<路径不可读>"
    try:
        buf = ctypes.create_unicode_buffer(1024)
        size = wintypes.DWORD(len(buf))
        if kernel32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(size)):
            return buf.value
        return "<路径不可读>"
    finally:
        kernel32.CloseHandle(h)


def _default_find_window(title: str, pid: int) -> Optional[int]:
    """标题包含匹配且窗口属于本进程 PID：外部同名实例不再劫持冒烟目标。"""
    for hwnd, win_pid, _t in _enum_title_windows(title):
        if win_pid == pid:
            return hwnd
    return None


def _default_foreign_matches(title: str, own_pid: int) -> list:
    """归因用：标题命中但不属于本进程的窗口 [(pid, 路径)]。只读枚举，绝不动别人进程。"""
    seen = set()
    out = []
    for _hwnd, win_pid, _t in _enum_title_windows(title):
        if win_pid != own_pid and win_pid not in seen:
            seen.add(win_pid)
            out.append((win_pid, _process_path(win_pid)))
    return out


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
                 terminate: Optional[TerminateFn] = None,
                 foreign_matches: Optional[ForeignMatchesFn] = None):
        self.exe_path = exe_path
        self.timeout = timeout
        self.title = title
        self.find_window = find_window or _default_find_window
        self.sleep = sleep or time.sleep
        self.terminate = terminate or _default_terminate
        self.foreign_matches = foreign_matches or _default_foreign_matches

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
                hwnd = self.find_window(self.title, proc.pid)
                if hwnd:
                    break
                self.sleep(POLL_INTERVAL)

            if hwnd is None:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()
                message = (f"超时({self.timeout:.1f}s)未找到主窗口"
                           f"（标题含“{self.title}”且属本进程），进程已终止")
                try:
                    others = self.foreign_matches(self.title, proc.pid)
                except Exception:  # noqa: BLE001 - 归因尽力而为，不得掩盖主失败原因
                    others = []
                if others:
                    listing = "；".join(
                        f"PID={opid} 路径={xpath}" for opid, xpath in others)
                    message += ("。检测到持有同名窗口的外部实例（" + listing +
                                "）——冒烟只操作自身进程，外部实例不会被终止，请人工确认后关闭")
                return SmokeResult(False, message, proc.returncode)

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
    # 环境预检（#51）：Qt bin 注入失败就不启动——否则子进程 0xC0000005
    # "进程提前退出"是假崩，归因浪费现场时间
    ok, qt_msg = ensure_qt_on_path()
    print(qt_msg)
    if not ok:
        return 2
    result = SmokeRunner(arguments[0]).run()
    print(result.message)
    return 0 if result.ok else 1


if __name__ == "__main__":
    sys.exit(main())
