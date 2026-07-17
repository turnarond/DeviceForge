/*
 * Copyright (c) 2025-2026 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: file_appender.h
 *
 * Date: 2025-04-17
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#ifndef _FILE_APPENDER_H_
#define _FILE_APPENDER_H_

#include "../lwlog.h"
#include <memory>
#include <mutex>
#include <string>
#include <ctime>
#include <chrono>

class FileAppender : public IAppender {
public:
    explicit FileAppender(const std::string& filename);
    ~FileAppender() override;

    void append(const LogEntry& entry) override;
    void set_formatter(std::shared_ptr<IFormatter> formatter) override;
    void set_options(const LogOptions& options) override;

private:
    std::string filename_;              // Full path to current log file
    std::string log_dir_;               // Log directory
    std::string current_date_;          // Date when current file was opened YYYYMMDD
    std::shared_ptr<IFormatter> formatter_;
    LogOptions options_;
    std::mutex write_mutex_;            // Per-instance lock (replaces global g_file_mutex)
    int    file_handle_;
    size_t current_file_size_;
    int    writes_since_fsync_;         // Counter for periodic fsync
    std::chrono::steady_clock::time_point last_fsync_time_;

    bool open_file();
    void close_file();

    // Date-driven rotation (returns false if reopen fails)
    bool rotate_by_date(const std::string& new_date);
    // Size-driven rotation (returns false if reopen fails)
    bool rotate_by_size(const std::string& today);

    // Clean up expired archives
    void cleanup_old_logs();

    // Utility methods
    static std::string make_date();     // Returns "YYYYMMDD"
    std::string basename() const;       // Extract filename from full path
    std::string make_archive_name(const std::string& date, int seq) const;
    int  find_max_sequence(const std::string& date) const;
};

#endif // _FILE_APPENDER_H_

/*
 * end
 */
