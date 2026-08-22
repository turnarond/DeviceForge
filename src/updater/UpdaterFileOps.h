// src/updater/UpdaterFileOps.h
// Updater.exe 文件操作原语 — 从 UpdaterMain.cpp 原样搬移以支持单元测试
// （issue #22：备份守卫依赖 copyDirectory 的失败返回语义，需可测锁定）
//
// 纯 Win32 API + CRT，无 Qt 依赖；供 UpdaterMain 与 tst_updater_fileops 共享。

#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// 递归复制目录（目录树整体拷贝）。任一文件/子目录复制失败时继续处理其余条目，
// 但最终返回 false——调用方（WinMain 备份步骤）必须检查返回值。
bool copyDirectory(const char* src, const char* dst);

// 递归删除目录树。路径不存在或末层 RemoveDirectoryA 失败时返回 false。
bool removeDirectory(const char* path);
