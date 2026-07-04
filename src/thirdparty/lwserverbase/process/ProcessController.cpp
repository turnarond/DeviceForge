/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: ProcessController.cpp
 *
 * Date: 2026-03-03
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#include <process/ProcessController.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <direct.h>
#define isatty _isatty
#define fileno _fileno
#define chdir  _chdir
#define getpid GetCurrentProcessId
#else
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif
#include <cstring>
#include <thread>
#include <chrono>

namespace lwserverbase {
namespace process {

// 静态实例指针
ProcessController* ProcessController::s_instance = nullptr;

ProcessController::ProcessController()
    : m_shouldQuit(false)
    , m_daemonMode(false)
    , m_lockFile(nullptr)
{
    m_exitEvent = EventCreate(false, false);
    s_instance = this;
    InitializeSignalHandling();
}

ProcessController::~ProcessController() {
    if (m_lockFile) {
#ifdef _WIN32
        CloseHandle((HANDLE)m_lockFile);
#else
        close((int)(intptr_t)m_lockFile);
#endif
    }
    if (m_exitEvent) {
        EventDestroySafe(&m_exitEvent);
    }
    s_instance = nullptr;
}

bool ProcessController::EnsureSingleInstance(const std::string& lockFile) {
#ifdef _WIN32
    HANDLE h = CreateFileA(lockFile.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,                          // dwShareMode=0 → 独占访问
        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    // 写入 PID
    char pid[16];
    snprintf(pid, sizeof(pid), "%lu\n", GetCurrentProcessId());
    DWORD written;
    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    SetEndOfFile(h);
    WriteFile(h, pid, (DWORD)strlen(pid), &written, NULL);
    // 保持句柄打开以维持锁（析构时 CloseHandle）
    m_lockFile = (void*)h;
    return true;
#else
    // 尝试打开锁文件
    int fd = open(lockFile.c_str(), O_CREAT | O_RDWR, 0644);
    if (fd == -1) {
        return false;
    }
    m_lockFile = (void*)(intptr_t)fd;

    // 尝试获取文件锁
    struct flock lock;
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;

    if (fcntl((int)(intptr_t)m_lockFile, F_SETLK, &lock) == -1) {
        close((int)(intptr_t)m_lockFile);
        m_lockFile = nullptr;
        return false;
    }

    // 写入进程ID
    char pid[16];
    snprintf(pid, sizeof(pid), "%d\n", getpid());
    if (ftruncate((int)(intptr_t)m_lockFile, 0) == -1) {
        close((int)(intptr_t)m_lockFile);
        m_lockFile = nullptr;
        return false;
    }
    if (write((int)(intptr_t)m_lockFile, pid, strlen(pid)) == -1) {
        close((int)(intptr_t)m_lockFile);
        m_lockFile = nullptr;
        return false;
    }

    return true;
#endif
}

bool ProcessController::ShouldQuit() const {
    return m_shouldQuit.load(std::memory_order_acquire);
}

bool ProcessController::ConsoleAvailable() const {
#ifdef _WIN32
    return !m_daemonMode && _isatty(_fileno(stdout));
#else
    return !m_daemonMode && isatty(fileno(stdout));
#endif
}

void ProcessController::SetDaemonMode(bool daemon) {
    m_daemonMode = daemon;
}

bool ProcessController::Daemonize() {
#ifdef _WIN32
    // Windows 上守护进程化需通过 SCM (Service Control Manager)，
    // 不在本次移植范围。调用方在 Daemonize() 失败时继续前台运行。
    return false;
#else
    // 第一次fork
    pid_t pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid > 0) {
        exit(0);
    }

    // 创建新会话
    if (setsid() < 0) {
        return false;
    }

    // 第二次fork
    pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid > 0) {
        exit(0);
    }

    // 关闭标准文件描述符
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // 重定向标准文件描述符到/dev/null
    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_WRONLY);

    // 更改工作目录
    if (chdir("/") == -1) {
        return false;
    }

    // 设置文件权限掩码
    umask(0);

    m_daemonMode = true;
    return true;
#endif
}

void ProcessController::WaitForExitSignal() {
    // 轮询等待退出信号（100ms 间隔）
    // EventWait 不支持超时，SignalHandler 中只能设置原子标志（非 async-signal-safe），
    // 因此用轮询方式检查 m_shouldQuit
    // 使用 fprintf 而非 write()/STDERR_FILENO，保持跨平台兼容（Windows 无 POSIX write）
    fprintf(stderr, "[EXIT] WaitForExitSignal: waiting...\n");
    while (!m_shouldQuit.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    fprintf(stderr, "[EXIT] WaitForExitSignal: got signal, exiting loop\n");
}

void ProcessController::TriggerExit() {
    m_shouldQuit.store(true, std::memory_order_release);
    EventSet(m_exitEvent);
}

void ProcessController::SignalHandler([[maybe_unused]] int signum) {
#ifndef _WIN32
    // 使用 async-signal-safe 的 write() 记录信号（诊断 Ctrl+C 崩溃用）
    // Windows: write()/STDERR_FILENO 是 POSIX 专用，用 ConsoleCtrlHandler 代替
    const char* name = (signum == SIGINT)  ? "SIGINT"  :
                       (signum == SIGTERM) ? "SIGTERM" :
                       (signum == SIGQUIT) ? "SIGQUIT" : "UNKNOWN";
    [[maybe_unused]] auto _r1 = write(STDERR_FILENO, "[SIGNAL] received ", 18);
    [[maybe_unused]] auto _r2 = write(STDERR_FILENO, name, strlen(name));
    [[maybe_unused]] auto _r3 = write(STDERR_FILENO, ", handler running\n", 18);
#endif

    if (s_instance) {
#ifdef _WIN32
        // Windows 上 ConsoleCtrlHandler 可以安全调用 TriggerExit
        s_instance->TriggerExit();
#else
        // POSIX 信号处理函数中只能调用 async-signal-safe 的函数
        // 这里只设置原子标志，不调用 EventSet（非 async-signal-safe）
        s_instance->m_shouldQuit.store(true, std::memory_order_release);
        [[maybe_unused]] auto _r4 = write(STDERR_FILENO, "[SIGNAL] m_shouldQuit = true\n", 29);
#endif
    } else {
#ifndef _WIN32
        [[maybe_unused]] auto _r5 = write(STDERR_FILENO, "[SIGNAL] s_instance is NULL!\n", 29);
#endif
    }
}

#ifdef _WIN32

static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT ||
        ctrlType == CTRL_CLOSE_EVENT) {
        if (ProcessController::s_instance) {
            ProcessController::s_instance->TriggerExit();
        }
        return TRUE;
    }
    return FALSE;
}

void ProcessController::InitializeSignalHandling() {
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    // SIGQUIT 在 Windows 上不存在
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
}

#else /* POSIX */

void ProcessController::InitializeSignalHandling() {
    // 注册信号处理函数
    // 注意：SIGKILL 不可捕获，无需注册（POSIX 标准规定 signal(SIGKILL, ...) 必定失败）
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    signal(SIGQUIT, SignalHandler);
}

#endif

} // namespace process
} // namespace lwserverbase
