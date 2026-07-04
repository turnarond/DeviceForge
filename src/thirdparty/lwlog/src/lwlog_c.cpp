/*
 * Copyright (c) 2024-2026 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: lwlog_c.cpp .
 *
 * Date: 2026-01-15
 *
 * Author: TraeAI Assistant
 *
 */

#include "../lwlog.h"
#include <stdarg.h>
#include <mutex>

/* Global CLWLog instance for C interface — protected by g_log_mutex */
static CLWLog *g_pGlobalLog = nullptr;
static std::mutex g_log_mutex;

/**
 * @brief Initialize the log system
 * @return 0 on success, -1 on failure
 */
LWLog_API int LWLogInit(void)
{
    try {
        std::lock_guard<std::mutex> lock(g_log_mutex);

        if (g_pGlobalLog) {
            /* Already initialized */
            return 0;
        }

        g_pGlobalLog = new CLWLog();
        return 0;
    } catch (...) {
        return -1;
    }
}

/**
 * @brief Finalize the log system
 * @return 0 on success, -1 on failure
 */
LWLog_API int LWLogFini(void)
{
    try {
        std::lock_guard<std::mutex> lock(g_log_mutex);

        if (g_pGlobalLog) {
            delete g_pGlobalLog;
            g_pGlobalLog = nullptr;
        }

        return 0;
    } catch (...) {
        return -1;
    }
}

/**
 * @brief Log a message with specified log level
 * @param nLogLevel Log level (LW_LOGLEVEL_DEBUG, LW_LOGLEVEL_INFO, etc.)
 * @param szFormat Format string, printf style
 * @param ... Variable arguments
 * @return 0 on success, -1 on failure
 *
 * Note: Source location (__FILE__/__LINE__) always reports lwlog.cpp.
 * This is a known limitation of the C API; prefer the C++ LWLOG_* macros
 * for accurate source location tracking.
 */
LWLog_API int LogMessage(int nLogLevel, const char *szFormat, ...)
{
    try {
        std::lock_guard<std::mutex> lock(g_log_mutex);

        if (!g_pGlobalLog || !szFormat) {
            return -1;
        }

        va_list ap;
        va_start(ap, szFormat);
        g_pGlobalLog->_Internal_LogMessageV(nLogLevel, szFormat, ap);
        va_end(ap);

        return 0;
    } catch (...) {
        return -1;
    }
}

/*
 * end
 */
