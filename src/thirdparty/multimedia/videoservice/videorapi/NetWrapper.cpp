/**************************************************************
 *  Filename:    NetWrapper.cpp
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: 消息处理.
 *
 *  @author:     zhucongfeng
 *  @version     04/20/2009  zhucongfeng  Initial Version
 *				 04/29/2009  zhucongfeng  将用户修改为超级用户；修改解析消息头
 *				 04/30/2009  zhucongfeng  修改组织消息头的组织结构为空字符串
 *				 05/20/2009  zhucongfeng  修改注册时的参数判断逻辑
 *				 06/01/2009  zhucongfeng  修改ParserMsgHeader
 *				 06/04/2009  zhucongfeng  修改评审缺陷，添加注释
 *				 08/11/2009	 zhucongfeng  根据静态代码检查修改参数合法性判断
 *				 06/23/2010  zhucongfeng  删除CVCommon的声明，直接用后台导出的定义
 *				 07/20/2010  zhucongfeng  添加客户端管理
 *				 09/15/2011  zhucongfeng  发送消息时，主备只要有一个成功就算成功
**************************************************************/
// NetWrapper.cpp: implementation of the CNetWrapper class.
//
//////////////////////////////////////////////////////////////////////


#include "NetWrapper.h"
#include "common/cvcomm.hxx"
#include "gettext/libintl.h"

#define _(STRING) gettext(STRING)
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CNetWrapper::CNetWrapper()
{
	m_hLocalQue = NULL;
	m_pMsgPacker = new CVideoProtoPacker();
	m_pMsgParser = new CVideoProtoParser();

	CVLog.SetLogFileName("VideoRAPI");
	m_lstampID = 0;
}

CNetWrapper::~CNetWrapper()
{
	SAFE_DELETE(m_pMsgPacker);
	SAFE_DELETE(m_pMsgParser);

	m_hLocalQue = NULL;
}

/**
 *  Free the buffer allocated in cvndk-call.
 *
 *  @param  -[in]  char*  pszResponse: [buffer pointer to be freed]
 *  @return $(no return).
 *
 *  @version     4/20/2009  zhucongfeng  Initial Version
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
 *  @version     4/20/2009  zhucongfeng  Initial Version
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
 *  @version     4/20/2009  zhucongfeng  Initial Version
 */
long CNetWrapper::InitializeNetwork()
{
	long lRet = ICV_SUCCESS;
	m_hLocalQue = NULL;
	
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

    //设置超时为2s，原因：由于videoclient页面运行初始化调用至少5次，切换摄像头调用也至少2次，这里设置发送超时 huangming 20130411
    lRet = CVNDK_SetSendTimeout(2000);//2s

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
 *  @version     4/20/2009  zhucongfeng  Initial Version
 */
long CNetWrapper::UnInitializeNetwork()
{
	long lRet = ICV_SUCCESS;

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
 *  @version     04/21/2009  zhucongfeng  Initial Version.
 */

/*long CNetWrapper::RegisterClient(CLIENT_INFO& clientInfo)
{
	long lRet = ICV_SUCCESS;

	if((clientInfo.szMainServIp == NULL) || (clientInfo.szMainServIp[0] == '\0'))
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR,"Register client failed, main server ip error");
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	
	if( clientInfo.bIsExistBak && ((clientInfo.szBakServIp == NULL) || (clientInfo.szBakServIp[0] == '\0')) )
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR,"Register client failed, bak server ip error");
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}

	//获取端口
	unsigned short uPort = 0;
	uPort = CVComm.GetServicePort(VIDEO_SERVICE_NAME, ICV_VIDEO_DEFAULT_TCPPORT);

	//注册Queue
	FreeRecvHandle(m_hMainServQue);
	m_hMainServQue = RegRemoteServer(clientInfo.szMainServIp, uPort);
	if(m_hMainServQue == NULL)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR,"Register remote server failed, serverIP:%s, port:%d", clientInfo.szMainServIp, uPort);
		return EC_ICV_CCTV_FAILTOREGREMOTEQUE;
	}

	if(clientInfo.bIsExistBak)
	{
		FreeRecvHandle(m_hBakServQue);
		m_hBakServQue = RegRemoteServer(clientInfo.szBakServIp, uPort);
		if(m_hBakServQue == NULL)
		{
			CVLog.LogMessage(LOG_LEVEL_ERROR,"Register remote server failed, serverIP:%s, port:%d", clientInfo.szBakServIp, uPort);
			return EC_ICV_CCTV_FAILTOREGREMOTEQUE;
		}
	}

	return lRet;
}*/

/**
 *  收发消息.
 *
 *  @param  -[in]  char*  pSendBuff: [要发送的消息内容]
 *  @param  -[iout]  long  lSendBuffLen: [要发送的消息长度]
 *  @param  -[out]  void*  *pRecvBuf: [接收的消息内容]
 *  @param  -[out]  long&  lRecvBufLen: [接收的消息长度]
 *  @param  -[in]  long  lTimeOut: [超时时间]
 *
 *  @version     04/21/2009  zhucongfeng  Initial Version.
 */

long CNetWrapper::SendAndReceiveMsg(PCLIENT_INFO pClient, char *pSendBuff, long lSendBuffLen, void **pRecvBuf, long &lRecvBufLen, long lTimeOut)
{
	ACE_GUARD_RETURN(ACE_Thread_Mutex, mon, m_mutex, EC_ICV_CVNDK_ACQUIRE_MUTEX_FAILED);

	long lRet = ICV_SUCCESS;
	HQUEUE hSrcQueue = NULL;

	if((pClient == NULL) || (pSendBuff == NULL) || (lSendBuffLen <= 0))
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}

	if((pClient->hServerQ == NULL) && ((pClient->bIsExistBak) && (pClient->hBakServQ == NULL)))
		return EC_ICV_CCTV_FAILTOREGREMOTEQUE;

	//发送数据
	bool bMainSend = true;
	bool bBakSend = true;

	if(pClient->hServerQ == NULL)
		bMainSend = false;
	else
	{
		lRet = CVNDK_SendWithStampID(pClient->hServerQ, m_hLocalQue, pSendBuff, lSendBuffLen, ASYNC_SEND);
		if(lRet != ICV_SUCCESS)
		{
			bMainSend = false;
			CVLog.LogMessage(LOG_LEVEL_ERROR,_("CVNDK_SendWithStampID() Failed, RetCode: %d"),  lRet);

			if(!pClient->bIsExistBak)
				return lRet;
		}
	}
	
	if(pClient->bIsExistBak)
	{
		if(pClient->hBakServQ == NULL)
			bBakSend = false;
		else
		{
            //if (CVNDK_GetConnectStat(pClient->hBakServQ) == CVNDK_CONNECTED)
            {
                lRet = CVNDK_Send(pClient->hBakServQ, m_hLocalQue, pSendBuff, lSendBuffLen, ASYNC_SEND);
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



	if((pSendBuff == NULL) || (lSendBuffLen <= 0))
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
}

/**
 *  组织消息头.
 *
 *  @param  -[in]  char*  pszBuff: [消息缓冲区]
 *  @param  -[in]  long  nInLen: [缓冲区长度]
 *  @param  -[in]  long  nCmd: [命令码]
 *  @param  -[out]  long&  nOutLen: [实际长度]
 *
 *  @version     04/21/2009  zhucongfeng  Initial Version.
 */

long CNetWrapper::PackMsgHeader(PCLIENT_INFO pClient, char* pszBuff, long nInLen, long nCmd, long &nOutLen)
{
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	/*消息头格式 命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名*/
	return m_pMsgPacker->PackCmdHeader(pszBuff, nInLen, nCmd, ++m_lstampID, pClient->szUserName, pClient->szGroup, nOutLen);
}

/**
 *  解析消息头.
 *
 *  @param  -[in]  char*  pszBuff: [缓冲区]
 *  @param  -[out]  long  lResult: [消息处理结果]
 *  @param  -[out]  long  lOutLen: [解析的长度]
 *
 *  @version     04/21/2009  zhucongfeng  Initial Version.
 */

long CNetWrapper::ParserMsgHeader(char* pszBuff, long &lResult, long &lOutLen)
{
	long lCmdCode;
	long lStamp;

	return m_pMsgParser->PaseRetHeader(pszBuff, lCmdCode, lStamp, lResult, lOutLen);
}
