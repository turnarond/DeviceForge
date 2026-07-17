/**************************************************************
 *  Filename:    VideoService.cpp
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description:  Defines the entry point for the console application.
 *
 *  @author:     shijunpu
 *  @version     05/23/2008  shijunpu  Initial Version
				 10/23/2009  chenzhan  增加定时器
**************************************************************/

#include "VideoCfg.h"
#include "NetWrapper.h"
#include "MsgDispatchTask.h"
#include "RemoteProcTask.h"
#include "DevCtrlTask.h"
#include "RemoteProcTask.h"
#include "TimerTask.h"
#include "common/LicChecker.h"
#include "common/ServiceBase.h"

#include "common/cvGlobalHelper.h"
#include "common/cvRedisAccess.h"
#include "common/cvRedisDefine.h"
#include "gettext/libintl.h"

#define _(STRING) gettext(STRING)
#define		SLEEPSECONDS_BEFORE_UNCOMMONEXIT		2	// wait seconds before exceptional exit
#define		VIDEOSERVICE_RUNNING_FLAG					ACE_TEXT("VIDEOSREVICE_RUNNING_FLAG")

#define ICV_CC_EVENTBUFF_MAXLEN			1024
void CVLogEvent(const char *szOperator, HQUEUE hQueue, const char *szComment, const char *szFormat, ...)
{
	char szbuf[ICV_CC_EVENTBUFF_MAXLEN];
	memset(szbuf, 0x00, ICV_CC_EVENTBUFF_MAXLEN);
	//定义变量指针
	va_list	ap;   
	//初始化ap
	va_start(ap, szFormat);   
	int len = vsprintf(szbuf, szFormat, ap);
	va_end(ap);
	
	if(szComment)
		strcat(szbuf, szComment);
	
	TAlm_EventMsg event;
	strcpy(event.szAppName, "VideoService");
	strcpy(event.szEventLoginName, VIDEO_STRING(szOperator));
	strcpy(event.szEventNodeName, "");
	
	CVStringHelper::Safe_StrNCpy(event.szMsg, szbuf, sizeof(event.szMsg));
	
	long lSendEventRet = ICV_SUCCESS;
	if(lSendEventRet != ICV_SUCCESS)
		CVLog.LogErrMessage(lSendEventRet, _("Send To Event Failed."));
}

long g_lSvcState = SERVICE_EVENT_START;
long g_lLastStatus = -1;	
long g_lCurStatus = RM_STATUS_UNAVALIBLE;

STATERWINSTANCE g_pStateRW;

class CVideoService : public CServiceBase
{
public:
	CVideoService();
	virtual ~CVideoService(){}

	virtual long Init(int argc, char* args[]);
	virtual long Start();
	virtual long Fini();

	virtual void PrintStartUpScreen();
	virtual void Misc();
	//设置日志文件名称，保证打印ServiceBase里的日志到期望的文件中
	virtual void SetLogFileName()
	{
		CVLog.SetLogFileName(VIDEO_LOG_SERVICE_FILE_NAME);
	}
	//记录事件的虚函数，默认什么事都不做
	virtual long RecordEvent(const char* szEventMsg)
	{
		CVLogEvent("", 0, "", szEventMsg);
		return ICV_SUCCESS;
	}
};

CServiceBase* g_pServiceHandler = new CVideoService;

CVideoService::CVideoService() : CServiceBase("VideoService", true, "VideoSupport")
{

}

void CVideoService::PrintStartUpScreen()
{
	printf("+==========================================+\n");
	printf(_("|  <<Welcome to use iCV video system >>   |\n"));//欢迎使用iCV视频子系统! 
	printf(_("|  Input following command to set			|\n"));//可以输入如下指令进行设置
	printf(_("|  q/Q:exit								|\n"));//q/Q:退出 
	printf(_("|  Other key:display tips					|\n"));//其他键:显示提示 
	printf("+==========================================+\n");
}

long CVideoService::Init(int argc, char* args[])
{
	PrintStartUpScreen();

	struct timeval timeout = { 1, 500000 }; // 1.5 seconds
	g_pStateRW = NULL;
	STATERW_HostInitialize("",0,timeout,&g_pStateRW);

	ACE_Time_Value timeValue = ACE_OS::gettimeofday();
	TCV_TimeStamp timeStamp;
	timeStamp.tv_sec = (uint32_t)timeValue.sec();
	timeStamp.tv_usec = (uint32_t)timeValue.usec();
	char szTime[ICV_HOSTNAMESTRING_MAXLEN] = {'\0'};
	cvcommon::CastTimeToASCII(szTime, ICV_HOSTNAMESTRING_MAXLEN, timeStamp);
	// 初始记为正常启动
	char szStatus[ICV_HOSTNAMESTRING_MAXLEN];
	memset(szStatus, 0x00, ICV_HOSTNAMESTRING_MAXLEN);
	ACE_OS::snprintf(szStatus, sizeof(szStatus), "0;%s", szTime);
	STATERW_SetStringCommand(&g_pStateRW, CV_STATUS_VIDEOSVR_STATUS, szStatus);

	// 启动时刻
	STATERW_SetStringCommand(&g_pStateRW, CV_STATUS_VIDEOSVR_STARTTIME, szTime);

	long lRet = GetRMStatus(&g_lCurStatus);
	if(lRet != ICV_SUCCESS)
	{
		g_lCurStatus = RM_STATUS_UNAVALIBLE;

		memset(szStatus, 0x00, ICV_HOSTNAMESTRING_MAXLEN);
		ACE_OS::snprintf(szStatus, sizeof(szStatus), "%d;%s", lRet,szTime);
		STATERW_SetStringCommand(&g_pStateRW, CV_STATUS_VIDEOSVR_STATUS, szStatus);
	}

	g_lLastStatus = g_lCurStatus;

	// 本机活动状态日志
	switch (g_lCurStatus)
	{
	case RM_STATUS_ACTIVE:
		CVLog.LogMessage(LOG_LEVEL_INFO, _("Start VideoServer, Current Status: Active status"));
		break;
	case RM_STATUS_INACTIVE:
		CVLog.LogMessage(LOG_LEVEL_INFO, _("Start VideoServer, Current Status: Inactive status"));
		break;
	case RM_STATUS_UNAVALIBLE:
		CVLog.LogMessage(LOG_LEVEL_INFO, _("Start VideoServer, Current Status: Unknown status"));
		break;
	default:
		CVLog.LogMessage(LOG_LEVEL_INFO, _("Start VideoServer, Current Status: Error status"));
		break;
	}

	return ICV_SUCCESS;
}

long CVideoService::Start()
{
	// 发出登录成功的事件
	CVLogEvent("", 0, "", "VideoService Started");
	
	// 如果当前冗余状态是非活动机器, 不需启用任何线程
	if(g_lCurStatus == RM_STATUS_INACTIVE)
	{
		CVLog.LogMessage(LOG_LEVEL_INFO, _("Redundant state is non active, not start a thread."));//冗余状态为非活动, 不启动相关线程
		return ICV_SUCCESS;
	}

	ACE_Time_Value timeValue = ACE_OS::gettimeofday();
	TCV_TimeStamp timeStamp;
	timeStamp.tv_sec = (uint32_t)timeValue.sec();
	timeStamp.tv_usec = (uint32_t)timeValue.usec();
	char szTime[ICV_HOSTNAMESTRING_MAXLEN] = {'\0'};
	cvcommon::CastTimeToASCII(szTime, ICV_HOSTNAMESTRING_MAXLEN, timeStamp);
	
	// 加载配置信息
	long lRet = VIDEO_CONFIG->LoadAllConfig();
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("LoadAllConfig failed."));

		char szStatus[ICV_HOSTNAMESTRING_MAXLEN];
		memset(szStatus, 0x00, ICV_HOSTNAMESTRING_MAXLEN);
		ACE_OS::snprintf(szStatus, sizeof(szStatus), "%d;%s", lRet,szTime);
		STATERW_SetStringCommand(&g_pStateRW, CV_STATUS_VIDEOSVR_STATUS, szStatus);
		//return lRet;
	}
	
	// 初始化网络
	lRet = NET_WRAPPER->InitializeNetwork();
	if (lRet != ICV_SUCCESS)
	{
		char szStatus[ICV_HOSTNAMESTRING_MAXLEN];
		memset(szStatus, 0x00, ICV_HOSTNAMESTRING_MAXLEN);
		ACE_OS::snprintf(szStatus, sizeof(szStatus), "%d;%s", lRet,szTime);
		STATERW_SetStringCommand(&g_pStateRW, CV_STATUS_VIDEOSVR_STATUS, szStatus);
	}

	// 启动必需的任务
	MSGDISPATCH_TASK->StartTask();	// 消息分发任务, 在其内部启动设备控制任务
	REMOTE_TASK->StartTask();		// 远程服务执行任务
	TIMER_TASK->StartTask();		// 定时器执行任务
	
	ACE_Time_Value tv(1);
	ACE_OS::sleep(tv);

	printf("+====================================+\n");
	printf(_("|      Start VideoServer Success     |\n"));
	printf("+====================================+\n");
	
	return ICV_SUCCESS;
}

long CVideoService::Fini()
{
	CVLog.LogMessage(LOG_LEVEL_INFO, _("Stop VideoServer, current status: %ld"), g_lCurStatus);
	
	// 记录退出事件
	CVLogEvent("", 0, "", _("VideoService Stopped"));
	
	// 如果之前冗余状态是非活动机器, 不需停止启用任何线程
	if(g_lLastStatus == RM_STATUS_INACTIVE)
	{	
		return ICV_SUCCESS;
	}
	
	// 停止各个执行的任务
	MSGDISPATCH_TASK->StopTask(); // 消息分发任务
	REMOTE_TASK->StopTask(); //远程服务执行任务
	TIMER_TASK->StopTask(); // 定时器执行任务
	
	// 等待任务正常停止
	
	MSGDISPATCH_TASK->wait(); // 消息分发任务
	REMOTE_TASK->wait(); //远程服务执行任务
	TIMER_TASK->wait(); // 定时器执行任务
	
	// 网络退出
	NET_WRAPPER->UnInitializeNetwork();
	
	VIDEO_CONFIG->UnLoadAllConfig();

	if (g_pStateRW)
	{
		STATERW_HostUnInitialize(g_pStateRW);
		g_pStateRW = NULL;
	}
	
	return ICV_SUCCESS;
}

void CVideoService::Misc()
{
	long lRet = GetRMStatus(&g_lCurStatus);
	if(lRet != ICV_SUCCESS)
		g_lCurStatus = RM_STATUS_UNAVALIBLE;
	
	// 如果冗余状态发生改变, 则需要服务重新启动一次
	if(g_lCurStatus != g_lLastStatus)
	{
		if(g_lLastStatus != -1) // 第一次不做该操作
		{
			CVLog.LogMessage(LOG_LEVEL_INFO, _("VideoService redundant state from %ld to %ld, restarting ..."), //VideoService 冗余状态由%ld切换为%ld, restarting ...
				g_lLastStatus, g_lCurStatus);
			Fini(); 
			ACE_OS::sleep(2);
		}
		
		lRet = Start();
		if(lRet != ICV_SUCCESS)
		{
			CVLog.LogMessage(LOG_LEVEL_ERROR, _("VideoService redundant state changed, but fail to try restarting!"));//VideoService 冗余状态发生改变，但尝试重启失败!
			ACE_OS::sleep(SLEEPSECONDS_BEFORE_UNCOMMONEXIT);
			return;
		}
		
		g_lLastStatus = g_lCurStatus;
		g_lSvcState = SERVICE_EVENT_NONE;
		return;
	}
	
	switch(g_lSvcState)
	{
	case SERVICE_EVENT_NONE:
		break;

	case SERVICE_EVENT_RESTART:
		CVLog.LogMessage(LOG_LEVEL_ERROR, _("VideoService received restart command, restarting..."));//VideoService 收到重启命令，restarting...
		Fini();
		ACE_OS::sleep(2);
		lRet = Start();
		if(lRet != ICV_SUCCESS)
		{
			CVLog.LogMessage(LOG_LEVEL_ERROR, _("VideoService received restart command, but fail to try restarting!"));//VideoService 收到重启命令，但尝试重启失败!
			ACE_OS::sleep(SLEEPSECONDS_BEFORE_UNCOMMONEXIT);
			return;
		}
		break;

	case SERVICE_EVENT_STOP:
		CVLog.LogMessage(LOG_LEVEL_INFO, _("VideoService received stop command, Exiting..."));//VideoService 收到停止命令，Exiting...
		Fini();
		return;
		break;
	}
	
	g_lSvcState = SERVICE_EVENT_NONE;
} 
