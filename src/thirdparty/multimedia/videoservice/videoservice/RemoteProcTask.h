/**************************************************************
 *  Filename:    ReqFetchTask.h
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: interface for the CRemoteProcTask class. This class get a CVNDK-request from CVNDK queue and put to queue of Request Handler Task
 *
 *  @author:     shijunpu
 *  @version     05/29/2008  shijunpu  Initial Version
**************************************************************/

#ifndef _REMOTE_TASK__
#define _REMOTE_TASK__

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "VideoSvcDef.h"

using namespace std;

typedef struct tagCLIREQMETA
{
	HQUEUE hCliQue;
	long lRetCmdCode;
	long lCamID;
	long lSvrStampID;	// 服务端生成的命令序号, 为了保证返回的号都是对的
	long lCliStampID;	// 客户端请求时发送的命令序号
	time_t lSendTime;
	tagCLIREQMETA()
	{
		memset(this, 0, sizeof(this));
		time(&lSendTime);
	}

	tagCLIREQMETA(const tagCLIREQMETA &x)
	{
		hCliQue = x.hCliQue;
		lRetCmdCode = x.lRetCmdCode;
		lSvrStampID = x.lSvrStampID;
		lCliStampID = x.lCliStampID;
		lCamID = x.lCamID;
		lSendTime = x.lSendTime;
	}
}CLIREQMETA;

typedef list<CLIREQMETA> CLIREQMETALIST;

class CRemoteProcTask : public ACE_Task<ACE_MT_SYNCH>  
{
friend class ACE_Singleton<CRemoteProcTask, ACE_Null_Mutex>;
UNITTEST(CRemoteProcTask);
public:
	CRemoteProcTask();
	virtual ~CRemoteProcTask();
public:
	CLIREQMETALIST m_reqMetaList;
	
public:
	long	CallRemoteService(HQUEUE hCliQueID, long lCamID, char *szReqBuff, long lReqBufLen);	// 调用远程服务
	long	SendRequestToRemoteSvr(ACE_Message_Block *pMsgBlk); // 发送请求给远程服务器
	long	RecvResponseFromRemoteSvr(char *szReqBuff, long lReqBufLen); // 处理远程服务器返回的请求

public:
	int		StartTask();
	int		StopTask();
	virtual int		svc();

protected:
	long	SendToRemoteSvr(char *szSvrIP, char *szReqBuf, long lReqLen);
	long	InitializeNetwork();
	long	UnInitializeNetwork();
	void	CheckReqPackList();	// 检查请求包队列中是否有超过时间需要删除的

	// 返回处理失败的结果缓冲区给客户端
	long	DirectRetErrCode(long lCmdCode, long lStampID, long lErrRetCode, HQUEUE hCliQue);

	HQUEUE	m_hLocalQue;

};
typedef ACE_Singleton<CRemoteProcTask, ACE_Null_Mutex> CRemoteProcTaskSingleton;
#define REMOTE_TASK CRemoteProcTaskSingleton::instance()

#endif // _REQUESTFETCH_TASK_EVENT_REQEST_HANDLER_
