/**************************************************************
 *  Filename:    NetWrapper.cpp
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: implementation of the CNetWrapper class.
 *
 *  @author:     shijunpu
 *  @version     05/22/2008  shijunpu  Initial Version
				 12/08/2009  chenzhan  取消获取QIP的打印日志.
**************************************************************/

#include "VideoSvcDef.h"
#include "NetWrapper.h"
#include "VideoCfg.h"
#include "gettext/libintl.h"

#define _(STRING) gettext(STRING)
#define DEFAULT_RECVREQUEST_TIMEOUT		200
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CNetWrapper::CNetWrapper()
{
	m_hLocalQue = NULL;
}

CNetWrapper::~CNetWrapper()
{

}

/**
 *  Get local queue(server queue).
 *
 *  @return $(local queue).
 *
 *  @version     05/22/2008  shijunpu  Initial Version.
 */
HQUEUE CNetWrapper::GetLocalQue()
{
	return m_hLocalQue;
}

/**
 *  Free the buffer allocated in cvndk-call.
 *
 *  @param  -[in]  char*  pszResponse: [buffer pointer to be freed]
 *  @return $(no return).
 *
 *  @version     05/22/2008  shijunpu  Initial Version.
 */
void CNetWrapper::FreeRecvBuff(char *pszResponse)
{
	if(pszResponse)
		CVNDK_Free((void *&)pszResponse);
	pszResponse = NULL;
}

/**
 *  Free the client QUEUE handle allocated in cvndk-call.
 *
 *  @param  -[in]  HQUEUE  hCliQue: [CLIENT QUEUE handle allocated in cvndk-call]
 *  @return $(no return).
 *
 *  @version     05/27/2008  shijunpu  Initial Version.
 */
void CNetWrapper::FreeRecvHandle(HQUEUE hCliQue)
{
	if(hCliQue)
		CVNDK_Free(hCliQue);
	hCliQue = NULL;
}

/**
 *  Initialize network.
 *  call the CVNDK's interface to regmote queue and local queue
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     05/22/2008  shijunpu  Initial Version.
 */
long CNetWrapper::InitializeNetwork()
{
	m_hLocalQue = NULL;
	long lRet = CVNDK_Init(ASYNC_SEND); // adopting syncronous sending mode
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogMessage(LOG_LEVEL_CRITICAL,_("CVNDK_Init(SYNC_SEND), RetCode: %d"), lRet);
		return lRet;
	}

	m_hLocalQue = CVNDK_RegLocalQueue(VIDEO_SERVICE_RECV_CLIREQUEST_QUEUEID);
	if(!m_hLocalQue)
	{
		lRet = EC_ICV_CC_FAILTOREGLOCALQUE;
		CVLog.LogMessage(LOG_LEVEL_CRITICAL,_("CVNDK_RegLocalQueue(%d) Failed, RetCode: %d"), VIDEO_SERVICE_RECV_CLIREQUEST_QUEUEID, lRet);
		return lRet;
	}	
	
	// 获取服务端口
	unsigned short uPort = VIDEO_CONFIG->GetListenPort();
	lRet = CVNDK_Listen(uPort);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogMessage(LOG_LEVEL_CRITICAL,_("CVNDK_Listen(%d) Failed, RetCode: %d"), uPort, lRet);
		return lRet;
	}
	else
		CVLog.LogMessage(LOG_LEVEL_INFO,_("CVNDK_Listen(%d) Success"), uPort);

	return ICV_SUCCESS;
}

/**
 *  un initialize of network.
 *  release network-related resource via calling interface of CVNDK.
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     05/22/2008  shijunpu  Initial Version.
 */
long CNetWrapper::UnInitializeNetwork()
{
	long lRet = ICV_SUCCESS;
	if(m_hLocalQue)
		lRet = CVNDK_ReleaseQueue(m_hLocalQue);
	
	if(lRet != ICV_SUCCESS)
		CVLog.LogErrMessage(lRet,_("CVNDK_ReleaseQueue(%d) Failed."), (long)m_hLocalQue);

	lRet = CVNDK_Finalize();
	if(lRet != ICV_SUCCESS)
		CVLog.LogErrMessage(lRet,_("CVNDK_Finalize() Failed."));

	return lRet;
}

/**
 *  Send a request buffer to service.
 *  Call CVNDK-related interface to send a request.
 *
 *  @param  -[in]  char*  pszBuf: [消息]
 *  @param  -[in]  long  lBufSize: [消息长度]
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     05/22/2008  shijunpu  Initial Version.
 */
long CNetWrapper::SendToClient(HQUEUE hCliQue, char *pszBuf, long lBufSize)
{
	long lRet = PrintClientInfo(hCliQue, false);
	if (ICV_SUCCESS != lRet)
		CVLog.LogErrMessage(lRet, _("CNetWrapper::SendToClient() PrintClientInfo Error!"));

	// 发送
	lRet = CVNDK_Send(hCliQue, m_hLocalQue, pszBuf, lBufSize);
	if(lRet != ICV_SUCCESS)
		CVLog.LogErrMessage(lRet, _("Send To Client Failed."));

	long lLogSize = lBufSize<VIDEO_LOG_SIZE?lBufSize:VIDEO_LOG_SIZE;
	char szLogBuf[VIDEO_LOG_SIZE];
	memset(szLogBuf, 0x00, sizeof(szLogBuf));
	memcpy(szLogBuf, pszBuf, lLogSize);
	CVLog.LogMessage(LOG_LEVEL_DEBUG, _("Send:<%s>"), szLogBuf);

	return lRet;
}

/**
 *  receive a response buffer to service.
 *  Call CVNDK-related interface to receive a response buffer, pszResponse is allocated in CVNDK, and should be release via CVNDK-Release. 
 *
 *  @param  -[out]  char*  pszResponse: [pszResponse is allocated in CVNDK, and should be release via CVNDK-Release.]
 *  @param  -[out]  long*  plRepsonseLen: [allocated buffer length by CVNDK]
 *  @param  -[in]  long  lTimeOut: [recv timeout value]
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     05/22/2008  shijunpu  Initial Version.
 */
long CNetWrapper::RecvFromClient(HQUEUE &hCliQue, char *&pszResponse, long &lRcvLen)
{
	if (NULL == m_hLocalQue)
	{
		m_hLocalQue = CVNDK_RegLocalQueue(VIDEO_SERVICE_RECV_CLIREQUEST_QUEUEID);
	}

	long lRet = CVNDK_Recv(m_hLocalQue, &hCliQue, (void **)&pszResponse, lRcvLen, DEFAULT_RECVREQUEST_TIMEOUT);
	if (lRet != ICV_SUCCESS)
	{
		CVNDK_Free((void *&)pszResponse);
		return lRet;
	}

	lRet = PrintClientInfo(hCliQue, true);
	if (ICV_SUCCESS != lRet)
		CVLog.LogErrMessage(lRet, _("CNetWrapper::RecvFromClient() PrintClientInfo Error!"));

	long lLogSize = lRcvLen<VIDEO_LOG_SIZE?lRcvLen:VIDEO_LOG_SIZE;
	char szLogBuf[VIDEO_LOG_SIZE];
	memset(szLogBuf, 0x00, sizeof(szLogBuf));
	memcpy(szLogBuf, pszResponse, lLogSize);
	CVLog.LogMessage(LOG_LEVEL_DEBUG, _("Receive:<%s>"), szLogBuf);

	return lRet;
}

/**
 *  print information about client.
 *
 *  @param  -[in]  HQUEUE hQueue: [client Queue]
 *  @param  -[in]  bool bIsRecv: [is receive infomation]
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     03/25/2009  chenzhan  Initial Version.
 */
long CNetWrapper::PrintClientInfo(HQUEUE hQueue, bool bIsRecv)
{
	return ICV_SUCCESS;
}
