/**************************************************************
 *  Filename:    NetWrapper.cpp
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: $(消息处理).
 *
 *  @author:     zhucongfeng
 *  @version     08/28/2009  zhucongfeng  Initial Version
 *  @version     09/17/2009  zhucongfeng  修改FreeRecvHandle的参数
 *  @version	 06/23/2010  zhucongfeng  删除CVCommon的声明，直接用后台导出的定义
**************************************************************/

// NetWrapper.cpp: implementation of the CNetWrapper class.
//
//////////////////////////////////////////////////////////////////////

#include "NetWrapper.h"
#include "VideoTimeSynchDef.h"
#include "common/cvcomm.hxx"
#include "gettext/libintl.h"

#define _(STRING) gettext(STRING)
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CNetWrapper::CNetWrapper()
{
	m_hLocalQue = NULL;
	m_hMainServQue = NULL;
	m_hBakServQue = NULL;
	m_pMsgPacker = new CVideoProtoPacker();
	m_pMsgParser = new CVideoProtoParser();

	InitializeNetwork();
}

CNetWrapper::~CNetWrapper()
{
//	UnInitializeNetwork();

	SAFE_DELETE(m_pMsgPacker);
	SAFE_DELETE(m_pMsgParser);
}

/**
 *  Free the buffer allocated in cvndk-call.
 *
 *  @param  -[in]  char*  pszResponse: [buffer pointer to be freed]
 *  @return $(no return).
 *
 *  @version     08/28/2009  zhucongfeng  Initial Version
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
 *  @version     08/28/2009  zhucongfeng  Initial Version
 */
void CNetWrapper::FreeRecvHandle(HQUEUE &hCliQue)
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
 *  @version     08/28/2009  zhucongfeng  Initial Version
 */
long CNetWrapper::InitializeNetwork()
{
	long lRet = ICV_SUCCESS;
	
	lRet = CVNDK_Init(ASYNC_SEND); // adopting syncronous sending mode
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogMessage(LOG_LEVEL_CRITICAL,_("CVNDK_Init(ASYNC_SEND), RetCode: %d"), lRet);
		return lRet;
	}

	//注册本地Queue
	m_hLocalQue = CVNDK_RegLocalQueue(RANDOM_QUEUEID);
	if(m_hLocalQue == NULL)
	{
		lRet = EC_ICV_CCTV_FAILTOREGLOCALQUE;
		CVLog.LogMessage(LOG_LEVEL_CRITICAL,_("CVNDK_RegLocalQueue(%d) Failed, RetCode: %d"),RANDOM_QUEUEID , lRet);
		return lRet;
	}	
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
 *  @version     08/28/2009  zhucongfeng  Initial Version
 */
long CNetWrapper::UnInitializeNetwork()
{
	long lRet = ICV_SUCCESS;

	//释放服务端Queue
	FreeRecvHandle(m_hMainServQue);
	FreeRecvHandle(m_hBakServQue);

	if(m_hLocalQue)
	{
		lRet = CVNDK_ReleaseQueue(m_hLocalQue);
	}

	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR,_("CVNDK_ReleaseQueue(%d) Failed, RetCode: %d"), (long)m_hLocalQue, lRet);
	}

	lRet = CVNDK_Finalize();

	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR,_("CVNDK_Finalize() Failed, RetCode: %d"),  lRet);
	}
	return lRet;
}


HQUEUE CNetWrapper::RegRemoteServer(char *szIp, long lPort)
{
	if((szIp == NULL) || (szIp[0] == '\0') || (lPort <= 0))
	{
		return NULL;
	}
	return CVNDK_RegRemoteQueue(VIDEO_SERVICE_RECVQUEST_QUEUEID, szIp, (unsigned short)lPort);	
}

/**
 *  注册客户端.
 *
 *  @param  -[in]  CLIENT_INFO&  clientInfo: [comment]
 *
 *  @version     08/28/2009  zhucongfeng  Initial Version.
 */

long CNetWrapper::RegisterClient()
{
	long lRet = ICV_SUCCESS;

	if((VIDEO_TIME_SYNCH_CONFIG->m_szMainSvrIP == NULL) || (VIDEO_TIME_SYNCH_CONFIG->m_szMainSvrIP[0] == '\0'))
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR,_("Register client failed, main server ip error"));
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}

	//获取端口
	unsigned short uPort = 0;
	uPort = CVComm.GetServicePort(VIDEO_SERVICE_NAME, ICV_VIDEO_DEFAULT_TCPPORT);

	//注册Queue
	FreeRecvHandle(m_hMainServQue);
	m_hMainServQue = RegRemoteServer(VIDEO_TIME_SYNCH_CONFIG->m_szMainSvrIP, uPort);
	if(m_hMainServQue == NULL)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR,_("Register remote server failed, serverIP:%s, port:%d"), VIDEO_TIME_SYNCH_CONFIG->m_szMainSvrIP, uPort);
		return EC_ICV_CCTV_FAILTOREGREMOTEQUE;
	}

	if(VIDEO_TIME_SYNCH_CONFIG->m_bIsExistBak)
	{
		FreeRecvHandle(m_hBakServQue);
		m_hBakServQue = RegRemoteServer(VIDEO_TIME_SYNCH_CONFIG->m_szBakSvrIP, uPort);
		if(m_hBakServQue == NULL)
		{
			CVLog.LogMessage(LOG_LEVEL_ERROR,_("Register remote server failed, serverIP:%s, port:%d"), VIDEO_TIME_SYNCH_CONFIG->m_szBakSvrIP, uPort);
			return EC_ICV_CCTV_FAILTOREGREMOTEQUE;
		}
	}

	return lRet;
}

/**
 *  收发消息.
 *
 *  @param  -[in]  char*  pSendBuff: [要发送的消息内容]
 *  @param  -[iout]  long  lSendBuffLen: [要发送的消息长度]
 *  @param  -[out]  void*  *pRecvBuf: [接收的消息内容]
 *  @param  -[out]  long&  lRecvBufLen: [接收的消息长度]
 *  @param  -[in]  long  lTimeOut: [超时时间]
 *
 *  @version     08/28/2009  zhucongfeng  Initial Version.
 */

long CNetWrapper::SendAndReceiveMsg(char *pSendBuff, long lSendBuffLen, void **pRecvBuf, long &lRecvBufLen, long lTimeOut)
{
	ACE_GUARD_RETURN(ACE_Thread_Mutex, mon, m_mutex, EC_ICV_CVNDK_ACQUIRE_MUTEX_FAILED);

	long lRet = ICV_SUCCESS;
	HQUEUE hSrcQueue = NULL;

	if((pSendBuff == NULL) || (lSendBuffLen <= 0))
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}

    if((m_hMainServQue == NULL) 
        && ((VIDEO_TIME_SYNCH_CONFIG->m_bIsExistBak) && (m_hBakServQue == NULL)))
        return EC_ICV_CCTV_FAILTOREGREMOTEQUE;

	//发送数据
    bool bMainSend = true;
    bool bBakSend = true;

    if(m_hMainServQue == NULL)
        bMainSend = false;
    else
    {
        lRet = CVNDK_SendWithStampID(m_hMainServQue, m_hLocalQue, pSendBuff, lSendBuffLen, ASYNC_SEND);
	    if(lRet != ICV_SUCCESS)
	    {
            bMainSend = false;
            CVLog.LogMessage(LOG_LEVEL_ERROR,_("CVNDK_SendWithStampID() Failed, RetCode: %d"),  lRet);
		    
            if(!(VIDEO_TIME_SYNCH_CONFIG->m_bIsExistBak))
                return lRet;
	    }
    }

	if(VIDEO_TIME_SYNCH_CONFIG->m_bIsExistBak)
	{
        if(m_hBakServQue == NULL)
            bBakSend = false;
        else
        {
            //if (CVNDK_GetConnectStat(m_hBakServQue) == CVNDK_CONNECTED)
            {
                lRet = CVNDK_Send(m_hBakServQue, m_hLocalQue, pSendBuff, lSendBuffLen, ASYNC_SEND);
                if(lRet != ICV_SUCCESS)
                {
                    bBakSend = false;
                    CVLog.LogMessage(LOG_LEVEL_ERROR,_("CVNDK_Send() Failed, RetCode: %d"),  lRet);
                    
                    if(!bMainSend)
                        return lRet;
                }
            }
        }
	}

    if(!bMainSend && !bBakSend)
        return EC_ICV_CCTV_FAILTOSENDBOTHSERVER;

	//接收电文
	lRet = CVNDK_RecvWithStampID(m_hLocalQue, &hSrcQueue, pRecvBuf, lRecvBufLen, lTimeOut);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR,_("CVNDK_RecvWithStampID() Failed, RetCode: %d"),  lRet);
		return lRet;
	}
	CVNDK_Free(hSrcQueue);
	return lRet;
}

/**
 *  组织消息头.
 *
 *  @param  -[in]  char*  pszBuff: [消息缓冲区]
 *  @param  -[in]  long  nInLen: [缓冲区长度]
 *  @param  -[in]  long  nCmd: [命令码]
 *  @param  -[out]  long&  nOutLen: [实际长度]
 *
 *  @version     08/28/2009  zhucongfeng  Initial Version.
 */

long CNetWrapper::PackMsgHeader(char* pszBuff, long nInLen, long nCmd, long &nOutLen)
{
	return m_pMsgPacker->PackCmdHeader(pszBuff, nInLen, nCmd, 0, ICV_AUTH_DEFAULT_USER, "", nOutLen);
}

/**
 *  解析消息头.
 *
 *  @param  -[in]  char*  pszBuff: [缓冲区]
 *  @param  -[out]  long  lResult: [消息处理结果]
 *  @param  -[out]  long  lOutLen: [解析的长度]
 *
 *  @version     08/28/2009  zhucongfeng  Initial Version.
 */

long CNetWrapper::ParserMsgHeader(char* pszBuff, long &lResult, long &lOutLen)
{
	long lCmdCode;
	long lStamp;

	return m_pMsgParser->PaseRetHeader(pszBuff, lCmdCode, lStamp, lResult, lOutLen);
}
