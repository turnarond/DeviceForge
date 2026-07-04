// NetWrapper.h: interface for the CNetWrapper class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_NETWRAPPER_H__722128D1_3609_4EFD_B808_95140F702FF8__INCLUDED_)
#define AFX_NETWRAPPER_H__722128D1_3609_4EFD_B808_95140F702FF8__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "errcode/ErrCode_iCV_CCTV.hxx"
#include "common/CVNDK.h"
#include <ace/Null_Mutex.h>
#include <ace/Singleton.h>
#include "common/CommHelper.h"
#include "multimedia/video/VideoRAPIDef.h"
#include <ace/Process_Mutex.h>
#include "multimedia/video/VideoProtoPacker.h"
#include "multimedia/video/VideoProtoParser.h"
#include "VideoTimeSynchCfg.h"

class CNetWrapper  
{
public:
	CNetWrapper();
	virtual ~CNetWrapper();

	CVideoProtoPacker *m_pMsgPacker;
	CVideoProtoParser *m_pMsgParser;

protected:
	//本地queue
	HQUEUE m_hLocalQue;
	
	//服务端Queue
	HQUEUE m_hMainServQue;
	HQUEUE m_hBakServQue;

public:
	long PackMsgHeader(char* pszBuff, long nInLen, long nCmd, long &nOutLen);
	long ParserMsgHeader(char* pszBuff, long &lResult, long &lOutLen);
	long InitializeNetwork();
	long UnInitializeNetwork();
	void FreeRecvBuff(char *pszResponse);
	void FreeRecvHandle(HQUEUE &hCliQue);
	HQUEUE RegRemoteServer(char *szIp, long lPort);
	long SendAndReceiveMsg(char *pSendBuff, long lSendBuffLen, void **pRecvBuf, long &lRecvBufLen, long lTimeOut = 2000);
	long RegisterClient();
	
private:
	ACE_Thread_Mutex m_mutex;

};

typedef ACE_Singleton<CNetWrapper, ACE_Thread_Mutex> CNetWrapperSingleton;
#define NET_WRAPPER CNetWrapperSingleton::instance()

#endif // !defined(AFX_NETWRAPPER_H__722128D1_3609_4EFD_B808_95140F702FF8__INCLUDED_)
