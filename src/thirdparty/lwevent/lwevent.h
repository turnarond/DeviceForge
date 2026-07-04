/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: lwevent.h
 *
 * Date: 2023-04-27
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#ifndef _LWEVENT_H_
#define _LWEVENT_H_

#ifdef _WIN32
#include <stdbool.h>
/* 使用 void* 避免引入 <windows.h> — lwcomm.h 统一管理 winsock2/windows 顺序 */
typedef void* EVENT_HANDLE;

/* Export/import macros for Windows DLL */
#ifdef LWEVENT_EXPORTS
#define LWEVENT_API __declspec(dllexport)
#else
#define LWEVENT_API __declspec(dllimport)
#endif
#else
#include <pthread.h>
typedef struct event_t *EVENT_HANDLE;
#define LWEVENT_API
#endif
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    #define EVENT_SUCCESS   0                           /* Event succeeded */
    #define EVENT_TIMEOUT   1                           /* Event wait timed out */
    #define EVENT_ERROR    -1                           /* Event wait failed */

    /* Create event object */
    LWEVENT_API EVENT_HANDLE EventCreate(bool manual_reset, bool init_state);

    /* Wait for event (blocking) */
    LWEVENT_API int EventWait(EVENT_HANDLE hevent);

    /* Wait for event with timeout */
    LWEVENT_API int EventTimedWait(EVENT_HANDLE hevent, long milliseconds);

    /* Set event to signaled state */
    LWEVENT_API int EventSet(EVENT_HANDLE hevent);

    /* Reset event to non-signaled state */
    LWEVENT_API int EventReset(EVENT_HANDLE hevent);

    /* Destroy event (deprecated, use EventDestroySafe instead) */
    LWEVENT_API void EventDestroy(EVENT_HANDLE hevent);

    /* Safely destroy event and set pointer to NULL */
    LWEVENT_API void EventDestroySafe (EVENT_HANDLE* hevent);

#ifdef __cplusplus
}
#endif

#endif /* _LWEVENT_H_ */

/*
 * end
 */
