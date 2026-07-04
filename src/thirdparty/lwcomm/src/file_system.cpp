#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>
#include <string>
#include <string_view>
#include <utility>
#include <algorithm>
#include <mutex>
#include <unordered_set>

#include "lwcomm/lwcomm.h"

namespace fs = std::filesystem;

namespace {
    /* Thread-safe current working directory cache (reduces system calls) */
    std::string cached_working_dir;
    std::mutex cwd_mutex;

    void refresh_working_directory_cache() 
    {
        std::lock_guard lock(cwd_mutex);
        std::error_code ec;
        cached_working_dir = fs::current_path(ec).string();
        if (ec) cached_working_dir.clear();
    }

    /* Convert the path to a canonical form (handle ., .., redundant separators, etc.) */
    std::string normalize_path(std::string_view path) 
    {
        try {
            /*  Convert to absolute path before normalization to avoid relative path issues */
            fs::path p = fs::absolute(path);
            return fs::weakly_canonical(p).string();
        } catch (...) {
            fs::path p(path);
            /*  Lexically normalize the path */ 
            p = p.lexically_normal();
            return p.string();
        }
    }

    bool atomic_write(const fs::path& path, const char* data, size_t size) 
    {
        /* Write to a temporary file first */
        fs::path temp_path = path;
        temp_path += ".tmp";
        
        std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        
        out.write(data, static_cast<std::streamsize>(size));
        if (!out) {
            fs::remove(temp_path);
            return false;
        }
        out.close();

        std::error_code ec;
        fs::rename(temp_path, path, ec);
        if (ec) {
            fs::remove(temp_path);
            return false;
        }
        return true;
    }
}

bool LWFileSystem::WriteToFile(std::string_view file_path, std::string_view content) 
{
    return atomic_write(fs::path(file_path), content.data(), content.size());
}

bool LWFileSystem::WriteToFile(std::string_view path, const std::vector<char>& data) 
{
    return atomic_write(fs::path(path), data.data(), data.size());
}

std::vector<char> LWFileSystem::ReadFile(std::string_view path) 
{
    std::ifstream file(normalize_path(path), std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};

    const auto size = file.tellg();
    if (size < 0) return {};

    std::vector<char> buffer(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    
    return file.good() ? buffer : std::vector<char>();
}

bool LWFileSystem::CreateFile(std::string_view path) 
{
    std::ofstream file(normalize_path(path));
    return file.is_open();
}

bool LWFileSystem::DeleteFile(std::string_view path) 
{
    std::error_code ec;
    bool success = fs::remove(normalize_path(path), ec);
    return success && !ec;
}

bool LWFileSystem::FileExist(std::string_view path) 
{
    std::error_code ec;
    return fs::exists(normalize_path(path), ec) && !ec && fs::is_regular_file(path, ec);
}

bool LWFileSystem::IsFile(std::string_view path) 
{
    return FileExist(path);
}

bool LWFileSystem::CreateDirectory(std::string_view path, bool recursive) 
{
    std::error_code ec;
    fs::path p = normalize_path(path);
    
    if (recursive) {
        fs::create_directories(p, ec);
    } else {
        fs::create_directory(p, ec);
    }
    return !ec && fs::is_directory(p, ec);
}

bool LWFileSystem::RemoveDirectory(std::string_view path, bool recursive) 
{
    std::error_code ec;
    fs::path p = normalize_path(path);
    
    if (!fs::exists(p, ec) || !fs::is_directory(p, ec)) {
        return false;
    }
    
    if (recursive) {
        fs::remove_all(p, ec);
    } else {
        fs::remove(p, ec);
    }
    return !ec;
}

bool LWFileSystem::RenamePath(std::string_view oldPath, std::string_view newPath) 
{
    std::error_code ec;
    fs::rename(normalize_path(oldPath), normalize_path(newPath), ec);
    return !ec;
}

bool LWFileSystem::IsDirectory(std::string_view path) 
{
    std::error_code ec;
    return fs::is_directory(normalize_path(path), ec) && !ec;
}

bool LWFileSystem::IsDirectoryEmpty(std::string_view path) 
{
    std::error_code ec;
    fs::directory_iterator it(normalize_path(path), ec);
    if (ec) return false;
    
    return it == fs::directory_iterator();
}

std::string LWFileSystem::ConcatPath(std::string_view parent, std::string_view child) 
{
    try {
        fs::path p(parent);
        p /= child;
        return p.string();
    } catch (...) {
        std::string result(parent);
        if (!result.empty() && result.back() != fs::path::preferred_separator) {
            result += fs::path::preferred_separator;
        }
        result += child;
        return result;
    }
}

std::vector<LWFileSystem::Entry> LWFileSystem::ListDirectory(std::string_view path) 
{
    std::vector<Entry> result;
    std::error_code ec;
    fs::path p = normalize_path(path);
    
    if (!fs::exists(p, ec) || !fs::is_directory(p, ec)) {
        return result;
    }

    std::unordered_set<std::string> visited;
    visited.insert(fs::canonical(p, ec).string());
    
    for (const auto& entry : fs::directory_iterator(p, ec)) {
        if (ec) break;
        
        if (entry.path().filename() == "." || entry.path().filename() == "..") {
            continue;
        }
        
        if (entry.is_symlink(ec)) {
            fs::path target = fs::read_symlink(entry.path(), ec);
            if (!ec) {
                fs::path canonical_target = fs::canonical(target, ec);
                if (!ec && visited.find(canonical_target.string()) != visited.end()) {
                    continue;
                }
            }
        }
        
        Entry e;
        e.is_directory = entry.is_directory(ec);
        e.name = entry.path().filename().string();
        e.path = entry.path().string();
        result.push_back(e);
    }
    return result;
}

std::string LWFileSystem::AbsolutePath(std::string_view path) 
{
    try {
        return fs::absolute(normalize_path(path)).string();
    } catch (...) {
        return normalize_path(path);
    }
}

std::string LWFileSystem::CurrentWorkingDirectory() 
{
    refresh_working_directory_cache();
    return cached_working_dir;
}

bool LWFileSystem::ChangeWorkingDirectory(std::string_view path) 
{
    std::error_code ec;
    fs::current_path(normalize_path(path), ec);
    if (!ec) {
        refresh_working_directory_cache();
    }
    return !ec;
}

std::pair<std::string, std::string> LWFileSystem::SplitPath(std::string_view fullPath) 
{
    fs::path p = normalize_path(fullPath);
    return {
        p.parent_path().string(),
        p.filename().string()
    };
}

bool LWFileSystem::IsAbsolutePath(std::string_view path) 
{
    return fs::path(path).is_absolute();
}