/**************************************************************
 *  Filename:    ReqFetchTask.cpp
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: implementation of the CRemoteProcTask class.
 *	远程命令包括与摄像头控制相关的动作, 且摄像头的控制设备配置在其他视频服务器上
 *	远程命令采用异步方式处理, 发送完成后将客户端的头部保存在队列中, 等接收到应答时比较, 如果匹配则返回给客户端
 *	发送给另外一个视频服务时采用主备都发的机制
 *
 *  @author:     shijunpu
 *  @version     05/29/2008  shijunpu  Initial Version
				 12/08/2009  chenzhan  取消获取QIP的打印日志.
**************************************************************/
 
#include "RemoteProcTask.h"
#include "NetWrapper.h"
#include "VideoCfg.h"
#include "gettext/libintl.h"

#define _(STRING) gettext(STRING)
#define TIMEOUT_RECVREQUEST_FROM_MSGDISPATCH		50	// ms,等待消息转发线程发送来的远程消息请求的时间间隔
#define TIMEOUT_RECVRESPONSE_FROM_REMOTESVR			50	// ms,等待消息远程消息发送回来的应答的时间间隔

#define THREAD_COUNT_IN_RMOTETASK					1		// 远程任务处理中的线程个数
#define RETMSG_BUFFER_MINSIZE						13		// 命令码+序列号
#define CHECK_REQUEST_LIST_CYCLE					60		// 大约每60秒检查一次

#define VIDEO_SERVICE_RECV_REMOTESVR_QUEUEID		202		// 服务端接收转发给远程服务的返回的QueueID

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CRemoteProcTask::CRemoteProcTask()
{

}

CRemoteProcTask::~CRemoteProcTask()
{
	m_reqMetaList.clear();
}

/**
 *  Start the Task and threads in the task are activated.
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     05/23/2008  shijunpu  Initial Version.
 */
int CRemoteProcTask::StartTask()
{
	long lRet = ICV_SUCCESS;
	int nRet = (long)this->activate(THR_NEW_LWP | THR_JOINABLE |THR_INHERIT_SCHED, THREAD_COUNT_IN_RMOTETASK);
	if(nRet != 0)
	{
		lRet = EC_ICV_CC_FAILTOSTARTTASK;
		CVLog.LogErrMessage(lRet, _("StartTask() Failed"));
	}
	
	return lRet;	
}

/**
 *  stop the request handler task and threads in it.
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     05/23/2008  shijunpu  Initial Version.
 */
int CRemoteProcTask::StopTask()
{
	// 投递退出消息令其退出
	for(long nWkr = 0; nWkr < THREAD_COUNT_IN_RMOTETASK; nWkr++)
	{
		ACE_Message_Block *pQuitMsgBlk = NULL;
		ACE_NEW_RETURN(pQuitMsgBlk, ACE_Message_Block(sizeof(long)), 1);
		pQuitMsgBlk->msg_type(ACE_Message_Block::MB_STOP);
		this->putq(pQuitMsgBlk);
	}

	return ICV_SUCCESS;	
}

/**
 *  main procedure of the task.
 *	get a msg from CVNDK and put it to Request Handler Queue
 *	消息头部会放入:
	1. 客户请求QueueID
	2. 命令号
	3. 命令序号
	4. 摄像头ID
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     05/23/2008  shijunpu  Initial Version.
 */
int CRemoteProcTask::svc()
{
	CVLog.LogMessage(LOG_LEVEL_INFO,_("CRemoteProcTask::svc() begin"));

	long lRet = InitializeNetwork();

	// ms,等待消息转发线程发送来的远程消息请求的时间间隔
	ACE_Time_Value tvRcvRequestTimeout;
	tvRcvRequestTimeout.msec(0);
	
	// get a msg from CVNDK and put it to Request Handler Queue
	ACE_Message_Block *pMsgBlk = NULL;
	while (true)
	{
		// 处理接收到的客户端请求消息
		int nRet = getq(pMsgBlk, &tvRcvRequestTimeout);
		// 收到客户端请求消息
		if(nRet >= 0) 
		{
			// process thread stop message
			if(pMsgBlk->msg_type() == ACE_Message_Block::MB_STOP) 
			{ 
				pMsgBlk->release(); 
				CVLog.LogMessage(LOG_LEVEL_INFO,_("CRemoteProcTask::svc() rcv a MB_STOP Msg, exit!"));
				break;
			}

			SendRequestToRemoteSvr(pMsgBlk);
			pMsgBlk->release();
			// release hCliQue
			// CVNDK_Free(hCliQue);
		}

		// 处理远程服务的应答消息
		char *pszResponse = NULL;
		HQUEUE hSrcQueue = 0;
		long lRcvLen = 0;
		long lTimeOut = TIMEOUT_RECVRESPONSE_FROM_REMOTESVR;
		long lRet = CVNDK_Recv(m_hLocalQue, &hSrcQueue, (void **)&pszResponse, lRcvLen, lTimeOut);
		if (lRet == ICV_SUCCESS)
		{
			lRet = RecvResponseFromRemoteSvr(pszResponse, lRcvLen);
		}	
		
		CVNDK_Free((void *&)pszResponse);
		CVNDK_Free(hSrcQueue);
	}

	UnInitializeNetwork();

	CVLog.LogMessage(LOG_LEVEL_INFO,_("CRemoteProcTask::svc() end"));
	return ICV_SUCCESS;
}

// 调用远程服务
long CRemoteProcTask::CallRemoteService(HQUEUE hCliQueID, long lCamID, char *szReqBuff, long lReqBufLen)
{
	long lRet = ICV_SUCCESS;
	return lRet;
}

// 发送请求给远程服务器
// 至少需要包含:
// 客户请求QueueID.命令号.命令序号.摄像头ID.长度
long CRemoteProcTask::SendRequestToRemoteSvr(ACE_Message_Block *pMsgBlk)
{
	long lRet = ICV_SUCCESS;
	long lTotalMsgLen = pMsgBlk->length();
	if(lTotalMsgLen < sizeof(HQUEUE) + sizeof(long) * 3)
	{
		lRet = EC_ICV_CCTV_PARSERBUFFERTOOSHORT;
		CVLog.LogErrMessage(lRet,  _("SendRequestToRemoteSvr lTotalMsgLen(%d) less than the minimal length"), lTotalMsgLen);
		return lRet;
	}
	
	CLIREQMETA reqMeta;
	// 客户端队列
	reqMeta.hCliQue = *(HQUEUE *)pMsgBlk->rd_ptr();
	pMsgBlk->rd_ptr(sizeof(HQUEUE));

	// 命令号
	long lVideoSvrID = *(long *)pMsgBlk->rd_ptr();
	pMsgBlk->rd_ptr(sizeof(long));

	// 命令号
	long lCmdCode = *(long *)pMsgBlk->rd_ptr();
	reqMeta.lRetCmdCode = lCmdCode + COMMAND_CODE_GAP_REQANDRET;
	pMsgBlk->rd_ptr(sizeof(long));

	// 命令序号
	reqMeta.lCliStampID = *(long *)pMsgBlk->rd_ptr();
	pMsgBlk->rd_ptr(sizeof(long));

	// 摄像头ID
	reqMeta.lCamID = *(long *)pMsgBlk->rd_ptr();
	pMsgBlk->rd_ptr(sizeof(long));

	static long lSvrStampID = 0;
	lSvrStampID ++;
	reqMeta.lSvrStampID = lSvrStampID; // 每次生成一个新的服务端StampID

	// 客户端请求缓冲区(这是客户端真正发送过来的缓冲区)
	char *szReqBuff = pMsgBlk->rd_ptr();
	long lReqMsgLen = pMsgBlk->length();

	// 将客户端发送的StampID替换成服务端的StampID
	char szStampID[VIDEO_CMD_STAMP_SIZE + 1];
	memset(szStampID, 0, sizeof(szStampID));
	sprintf(szStampID, "%010d", reqMeta.lSvrStampID);
	memcpy(szReqBuff + VIDEO_CMD_SIZE, szStampID, VIDEO_CMD_STAMP_SIZE);

	// 找到CamID对应得Svr信息
	CVideoServer *pCtrlSvr = VIDEO_CONFIG->GetVideoServerList()->FindServerbyID(lVideoSvrID);
	if(!pCtrlSvr)  // 进来前已经判断，不应该出现该现象
	{
		CVLog.LogErrMessage(lRet,  _("Failed to find server information by VideoServerID(%d)"), lVideoSvrID);
		return ICV_SUCCESS;
	}

	// 发送命令给远程服务端(主备IP都发)
	lRet = SendToRemoteSvr(pCtrlSvr->GetIP(), szReqBuff, lReqMsgLen);
	long lRet2 = ICV_SUCCESS;
	if(pCtrlSvr->GetIsBak())
		SendToRemoteSvr(pCtrlSvr->GetBakIP(), szReqBuff, lReqMsgLen);
	
	if(lRet2 != ICV_SUCCESS && lRet != ICV_SUCCESS)
	{
		lRet = EC_ICV_CCTV_FAILTOSWITCHCMDTOBOTHSVR;
		CVLog.LogErrMessage(lRet,  _("Transmit to remote host machine and remote backup machine failed, CamID(%d)"), reqMeta.lCamID);
		DirectRetErrCode(lCmdCode, reqMeta.lCliStampID, lRet, reqMeta.hCliQue); // 直接返回错误码给客户端		
		return lRet;
	}
	
	// 将这些请求包的信息保存起来
	m_reqMetaList.push_back(reqMeta);

	return lRet;
}

// 处理远程服务器返回的响应
// 这些响应中一定包含:命令码/命令序号信息
long CRemoteProcTask::RecvResponseFromRemoteSvr(char *szResponseBuff, long lResponseBufLen)
{
	long lRet = ICV_SUCCESS;
	if(lResponseBufLen <  RETMSG_BUFFER_MINSIZE)
		return EC_ICV_CCTV_RETMSGTOOSHORT;

	long lOffset = 0;
	long lRetCmdCode = 0;
	lRet = VIDEO_PARSER->ParseBufferToInteger(szResponseBuff, &lOffset, lResponseBufLen, VIDEO_CMD_SIZE, lRetCmdCode);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet,  _("RecvResponseFromRemoteSvr(), parse command code error"));
		return lRet;
	}

	long lStampID = 0;
	lRet = VIDEO_PARSER->ParseBufferToInteger(szResponseBuff, &lOffset, lResponseBufLen, VIDEO_CMD_STAMP_SIZE, lStampID);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet,  _("RecvResponseFromRemoteSvr(), parse StampID error"));
		return lRet;
	}
	
	CLIREQMETALIST::iterator itMeta = m_reqMetaList.begin();
	for(; itMeta != m_reqMetaList.end(); itMeta ++)
	{
		CLIREQMETA &reqMeta = *itMeta;
		if(reqMeta.lSvrStampID == lStampID && reqMeta.lRetCmdCode == lRetCmdCode)
			break;
	}

	// 如果找到了
	if(itMeta != m_reqMetaList.end())
	{
		CLIREQMETA &reqMeta = *itMeta;

		// 替换其中的StampID
		char szStampID[RETMSG_BUFFER_MINSIZE];
		memset(szStampID, 0,  sizeof(szStampID));
		sprintf(szStampID, "%010d", reqMeta.lCliStampID);
		memcpy(szResponseBuff + sizeof(long), szStampID, VIDEO_CMD_STAMP_SIZE);

		// 发送给相应的请求客户端
		lRet = CVNDK_Send(reqMeta.hCliQue, m_hLocalQue, szResponseBuff, lResponseBufLen);
		
		// 删除
		m_reqMetaList.erase(itMeta);
	}
	
	return lRet;
}

long CRemoteProcTask::SendToRemoteSvr(char *szSvrIP, char *szReqBuf, long lReqLen)
{
	long lRet = ICV_SUCCESS;

	unsigned short uPort = VIDEO_CONFIG->GetListenPort();
	HQUEUE hRemoteQueue = CVNDK_RegRemoteQueue(VIDEO_SERVICE_RECV_CLIREQUEST_QUEUEID, szSvrIP, uPort);
	lRet = CVNDK_Send(hRemoteQueue, m_hLocalQue, szReqBuf, lReqLen, SYNC_SEND);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet,  _("Send to <%s:%d> transmit informaion<length:%d> failed."), szSvrIP, uPort, lReqLen);
	}
	else
		CVLog.LogMessage(LOG_LEVEL_INFO, _("Send to <%s:%d> transmit information <length:%d> success."), szSvrIP, uPort, lReqLen);


	CVNDK_ReleaseQueue(hRemoteQueue);
	return lRet;
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
long CRemoteProcTask::InitializeNetwork()
{
	long lRet = ICV_SUCCESS;
	m_hLocalQue = CVNDK_RegLocalQueue(VIDEO_SERVICE_RECV_REMOTESVR_QUEUEID);
	if(!m_hLocalQue)
	{
		lRet = EC_ICV_CC_FAILTOREGLOCALQUE;
		CVLog.LogMessage(LOG_LEVEL_CRITICAL,_("CVNDK_RegLocalQueue(%d) Failed"), VIDEO_SERVICE_RECV_REMOTESVR_QUEUEID);
		return lRet;
	}	
	
	return lRet;
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
long CRemoteProcTask::UnInitializeNetwork()
{
	long lRet = CVNDK_ReleaseQueue(m_hLocalQue);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("CVNDK_ReleaseQueue(%d) Failed"), (long)m_hLocalQue);
	}
	return lRet;
}

// 检查请求包队列中是否有超过时间需要删除的
void CRemoteProcTask::CheckReqPackList()
{
	static long lCheckCycle = 0;
	lCheckCycle ++;

	if(lCheckCycle < CHECK_REQUEST_LIST_CYCLE)
		return;

	lCheckCycle = 0;
	time_t tmNow;
	time(&tmNow);

	for(CLIREQMETALIST::iterator itMeta = m_reqMetaList.begin(); itMeta != m_reqMetaList.end(); itMeta ++)
	{
		CLIREQMETA &reqMeta = *itMeta;
		time_t lTimeSend = reqMeta.lSendTime;
		// 到达删除的上限
		if(abs((int)(lTimeSend - tmNow)) > CHECK_REQUEST_LIST_CYCLE)
		{
			CVLog.LogMessage(LOG_LEVEL_DEBUG,  _("Delete a command information that remote server has not retrun, RetCmdCode:%d,CamID:%d,StampID:%d"), 
				reqMeta.lRetCmdCode, reqMeta.lCamID, reqMeta.lCliStampID);

			m_reqMetaList.erase(itMeta);
			itMeta --;
		}
	}
}

// 返回处理失败的结果缓冲区给客户端
long CRemoteProcTask::DirectRetErrCode(long lCmdCode, long lStampID, long lErrRetCode, HQUEUE hCliQue)
{
	long lRet = ICV_SUCCESS;
	long lRetCmdCode = lCmdCode + COMMAND_CODE_GAP_REQANDRET; // 命令对应的RetCode

	char szRetCmdBuf[VIDEO_GENERAL_RET_MAX_SIZE]; // 4K at most
	memset(szRetCmdBuf, 0, sizeof(szRetCmdBuf)); // 返回buffer的指针
	
	// 插入命令的RetCode和StampID/错误代码
	sprintf(szRetCmdBuf, "%03d%010d%010d", lRetCmdCode, lStampID, lErrRetCode);
	lRet = NET_WRAPPER->SendToClient(hCliQue, szRetCmdBuf, sizeof(szRetCmdBuf));
	CVLog.LogErrMessage(lRet,  _("DirectRetErrCode retrun error code(%s) to Client!"), szRetCmdBuf);

	return lRet;
}
