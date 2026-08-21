#include "ui/LocalFileSource.h"
#include <QDateTime>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace {

// QString → std::filesystem::path 的安全转换：
// 禁止 QString::toStdWString() 直接构造——其返回的 std::wstring 在 Qt DLL 内分配，
// Debug 配置下 Qt DLL（/MDd+IDL=2）与 exe（/MD+IDL=0）CRT 不匹配，跨边界析构会崩溃
// （启动崩溃栈：std::wstring::_Deallocate in LocalFileSource::list）。
// 用 QByteArray（Qt 类型，跨 DLL 安全）+ const char* 在 exe 内构造 path。
inline fs::path toFsPath(const QString& p)
{
    return fs::path(p.toLocal8Bit().constData());   // Windows ANSI（GBK）编码，fs::path(char*) 同按 ANSI 解释
}

// 文件时间戳 → ISO 8601 "yyyy-MM-dd HH:mm:ss"（C++17 兼容转换：file_clock → system_clock）
QString formatFileTime(const fs::file_time_type& ft)
{
    const auto sct = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return QDateTime::fromSecsSinceEpoch(
        std::chrono::system_clock::to_time_t(sct)).toString("yyyy-MM-dd HH:mm:ss");
}

// fs::perms → "rwxrwxrwx" 字符串（owner/group/others 各 rwx 位）
QString formatPermissions(fs::perms perm)
{
    auto bit = [perm](fs::perms p) { return (perm & p) != fs::perms::none; };
    auto triplet = [bit](fs::perms r, fs::perms w, fs::perms x) {
        QString s;
        s += bit(r) ? 'r' : '-';
        s += bit(w) ? 'w' : '-';
        s += bit(x) ? 'x' : '-';
        return s;
    };
    return triplet(fs::perms::owner_read,  fs::perms::owner_write,  fs::perms::owner_exec)
         + triplet(fs::perms::group_read,  fs::perms::group_write,  fs::perms::group_exec)
         + triplet(fs::perms::others_read, fs::perms::others_write, fs::perms::others_exec);
}

} // namespace

std::vector<FtpFileInfo> LocalFileSource::list(const QString& path)
{
    std::vector<FtpFileInfo> result;

    std::error_code ec;
    fs::directory_iterator it(toFsPath(path), ec);
    if (ec) {
        m_lastError = QStringLiteral("列目录失败: %1").arg(QString::fromStdString(ec.message()));
        return result;
    }

    const fs::directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        const auto& entry = *it;
        const std::string name = entry.path().filename().string();
        if (name == "." || name == "..") continue;   // 不返回（面板统一补充 ".."）

        FtpFileInfo info;
        info.name = name;

        std::error_code sec;
        const auto st = entry.status(sec);
        info.isDir = fs::is_directory(st);
        if (!info.isDir) {
            std::error_code sizec;
            info.size = static_cast<uint64_t>(entry.file_size(sizec));
            if (sizec) info.size = 0;
        }
        if (!sec) info.permissions = formatPermissions(st.permissions()).toStdString();

        std::error_code timeec;
        const auto ft = entry.last_write_time(timeec);
        if (!timeec) info.dateTime = formatFileTime(ft).toStdString();

        result.push_back(std::move(info));
    }
    if (ec) {
        m_lastError = QStringLiteral("列目录失败: %1").arg(QString::fromStdString(ec.message()));
        return result;
    }

    m_lastError.clear();
    return result;
}

bool LocalFileSource::mkdir(const QString& path)
{
    // error_code 重载不抛异常（跨 bool 接口安全）；目录已存在时返回 false 但
    // ec 为空——视为幂等成功（与远程 MKD 已存在语义对齐），故仅按 ec 判断
    std::error_code ec;
    std::filesystem::create_directories(toFsPath(path), ec);
    if (ec) {
        m_lastError = QString::fromLocal8Bit(ec.message().c_str());
        return false;
    }
    m_lastError.clear();
    return true;
}

bool LocalFileSource::rename(const QString& oldPath, const QString& newPath)
{
    std::error_code ec;
    fs::rename(toFsPath(oldPath), toFsPath(newPath), ec);
    if (ec) {
        m_lastError = QStringLiteral("重命名失败: %1").arg(QString::fromStdString(ec.message()));
        return false;
    }
    m_lastError.clear();
    return true;
}

bool LocalFileSource::remove(const QString& path, bool isDir)
{
    std::error_code ec;
    if (isDir)
        fs::remove_all(toFsPath(path), ec);   // 目录：递归删除
    else
        fs::remove(toFsPath(path), ec);       // 文件
    if (ec) {
        m_lastError = QStringLiteral("删除失败: %1").arg(QString::fromStdString(ec.message()));
        return false;
    }
    m_lastError.clear();
    return true;
}

bool LocalFileSource::clearDirectory(const QString& path)
{
    std::error_code ec;
    fs::directory_iterator it(toFsPath(path), ec);
    if (ec) {
        m_lastError = QStringLiteral("清空目录失败: %1").arg(QString::fromStdString(ec.message()));
        return false;
    }

    const fs::directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code rec;
        if (it->is_directory(rec))
            fs::remove_all(it->path(), rec);
        else
            fs::remove(it->path(), rec);
        if (rec) {
            m_lastError = QStringLiteral("清空目录失败: %1").arg(QString::fromStdString(rec.message()));
            return false;
        }
    }
    if (ec) {
        m_lastError = QStringLiteral("清空目录失败: %1").arg(QString::fromStdString(ec.message()));
        return false;
    }

    m_lastError.clear();
    return true;   // 目录本身保留
}

bool LocalFileSource::upload(const QString& localPath, const QString& remotePath)
{
    std::error_code ec;
    fs::copy_file(toFsPath(localPath), toFsPath(remotePath),
                  fs::copy_options::overwrite_existing, ec);
    if (ec) {
        m_lastError = QStringLiteral("复制失败: %1").arg(QString::fromStdString(ec.message()));
        return false;
    }
    m_lastError.clear();
    return true;
}

bool LocalFileSource::download(const QString& remotePath, const QString& localPath)
{
    // 本地源语义：download = 从 "远程"（实为另一本地路径）复制到本地
    return upload(remotePath, localPath);
}
