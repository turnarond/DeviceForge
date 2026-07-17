/**************************************************************
 *  Filename:    TimerTask.h
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: 定时器文件.
 *
 *  @author:     chenzhan
 *  @version     10/23/2009  chenzhan  Initial Version
**************************************************************/
// TimerTask.h: interface for the CTimerTask class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_TIMERTASK_H__0C16A0F5_2B57_419F_80A7_B2A91103F127__INCLUDED_)
#define AFX_TIMERTASK_H__0C16A0F5_2B57_419F_80A7_B2A91103F127__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "VideoSvcDef.h"

class CTimerTask : public ACE_Task<ACE_NULL_SYNCH>
{
public:
	CTimerTask();
	virtual ~CTimerTask();
// Attributes
private:
	// 监视器轮询列表
	CMonitorLinkList m_listMonLink;
// Methods
private:
	// 时间到时处理函数
	long TimerFunc();
	// 消息处理函数
	long ProcessMessage(HQUEUE hCliQue, char *pReqBuff, long lReqBufLen);
public:
	// 启动线程
	int	StartTask();
	// 停止线程
	int	StopTask();
	// 线程处理函数
	virtual int	svc();
};
typedef ACE_Singleton<CTimerTask, ACE_Null_Mutex> CTimerTaskSingleton;
#define TIMER_TASK CTimerTaskSingleton::instance()

#endif // !defined(AFX_TIMERTASK_H__0C16A0F5_2B57_419F_80A7_B2A91103F127__INCLUDED_)
