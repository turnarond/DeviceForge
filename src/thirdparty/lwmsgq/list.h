/*
* Copyright (c) 2024 ACOINFO CloudNative Team.
* All rights reserved.
*
* Detailed license information can be found in the LICENSE file.
*
* File: list.h .
*
* Date: 2024-06-07
*
* Author: Yan.chaodong <yanchaodong@acoinfo.com>
*
*/

#ifndef _LW_DLIST_H_
#define _LW_DLIST_H_
    
typedef struct tagDlistNode {
    struct tagDlistNode *prev;
    struct tagDlistNode *next;
} LW_DLIST_NODE_S, *PLW_DLIST_NODE_S;

#ifndef offsetof
/* Offset of member MEMBER in a struct of type TYPE. */
#ifdef _WIN32
#define offsetof(TYPE, MEMBER) ((size_t)&(((TYPE*)0)->MEMBER))
#else
#define offsetof(TYPE, MEMBER) __builtin_offsetof (TYPE, MEMBER)
#endif
#endif

#ifndef container_of
#ifdef _WIN32
/* MSVC compatible container_of - avoids GCC extensions */
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#else
#define container_of(ptr, type, member) ({              \
    const typeof( ((type *)0)->member ) *__mptr = (ptr); \
    (type *)( (char *)__mptr - offsetof(type,member) );})
#endif
#endif
    
#define LW_DLIST_ENTRY(pnode,type,membernode) \
             container_of(pnode,type,membernode)
         
#define LW_DLIST_FOR_EACH_ENTRY(pos, pstList, type, stnode) \
             for (pos = LW_DLIST_ENTRY((pstList)->next, type, stnode); \
                  &pos->stnode != (pstList); \
                  pos =LW_DLIST_ENTRY(pos->stnode.next, type, stnode)) \

#define LW_DLIST_FOR_EACH_ENTRY_SAFE(pos, n, pstList, type, stnode) \
             for (pos = LW_DLIST_ENTRY((pstList)->next, type, stnode); \
                  &pos->stnode != (pstList) && \
                  (n = LW_DLIST_ENTRY(pos->stnode.next, type, stnode), \
                  &n->stnode != (pstList)); \
                  pos = n) \

#define LW_DLIST_INIT(pstNode) \
    do { \
        if (NULL != (pstNode)) {\
            (pstNode)->prev = (pstNode);\
            (pstNode)->next = (pstNode);\
        }\
    } while(0)

#define LW_DLIST_ADD_HEAD(pstList, pstNode)\
    {\
        if ((NULL != (pstList)) && (NULL != (pstNode))) {\
            (pstNode)->next = (pstList)->next;\
            (pstNode)->prev = (pstList);\
            (pstList)->next->prev = (pstNode);\
            (pstList)->next = (pstNode);\
        }\
    }
      
#define LW_DLIST_ADD_TAIL(pstList,pstNode)\
    {\
        if ((NULL != (pstList)) && (NULL != (pstNode))) {\
            (pstNode)->next = (pstList);\
            (pstNode)->prev = (pstList)->prev;\
            (pstList)->prev->next = (pstNode);\
            (pstList)->prev = (pstNode);\
        }\
    }
              
#define LW_DLIST_INSERT(pstPrev,pstNext,pstNode)\
    {\
        if ((NULL != (pstPrev)) \
            && (NULL != (pstNext))\
            && (NULL != (pstNode))) {\
            (pstPrev)->next = (pstNode);\
            (pstNext)->prev = (pstNode);\
            (pstNode)->prev = (pstPrev);\
            (pstNode)->next = (pstNext);\
        }\
    }

#define LW_DLIST_DEL(pstNode)\
    {\
        if (NULL != (pstNode)) {\
            (pstNode)->next->prev = (pstNode)->prev;\
            (pstNode)->prev->next = (pstNode)->next;\
            (pstNode)->next = (pstNode);\
            (pstNode)->prev = (pstNode);\
        }\
    }

#define LW_DLIST_SPLICE_INIT(pstListFrom,pstListTo) \
    {\
        if ((NULL != (pstListFrom)) && (NULL != (pstListTo))) {\
            (pstListTo)->next = (pstListFrom)->next;\
            (pstListFrom)->next->prev = (pstListTo);\
            (pstListFrom)->prev->next = (pstListTo);\
            (pstListTo)->prev = (pstListFrom)->prev;\
            (pstListFrom)->next = (pstListFrom);\
            (pstListFrom)->prev = (pstListFrom);\
        }\
    }

#define LW_DLIST_ISEMPTY(pstList) \
            (((pstList)->next == pstList) && ((pstList)->prev == pstList)) 

int LW_Node_HeadGet(LW_DLIST_NODE_S *pstListHead, LW_DLIST_NODE_S **ppstNode);

int LW_Node_TailGet(LW_DLIST_NODE_S *pstListHead, LW_DLIST_NODE_S **ppstNode);

#endif // _LW_DLIST_H_

/*
 * end
 */
