/**************************************************************
 *  Filename:    NetWrapper.h
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: NetWrapper.h: interface for the CNetWrapper class.
 *	CNetWrapper is a class which wrapper the CVNDK SDK
 *	it uses singleton design mode 
 *  @author:     shijunpu
 *  @version     05/22/2008  shijunpu  Initial Version
**************************************************************/

#if !defined(AFX_NETWRAPPER_H__4220AA89_F90B_41DF_9145_F1C19F63B71B__INCLUDED_)
#define AFX_NETWRAPPER_H__4220AA89_F90B_41DF_9145_F1C19F63B71B__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000


#include "errcode/ErrCode_iCV_CC.hxx"
#include "common/CVNDK.h"
#include <ace/Null_Mutex.h>
#include <ace/Singleton.h>
//#include "configcenter/CCDef.h"
#include "common/CommHelper.h"

class CNetWrapper  
{
friend class ACE_Singleton<CNetWrapper, ACE_Thread_Mutex>;
UNITTEST(CNetWrapper);
public:
	CNetWrapper();
	virtual ~CNetWrapper();


protected:
	HQUEUE m_hLocalQue;

public:
	long InitializeNetwork();
	long UnInitializeNetwork();
	long SendToClient(HQUEUE hCliQue, char *pszBuf, long lBufSize);
	long RecvFromClient(HQUEUE &hCliQue, char *&pszResponse, long &lRcvLen);
	void FreeRecvBuff(char *pszResponse);
	void FreeRecvHandle(HQUEUE hCliQue);
	HQUEUE GetLocalQue();
	long PrintClientInfo(HQUEUE hQueue, bool bIsRecv);
};

typedef ACE_Singleton<CNetWrapper, ACE_Thread_Mutex> CNetWrapperSingleton;
#define NET_WRAPPER CNetWrapperSingleton::instance()


#endif // !defined(AFX_NETWRAPPER_H__4220AA89_F90B_41DF_9145_F1C19F63B71B__INCLUDED_)
