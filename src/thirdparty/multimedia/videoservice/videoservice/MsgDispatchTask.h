/**************************************************************
 *  Filename:    ReqFetchTask.h
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: interface for the CMsgDispatchTask class. This class get a CVNDK-request from CVNDK queue and put to queue of Request Handler Task
 *
 *  @author:     shijunpu
 *  @version     05/29/2008  shijunpu  Initial Version
**************************************************************/

#ifndef _MESSAGE_DISPATCH_TASK_
#define _MESSAGE_DISPATCH_TASK_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "VideoSvcDef.h"
#include "InfoMgrCmd.h"

using namespace std;

class CMsgDispatchTask : public ACE_Task<ACE_NULL_SYNCH>  
{
friend class ACE_Singleton<CMsgDispatchTask, ACE_Null_Mutex>;
UNITTEST(CMsgDispatchTask);
public:
	CMsgDispatchTask();
	virtual ~CMsgDispatchTask();
public:
	CInfoMgrCmd	m_infoMgrCmd;	// 信息管理类命令

protected:
	CDriverTaskList	m_drvTaskList;

	// 返回处理失败的结果缓冲区给客户端
	long RecvNetMessage();
	long DirectRetErrCode(long lCmdCode, long lStampID, long lErrRetCode, HQUEUE hCliQue);

public:
	long StartAllDriverTask();	// 开启各个设备产品型号的驱动的任务
	long StopAllDriverTask();	// 停止各个设备产品型号的驱动的任务
	long DispatchCmdMessage(char *szCmdMsg, long lCmdMsgLen, HQUEUE hCliQue = NULL);	// 分发各种控制命令
	long StartTask();
	long StopTask();
	virtual int svc();

};
typedef ACE_Singleton<CMsgDispatchTask, ACE_Null_Mutex> CMsgDispatchTaskSingleton;
#define MSGDISPATCH_TASK CMsgDispatchTaskSingleton::instance()

#endif // _MESSAGE_DISPATCH_TASK_
