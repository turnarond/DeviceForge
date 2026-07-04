/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: lwevent.c
 *
 * Date: 2023-04-27
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#include "lwevent.h"

#ifdef _WIN32
#include <windows.h>

EVENT_HANDLE EventCreate (bool manual_reset, bool init_state)
{
    return CreateEvent(NULL, manual_reset, init_state, NULL);
}

int EventWait (EVENT_HANDLE hevent)
{
    if (!hevent) return EVENT_ERROR;
    DWORD ret = WaitForSingleObject(hevent, INFINITE);
    return (ret == WAIT_OBJECT_0) ? EVENT_SUCCESS : EVENT_ERROR;
}

int EventTimedWait (EVENT_HANDLE hevent, long milliseconds)
{
    if (!hevent || milliseconds < 0) return EVENT_ERROR;
    DWORD ret = WaitForSingleObject(hevent, (DWORD)milliseconds);
    if (ret == WAIT_OBJECT_0)      return EVENT_SUCCESS;
    if (ret == WAIT_TIMEOUT)       return EVENT_TIMEOUT;
    return EVENT_ERROR;
}

int EventSet (EVENT_HANDLE hevent)
{
    if (!hevent) return -1;
    return SetEvent(hevent) ? EVENT_SUCCESS : EVENT_ERROR;
}

int EventReset (EVENT_HANDLE hevent)
{
    if (!hevent) return -1;
    return ResetEvent(hevent) ? EVENT_SUCCESS : EVENT_ERROR;
}

void EventDestroy (EVENT_HANDLE hevent)
{
    if (hevent) CloseHandle(hevent);
}

void EventDestroySafe (EVENT_HANDLE* hevent)
{
    if (hevent && *hevent) {
        CloseHandle(*hevent);
        *hevent = NULL;
    }
}

#else /* POSIX */

#include <malloc.h>
#include <errno.h>
#include <sys/time.h>

struct event_t {
    bool state;
    bool manual_reset;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
};

EVENT_HANDLE EventCreate (bool manual_reset, bool init_state)
{
    EVENT_HANDLE hevent = (EVENT_HANDLE)calloc(1, sizeof(struct event_t));
    if (hevent == NULL) {
        return NULL;
    }

    hevent->state = init_state;
    hevent->manual_reset = manual_reset;
    if (pthread_mutex_init(&hevent->mutex, NULL)) {
        free(hevent);
        return NULL;
    }

    if (pthread_cond_init(&hevent->cond, NULL)) {
        pthread_mutex_destroy(&hevent->mutex);
        free(hevent);
        return NULL;
    }

    return hevent;
}

int EventWait (EVENT_HANDLE hevent)
{
    if (hevent == NULL) {
        return -1;
    }

    if (pthread_mutex_lock(&hevent->mutex)) {
        return -1;
    }

    while (!hevent->state) {
        if (pthread_cond_wait(&hevent->cond, &hevent->mutex)) {
            pthread_mutex_unlock(&hevent->mutex);
            return -1;
        }
    }

    if (!hevent->manual_reset) {
        hevent->state = false;
    }

    if (pthread_mutex_unlock(&hevent->mutex)) {
        return -1;
    }
    return 0;
}

int EventTimedWait (EVENT_HANDLE hevent, long milliseconds)
{
    int rc = 0;
    struct timespec abstime;

    if (hevent == NULL || milliseconds < 0) {
        return EVENT_ERROR;
    }

    if (clock_gettime(CLOCK_REALTIME, &abstime) != 0) {
        return -1;
    }
    abstime.tv_sec += milliseconds / 1000;
    abstime.tv_nsec += (milliseconds % 1000) * 1000000L;
    if (abstime.tv_nsec >= 1000000000L) {
        abstime.tv_sec++;
        abstime.tv_nsec -= 1000000000L;
    }

    if (pthread_mutex_lock(&hevent->mutex) != 0) {
        return EVENT_ERROR;
    }

    while (!hevent->state) {
        rc = pthread_cond_timedwait(&hevent->cond, &hevent->mutex, &abstime);
        if (rc != 0) {
            if (rc == ETIMEDOUT) {
                break;
            }
            pthread_mutex_unlock(&hevent->mutex);
            return EVENT_ERROR;
        }
    }

    if (rc == 0 && !hevent->manual_reset) {
        hevent->state = false;
    }

    if (pthread_mutex_unlock(&hevent->mutex) != 0) {
        return EVENT_ERROR;
    }

    return (rc == ETIMEDOUT) ? EVENT_TIMEOUT : EVENT_SUCCESS;
}

int EventSet (EVENT_HANDLE hevent)
{
    if (hevent == NULL) {
        return -1;
    }
    if (pthread_mutex_lock(&hevent->mutex) != 0) {
        return -1;
    }

    if (hevent->manual_reset) {
        if (pthread_cond_broadcast(&hevent->cond)) {
            return -1;
        }
    } else {
        if (pthread_cond_signal(&hevent->cond)) {
            return -1;
        }
    }
    hevent->state = true;

    if (pthread_mutex_unlock(&hevent->mutex) != 0) {
        return -1;
    }

    return 0;
}

int EventReset (EVENT_HANDLE hevent)
{
    if (hevent == NULL) {
        return -1;
    }

    if (pthread_mutex_lock(&hevent->mutex) != 0) {
        return -1;
    }

    hevent->state = false;
    if (pthread_mutex_unlock(&hevent->mutex) != 0) {
        return -1;
    }
    return 0;
}

void EventDestroy (EVENT_HANDLE hevent)
{
    if (hevent == NULL) {
        return ;
    }

    pthread_cond_destroy(&hevent->cond);
    pthread_mutex_destroy(&hevent->mutex);
    free(hevent);
}

void EventDestroySafe (EVENT_HANDLE* hevent)
{
    if (hevent == NULL || *hevent == NULL) {
        return ;
    }

    pthread_cond_destroy(&(*hevent)->cond);
    pthread_mutex_destroy(&(*hevent)->mutex);
    free(*hevent);
    *hevent = NULL;
}

#endif /* _WIN32 */

/*
 * end
 */
