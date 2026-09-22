// LocalFileOpen.h — Adapter 内部 UTF-8 本地路径文件打开辅助

#pragma once

#include <QString>

#include <cstdio>
#include <string>

namespace adapter_internal {

enum class LocalFileOpenMode {
    Read,
    Write
};

inline FILE* openLocalFileUtf8(const std::string& path, LocalFileOpenMode mode)
{
#ifdef Q_OS_WIN
    const std::wstring nativePath = QString::fromUtf8(path).toStdWString();
    return _wfopen(nativePath.c_str(),
                   mode == LocalFileOpenMode::Read ? L"rb" : L"wb");
#else
    return fopen(path.c_str(), mode == LocalFileOpenMode::Read ? "rb" : "wb");
#endif
}

} // namespace adapter_internal
