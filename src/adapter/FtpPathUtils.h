// FtpPathUtils.h — FTP Adapter 内部路径契约辅助

#pragma once

#include <string>

namespace adapter_internal {

inline bool isFtpUrlPathUnreserved(unsigned char value)
{
    return (value >= 'A' && value <= 'Z')
        || (value >= 'a' && value <= 'z')
        || (value >= '0' && value <= '9')
        || value == '-' || value == '.' || value == '_' || value == '~';
}

inline std::string percentEncodeFtpUrlPath(const std::string& remotePath)
{
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(remotePath.size());
    for (const char character : remotePath) {
        const auto value = static_cast<unsigned char>(character);
        if (value == '/' || isFtpUrlPathUnreserved(value)) {
            encoded.push_back(character);
            continue;
        }
        encoded.push_back('%');
        encoded.push_back(hex[value >> 4]);
        encoded.push_back(hex[value & 0x0f]);
    }
    return encoded;
}

inline std::string buildFtpUrl(bool useFtps,
                               const std::string& ip,
                               int port,
                               const std::string& remotePath)
{
    std::string url = (useFtps ? "ftps://" : "ftp://") + ip + ':'
        + std::to_string(port) + '/';
    const std::string pathWithoutLeadingSlash =
        (!remotePath.empty() && remotePath.front() == '/')
            ? remotePath.substr(1)
            : remotePath;
    url += percentEncodeFtpUrlPath(pathWithoutLeadingSlash);
    return url;
}

inline std::string ftpDirectoryPathForListing(const std::string& remotePath)
{
    if (remotePath.empty())
        return "/";
    if (remotePath.back() == '/')
        return remotePath;
    return remotePath + '/';
}

} // namespace adapter_internal
