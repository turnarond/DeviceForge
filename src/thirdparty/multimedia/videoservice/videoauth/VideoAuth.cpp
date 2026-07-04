// MMSvrAuth.cpp : Defines the entry point for the DLL application.
//
#include "multimedia/video/VideoAuth.h"
#include "ObjLockHandle.h"
#include "errcode/ErrCode_iCV_Common.hxx"
#include "authmanager/AMDef.h"
#include "authmanager/AM_REMOTE_API.h"
#include "authmanager/AM_LOCAL_API.h"
#include "multimedia/video/VideoDefine.h"

#define ICV_CCTV_DEFAULT_TIMEOUT		10	//10秒

CObjLockHandle g_objLockHandle;

/**
 *  初始化资源.
 *
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     09/10/2008  fengjuanyong  Initial Version.
 */
long API_CALL_METHOD InitLockAuth()
{
	return g_objLockHandle.InitLockHandle();
}

/**
 *  释放资源.
 *
 *  @return
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     09/10/2008  fengjuanyong  Initial Version.
 */
long API_CALL_METHOD ExitLockAuth()
{
	return g_objLockHandle.UnInitLockHandle();
}

/**
 *  获取用户信息.
 *
 *  @param  -[in]  char*  szUserName: [用户名]
 *  @param  -[in]    char*szLoginGroupName: [群组名]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     09/16/2008  fengjuanyong  Initial Version.
 */
long API_CALL_METHOD GetCurrentUserInfo(char* szUserName, char* szLoginGroup)
{
	// 获取当前用户信息
#ifdef _NOAUTH_
	sprintf(szUserName, ICV_AUTH_DEFAULT_USER);
	sprintf(szLoginGroup, "");
	return ICV_SUCCESS;
#endif
	char szServerIP[ICV_IPSTRING_MAXLEN];
	memset(szServerIP, 0, sizeof(szServerIP));
	long lRet = AM_REMOTE_SetServerInfo(ICV_CLINET_LOGIN_APPNAME, szServerIP, 0, szServerIP, 0);
	if (ICV_SUCCESS != lRet)
		return lRet;
	return AM_REMOTE_GetCurUserInfo(szUserName, CVAUTH_USERNAME_LEN, szLoginGroup, CVAUTH_GROUPNAME_LEN);
}

/**
 *  获取所有权限项.
 *
 *  @param  -[in]  char*  szItemInfo: [权限项列表]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     09/16/2008  fengjuanyong  Initial Version.
 */
long API_CALL_METHOD GetAllAuthItem(char* szItemInfo)
{
	long lCount = 0;
#ifdef _NOAUTH_
	lCount = 30;
	sprintf(szItemInfo, "%d,", lCount);
	for (int i=0; i<lCount; i++)
		sprintf(szItemInfo, "%s%d,Label%d,", szItemInfo,i,i);

	return ICV_SUCCESS;
#endif
	AM_Resource *pRes = NULL;
	long lRet = AM_REMOTE_GetCountOfResourcesByType(&lCount, "");
	if (lRet == ICV_SUCCESS)
	{
		pRes = new AM_Resource[lCount];
		lRet = AM_REMOTE_GetResourcesByType(pRes, lCount, 0, "");
	}
	if (lRet == ICV_SUCCESS)
	{
		sprintf(szItemInfo, "%d,", lCount);
		for (int i=0; i<lCount; i++)
			sprintf(szItemInfo, "%s%d,%s,", szItemInfo,pRes[i].lResid,pRes[i].szLabel);
	}
	
	if (pRes != NULL)
		delete [] pRes;
	
	return lRet;
}

/**
 *  验证用户权限.
 *
 *  @param  -[in]  char*  szUserName: [用户名]
 *  @param  -[in]    char*szLoginGroupName: [群组名]
 *  @param  -[in]  char*  szAuthLabel: [资源标签]
 *  @param  -[out]  long*  plAuth: [权限]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     09/16/2008  fengjuanyong  Initial Version.
 */
long API_CALL_METHOD VerifyUserRight(char *szUserName, char *szLoginGroupName, char *szAuthLabel, long *plAuth)
{
	// 超级用户
	if (0 == strcmp(szUserName, ICV_AUTH_DEFAULT_USER))
	{
		*plAuth = 0xFF;
		return ICV_SUCCESS;
	}

	return AM_LOCAL_VerifyUserRights_NoPasswd(plAuth, szUserName, szLoginGroupName, &szAuthLabel, 1);
}

/**
 *  设置自动锁定时间.
 *
 *  @param  -[in]  long  lAutoTimeOut: [自动锁定时间]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     09/10/2008  fengjuanyong  Initial Version.
 */
long API_CALL_METHOD SetAutoUnLockTime(long lAutoTimeOut)
{
	if (lAutoTimeOut <= 0)
		g_objLockHandle.m_lAutoLockTime = ICV_CCTV_DEFAULT_TIMEOUT;
	else
		g_objLockHandle.m_lAutoLockTime = lAutoTimeOut;

	return ICV_SUCCESS;
}

/**
 *  获取用户等级.
 *
 *  @param  -[in]  char *szUserName: [用户名]
 *  @param  -[in,out]  long &lLevel: [等级]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     01/18/2011  fengjuanyong  Initial Version.
 */
long API_CALL_METHOD GetUserPRI(char *szUserName, char *szLoginGroupName, long &lLevel)
{
	// 超级用户
	if (0 == strcmp(szUserName, ICV_AUTH_DEFAULT_USER))
	{
		lLevel = 0xFF;
		return ICV_SUCCESS;
	}

	long lRetval = ICV_SUCCESS;	

	//检查参数的合法性
	if(szUserName == NULL || szLoginGroupName == NULL)
		return EC_ICV_AMAPI_PARAMERROR;

	if(strlen(szUserName) > CVAUTH_USERNAME_LEN || strcmp(szUserName, "") == 0)
		return EC_ICV_AMAPI_INVALID_USER_LOGINNAME;

	if(strlen(szLoginGroupName) > CVAUTH_GROUPNAME_LEN)
		return EC_ICV_AMAPI_INVALID_GROUPNAME;

	//以登陆群组的权限判断
	if(strcmp(szLoginGroupName, "") != 0)
	{
		AM_Group GroupInfo;
		lRetval = AM_LOCAL_GetGroup(&GroupInfo, szLoginGroupName);
		if (lRetval != ICV_SUCCESS)
			return lRetval;

		lLevel = GroupInfo.lLevel;
	}
	//以用户的权限判断
	else
	{
		long lGroupCount = 0;
		lRetval = AM_LOCAL_GetGroupCountOfUser(&lGroupCount, szUserName);
		if (lRetval != ICV_SUCCESS)
			return lRetval;

		AM_Group *pGroups = new AM_Group[lGroupCount];
		lRetval = AM_LOCAL_GetGroupsOfUser(pGroups, lGroupCount, 0, szUserName);
		if (lRetval != ICV_SUCCESS)
		{
			delete[] pGroups;
			return lRetval;
		}

		lLevel = 0;
		for (int i=0; i<lGroupCount; i++)
		{
			if (pGroups[i].lLevel > lLevel)
				lLevel = pGroups[i].lLevel;
		}
		delete[] pGroups;
	}
	return lRetval;
}

long GetUserLevel(char *szUserName, char *szLoginGroupName, char *szAuthLabel, long &lLevel)
{
	// 超级用户
	if (0 == strcmp(szUserName, ICV_AUTH_DEFAULT_USER))
	{
		lLevel = 0xFF;
		return ICV_SUCCESS;
	}

	long lRetval = ICV_SUCCESS;
	
	//检查参数的合法性
	if(szUserName == NULL || szLoginGroupName == NULL || szAuthLabel == NULL)
		return EC_ICV_AMAPI_PARAMERROR;
	
	if(strlen(szUserName) > CVAUTH_USERNAME_LEN || strcmp(szUserName, "") == 0)
		return EC_ICV_AMAPI_INVALID_USER_LOGINNAME;
		
	if(strlen(szLoginGroupName) > CVAUTH_GROUPNAME_LEN)
		return EC_ICV_AMAPI_INVALID_GROUPNAME;

	if (strlen(szAuthLabel) > CVAUTH_RESOURCE_LEN || strcmp(szAuthLabel, "") == 0)
		return EC_ICV_AMAPI_INVALID_RESOURCELABEL;

	long lRight = 0;
	lRetval = VerifyUserRight(szUserName, szLoginGroupName, szAuthLabel, &lRight);
	if (lRetval != ICV_SUCCESS)
		return lRetval;
	
	if (lRight <= 0)
		return EC_ICV_CCTV_NOAUTH;
	
	//以登陆群组的权限判断
	if(strcmp(szLoginGroupName, "") != 0)
	{
		AM_Group GroupInfo;
		lRetval = AM_LOCAL_GetGroup(&GroupInfo, szLoginGroupName);
		if (lRetval != ICV_SUCCESS)
			return lRetval;

		lLevel = GroupInfo.lLevel;
	}
	//以用户的权限判断
	else
	{
		long lGroupCount = 0;
		lRetval = AM_LOCAL_GetGroupCountOfUser(&lGroupCount, szUserName);
		if (lRetval != ICV_SUCCESS)
			return lRetval;

		AM_Group *pGroups = new AM_Group[lGroupCount];
		lRetval = AM_LOCAL_GetGroupsOfUser(pGroups, lGroupCount, 0, szUserName);
		if (lRetval != ICV_SUCCESS)
		{
			delete[] pGroups;
			return lRetval;
		}

		for (int i=0; i<lGroupCount; i++)
		{
			lRetval = VerifyUserRight(szUserName, pGroups[i].szName, szAuthLabel, &lRight);
			if (lRetval != ICV_SUCCESS)
				continue;

			if (lRight > 0)
			{
				if (pGroups[i].lLevel > lLevel)
					lLevel = pGroups[i].lLevel;
			}
		}
		delete[] pGroups;
	}
	return lRetval;
}

/**
 *  用户自动锁定对象.
 *
 *  @param  -[in]  char*  szUserName: [用户名]
 *  @param  -[in]    char*szLoginGroupName: [群组名]
 *  @param  -[in]  long  lObjID: [对象ID]
 *  @param  -[in]  long  lObjType: [对象类型]
 *  @param  -[in]  char*  szAuthLabel: [资源名]
 *  @return 0-锁定对象 
 *			1-无权限，不能锁定
 *			2-有权限，已被优先级更高的人锁定
 *			3-有权限，已被优先级相同的人锁定.
 *
 *  @version     09/16/2008  fengjuanyong  Initial Version.
 */
long API_CALL_METHOD AcquireLock(char *szUserName, char *szLoginGroupName, long lObjID, long lObjType, char *szAuthLabel)
{
	// 超级用户
	if (0 == strcmp(szUserName, ICV_AUTH_DEFAULT_USER))
		return ICV_SUCCESS;

	long lLevel = 0;
	long lRetval = GetUserLevel(szUserName, szLoginGroupName, szAuthLabel, lLevel);
	if (lRetval != ICV_SUCCESS)
		return lRetval;
	
	lRetval = g_objLockHandle.UpdateLockHandle(lObjID, lObjType, lLevel, szUserName, g_objLockHandle.m_lAutoLockTime, ICV_CCTV_AUTOLOCK, ICV_CCTV_ADDLOCK);
	return lRetval;
}

/**
 *  用户手动锁定对象.
 *
 *  @param  -[in]  char*  szUserName: [用户名]
 *  @param  -[in]    char*szLoginGroupName: [群组名]
 *  @param  -[in]  long  lObjID: [对象ID]
 *  @param  -[in]  long  lObjType: [对象类型]
 *  @param  -[in]  char*  szAuthLabel: [资源名]
 *  @param  -[in]  long  lManualTimeOut: [超时]
 *  @return 0-锁定对象 
 *			1-无权限，不能锁定
 *			2-有权限，已被优先级更高的人锁定
 *			3-有权限，已被优先级相同的人锁定.
 *
 *  @version     09/16/2008  fengjuanyong  Initial Version.
 */
long API_CALL_METHOD ManualAcquireLock(char *szUserName, char*szLoginGroupName, long lObjID, long lObjType, char *szAuthLabel, long lManualTimeOut)
{
	// 超级用户
	if (0 == strcmp(szUserName, ICV_AUTH_DEFAULT_USER))
		return ICV_SUCCESS;

	long lLevel = 0;
	long lRetval = GetUserLevel(szUserName, szLoginGroupName, szAuthLabel, lLevel);
	if (lRetval != ICV_SUCCESS)
		return lRetval;
	
	lRetval = g_objLockHandle.UpdateLockHandle(lObjID, lObjType, lLevel, szUserName, lManualTimeOut, ICV_CCTV_MANUALLOCK, ICV_CCTV_ADDLOCK);
	return lRetval;
}

/**
 *  用户手动释放对象.
 *
 *  @param  -[in]  char*  szUserName: [用户名]
 *  @param  -[in]    char*szLoginGroupName: [群组名]
 *  @param  -[in]  long  lObjID: [对象ID]
 *  @param  -[in]  long  lObjType: [对象类型]
 *  @param  -[in]  char*  szAuthLabel: [资源名]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     09/16/2008  fengjuanyong  Initial Version.
 */
long API_CALL_METHOD ManualReleaseLock(char *szUserName, char*szLoginGroupName, long lObjID, long lObjType, char *szAuthLabel)
{
	// 超级用户
	if (0 == strcmp(szUserName, ICV_AUTH_DEFAULT_USER))
		return ICV_SUCCESS;

	long lRetval = ICV_SUCCESS;
	
	//检查参数的合法性
	if(szUserName == NULL || szLoginGroupName == NULL || szAuthLabel == NULL)
		return EC_ICV_AMAPI_PARAMERROR;
	
	if(strlen(szUserName) > CVAUTH_USERNAME_LEN || strcmp(szUserName, "") == 0)
		return EC_ICV_AMAPI_INVALID_USER_LOGINNAME;


	long lLevel = 0;
	lRetval = GetUserLevel(szUserName, szLoginGroupName, szAuthLabel, lLevel);
	if (lRetval != ICV_SUCCESS)
		return lRetval;

	lRetval = g_objLockHandle.UpdateLockHandle(lObjID, lObjType, lLevel, szUserName, g_objLockHandle.m_lAutoLockTime, ICV_CCTV_AUTOLOCK, ICV_CCTV_DELLOCK);
	return lRetval;
}


