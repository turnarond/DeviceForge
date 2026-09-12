// FtpPathUtils.h — FTP Adapter 内部路径契约辅助

#pragma once

#include <string>

namespace adapter_internal {

inline std::string ftpDirectoryPathForListing(const std::string& remotePath)
{
    if (remotePath.empty())
        return "/";
    if (remotePath.back() == '/')
        return remotePath;
    return remotePath + '/';
}

} // namespace adapter_internal
