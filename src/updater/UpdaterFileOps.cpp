// src/updater/UpdaterFileOps.cpp
// 实现原样搬移自 UpdaterMain.cpp（匿名命名空间 → 具名翻译单元），零行为变更。

#include "updater/UpdaterFileOps.h"
#include <cstring>
#include <string>

bool copyDirectory(const char* src, const char* dst) {
    CreateDirectoryA(dst, nullptr);

    std::string searchPath = std::string(src) + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return false;

    bool ok = true;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        std::string srcPath = std::string(src) + "\\" + fd.cFileName;
        std::string dstPath = std::string(dst) + "\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!copyDirectory(srcPath.c_str(), dstPath.c_str())) ok = false;
        } else {
            if (!CopyFileA(srcPath.c_str(), dstPath.c_str(), FALSE)) {
                ok = false;
            }
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
    return ok;
}

bool removeDirectory(const char* path) {
    std::string searchPath = std::string(path) + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return false;

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        std::string full = std::string(path) + "\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            removeDirectory(full.c_str());
        } else {
            DeleteFileA(full.c_str());
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
    return RemoveDirectoryA(path) != 0;
}
