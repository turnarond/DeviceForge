/**************************************************************
 *  Filename:    TimerTask.cpp
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: 定时器文件.
 *
 *  @author:     chenzhan
 *  @version     10/23/2009  chenzhan  Initial Version
				 11/02/2009  chenzhan  定时处理函数增加sleep(1)
				 11/02/2009  chenzhan  修改if (pMonLink->GetNextCam() < pMonLink->GetCamList()->GetSize()-1)因为序号的最大值为pMonLink->GetCamList()->GetSize()-1
**************************************************************/
// TimerTask.cpp: implementation of the CTimerTask class.
//
//////////////////////////////////////////////////////////////////////

#include "TimerTask.h"
#include "NetWrapper.h"
#include "VideoCfg.h"
#include "MsgDispatchTask.h"
#include "gettext/libintl.h"

#define _(STRING) gettext(STRING)
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CTimerTask::CTimerTask()
{

}

CTimerTask::~CTimerTask()
{

}

/**
 *  Start the Task and threads in the task are activated.
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     10/22/2009  chenzhan  Initial Version.
 */
int CTimerTask::StartTask()
{
	long lRet = ICV_SUCCESS;
	int nRet = (long)this->activate();
	if(nRet != 0)
	{
		lRet = EC_ICV_CC_FAILTOSTARTTASK;
		CVLog.LogErrMessage(lRet, _("StartTask() Failed."));
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
 *  @version     10/22/2009  chenzhan  Initial Version.
 */
int CTimerTask::StopTask()
{
	// 投递一个退出消息令其退出
	ACE_Message_Block *pQuitMsgBlk = NULL;
	ACE_NEW_RETURN(pQuitMsgBlk, ACE_Message_Block(sizeof(long)), 1);
	pQuitMsgBlk->msg_type(ACE_Message_Block::MB_STOP);
	this->putq(pQuitMsgBlk);

	return ICV_SUCCESS;	
}

/**
 *  main procedure of the task.
 *	get a msg from CVNDK and put it to Request Handler Queue
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     10/22/2009  chenzhan  Initial Version.
 */
int CTimerTask::svc()
{
	CVLog.LogMessage(LOG_LEVEL_INFO, _("CTimerTask::svc() begin"));

	// get a msg from CVNDK and put it to Request Handler Queue
	ACE_Time_Value tvTimeout;
	tvTimeout.msec(0);
	ACE_Message_Block *pMsgBlk = NULL;
	while (true)
	{
		// Get a client request from
		ACE_Time_Value tv = tvTimeout + ACE_OS::gettimeofday();
		int nQnum = getq(pMsgBlk, &tv);
		if (nQnum < 0)
		{
			TimerFunc();
			ACE_Time_Value tvTimeOut;
			tvTimeOut.msec(1);
			ACE_OS::sleep(tvTimeOut);
			continue;
		}
		
		CVLog.LogMessage(LOG_LEVEL_DEBUG,_("CTimerTask::svc() Q number %d!"), nQnum);
		
		// process thread stop message
		if(pMsgBlk->msg_type() == ACE_Message_Block::MB_STOP) 
		{
			pMsgBlk->release(); 
			CVLog.LogMessage(LOG_LEVEL_INFO,_("CTimerTask::svc() rcv a MB_STOP Msg, exit!"));
			break;
		}

		// 客户端队列
		HQUEUE *phCliQue = (HQUEUE *)pMsgBlk->rd_ptr();
		HQUEUE hCliQue = *phCliQue;
		pMsgBlk->rd_ptr(sizeof(HQUEUE));

		// 客户端请求缓冲区
		char *pReqBuff = pMsgBlk->rd_ptr();
		long lReqLen = (long)pMsgBlk->length();

		// 执行控制命令
		long lRet = ProcessMessage(hCliQue, pReqBuff, lReqLen);
		if(lRet != ICV_SUCCESS)
			CVLog.LogErrMessage(lRet,_("CTimerTask::svc() ProcessDriverCtrlCmd Error!"));

		// we can release msgblk, because when DispatchRequest return, handle process is over
		pMsgBlk->release();
	}

	CVLog.LogMessage(LOG_LEVEL_INFO,_("CTimerTask::svc() end"));

	return ICV_SUCCESS;
}

/**
 *  定时处理函数.
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     10/22/2009  chenzhan  Initial Version.
 */
long CTimerTask::TimerFunc()
{
	CMonitorLink* pMonLink = NULL;
	CVideoCamera* pCam = NULL;
	for (int i=0; i<m_listMonLink.GetSize(); i++)
	{
		pMonLink = m_listMonLink.GetAt(i);

		// 检测现在是否有监视器可以被切换
		ACE_Time_Value tCurr = ACE_OS::gettimeofday();
		if (tCurr.sec() < (time_t)pMonLink->GetTime())
			continue;

		// 设置要被切换的摄像头
		pCam = pMonLink->GetCamList()->GetAt(pMonLink->GetNextCam());

		// 设置下个要执行的摄像头索引
		if (pMonLink->GetNextCam() < (pMonLink->GetCamList()->GetSize()-1))
			pMonLink->SetNextCam(pMonLink->GetNextCam()+1);
		else
			pMonLink->SetNextCam(0);

		// 设置下个摄像头要执行的时间点
		time_t tvNext = (time_t)pMonLink->GetIntv() + pMonLink->GetTime();
		pMonLink->SetTime(tvNext);
	}

	// 判断是否有可以被切换的监视器和摄像头
	if (NULL == pCam)
		return EC_ICV_CCTV_CAMERANOTEXIST;
	if (NULL == pMonLink)
		return EC_ICV_CCTV_MONITORNOTEXIST;

	// 组织视频切换的命令消息
	char szSwitchBuff[VIDEO_MESSAGE_MAX_SIZE];
	memset(szSwitchBuff, 0x00, sizeof(szSwitchBuff));
	long nOffSet = 0;
	long lRet = VIDEO_PACKER->PackCmdHeader(szSwitchBuff, sizeof(szSwitchBuff), VIDEO_CMD_SWITCH_BY_NAME, 0, "SUPPER_NONEED_USER", "", nOffSet);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Fail to package message head!"));//打包消息头失败！
		return lRet;
	}
	long nOutLen = 0;
	lRet = VIDEO_PACKER->PackCmdSwitchByName(szSwitchBuff+nOffSet, sizeof(szSwitchBuff)-nOffSet, pCam->GetName(), pMonLink->GetName(), nOutLen);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Fail to package command message!"));//打包命令消息失败！
		return lRet;
	}

	// 形成转发的消息请求块
	ACE_Message_Block *pMsgBlk = new ACE_Message_Block(nOffSet+nOutLen);
	if(pMsgBlk)
	{
		pMsgBlk->copy(szSwitchBuff, nOffSet+nOutLen);
		MSGDISPATCH_TASK->putq(pMsgBlk);
	}

	return ICV_SUCCESS;
}

/**
 *  消息处理函数.
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     10/22/2009  chenzhan  Initial Version.
 */
long CTimerTask::ProcessMessage(HQUEUE hCliQue, char *pReqBuff, long lReqBufLen)
{
	if ((lReqBufLen <= 0) || (NULL == pReqBuff))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	// 解析消息包
	long nCmd = 0;
	long nStampID = 0;
	char szUserName[VIDEO_NAME_MAXSIZE];
	char szGroupName[VIDEO_NAME_MAXSIZE];
	memset(szUserName, 0x00, sizeof(szUserName));
	memset(szGroupName, 0x00, sizeof(szGroupName));
	long lOffSet = 0;
	long lRet = VIDEO_PARSER->PaseCmdHeader(pReqBuff, nCmd, nStampID, szUserName, sizeof(szUserName), szGroupName, sizeof(szGroupName), lOffSet);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Fail to parse message head!"));//解析消息头失败！
		return lRet;
	}

	switch (nCmd)
	{
	case VIDEO_CMD_CYCLE_SWITCH_BY_BUFFER:
		{
			// 解析消息内容
			char szModeBuff[VIDEO_MODE_PARA_MAXSIZE];
			memset(szModeBuff, 0x00, sizeof(szModeBuff));
			lRet = VIDEO_PARSER->ParseSubBuffer(pReqBuff, &lOffSet, szModeBuff, lReqBufLen, VIDEO_MODE_PARA_LENGTH_SIZE, sizeof(szModeBuff));
			if (ICV_SUCCESS != lRet)
			{
				CVLog.LogErrMessage(lRet, _("Fail to parse message content!"));//解析消息内容失败！
				break;
			}
			
			// 解析监视器名称
			char szMonitorName[VIDEO_NAME_MAXSIZE];
			memset(szMonitorName, 0x00, sizeof(szMonitorName));
			VIDEO_CONFIG->GetNextToken(szModeBuff, szMonitorName, sizeof(szMonitorName));

			// 解析轮询间隔
			char szTemp[VIDEO_NAME_MAXSIZE];
			memset(szTemp, 0x00, sizeof(szTemp));
			VIDEO_CONFIG->GetNextToken(szModeBuff, szTemp, sizeof(szTemp));
			unsigned long ulTimerIntv = atoi(szTemp);

			// 解析摄像头个数
			memset(szTemp, 0x00, sizeof(szTemp));
			VIDEO_CONFIG->GetNextToken(szModeBuff, szTemp, sizeof(szTemp));
			int nCamNum = atoi(szTemp);

			// 解析摄像头列表
			CVideoCameraList *plistCam = new CVideoCameraList;
			for (int i=0; i<nCamNum; i++)
			{
				CVideoCamera theCam;
				memset(szTemp, 0x00, sizeof(szTemp));
				VIDEO_CONFIG->GetNextToken(szModeBuff, szTemp, sizeof(szTemp));
				theCam.SetName(szTemp);
				CVideoCameraEx *pCamEx = NULL;
				lRet = VIDEO_CONFIG->GetCameraByCamName(szTemp, pCamEx);
				if (ICV_SUCCESS != lRet)
				{
					CVLog.LogErrMessage(lRet, _("Fail to check camera <%s> whether is not existed!"), szTemp);
					continue;
				}
				plistCam->InsertObject(theCam);
			}

			// 删除监视器对应的信息
			CMonitorLink *pLink = m_listMonLink.FindMonitorLinkbyName(szMonitorName);
			if (NULL == pLink)
			{
				// 添加一条监视器轮询记录
				CMonitorLink linkMon;
				linkMon.SetName(szMonitorName);
				linkMon.SetIntv(ulTimerIntv);
				linkMon.SetCamList(plistCam);
				linkMon.SetNextCam(0);
				ACE_Time_Value tCurr = ACE_OS::gettimeofday();
				linkMon.SetTime(tCurr.sec());
				m_listMonLink.InsertObject(linkMon);
			}
			else
				lRet = EC_ICV_CCTV_EXISTCYCLEMON;
		}
		break;
	case VIDEO_CMD_STOP_CYCLE_SWITCH:
		{
			// 解析消息内容
			char szMonitorName[VIDEO_NAME_MAXSIZE];
			memset(szMonitorName, 0x00, sizeof(szMonitorName));
			lRet = VIDEO_PARSER->ParseSubBuffer(pReqBuff, &lOffSet, szMonitorName, lReqBufLen, VIDEO_NAME_LENGTH_SIZE, sizeof(szMonitorName));
			if (ICV_SUCCESS != lRet)
			{
				CVLog.LogErrMessage(lRet, _("Fail to parse message content!"));//解析消息内容失败！
				return lRet;
			}

			// 删除监视器对应的信息
			CMonitorLink *pLink = m_listMonLink.FindMonitorLinkbyName(szMonitorName);
			if (NULL != pLink)
			{
				delete pLink->GetCamList();
				pLink->SetCamList(NULL);
				pLink->SetNextCam(0); //passing NULL to non-pointer argument 1 of ‘void CMonitorLink::SetNextCam(int)’
				int nIndex = m_listMonLink.FindMonitorLinkIndexbyName(szMonitorName);
				m_listMonLink.RemoveAt(nIndex);
			}
			else
				lRet = EC_ICV_CCTV_NOTEXISTCYCLEMON;
		}
		break;
	default:
		break;
	}

	// 打包返回客户端信息
	long nRetCode = nCmd + COMMAND_CODE_GAP_REQANDRET;
	long nOutLen = 0;
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer,0x00,VIDEO_MESSAGE_MAX_SIZE);
	lRet = VIDEO_PACKER->PackRetHeader(szBuffer, sizeof(szBuffer), nRetCode, nStampID, lRet, nOutLen);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Fail to package message head to send client!"));//程序打包向客户端发送的消息头信息失败！
		return lRet;
	}

	// 向客户端发送消息
	lRet = NET_WRAPPER->SendToClient(hCliQue, szBuffer, nOutLen);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Fail to send message to client!"));//向客户端发送消息失败！
		return lRet;
	}

	CVNDK_Free(hCliQue);

	return ICV_SUCCESS;
}
