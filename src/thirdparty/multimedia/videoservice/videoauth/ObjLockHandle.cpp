/**************************************************************
 *  Filename:    CObjLockHandle.cpp
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: implementation of the CCfgFileManager class.
 *
 *  @author:     fengjuanyong
 *  @version     05/30/2008  fengjuanyong  Initial Version
**************************************************************/

#include "ObjLockHandle.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CObjLockHandle::CObjLockHandle()
{
	m_objLockInfoVec.clear();
	m_lAutoLockTime = 0;
}

CObjLockHandle::~CObjLockHandle()
{
}

/**
 *  初始化.
 *
 *  @return $(ICV_SUCCESS).
 *
 *  @version     07/01/2008  fengjuanyong  Initial Version.
 */
long CObjLockHandle::InitLockHandle()
{
	m_lAutoLockTime = 0;
	for (OBJLOCKINFOVEC::iterator it=m_objLockInfoVec.begin(); it!=m_objLockInfoVec.end(); it++)
	{
		OBJLOCKINFO	*pObjLockInfo	= *it;
		if (pObjLockInfo)
		{
			if (pObjLockInfo->pObjBaseInfoMap)
			{
				for (OBJBASEINFOMAP::iterator itMap=pObjLockInfo->pObjBaseInfoMap->begin();
						itMap!=pObjLockInfo->pObjBaseInfoMap->end();
						itMap++)
				{	
					if (itMap->second)
					{
						delete itMap->second;
						itMap->second = NULL;
					}
				}
				pObjLockInfo->pObjBaseInfoMap->clear();
				delete pObjLockInfo->pObjBaseInfoMap;
				pObjLockInfo->pObjBaseInfoMap = NULL;
			}
			delete pObjLockInfo;
			pObjLockInfo = NULL;
		}
	}
	m_objLockInfoVec.clear();
	return ICV_SUCCESS;
}

/**
 *  释放.
 *
 *  @return $(ICV_SUCCESS).
 *
 *  @version     07/22/2008  likai  Initial Version.
 */

long CObjLockHandle::UnInitLockHandle()
{
	m_lAutoLockTime = 0;

	for (OBJLOCKINFOVEC::iterator it=m_objLockInfoVec.begin(); it!=m_objLockInfoVec.end(); it++)
	{
		OBJLOCKINFO	*pObjLockInfo	= *it;
		if (pObjLockInfo)
		{
			if (pObjLockInfo->pObjBaseInfoMap)
			{
				for (OBJBASEINFOMAP::iterator itMap=pObjLockInfo->pObjBaseInfoMap->begin();
						itMap!=pObjLockInfo->pObjBaseInfoMap->end();
						itMap++)
				{	
					if (itMap->second)
					{
						delete itMap->second;
						itMap->second = NULL;
					}
				}
				pObjLockInfo->pObjBaseInfoMap->clear();
				delete pObjLockInfo->pObjBaseInfoMap;
				pObjLockInfo->pObjBaseInfoMap = NULL;
			}
			delete pObjLockInfo;
			pObjLockInfo = NULL;
		}
	}
	m_objLockInfoVec.clear();
	return ICV_SUCCESS;
}

/**
 *  更新列表.
 *
 *  @return ICV_SUCCESS.
 *
 *  @version     06/12/2008  fengjuanyong  Initial Version.
 */
long CObjLockHandle::UpdateLockHandle(long lObjID, long lObjType, long lUserPRI, char *szUserName, long lManualTimeOut, long lAutoLock, long lDelLock)
{
	long lRet = ICV_SUCCESS;

	// 释放对象锁定
	if (lDelLock == ICV_CCTV_DELLOCK)
	{
		if (m_objLockInfoVec.size() == 0)
			return EC_ICV_CCTV_FREEEMPTYOBJ;

		for (OBJLOCKINFOVEC::iterator it=m_objLockInfoVec.begin(); it!=m_objLockInfoVec.end(); it++)
		{
			OBJLOCKINFO *pObjLockInfo = *it;
			if (!pObjLockInfo)
				continue;
			
			if (pObjLockInfo->lObjType == lObjType)
			{
				// 查找资源对应的锁定信息
				OBJBASEINFOMAP::iterator itMapLockInfo = pObjLockInfo->pObjBaseInfoMap->find(lObjID);
				if ( itMapLockInfo != pObjLockInfo->pObjBaseInfoMap->end())
				{	
					// 这次用户就是上次用户或者优先级高于上次用户，解除锁定
					if ((strcmp(szUserName, itMapLockInfo->second->szUserName) == 0) || (itMapLockInfo->second->lUserPRI < lUserPRI))
					{
						delete itMapLockInfo->second;
						itMapLockInfo->second = NULL;

						pObjLockInfo->pObjBaseInfoMap->erase(itMapLockInfo);
						return ICV_SUCCESS;
					}
					else
						return EC_ICV_CCTV_NOAUTH;

				}
				else
					return EC_ICV_CCTV_FREEEMPTYOBJ;
			}
		}
	}

	time_t tmNow;
	time(&tmNow);

	// 增加对象锁定
	bool bFindObjType = false;
	OBJLOCKINFO *pObjLockInfo = NULL;
	for (OBJLOCKINFOVEC::iterator it=m_objLockInfoVec.begin(); it!=m_objLockInfoVec.end(); it++)
	{
		pObjLockInfo = *it;
		if (!pObjLockInfo)
			continue;
		
		if (pObjLockInfo->lObjType == lObjType)
		{
			bFindObjType = true;
			OBJBASEINFOMAP::iterator itMap = pObjLockInfo->pObjBaseInfoMap->find(lObjID);
			if (itMap != pObjLockInfo->pObjBaseInfoMap->end())
			{
				OBJBASEINFO *pObjBaseInfo = itMap->second;
				// 锁定时间到达，自动解除锁定
				if (abs(tmNow - pObjBaseInfo->tmLock) > pObjBaseInfo->lLockTime)
				{
					OBJBASEINFO* pObjBaseInfoErased = itMap->second;
					delete pObjBaseInfoErased;
					pObjBaseInfoErased = NULL;
					pObjLockInfo->pObjBaseInfoMap->erase(itMap);
					break;
				}
				// 这次用户优先级较低，不能抢夺锁定
				if (pObjBaseInfo->lUserPRI > lUserPRI)
					return EC_ICV_CCTV_PRIORLOW;

				// 这次用户优先级相等，且不是上次用户，不能抢夺锁定
				if ((pObjBaseInfo->lUserPRI == lUserPRI) && strcmp(szUserName, pObjBaseInfo->szUserName) != 0)
					return EC_ICV_CCTV_PRIOREQUAL;

				// 如果用户已经存在且没有超过锁定时间
				// 且上次锁定是手动锁定
				// 且这次锁定是自动锁定
				// 直接返回，上次手动锁定有效
				if (strcmp(szUserName, pObjBaseInfo->szUserName) == 0)
				{
					OBJBASEINFO* pObjBaseInfoUser = itMap->second;
					if ((pObjBaseInfoUser->lAutoLock > 0) && (lAutoLock == 0))
						return ICV_SUCCESS;
				}

				// 这次用户就是上次用户或者优先级高于上次用户，抢夺锁定
				if ((strcmp(szUserName, pObjBaseInfo->szUserName) == 0) || (pObjBaseInfo->lUserPRI < lUserPRI))
				{
					OBJBASEINFO* pObjBaseInfoErased = itMap->second;
					delete pObjBaseInfoErased;
					pObjBaseInfoErased = NULL;
					pObjLockInfo->pObjBaseInfoMap->erase(itMap);
					break;
				}
			}
		}
	}

	OBJBASEINFO *pObjBaseInfo = new OBJBASEINFO();
	pObjBaseInfo->lAutoLock = lAutoLock;
	pObjBaseInfo->lLockTime = lManualTimeOut;
	pObjBaseInfo->lUserPRI = lUserPRI;
	pObjBaseInfo->tmLock = tmNow;
	strcpy(pObjBaseInfo->szUserName, szUserName);

	if (bFindObjType)
	{	
		pObjLockInfo->pObjBaseInfoMap->insert(make_pair(lObjID, pObjBaseInfo));
	}
	else
	{
		OBJLOCKINFO *pObjLockInfo = new OBJLOCKINFO();
		pObjLockInfo->lObjType = lObjType;
		OBJBASEINFOMAP *pObjBaseInfoMap = new OBJBASEINFOMAP();
		pObjBaseInfoMap->insert(make_pair(lObjID, pObjBaseInfo));
		pObjLockInfo->pObjBaseInfoMap = pObjBaseInfoMap;
		m_objLockInfoVec.push_back(pObjLockInfo);
	}
	return lRet; 
}
