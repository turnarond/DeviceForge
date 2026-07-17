// NetWrapper.h: interface for the CNetWrapper class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_NETWRAPPER_H__427DB2AD_B732_4E66_B3DB_5F80EB925A5B__INCLUDED_)
#define AFX_NETWRAPPER_H__427DB2AD_B732_4E66_B3DB_5F80EB925A5B__INCLUDED_

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

class CNetWrapper  
{
friend class ACE_Singleton<CNetWrapper, ACE_Thread_Mutex>;
UNITTEST(CNetWrapper);
public:
	CNetWrapper();
	virtual ~CNetWrapper();

	CVideoProtoPacker *m_pMsgPacker;
	CVideoProtoParser *m_pMsgParser;

protected:
	//±¾µØqueue
	HQUEUE m_hLocalQue;
	long m_lstampID;
public:
	long PackMsgHeader(PCLIENT_INFO pClient, char* pszBuff, long nInLen, long nCmd, long &nOutLen);
	long ParserMsgHeader(char* pszBuff, long &lResult, long &lOutLen);
	long InitializeNetwork();
	long UnInitializeNetwork();
	void FreeRecvBuff(char *pszResponse);
	void FreeRecvHandle(HQUEUE hCliQue);
	HQUEUE RegRemoteServer(char *szIp, long lPort);
	long SendAndReceiveMsg(PCLIENT_INFO pClient, char *pSendBuff, long lSendBuffLen, void **pRecvBuf, long &lRecvBufLen, long lTimeOut = 2000);

private:
	ACE_Thread_Mutex m_mutex;
};

typedef ACE_Singleton<CNetWrapper, ACE_Thread_Mutex> CNetWrapperSingleton;
#define NET_WRAPPER CNetWrapperSingleton::instance()

#endif // !defined(AFX_NETWRAPPER_H__427DB2AD_B732_4E66_B3DB_5F80EB925A5B__INCLUDED_)
