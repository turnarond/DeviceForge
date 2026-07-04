/*
* Copyright (c) 2024 ACOINFO CloudNative Team.
* All rights reserved.
*
* Detailed license information can be found in the LICENSE file.
*
* File: list.c .
*
* Date: 2024-06-07
*
* Author: Yan.chaodong <yanchaodong@acoinfo.com>
*
*/


#include "list.h"
#include <stdio.h>

int LW_Node_HeadGet (LW_DLIST_NODE_S *pstListHead, LW_DLIST_NODE_S **ppstNode)
{
    LW_DLIST_NODE_S *pstTmp = NULL;
    
    if ( (NULL == pstListHead) || (NULL == ppstNode)) {
        return -1;
    }

    *ppstNode = NULL;

    if (!LW_DLIST_ISEMPTY(pstListHead)) {
        pstTmp = pstListHead->next;        
        LW_DLIST_DEL(pstTmp);
        *ppstNode = pstTmp;        
    }
        
    return 0;
}

int LW_Node_TailGet (LW_DLIST_NODE_S *pstListHead, LW_DLIST_NODE_S **ppstNode)
{
    LW_DLIST_NODE_S * pstTmp = NULL;

    if ((NULL == pstListHead) || (NULL == ppstNode)) {
        return -1;
    }

    *ppstNode = NULL;

    if (!LW_DLIST_ISEMPTY(pstListHead)) {
        pstTmp = pstListHead->prev;        
        LW_DLIST_DEL(pstTmp);
        *ppstNode = pstTmp;
    }

    return 0;
}

/*
 * end
 */