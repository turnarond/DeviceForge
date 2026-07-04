/*
* Copyright (c) 2024 ACOINFO CloudNative Team.
* All rights reserved.
*
* Detailed license information can be found in the LICENSE file.
*
* File: lwmsgq.c .
*
* Date: 2024-06-07
*
* Author: Yan.chaodong <yanchaodong@acoinfo.com>
*
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "list.h"
#include "lwmsgq.h"

#ifdef _WIN32
#include <windows.h>
#include <errno.h>
#define bzero(p, n) memset(p, 0, n)
#endif

#ifdef _WIN32
/* Windows condition variable wrappers */
#define MSGQ_COND_INIT(cv)        InitializeConditionVariable(cv)
#define MSGQ_COND_DESTROY(cv)     /* no-op: CONDITION_VARIABLE needs no destroy */
#define MSGQ_COND_SIGNAL(cv)      WakeConditionVariable(cv)
#define MSGQ_COND_BROADCAST(cv)   WakeAllConditionVariable(cv)
#define MSGQ_COND_WAIT(cv, mtx)   SleepConditionVariableCS(cv, mtx, INFINITE)
#define MSGQ_COND_TIMEDWAIT(cv, mtx, ms) SleepConditionVariableCS(cv, mtx, (DWORD)(ms))
#else
#include <unistd.h>
#include <errno.h>
/* POSIX condition variable wrappers (preserve existing behavior) */
#define MSGQ_COND_INIT(cv)        pthread_cond_init(cv, NULL)
#define MSGQ_COND_DESTROY(cv)     pthread_cond_destroy(cv)
#define MSGQ_COND_SIGNAL(cv)      pthread_cond_signal(cv)
#define MSGQ_COND_BROADCAST(cv)   pthread_cond_broadcast(cv)
#define MSGQ_COND_WAIT(cv, mtx)   pthread_cond_wait(cv, mtx)
#endif

/* The type of each msg */
typedef enum {
    LW_QUEMSG_TYPE_UNKNOW = 0,
    LW_QUEMSG_TYPE_DATA,       /* data type */
    LW_QUEMSG_TYPE_CMD_STOP,   /* stop cmd */

    LW_QUEMSG_TYPE_NUMS,
} LW_QUEMSG_TYPE_E;

/*
 * msg node.
 */
typedef struct MsgQueNode {
    LW_DLIST_NODE_S list_node;
    unsigned int msg_code;      /* type of this node */
    unsigned int msg_length;    /* length of this msg */
    void* user_msg;             /* user context */
} LW_MSGQUE_NODE_S, * PLW_MSGQUE_NODE_S;

/**
 * @brief Create msg queue.
 * @param size
 * @param pfnuser_handle
 * @param user_ctx
 * @return PLW_MSGQUE_S
 */
PLW_MSGQUE_S MsgQueCreate (int size, pfQueUserHandleCb pfnuser_handle, void* user_ctx)
{
    PLW_MSGQUE_S msgq = NULL;
    PLW_MSGQUE_NODE_S pstEntry = NULL;

    if (size > LW_QUE_MAXSIZE || size <= 0) {
        printf("MsgQueCreate: size=%d is invalid", size);
        return NULL;
    }

    msgq = (PLW_MSGQUE_S)malloc(sizeof(LW_MSGQUE_S));
    if (NULL == msgq) {
        return msgq;
    }

    bzero(msgq, sizeof(LW_MSGQUE_S));

    LW_DLIST_INIT(&msgq->idle_list);
    msgq->idle_nums = 0;
    LW_DLIST_INIT(&msgq->used_list);
    msgq->used_nums = 0;
    hmutex_init(&msgq->msg_mutex);

    {
#ifdef _WIN32
        MSGQ_COND_INIT(&msgq->msg_pop_cond);
#else
        /*
         * Use CLOCK_MONOTONIC for timed wait timeouts.  Unlike CLOCK_REALTIME,
         * MONOTONIC is unaffected by system time adjustments (NTP, manual
         * changes) and cannot jump backward causing premature wakeup, or jump
         * forward causing an extended wait.  MsgQuePopTimeout computes its
         * absolute timeout against the same clock.
         */
        pthread_condattr_t cond_attr;
        pthread_condattr_init(&cond_attr);
        pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);
        MSGQ_COND_INIT(&msgq->msg_pop_cond);
        pthread_condattr_destroy(&cond_attr);
#endif
    }

    if ((msgq->msg_push_notify = EventCreate(false, false)) == NULL) {
        printf("MsgQueCreate: EventCreate failed\n");
        goto FAILED;
    }

    msgq->user_ctx.pvUserCtx = user_ctx;
    msgq->user_ctx.pfUserHandler = pfnuser_handle;

    for (int i = 0; i < size; i++) {
        pstEntry = (PLW_MSGQUE_NODE_S)malloc(sizeof(LW_MSGQUE_NODE_S));
        if (NULL == pstEntry)
        {
            printf("message queue create malloc node error!\n");
            MsgQueRelease(&msgq);
            return NULL;
        }

        LW_DLIST_INIT(&pstEntry->list_node);
        LW_DLIST_ADD_TAIL(&msgq->idle_list, &pstEntry->list_node);
        msgq->idle_nums++;
    }

    return msgq;

FAILED:
    MsgQueRelease(&msgq);
    return NULL;
}

/**
 * @brief Release this msgq.
 * @param ppstQueue
 */
void MsgQueRelease(PLW_MSGQUE_S* ppstQueue)
{
    PLW_DLIST_NODE_S   cur_entry, next_entry, list_head;
    PLW_MSGQUE_NODE_S  msgq_node = NULL;
    PLW_MSGQUE_S       pstQue = NULL;

    if (NULL == ppstQueue) {
        return;
    }

    pstQue = *ppstQueue;
    if (NULL != pstQue) {
        if (TRUE != LW_DLIST_ISEMPTY(&pstQue->idle_list)) {
            list_head = &pstQue->idle_list;
            for (cur_entry = list_head->prev;
                cur_entry != list_head;
                cur_entry = next_entry) {
                next_entry = cur_entry->prev;
                msgq_node = LW_DLIST_ENTRY(cur_entry, LW_MSGQUE_NODE_S, list_node);
                LW_DLIST_DEL(&msgq_node->list_node);
                free(msgq_node);
                pstQue->idle_nums--;
            }
        }

        if (TRUE != LW_DLIST_ISEMPTY(&pstQue->used_list)) {
            list_head = &pstQue->used_list;
            for (cur_entry = list_head->prev;
                cur_entry != list_head;
                cur_entry = next_entry) {
                next_entry = cur_entry->prev;
                msgq_node = LW_DLIST_ENTRY(cur_entry, LW_MSGQUE_NODE_S, list_node);
                LW_DLIST_DEL(&msgq_node->list_node);
                if (msgq_node->user_msg != NULL) {
                    printf("MsgQueRelease: warning - freeing leaked message (caller should have popped it)\n");
                    free(msgq_node->user_msg);
                    msgq_node->user_msg = NULL;
                }
                free(msgq_node);
                pstQue->used_nums--;
            }
        }

        hmutex_destroy(&pstQue->msg_mutex);
        MSGQ_COND_DESTROY(&pstQue->msg_pop_cond);
        EventDestroySafe(&pstQue->msg_push_notify);

        free(pstQue);
        *ppstQueue = NULL;
    }
}

/*
 * Non-blocking push — returns -1 immediately if idle_list is empty.
 */
static int MsgQuePushNonBlock(LW_MSGQUE_S* msgq, void* pvMsg, int iMsgLen, int to_head)
{
    PLW_MSGQUE_NODE_S msgq_node = NULL;
    PLW_DLIST_NODE_S pstEntry = NULL;

    if (NULL == msgq || NULL == pvMsg || iMsgLen <= 0) {
        return -1;
    }

    hmutex_lock(&msgq->msg_mutex);

    if (LW_DLIST_ISEMPTY(&msgq->idle_list)) {
        hmutex_unlock(&msgq->msg_mutex);
        return -1;
    }

    LW_Node_HeadGet(&msgq->idle_list, &pstEntry);
    if (pstEntry == NULL) {
        hmutex_unlock(&msgq->msg_mutex);
        return -1;
    }

    msgq->idle_nums--;
    msgq_node = LW_DLIST_ENTRY(pstEntry, LW_MSGQUE_NODE_S, list_node);
    msgq_node->msg_code = LW_QUEMSG_TYPE_DATA;
    msgq_node->msg_length = iMsgLen;
    msgq_node->user_msg = pvMsg;

    if (to_head) {
        LW_DLIST_ADD_HEAD(&msgq->used_list, &msgq_node->list_node);
    } else {
        LW_DLIST_ADD_TAIL(&msgq->used_list, &msgq_node->list_node);
    }
    msgq->used_nums++;

    MSGQ_COND_SIGNAL(&msgq->msg_pop_cond);
    hmutex_unlock(&msgq->msg_mutex);

    EventSet(msgq->msg_push_notify);

    return 0;
}

/*
 * Blocking push — waits on msg_pop_cond until an idle node becomes available.
 * Consumer recycles nodes to idle_list and signals msg_pop_cond after each Pop.
 */
static int MsgQuePushBlock(LW_MSGQUE_S* msgq, void* pvMsg, int iMsgLen, int to_head)
{
    PLW_MSGQUE_NODE_S msgq_node = NULL;
    PLW_DLIST_NODE_S pstEntry = NULL;

    if (NULL == msgq || NULL == pvMsg || iMsgLen <= 0) {
        return -1;
    }

    hmutex_lock(&msgq->msg_mutex);

    while (LW_DLIST_ISEMPTY(&msgq->idle_list) && !msgq->stop_flag) {
        MSGQ_COND_WAIT(&msgq->msg_pop_cond, &msgq->msg_mutex);
    }
    if (msgq->stop_flag) {
        hmutex_unlock(&msgq->msg_mutex);
        return -1;
    }

    LW_Node_HeadGet(&msgq->idle_list, &pstEntry);
    if (pstEntry == NULL) {
        hmutex_unlock(&msgq->msg_mutex);
        return -1;
    }

    msgq->idle_nums--;
    msgq_node = LW_DLIST_ENTRY(pstEntry, LW_MSGQUE_NODE_S, list_node);
    msgq_node->msg_code = LW_QUEMSG_TYPE_DATA;
    msgq_node->msg_length = iMsgLen;
    msgq_node->user_msg = pvMsg;

    if (to_head) {
        LW_DLIST_ADD_HEAD(&msgq->used_list, &msgq_node->list_node);
    } else {
        LW_DLIST_ADD_TAIL(&msgq->used_list, &msgq_node->list_node);
    }
    msgq->used_nums++;

    MSGQ_COND_SIGNAL(&msgq->msg_pop_cond);
    hmutex_unlock(&msgq->msg_mutex);

    EventSet(msgq->msg_push_notify);

    return 0;
}

int MsgQuePush(LW_MSGQUE_S* msgq, void* pvMsg, int iMsgLen) {
    /*
     * Non-blocking push: returns -1 immediately when the queue is full.
     * Use MsgQueTryPush for the same non-blocking semantics; MsgQuePushHead
     * and MsgQuePushWithEvict offer blocking/evicting alternatives.
     */
    return MsgQuePushNonBlock(msgq, pvMsg, iMsgLen, 0);
}

int MsgQueTryPush(LW_MSGQUE_S* msgq, void* pvMsg, int iMsgLen) {
    return MsgQuePushNonBlock(msgq, pvMsg, iMsgLen, 0);
}

int MsgQuePushHead(LW_MSGQUE_S* msgq, void* pvMsg, int iMsgLen) {
    return MsgQuePushBlock(msgq, pvMsg, iMsgLen, 1);
}

/**
 * @brief Push msg to queue; if full, pop the oldest (head) message.
 * @param msgq      Message queue handle
 * @param msg       Message to push
 * @param msg_len   Length of message
 * @param p_popped_msg  [out] The popped old message (can be NULL if not needed)
 * @return int      0 on success, -1 on error (e.g., invalid args)
 */
int MsgQuePushWithEvict(PLW_MSGQUE_S msgq, void *msg, int msg_len, void **p_popped_msg)
{
    PLW_DLIST_NODE_S pstEntry = NULL;
    PLW_MSGQUE_NODE_S new_node = NULL, old_node = NULL;
    int need_evict = FALSE;

    if (!msgq || !msg || msg_len <= 0) {
        return -1;
    }

    hmutex_lock(&msgq->msg_mutex);

    // 判断是否需要驱逐旧消息
    if (LW_DLIST_ISEMPTY(&msgq->idle_list)) {
        // 没有空闲节点，必须驱逐一个
        if (LW_DLIST_ISEMPTY(&msgq->used_list)) {
            // 理论上不会发生（idle 和 used 都空）
            hmutex_unlock(&msgq->msg_mutex);
            return -1;
        }

        // 从 used_list 头部摘除最老消息
        LW_Node_HeadGet(&msgq->used_list, &pstEntry);
        if (pstEntry == NULL) {
            hmutex_unlock(&msgq->msg_mutex);
            return -1;
        }

        old_node = LW_DLIST_ENTRY(pstEntry, LW_MSGQUE_NODE_S, list_node);
        if (p_popped_msg) {
            *p_popped_msg = old_node->user_msg;  // 返回被弹出的消息
        } else {
            // 即使不需要返回，也要回收节点
        }

        // LW_Node_HeadGet 已经从 used_list 中移除了节点，无需再次移除
        msgq->used_nums--;
        need_evict = TRUE;
    }

    // 获取空闲节点
    if (!need_evict) {
        // 正常流程：从 idle_list 头部获取
        LW_Node_HeadGet(&msgq->idle_list, &pstEntry);
        if (pstEntry == NULL) {
            hmutex_unlock(&msgq->msg_mutex);
            return -1;
        }
        msgq->idle_nums--;
        
        new_node = LW_DLIST_ENTRY(pstEntry, LW_MSGQUE_NODE_S, list_node);
        new_node->msg_code = LW_QUEMSG_TYPE_DATA;
        new_node->msg_length = msg_len;
        new_node->user_msg = msg;

        // 插入 used_list 尾部（FIFO）
        LW_DLIST_ADD_TAIL(&msgq->used_list, &new_node->list_node);
        msgq->used_nums++;
    } else {
        // 驱逐流程：复用弹出的节点
        old_node->msg_code = LW_QUEMSG_TYPE_DATA;
        old_node->msg_length = msg_len;
        old_node->user_msg = msg;

        /* Insert into used_list tail (FIFO). */
        LW_DLIST_ADD_TAIL(&msgq->used_list, &old_node->list_node);
        msgq->used_nums++;
    }

    MSGQ_COND_SIGNAL(&msgq->msg_pop_cond);
    hmutex_unlock(&msgq->msg_mutex);

    // 通知等待线程
    EventSet(msgq->msg_push_notify);

    /* If eviction happened, no need to recycle the node, as it's been reused. */

    return 0;
}

int MsgQueStop (LW_MSGQUE_S* msgq)
{
    if (NULL == msgq) {
        return -1;
    }

    hmutex_lock(&msgq->msg_mutex);

    /* Set stop flag — no longer requires an idle node.
     * All blocking Pop / Push / WorkerWaitfor check this flag on wakeup. */
    msgq->stop_flag = 1;

    /* Broadcast to wake ALL blocked waiters (Pop, PopTimeout, blocking Push) */
    MSGQ_COND_BROADCAST(&msgq->msg_pop_cond);
    hmutex_unlock(&msgq->msg_mutex);

    /* Wake the worker thread if it's blocked on EventWait */
    EventSet(msgq->msg_push_notify);

    return 0;
}

/**
 * @brief Waitingfor event.
 * @param msgq 
 * @param piErrorCode 
 * @return int 
 */
int MsgQueWorkerWaitfor (PLW_MSGQUE_S msgq, int* piErrorCode)
{
    int ret = 0;
    PLW_DLIST_NODE_S pstEntry = NULL;
    PLW_MSGQUE_NODE_S msgq_node = NULL;

    if (NULL == msgq || NULL == piErrorCode) {
        return -1;
    }

    while (TRUE) {
        ret = EventWait(msgq->msg_push_notify);
        if (ret == -1) {
            printf("EventWait for error!errno=%d\n", errno);
            break;
        }

        hmutex_lock(&msgq->msg_mutex);

        /* Stop flag set — exit without processing remaining messages */
        if (msgq->stop_flag) {
            hmutex_unlock(&msgq->msg_mutex);
            return 0;
        }

        if (TRUE == LW_DLIST_ISEMPTY(&msgq->used_list)) {
            hmutex_unlock(&msgq->msg_mutex);
            continue;
        }

        while (LW_DLIST_ISEMPTY(&msgq->used_list) != TRUE) {
            LW_Node_HeadGet(&msgq->used_list, &pstEntry);
            msgq->used_nums--;
            hmutex_unlock(&msgq->msg_mutex);

            msgq_node = LW_DLIST_ENTRY(pstEntry, LW_MSGQUE_NODE_S, list_node);
            switch (msgq_node->msg_code)
            {
            case LW_QUEMSG_TYPE_DATA:
                if (NULL != msgq->user_ctx.pfUserHandler) {
                    (msgq->user_ctx.pfUserHandler(msgq_node->user_msg, msgq_node->msg_length, msgq->user_ctx.pvUserCtx));
                }
                break;
            case LW_QUEMSG_TYPE_CMD_STOP:
                /* Legacy CMD_STOP — recycle node and exit */
                hmutex_lock(&msgq->msg_mutex);
                LW_DLIST_ADD_TAIL(&msgq->idle_list, &msgq_node->list_node);
                msgq->idle_nums++;
                hmutex_unlock(&msgq->msg_mutex);
                return 0;
            default:
                printf("MsgQueWorkerWaitfor: unexpected msg_code=%u\n", msgq_node->msg_code);
                break;
            }

            hmutex_lock(&msgq->msg_mutex);
            msgq_node->user_msg = NULL;
            msgq_node->msg_code = 0;
            msgq_node->msg_length = 0;
            LW_DLIST_ADD_TAIL(&msgq->idle_list, &msgq_node->list_node);
            msgq->idle_nums++;
        }

        hmutex_unlock(&msgq->msg_mutex);
    }

    return -1;
}

int MsgQueGetUsedNums (PLW_MSGQUE_S msgq)
{
    if (msgq == NULL) {
        return -1;
    }

    return msgq->used_nums;
}

/*
 * Recycle a used node back to idle_list and signal waiting producers.
 */
static void recycleNode(PLW_MSGQUE_S msgq, PLW_MSGQUE_NODE_S node)
{
    LW_DLIST_ADD_TAIL(&msgq->idle_list, &node->list_node);
    msgq->idle_nums++;
    /* Wake one blocked Push in case it was waiting for an idle node */
    MSGQ_COND_SIGNAL(&msgq->msg_pop_cond);
}

int MsgQuePop(PLW_MSGQUE_S msgq, void **msg, int *len)
{
    PLW_DLIST_NODE_S pstEntry = NULL;
    PLW_MSGQUE_NODE_S msgq_node = NULL;

    if (!msgq || !msg || !len) return -1;

    *msg = NULL;

    hmutex_lock(&msgq->msg_mutex);

    while (LW_DLIST_ISEMPTY(&msgq->used_list) && !msgq->stop_flag) {
        MSGQ_COND_WAIT(&msgq->msg_pop_cond, &msgq->msg_mutex);
    }

    /* Stopped — no more messages will arrive */
    if (msgq->stop_flag && LW_DLIST_ISEMPTY(&msgq->used_list)) {
        hmutex_unlock(&msgq->msg_mutex);
        return -1;
    }

    LW_Node_HeadGet(&msgq->used_list, &pstEntry);
    if (pstEntry) {
        msgq->used_nums--;
        msgq_node = LW_DLIST_ENTRY(pstEntry, LW_MSGQUE_NODE_S, list_node);

        if (msgq_node->msg_code == LW_QUEMSG_TYPE_CMD_STOP) {
            recycleNode(msgq, msgq_node);
            /* Broadcast — wake ALL other Pop waiters so they also detect
             * the CMD_STOP and exit cleanly. */
            MSGQ_COND_BROADCAST(&msgq->msg_pop_cond);
            hmutex_unlock(&msgq->msg_mutex);
            return -1;
        }

        *msg = msgq_node->user_msg;
        *len = msgq_node->msg_length;
        recycleNode(msgq, msgq_node);
    }

    hmutex_unlock(&msgq->msg_mutex);
    return 0;
}

int MsgQuePopTimeout(PLW_MSGQUE_S msgq, void **msg, int *len, int timeout_ms)
{
    PLW_DLIST_NODE_S pstEntry = NULL;
    PLW_MSGQUE_NODE_S msgq_node = NULL;
#ifndef _WIN32
    struct timespec ts;
#endif

    if (!msgq || !msg || !len || timeout_ms < 0) return -1;

    *msg = NULL;

#ifndef _WIN32
    /*
     * Compute absolute timeout against CLOCK_MONOTONIC — consistent with the
     * condition-variable clock set in MsgQueCreate.  MONOTONIC ensures the
     * relative timeout_ms is honored regardless of system time changes.
     */
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }
#endif

    hmutex_lock(&msgq->msg_mutex);

    while (LW_DLIST_ISEMPTY(&msgq->used_list) && !msgq->stop_flag) {
#ifdef _WIN32
        /*
         * SleepConditionVariableCS returns non-zero on success (condition signaled),
         * zero on timeout or error.  Only handle the timeout/error path explicitly;
         * on success the while-condition is re-evaluated.
         */
        if (!MSGQ_COND_TIMEDWAIT(&msgq->msg_pop_cond, &msgq->msg_mutex, (DWORD)timeout_ms)) {
            hmutex_unlock(&msgq->msg_mutex);
            return -1;
        }
#else
        int ret = pthread_cond_timedwait(&msgq->msg_pop_cond, &msgq->msg_mutex, &ts);
        if (ret == ETIMEDOUT) {
            hmutex_unlock(&msgq->msg_mutex);
            return -1;
        }
#endif
    }

    if (msgq->stop_flag && LW_DLIST_ISEMPTY(&msgq->used_list)) {
        hmutex_unlock(&msgq->msg_mutex);
        return -1;
    }

    LW_Node_HeadGet(&msgq->used_list, &pstEntry);
    if (pstEntry) {
        msgq->used_nums--;
        msgq_node = LW_DLIST_ENTRY(pstEntry, LW_MSGQUE_NODE_S, list_node);

        if (msgq_node->msg_code == LW_QUEMSG_TYPE_CMD_STOP) {
            recycleNode(msgq, msgq_node);
            MSGQ_COND_BROADCAST(&msgq->msg_pop_cond);
            hmutex_unlock(&msgq->msg_mutex);
            return -1;
        }

        *msg = msgq_node->user_msg;
        *len = msgq_node->msg_length;
        recycleNode(msgq, msgq_node);
    }

    hmutex_unlock(&msgq->msg_mutex);
    return 0;
}

int MsgQueTryPop(PLW_MSGQUE_S msgq, void **msg, int *len)
{
    PLW_DLIST_NODE_S pstEntry = NULL;
    PLW_MSGQUE_NODE_S msgq_node = NULL;

    if (!msgq || !msg || !len) return -1;

    hmutex_lock(&msgq->msg_mutex);

    if (LW_DLIST_ISEMPTY(&msgq->used_list)) {
        hmutex_unlock(&msgq->msg_mutex);
        return -1;
    }

    LW_Node_HeadGet(&msgq->used_list, &pstEntry);
    if (pstEntry) {
        msgq->used_nums--;
        msgq_node = LW_DLIST_ENTRY(pstEntry, LW_MSGQUE_NODE_S, list_node);

        if (msgq_node->msg_code == LW_QUEMSG_TYPE_CMD_STOP) {
            recycleNode(msgq, msgq_node);
            hmutex_unlock(&msgq->msg_mutex);
            return -1;
        }

        *msg = msgq_node->user_msg;
        *len = msgq_node->msg_length;
        recycleNode(msgq, msgq_node);
    }

    hmutex_unlock(&msgq->msg_mutex);
    return 0;
}

/*
 * end
 */
