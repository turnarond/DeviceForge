/**************************************************************
 *  Filename:    VideoTimeSynchService.cpp
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: $(视频校时服务).
 *
 *  @author:     zhucongfeng
 *  @version     08/27/2009  zhucongfeng  Initial Version
 *  @version     08/30/2009  zhucongfeng  启动服务时添加客户端注册
 *  @version     09/01/2009  zhucongfeng  启动服务时若失败，调用一次停止服务
**************************************************************/

#include "common/OS.h"
#include "common/cvGlobalHelper.h"
#include "common/CVProcessController.h"
#include "NetWrapper.h"
#include "TimerTask.h"
#include "gettext/libintl.h"

#define _(STRING) gettext(STRING)
CCVProcessController g_cvProcController("VideoTimeSynchService");

void PrintCmdTips()
{
	printf("+=========================================================+\n");
	printf(_("|  <<Welcome to use iCV video time-checking systeme >>    |\n"));//欢迎使用iCV视频校时子系统!
	printf(_("|  Input following command to set							|\n"));//可以输入如下指令进行设置
	printf(_("|  q/Q:exit												|\n"));//q/Q:退出
	printf(_("|  Other key:display tips									|\n"));//其他键:显示提示
	printf("+=========================================================+\n");
}

/**
 *  process the console command interacting with user.
 *
 *  @return $(no return).
 *
 *  @version     08/27/2009  zhucongfeng  Initial Version
 */
void InteractiveLoop()
{
	PrintCmdTips();
	bool bAlive = true;

	while(bAlive)
	{
		ACE_Time_Value tvTimeOut;
		tvTimeOut.msec(200);
		ACE_OS::sleep(tvTimeOut);

		if(g_cvProcController.CV_KbHit())
		{
			char chCmd = tolower(getchar());
			switch(chCmd)
			{
			case 'q':
				bAlive = false;
				break;
				
			default:
				PrintCmdTips();
				break;
			}
		}
		if(g_cvProcController.IsProcessQuitSignaled())
		{
			bAlive = false;
		}
	}
}

long StartService()
{
	long lRet = ICV_SUCCESS;
	char *szFuncName = "StartService()";
	char szError[VIDEO_LOG_SIZE + 1];
	memset(szError, 0, sizeof(szError));
	// 加载配置信息
	lRet = VIDEO_TIME_SYNCH_CONFIG->LoadXMLConfig();
	if(lRet != ICV_SUCCESS)
	{
		ACE_OS::snprintf(szError, sizeof(szError), _("LoadXMLConfig failed, RetCode: %d"), lRet);
		szError[VIDEO_LOG_SIZE] = '\0';
		CVLog.LogMessage(LOG_LEVEL_CRITICAL, szError);
		return lRet;
	}
	
	// 初始化网络
	lRet = NET_WRAPPER->InitializeNetwork();
	if(lRet != ICV_SUCCESS)
	{
		ACE_OS::snprintf(szError, sizeof(szError), _("Initialize Network failed, RetCode: %d"), lRet);
		szError[VIDEO_LOG_SIZE] = '\0';
		CVLog.LogMessage(LOG_LEVEL_CRITICAL, szError);
		return lRet;
	}

	lRet = NET_WRAPPER->RegisterClient();
	if(lRet != ICV_SUCCESS)
	{
		ACE_OS::snprintf(szError, sizeof(szError), _("Register client failed, RetCode: %d"), lRet);
		szError[VIDEO_LOG_SIZE] = '\0';
		CVLog.LogMessage(LOG_LEVEL_CRITICAL, szError);
		return lRet;
	}

	//启动定时器线程
	lRet = TIMER_TASK->StartTask();
	if(lRet != ICV_SUCCESS)
	{
		ACE_OS::snprintf(szError, sizeof(szError), _("Start timer task failed, RetCode: %d"), lRet);
		szError[VIDEO_LOG_SIZE] = '\0';
		CVLog.LogMessage(LOG_LEVEL_CRITICAL, szError);
		return lRet;
	}
	ACE_Time_Value tv(1);
	ACE_OS::sleep(tv);
	printf("+====================================+\n");
	printf(_("|  Start Time Synch Server Success   |\n"));
	printf("+====================================+\n");
	return lRet;
}

long StopService()
{
	long lRet = ICV_SUCCESS;

	// 停止各个执行的任务
	TIMER_TASK->StopTask();

	// 等待任务正常停止
	TIMER_TASK->wait();
	// 网络退出
	NET_WRAPPER->UnInitializeNetwork();
	return lRet;
}


/**
 *  entry point of cc service.
 *
 *  @param  -[in]  int  argc: [argument count]
 *  @param  -[in]  ACE_TCHAR*  argvs[]: [argument array]
 *  @return $(return code on exiting).
 *
 *  @version     08/27/2009  zhucongfeng  Initial Version
 */
int ACE_TMAIN(int argc, ACE_TCHAR* argvs[])
{
	long lRet = ICV_SUCCESS;
	char *szFuncName = "ACE_TMAIN()";
	char szError[VIDEO_LOG_SIZE + 1];
	memset(szError, 0, sizeof(szError));
	//只能有一个进程可以运行
	if(g_cvProcController.SetProcessAliveFlag() != ICV_SUCCESS)
	{
		return EC_ICV_CCTV_VIDEOSERVICEISRUNNING;
	}
	// 是否接收键盘输入
	g_cvProcController.SetConsoleParam(argc,argvs);

	CVLog.SetLogFileName(VIDEO_TIME_SYNCH_SERVICE_LOG_FILE_NAME);
	
	lRet = StartService();
	if(lRet != ICV_SUCCESS)
	{
		ACE_OS::snprintf(szError, sizeof(szError), _("Start Service failed,RetCode:%ld"), lRet);
		szError[VIDEO_LOG_SIZE] = '\0';
		CVLog.LogMessage(LOG_LEVEL_CRITICAL, szError);

		//停止服务
		StopService();

		return lRet;
	}
	InteractiveLoop();

	StopService();
	
	return lRet;
} 
