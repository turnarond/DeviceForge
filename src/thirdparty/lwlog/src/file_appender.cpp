/*
 * Copyright (c) 2025-2026 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: file_appender.cpp
 *
 * Date: 2025-04-17
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#include "file_appender.h"
#include "pattern_formatter.h"
#include "lwcomm/lwcomm.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <map>
#include <algorithm>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#define mkdir(path, mode) _mkdir(path)
#define rmdir(path) _rmdir(path)
#define access(path, mode) _access(path, mode)
#define localtime_r(timep, result) localtime_s(result, timep)
/* Use raw fd API directly — no CRT FILE* wrapper */
#define open_file_impl(path, flags, mode) _open(path, flags, mode)
#define write(fd, buf, size) _write(fd, buf, size)
#define close(fd) _close(fd)
#define fsync(fd) _commit(fd)
#define O_WRONLY_APPEND (_O_WRONLY | _O_APPEND | _O_CREAT | _O_BINARY)

#include <cstddef>      // for ptrdiff_t
#ifdef _MSC_VER
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#else
#ifndef ssize_t
typedef ptrdiff_t ssize_t;
#endif
#endif

// Windows uses _stat instead of stat
typedef struct _stat stat_t;
#define stat(path, buf) _stat(path, buf)

// Windows directory iteration helpers
struct dirent {
    char d_name[MAX_PATH];
};

struct DIR {
    HANDLE hFind;
    WIN32_FIND_DATA findData;
    dirent dirEnt;
    bool first;
};

static DIR* opendir(const char* path) {
    std::string searchPath = std::string(path) + "/*";
    DIR* dir = new DIR();
    dir->hFind = FindFirstFileA(searchPath.c_str(), &dir->findData);
    if (dir->hFind == INVALID_HANDLE_VALUE) {
        delete dir;
        return nullptr;
    }
    dir->first = true;
    return dir;
}

static dirent* readdir(DIR* dir) {
    if (!dir) return nullptr;

    if (dir->first) {
        dir->first = false;
    } else {
        if (!FindNextFileA(dir->hFind, &dir->findData)) {
            return nullptr;
        }
    }

    strncpy(dir->dirEnt.d_name, dir->findData.cFileName, MAX_PATH - 1);
    dir->dirEnt.d_name[MAX_PATH - 1] = '\0';
    return &dir->dirEnt;
}

static int closedir(DIR* dir) {
    if (!dir) return -1;
    FindClose(dir->hFind);
    delete dir;
    return 0;
}

#else
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
/* Use raw fd API directly — no CRT FILE* wrapper */
#define open_file_impl(path, flags, mode) open(path, flags, mode)
#define O_WRONLY_APPEND (O_WRONLY | O_APPEND | O_CREAT)
typedef struct stat stat_t;
#endif

// ---- Utility functions ----------------------------------------------------

std::string FileAppender::make_date() {
    time_t now = time(nullptr);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char buf[16];
    snprintf(buf, sizeof(buf), "%04d%02d%02d",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday);
    return buf;
}

std::string FileAppender::basename() const {
    size_t pos = filename_.find_last_of("/\\");
    if (pos != std::string::npos) {
        return filename_.substr(pos + 1);
    }
    return filename_;
}

std::string FileAppender::make_archive_name(const std::string& date, int seq) const {
    return filename_ + "." + date + "." + std::to_string(seq);
}

// ---- Constructor / Destructor --------------------------------------------

FileAppender::FileAppender(const std::string& filename)
    : filename_(filename), file_handle_(-1), current_file_size_(0),
      writes_since_fsync_(0), last_fsync_time_(std::chrono::steady_clock::now()) {
    formatter_ = std::make_shared<PatternFormatter>();

    // Extract log directory
    size_t last_slash = filename_.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        log_dir_ = filename_.substr(0, last_slash);
    } else {
        log_dir_ = ".";
    }

    current_date_ = make_date();
    open_file();
}

FileAppender::~FileAppender() {
    close_file();
}

// ---- File operations ------------------------------------------------------

bool FileAppender::open_file() {
    if (file_handle_ != -1) close_file();

    if (!LWFileSystem::CreateDirectory(log_dir_, true)) {
        file_handle_ = -1;
        return false;
    }

#ifdef _WIN32
    file_handle_ = open_file_impl(filename_.c_str(), O_WRONLY_APPEND, _S_IREAD | _S_IWRITE);
#else
    file_handle_ = open_file_impl(filename_.c_str(), O_WRONLY_APPEND, S_IRUSR | S_IWUSR | S_IRGRP);
#endif
    if (file_handle_ != -1) {
        stat_t st;
        if (stat(filename_.c_str(), &st) == 0) {
            current_file_size_ = st.st_size;
        }
        return true;
    }
    return false;
}

void FileAppender::close_file() {
    if (file_handle_ != -1) {
        // Final flush before close
        if (writes_since_fsync_ > 0) {
            fsync(file_handle_);
            writes_since_fsync_ = 0;
        }
        ::close(file_handle_);
        file_handle_ = -1;
        current_file_size_ = 0;
    }
}

// ---- Archive sequence lookup ----------------------------------------------

int FileAppender::find_max_sequence(const std::string& date) const {
    int max_seq = 0;
    // Use basename for directory matching — readdir returns bare filenames
    std::string prefix = basename() + "." + date + ".";

    DIR* dir = opendir(log_dir_.c_str());
    if (!dir) return 0;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (name.compare(0, prefix.size(), prefix) != 0) continue;

        const char* num_str = name.c_str() + prefix.size();
        char* end;
        long seq = strtol(num_str, &end, 10);
        if (end != num_str && *end == '\0' && seq > 0 && seq <= 9999) {
            if (seq > max_seq) max_seq = static_cast<int>(seq);
        }
    }
    closedir(dir);
    return max_seq;
}

// ---- Date-based rotation --------------------------------------------------

bool FileAppender::rotate_by_date(const std::string& new_date) {
    // Save file size before close_file() zeros it
    size_t saved_size = current_file_size_;
    std::string old_date = current_date_;
    close_file();

    // Only archive if something was written to the current file
    if (!old_date.empty() && saved_size > 0) {
        std::string archive = make_archive_name(old_date, 1);
        int seq = 1;
        stat_t st;
        while (stat(archive.c_str(), &st) == 0) {
            archive = make_archive_name(old_date, ++seq);
        }
        if (rename(filename_.c_str(), archive.c_str()) != 0) {
            // If rename fails, leave the file as-is; data is not lost
        }
    }

    current_date_ = new_date;
    bool ok = open_file();
    cleanup_old_logs();
    return ok;
}

// ---- Size-based rotation --------------------------------------------------

bool FileAppender::rotate_by_size(const std::string& today) {
    close_file();

    int max_seq = find_max_sequence(today);
    int next_seq = max_seq + 1;

    // Daily limit reached — delete oldest (seq 1) then shift remaining down
    if (next_seq > options_.max_file_count) {
        std::string oldest = make_archive_name(today, 1);
        remove(oldest.c_str());
        for (int i = 2; i <= max_seq; ++i) {
            std::string old_name = make_archive_name(today, i);
            stat_t st;
            if (stat(old_name.c_str(), &st) == 0) {
                std::string new_name = make_archive_name(today, i - 1);
                rename(old_name.c_str(), new_name.c_str());
            }
        }
        next_seq = max_seq;             // Now free after shifting
    }

    std::string archive = make_archive_name(today, next_seq);
    if (rename(filename_.c_str(), archive.c_str()) != 0) {
        // Rename failed (e.g. target exists on Windows) — reopen original
        bool ok = open_file();
        cleanup_old_logs();
        return ok;
    }

    bool ok = open_file();
    cleanup_old_logs();
    return ok;
}

// ---- Expiry cleanup -------------------------------------------------------

void FileAppender::cleanup_old_logs() {
    if (options_.save_days <= 0) return;

    // Compute cutoff as integer YYYYMMDD (robust against format changes)
    time_t cutoff = time(nullptr) - options_.save_days * 86400;
    struct tm tm_buf;
    localtime_r(&cutoff, &tm_buf);
    int cutoff_num = (tm_buf.tm_year + 1900) * 10000 + (tm_buf.tm_mon + 1) * 100 + tm_buf.tm_mday;

    // Use basename for directory matching — readdir returns bare filenames
    std::string archive_prefix = basename() + ".";

    DIR* dir = opendir(log_dir_.c_str());
    if (!dir) return;

    std::vector<std::string> to_delete;
    std::map<std::string, std::vector<std::string>> date_groups;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (name.compare(0, archive_prefix.size(), archive_prefix) != 0) continue;

        // Parse archive pattern {basename}.{YYYYMMDD}.{N}
        std::string suffix = name.substr(archive_prefix.size());  // "20260604.1"
        if (suffix.size() < 10) continue;

        std::string date_part = suffix.substr(0, 8);
        std::string seq_part  = suffix.substr(9);

        if (date_part.find_first_not_of("0123456789") != std::string::npos) continue;
        if (seq_part.find_first_not_of("0123456789") != std::string::npos) continue;

        int date_num = std::stoi(date_part);
        if (date_num < cutoff_num) {
            to_delete.push_back(name);
        } else {
            date_groups[date_part].push_back(name);
        }
    }
    closedir(dir);

    for (const auto& f : to_delete) {
        remove((log_dir_ + "/" + f).c_str());
    }

    // C++11 compatible iteration (no structured bindings for SylixOS)
    for (auto& entry_pair : date_groups) {
        const std::string& date = entry_pair.first;
        std::vector<std::string>& files = entry_pair.second;
        if (static_cast<int>(files.size()) > options_.max_file_count) {
            std::sort(files.begin(), files.end());
            size_t excess = static_cast<size_t>(files.size())
                - static_cast<size_t>(options_.max_file_count);
            for (size_t i = 0; i < excess; ++i) {
                remove((log_dir_ + "/" + files[i]).c_str());
            }
        }
    }
}

// ---- Core write path ------------------------------------------------------

void FileAppender::append(const LogEntry& entry) {
    std::lock_guard<std::mutex> lock(write_mutex_);

    if (file_handle_ == -1) {
        open_file();
        if (file_handle_ == -1) return;
    }

    // Date change detection
    std::string today = make_date();
    if (current_date_ != today) {
        if (!rotate_by_date(today)) return;  // Reopen failed — drop this entry
    }

    std::string formatted_message = formatter_->format(entry) + "\n";

    // Size overflow check (max_file_size already in bytes, converted by LogManager)
    if (options_.max_file_size > 0 &&
        current_file_size_ + formatted_message.size() > options_.max_file_size) {
        if (!rotate_by_size(today)) return;  // Rotation/reopen failed — drop this entry
    }

    // Defensive: double-check file_handle_ still valid after rotation
    if (file_handle_ == -1) return;

    ssize_t bytes_written = write(file_handle_, formatted_message.c_str(),
                                  formatted_message.size());
    if (bytes_written != -1) {
        current_file_size_ += bytes_written;

        // Periodic batch fsync: flush every 100 writes or every 1 second
        writes_since_fsync_++;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_fsync_time_).count();
        if (writes_since_fsync_ >= 100 || elapsed >= 1) {
            fsync(file_handle_);
            writes_since_fsync_ = 0;
            last_fsync_time_ = now;
        }
    }
}

void FileAppender::set_formatter(std::shared_ptr<IFormatter> formatter) {
    formatter_ = std::move(formatter);
}

void FileAppender::set_options(const LogOptions& options) {
    options_ = options;
}
