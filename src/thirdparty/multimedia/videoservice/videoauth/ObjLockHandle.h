// ObjLockHandle.h: interface for the CObjLockHandle class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_OBJLOCKHANDLE_H__AD609F3B_53DC_40E0_BA68_A5E1153CC1CB__INCLUDED_)
#define AFX_OBJLOCKHANDLE_H__AD609F3B_53DC_40E0_BA68_A5E1153CC1CB__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "authmanager/AMDef.h"
#include "errcode/ErrCode_iCV_CCTV.hxx"
#include <vector>
#include <map>

using namespace std;

#define  ICV_CCTV_AUTOLOCK					0
#define  ICV_CCTV_MANUALLOCK				1

#define ICV_CCTV_ADDLOCK				0
#define ICV_CCTV_DELLOCK				1

// 对象信息
typedef struct tagObjBaseInfo
{	
	char	szUserName[CVAUTH_USERNAME_LEN];		 // 用户名称
	time_t	tmLock;									 // 加锁时间
	long	lAutoLock;								 // 0自动锁定 1手动锁定
	long	lLockTime;								 // 锁定时间秒
	long	lUserPRI;								 // 优先级
	tagObjBaseInfo()
	{
		memset(szUserName, 0, CVAUTH_USERNAME_LEN);
		tmLock = 0;
		lAutoLock = 0;
		lLockTime = 0;
		lUserPRI = 0;
	}
}OBJBASEINFO;

typedef std::map<long, OBJBASEINFO *> OBJBASEINFOMAP;	 // 对象map

// 锁定的资源列表
typedef struct tagObjLockInfo
{
	long			lObjType;									 // 对象类型
	OBJBASEINFOMAP	*pObjBaseInfoMap;					 
	tagObjLockInfo()
	{
		lObjType = 0;
		pObjBaseInfoMap = NULL;
	}

	~tagObjLockInfo()
	{
		if(pObjBaseInfoMap)
		{
			pObjBaseInfoMap->clear();
			delete pObjBaseInfoMap;
			pObjBaseInfoMap = NULL;
		}
	}
}OBJLOCKINFO;

typedef std::vector<OBJLOCKINFO *> OBJLOCKINFOVEC;	 // 对象锁定列表


class CObjLockHandle
{
public:
	CObjLockHandle();
	virtual ~CObjLockHandle();

public:
	long	InitLockHandle();															  
	long	UnInitLockHandle();

	long	UpdateLockHandle(long lObjID, long lObjType, long lUserPRI, char *szUserName, long lManualTimeOut, long lAutoLock, long lDelLock);

public:
	OBJLOCKINFOVEC m_objLockInfoVec;
	long		   m_lAutoLockTime;
};

#endif // !defined(AFX_GENXMLNODE_H__AD609F3B_53DC_40E0_BA68_A5E1153CC1CB__INCLUDED_)
