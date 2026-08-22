// src/updater/UpdaterMain.cpp
// Updater.exe — DeviceForge 在线更新文件替换工具
// 无 Qt 依赖，纯 Win32 API + CRT，~100KB 静态链接

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <fstream>
#include <sstream>
#include "UpdaterFileOps.h"   // 文件操作原语（copyDirectory/removeDirectory，可测共享层）

namespace {

// 简易 JSON 解析（不引入第三方库）
std::string jsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\": \"";
    size_t pos = json.find(search);
    if (pos != std::string::npos) {
        pos += search.size();
        size_t end = json.find('"', pos);
        if (end != std::string::npos) return json.substr(pos, end - pos);
    }
    // 尝试无引号值
    std::string search2 = "\"" + key + "\": ";
    pos = json.find(search2);
    if (pos != std::string::npos) {
        pos += search2.size();
        size_t end = json.find_first_of(",\n}", pos);
        if (end != std::string::npos) {
            std::string val = json.substr(pos, end - pos);
            while (!val.empty() && (val.back() == '\r' || val.back() == '\n')) val.pop_back();
            return val;
        }
    }
    return {};
}

struct Manifest {
    std::string tempDir;
    std::string installDir;
    std::string exeName;
    std::string backupDir;
};

bool readManifest(const std::string& path, Manifest& m, std::string& err) {
    std::ifstream f(path);
    if (!f.is_open()) {
        err = "无法打开 manifest: " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string json = ss.str();
    f.close();

    m.tempDir    = jsonString(json, "tempDir");
    m.installDir = jsonString(json, "installDir");
    m.exeName    = jsonString(json, "exeName");
    m.backupDir  = jsonString(json, "backupDir");

    if (m.tempDir.empty() || m.installDir.empty() || m.exeName.empty()) {
        err = "manifest 字段不完整";
        return false;
    }
    return true;
}

FILE* g_logFile = nullptr;

void log(const char* fmt, ...) {
    if (!g_logFile) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(g_logFile, fmt, args);
    fflush(g_logFile);
    va_end(args);
}

// 等待进程退出
bool waitForProcessExit(const char* exeName, DWORD timeoutMs) {
    log("等待 %s 退出...\n", exeName);
    // 与 PROCESSENTRY32(非 W 版本)比较,szExeFile 为 CHAR[]，用 ANSI 严格比较
    CHAR targetName[MAX_PATH];
    strncpy_s(targetName, exeName, MAX_PATH - 1);
    targetName[MAX_PATH - 1] = '\0';

    DWORD elapsed = 0;
    const DWORD interval = 500;
    while (elapsed < timeoutMs) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) break;

        PROCESSENTRY32 pe{};
        pe.dwSize = sizeof(pe);
        bool found = false;
        if (Process32First(snapshot, &pe)) {
            do {
                if (_stricmp(pe.szExeFile, targetName) == 0) {
                    found = true;
                    break;
                }
            } while (Process32Next(snapshot, &pe));
        }
        CloseHandle(snapshot);

        if (!found) {
            log("%s 已退出\n", exeName);
            return true;
        }
        Sleep(interval);
        elapsed += interval;
    }

    log("等待 %s 退出超时 (%lu ms)\n", exeName, timeoutMs);
    return false;
}

// 递归复制目录 / 递归删除目录：已搬移至 UpdaterFileOps.{h,cpp}（issue #22 可测性拆分），
// 备份步骤的失败返回值检查见下方 Step 2。

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // 解析命令行
    int argc;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::string manifestPath, logPath;
    for (int i = 1; i < argc; ++i) {
        char buf[1024];
        WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, buf, sizeof(buf), nullptr, nullptr);
        if (strcmp(buf, "--manifest") == 0 && i + 1 < argc) {
            char buf2[1024];
            WideCharToMultiByte(CP_UTF8, 0, wargv[i+1], -1, buf2, sizeof(buf2), nullptr, nullptr);
            manifestPath = buf2;
            ++i;
        } else if (strcmp(buf, "--log") == 0 && i + 1 < argc) {
            char buf2[1024];
            WideCharToMultiByte(CP_UTF8, 0, wargv[i+1], -1, buf2, sizeof(buf2), nullptr, nullptr);
            logPath = buf2;
            ++i;
        }
    }
    LocalFree(wargv);

    if (!logPath.empty()) g_logFile = fopen(logPath.c_str(), "a");
    log("=== Updater 启动 ===\n");
    log("manifest: %s\n", manifestPath.c_str());

    Manifest m;
    std::string err;
    if (!readManifest(manifestPath, m, err)) {
        log("FATAL: %s\n", err.c_str());
        if (g_logFile) fclose(g_logFile);
        MessageBoxA(nullptr, (std::string("更新失败: ") + err).c_str(),
                    "DeviceForge Updater", MB_ICONERROR);
        return 1;
    }

    // Step 1: 等待 DF 退出
    if (!waitForProcessExit(m.exeName.c_str(), 30000)) {
        int ret = MessageBoxA(nullptr,
            "DeviceForge 未能在 30 秒内退出。\n请手动关闭 DeviceForge 后点击 [重试],或 [取消] 中止更新。",
            "DeviceForge Updater", MB_RETRYCANCEL | MB_ICONWARNING);
        if (ret == IDRETRY) {
            if (!waitForProcessExit(m.exeName.c_str(), 60000)) {
                MessageBoxA(nullptr, "更新失败:DeviceForge 仍未退出。",
                    "DeviceForge Updater", MB_ICONERROR);
                if (g_logFile) fclose(g_logFile);
                return 1;
            }
        } else {
            log("用户取消更新\n");
            if (g_logFile) fclose(g_logFile);
            return 0;
        }
    }

    // Step 2: 备份（issue #22：备份失败必须中止更新。否则替换失败时的"回滚"
    // 会把残缺备份覆盖到安装目录，毁掉原本可运行的旧版且无二次恢复手段）
    log("创建备份: %s -> %s\n", m.installDir.c_str(), m.backupDir.c_str());
    if (!copyDirectory(m.installDir.c_str(), m.backupDir.c_str())) {
        log("ERROR: 创建备份失败,中止更新(未执行任何替换)\n");
        MessageBoxA(nullptr,
            "更新失败:创建备份出错。\n原版本未被修改,请检查磁盘空间或文件占用后重试。",
            "DeviceForge Updater", MB_ICONERROR);
        if (g_logFile) fclose(g_logFile);
        return 1;
    }

    // Step 3: 替换
    log("替换文件: %s -> %s\n", m.tempDir.c_str(), m.installDir.c_str());
    if (!copyDirectory(m.tempDir.c_str(), m.installDir.c_str())) {
        log("ERROR: 文件替换失败,开始回滚\n");
        copyDirectory(m.backupDir.c_str(), m.installDir.c_str());
        MessageBoxA(nullptr,
            "更新失败:文件替换出错。\n旧版本已恢复,请稍后重试。",
            "DeviceForge Updater", MB_ICONERROR);
        if (g_logFile) fclose(g_logFile);
        return 1;
    }

    // Step 4: 清理
    log("清理临时文件\n");
    removeDirectory(m.backupDir.c_str());
    removeDirectory(m.tempDir.c_str());
    DeleteFileA(manifestPath.c_str());

    // Step 5: 启动新版
    std::string exePath = m.installDir + "\\" + m.exeName;
    log("启动新版: %s\n", exePath.c_str());

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(exePath.c_str(), nullptr, nullptr, nullptr, FALSE,
                        0, nullptr, m.installDir.c_str(), &si, &pi)) {
        log("ERROR: 启动新版失败 (err=%lu)\n", GetLastError());
        MessageBoxA(nullptr,
            "更新完成但启动新版失败。\n请手动运行 DeviceForge.exe。",
            "DeviceForge Updater", MB_ICONWARNING);
        if (g_logFile) fclose(g_logFile);
        return 1;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    log("=== Updater 完成 ===\n");
    if (g_logFile) fclose(g_logFile);
    return 0;
}
