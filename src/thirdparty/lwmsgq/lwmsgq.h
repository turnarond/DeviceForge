/*
* Copyright (c) 2024 ACOINFO CloudNative Team.
* All rights reserved.
*
* Detailed license information can be found in the LICENSE file.
*
* File: lwmsgq.h .
*
* Date: 2024-06-07
*
* Author: Yan.chaodong <yanchaodong@acoinfo.com>
*
*/

#ifndef __MSGQUE_H__
#define __MSGQUE_H__

#ifdef __cplusplus
#include "lwcomm/lwcomm.h"   // provides hmutex_t (CRITICAL_SECTION on Win, pthread_mutex_t on POSIX)
#else
#ifdef _WIN32
#include <windows.h>
typedef CRITICAL_SECTION hmutex_t;
#define hmutex_init(m)    InitializeCriticalSection(m)
#define hmutex_destroy(m) DeleteCriticalSection(m)
#define hmutex_lock(m)    EnterCriticalSection(m)
#define hmutex_unlock(m)  LeaveCriticalSection(m)
#else
#include <pthread.h>
typedef pthread_mutex_t hmutex_t;
#define hmutex_init(m)    pthread_mutex_init(m, NULL)
#define hmutex_destroy(m) pthread_mutex_destroy(m)
#define hmutex_lock(m)    pthread_mutex_lock(m)
#define hmutex_unlock(m)  pthread_mutex_unlock(m)
#endif
#endif
#include "list.h"
#include "lwevent/lwevent.h"

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define LW_QUE_MAXSIZE 100000000

#ifdef __cplusplus
extern "C"{
#endif

/* User callback function type */
typedef int (*pfQueUserHandleCb)(void* msg, int msglen, void* userctx); 

/* User context structure */
typedef struct tagMsgUserCtx
{    
    void* pvUserCtx;                                    /* User context */
    pfQueUserHandleCb pfUserHandler;                    /* User handler callback */
}MsgUsrCtx;

/* Message queue structure (designed with object pool) */
typedef struct tagMsgQueue
{
    int capacity;                                      /* Capacity of this message queue */
    LW_DLIST_NODE_S idle_list;                         /* Idle list */
    int idle_nums;                                     /* Count of idle list */
    LW_DLIST_NODE_S used_list;                         /* Used list */
    int used_nums;                                     /* Count of used list */
    hmutex_t msg_mutex;                                /* Mutex for list operations */
    EVENT_HANDLE msg_push_notify;                      /* Message pushed event */
#ifdef _WIN32
    CONDITION_VARIABLE msg_pop_cond;                   /* Condition variable for blocking pop/push */
#else
    pthread_cond_t msg_pop_cond;                       /* Condition variable for blocking pop/push */
#endif
    volatile int stop_flag;                            /* Stop flag (avoid relying on idle node) */
    MsgUsrCtx user_ctx;                                /* User context */
}LW_MSGQUE_S, *PLW_MSGQUE_S;

/* Create message queue */
PLW_MSGQUE_S MsgQueCreate(int queue_size, pfQueUserHandleCb user_handle_cb, void *ctx);

/* Release message queue */
void MsgQueRelease(PLW_MSGQUE_S *msgq);

/* Push message to queue */
int MsgQuePush(PLW_MSGQUE_S msgq, void *msg, int msg_len);

/* Push message to queue (evict front message if full) */
int MsgQuePushWithEvict(PLW_MSGQUE_S msgq, void* msg, int msg_len, void **p_popped_msg);

/* Push message to head of queue */
int MsgQuePushHead(PLW_MSGQUE_S msgq, void *msg, int msg_len);

/* Pop message from queue (blocking, waits when empty) */
int MsgQuePop(PLW_MSGQUE_S msgq, void **msg, int *len);

/* Pop message from queue with timeout (blocking, waits when empty, returns -1 on timeout) */
int MsgQuePopTimeout(PLW_MSGQUE_S msgq, void **msg, int *len, int timeout_ms);

/* Try to pop message from queue (non-blocking) */
int MsgQueTryPop(PLW_MSGQUE_S msgq, void **msg, int *len);

/* Try to push message to queue (non-blocking, returns -1 if full) */
int MsgQueTryPush(PLW_MSGQUE_S msgq, void *msg, int msg_len);

/* Stop message queue */
int MsgQueStop(PLW_MSGQUE_S msgq);

/* Wait for event in message queue worker */
int MsgQueWorkerWaitfor(PLW_MSGQUE_S msgq, int *errcode);

/* Get used message count in queue */
int MsgQueGetUsedNums(PLW_MSGQUE_S msgq);

#ifdef __cplusplus
}
#endif

#endif

/*
 * end
 */
