/**************************************************************
 *  Filename:    ReqFetchTask.cpp
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: implementation of the CDevCtrlTask class.
 *
 *  @author:     shijunpu
 *  @version     05/29/2008  shijunpu  Initial Version
				 09/28/2009  chenzhan  驱动的返回码不在服务里面处理，所有的驱动错误码以EC_ICV_CCTV_DRIVER_ERROR代替,驱动执行成功必须以0返回
				 09/29/2009  chenzhan  添加驱动的版本号判断
				 12/08/2009  chenzhan  取消获取QIP的打印日志.
				 04/20/2010  chenzhan  修改视频单个设备的切换为设备级连切换(由监视器摄像机改为矩阵的输入输出通道).
**************************************************************/
 
#include "NetWrapper.h"
#include "DevCtrlTask.h"
#include "VideoCfg.h"
#include "multimedia/video/VideoAuth.h"
#include "gettext/libintl.h"

#define _(STRING) gettext(STRING)
#define	CTRLCOMMAND_RETMSG_MAXLEN		64	// 控制命令最长返回缓冲区
#define	TIMER_FUNC_INTVL_MAX_MS			500	// 定时器调用周期最大值500ms
#define	TIMER_FUNC_INTVL_MIN_MS			50	// 定时器调用周期最小值500ms
#define	TIMER_FUNC_INTVL_DEFAULT_MS		200	// 定时器调用周期缺省值500ms
#define VIDEO_DEFAULT_VERSION			VIDEO_CURRENT_VERSION // 驱动的版本号
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CDevCtrlTask::CDevCtrlTask()
{
	memset(m_szDrvDllName, 0x00, sizeof(m_szDrvDllName));
	m_ulTimerIntvMS = TIMER_FUNC_INTVL_DEFAULT_MS;
	m_nVersion = 0;
}

CDevCtrlTask::~CDevCtrlTask()
{
	m_hDrvDll.close();
}

long CDevCtrlTask::LoadDriverDLL()
{
	// 打开驱动DLL
	CVLog.LogMessage(LOG_LEVEL_INFO, _("Begin to Load Driver DLL %s "), this->GetDllName());
	long lRet = ICV_SUCCESS;
	if(m_hDrvDll.open(m_szDrvDllName, RTLD_LAZY, 0) != ICV_SUCCESS)
	{
		lRet = EC_ICV_CCTV_LOADDLLFAILED;
		CVLog.LogErrMessage(lRet, _("Begin to Load Driver DLL %s."), GetDllName());
		return lRet;
	}

	// 加载接口
	PFN_GetVersionNumber GetVersionNumber = (PFN_GetVersionNumber)m_hDrvDll.symbol(VIDEO_FUNC_GET_VERSION);
	if (GetVersionNumber)
		m_nVersion = GetVersionNumber();

	CVLog.LogMessage(LOG_LEVEL_INFO, _("Load Driver DLL(%s) success!"), m_szDrvDllName);
	return lRet;
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
int CDevCtrlTask::StartTask()
{
	long lRet = ICV_SUCCESS;
	
	// 打开驱动DLL
	lRet = LoadDriverDLL();
	if(lRet != ICV_SUCCESS)
		return lRet;

	int nRet = (long)this->activate();
	if(nRet != 0)
	{
		lRet = EC_ICV_CC_FAILTOSTARTTASK;
		CVLog.LogErrMessage(lRet, _("StartTask(DriveDLL:%s) Failed."), GetDllName());
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
int CDevCtrlTask::StopTask()
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
 *  @version     05/23/2008  shijunpu  Initial Version.
 */
int CDevCtrlTask::svc()
{
	CVLog.LogMessage(LOG_LEVEL_INFO,_("CDevCtrlTask::svc() begin"));

	// 调用驱动初始化函数
	PFN_InitDriver InitDriver = (PFN_InitDriver)m_hDrvDll.symbol(VIDEO_FUNC_INIT_DRIVER);
	if (!InitDriver)
	{
		long lRet = EC_ICV_CCTV_DRIVER_ERROR;
		CVLog.LogErrMessage(lRet, _("Fail to get driver initialization function pointer!"));//获取驱动初始化函数指针失败！
		return lRet; 
	}
	long lRet = InitDriver();
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Fail to initialize driver!"));//初始化驱动失败！
		return EC_ICV_CCTV_DRIVER_ERROR;
	}

	// 获取驱动定时器周期
	PFN_GetTimerIntvMS GetTimerIntvMS = (PFN_GetTimerIntvMS)m_hDrvDll.symbol(VIDEO_FUNC_TIMERINTERVAL_MS);
	if(GetTimerIntvMS)
	{
		lRet = GetTimerIntvMS(m_ulTimerIntvMS);
		if (ICV_SUCCESS != lRet)
		{
			CVLog.LogErrMessage(lRet, _("Fail to get row spacing."));//获取执行间隔失败！
		}
	}
	else
	{
		lRet = EC_ICV_CCTV_DRIVER_ERROR;
		CVLog.LogErrMessage(lRet, _("Fail to get the driver timer function pointer!"));//获取驱动定时器周期函数指针失败！
		return EC_ICV_CCTV_DRIVER_ERROR; 
	}
	
	if(m_ulTimerIntvMS > TIMER_FUNC_INTVL_MAX_MS)
		m_ulTimerIntvMS = TIMER_FUNC_INTVL_MAX_MS;
	
	if(m_ulTimerIntvMS < TIMER_FUNC_INTVL_MIN_MS)
		m_ulTimerIntvMS = TIMER_FUNC_INTVL_MIN_MS;

	CVLog.LogMessage(LOG_LEVEL_INFO, _("The cycle that Driver(%s) timer function was called is %d"), m_szDrvDllName, m_ulTimerIntvMS);

	//long uTotalCycleCount = m_ulTimerIntvMS / 50;
	//long uTimeCntIdx = uTotalCycleCount;
	
	ACE_Time_Value tvTimeout;
	tvTimeout.msec((long)m_ulTimerIntvMS);
	// get a msg from CVNDK and put it to Request Handler Queue
	ACE_Message_Block *pMsgBlk = NULL;
	while (true)
	{
		// Get a client request from
		ACE_Time_Value tv = tvTimeout + ACE_OS::gettimeofday();
		int nQnum = getq(pMsgBlk, &tv);
		if (nQnum < 0)
		{
			//if(uTimeCntIdx > 0)
			PFN_TimerFunc TimerFunc = (PFN_TimerFunc)m_hDrvDll.symbol(VIDEO_FUNC_TIMER_FUNC);
			if(TimerFunc)
			{
				long lRet = TimerFunc();
				if (ICV_SUCCESS != lRet)
				{
					CVLog.LogErrMessage(lRet, _("Fail to execte driver function by time!"));//按照时间执行驱动函数失败！
					return EC_ICV_CCTV_DRIVER_ERROR;
				}
			}
			else
			{
				CVLog.LogMessage(LOG_LEVEL_ERROR, _("Fail to get the driver timing function pointer!"));//获取驱动定时执行函数指针失败！
			}

			continue;
		}

		CVLog.LogMessage(LOG_LEVEL_DEBUG,_("CDevCtrlTask::svc() Q number %d!"), nQnum);

		// CVLog.LogMessage(LOG_LEVEL_DEBUG,"CDevCtrlTask::svc() get a msgblcok!");

		// process thread stop message
		if(pMsgBlk->msg_type() == ACE_Message_Block::MB_STOP) 
		{ 
			pMsgBlk->release(); 
			CVLog.LogMessage(LOG_LEVEL_INFO,_("CDevCtrlTask::svc() rcv a MB_STOP Msg, exit!"));
			break;
		}

		// 客户端队列
		HQUEUE *phCliQue = (HQUEUE *)pMsgBlk->rd_ptr();
		HQUEUE hCliQue = *phCliQue;
		pMsgBlk->rd_ptr(sizeof(HQUEUE));
		
		// 客户端请求缓冲区
		char *pReqBuff = pMsgBlk->rd_ptr();
		long lReqLen = (long)pMsgBlk->length();
		long lRetCmdCode = VIDEO_ID_INVALIDATION;	// 控制命令RetCode
		long lStampID = VIDEO_ID_INVALIDATION;		// 控制命令序列号

		//CVLog.LogMessage(LOG_LEVEL_DEBUG,"CDevCtrlTask::svc() ProcessDriverCtrlCmd");

		// 执行控制命令
		long lRet = ProcessDriverCtrlCmd(pReqBuff, lReqLen, lRetCmdCode, lStampID);
		if(lRet != ICV_SUCCESS)
			CVLog.LogErrMessage(lRet,_("CDevCtrlTask::svc() ProcessDriverCtrlCmd Error!"));

		//CVLog.LogMessage(LOG_LEVEL_DEBUG,"CDevCtrlTask::svc() RetControlCommand");

		// 组织执行结果返回给客户端
		lRet = RetControlCommand(hCliQue, lStampID, lRet, lRetCmdCode);
		if(lRet != ICV_SUCCESS)
			CVLog.LogErrMessage(lRet,_("CDevCtrlTask::svc() RetControlCommand Error!"));

		// we can release msgblk, because when DispatchRequest return, handle process is over
		//CVLog.LogMessage(LOG_LEVEL_DEBUG,"CDevCtrlTask::svc() release msgblk");
		pMsgBlk->release();
		
		// release hCliQue
		//CVLog.LogMessage(LOG_LEVEL_DEBUG,"CDevCtrlTask::svc() free hCliQue");
		CVNDK_Free(hCliQue);
	}

	lRet = VideoLogout();
	if (ICV_SUCCESS != lRet)
		CVLog.LogErrMessage(lRet, _("VideoLogout Driver Error."));

	// 调用驱动退出函数
	PFN_ExitDriver ExitDriver = (PFN_ExitDriver)m_hDrvDll.symbol(VIDEO_FUNC_EXIT_DRIVER);
	if (!ExitDriver)
	{
		lRet = EC_ICV_CCTV_DRIVER_ERROR;
		CVLog.LogErrMessage(lRet, _("Fail to get driver exit function pointer!"));//获取驱动退出函数指针失败！
		return EC_ICV_CCTV_DRIVER_ERROR; 
	}

	lRet = ExitDriver();
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Fail to exti driver."));//退出驱动失败！
		return EC_ICV_CCTV_DRIVER_ERROR;
	}

	CVLog.LogMessage(LOG_LEVEL_INFO,_("CDevCtrlTask::svc() end"));
	return ICV_SUCCESS;
}

/**
 *  处理各类设备控制命令（发给驱动执行）.
 *  nRetCmdCode是返回的Device driver号.
 *
 *  @param  -[in]  char*  szReqBuff: [待处理的消息]
 *  @param  -[in]  long  lReqBufLen: [待处理的消息长度]
 *  @param  -[out]  long&  nRetCmdCode: [返回客户端的命令码]
 *  @param  -[out]  long&  lStampID: [返回客户端的消息戳]
 *
 *  @version     09/15/2008  shijunpu  Initial Version.
 */
long CDevCtrlTask::ProcessDriverCtrlCmd(char *szReqBuff, long lReqBufLen, long &lRetCmdCode, long &lStampID)
{
	if ((lReqBufLen <= 0) || (NULL == szReqBuff))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lOffSet = 0;
	long lCmdCode = 0;
	char szUserName[VIDEO_NAME_MAXSIZE];
	char szGroupName[VIDEO_NAME_MAXSIZE];
	memset(szUserName, 0x00, VIDEO_NAME_MAXSIZE);
	memset(szGroupName, 0x00, VIDEO_NAME_MAXSIZE);

	// 1.命令号.命令序列号.用户名.群组名
	VIDEO_CHECKFAIL_RETURN(VIDEO_PARSER->ParseRequestCmdHeaderInfo(szReqBuff, lReqBufLen, lOffSet, lCmdCode, lStampID,
					szUserName, sizeof(szUserName), szGroupName, sizeof(szGroupName)));

	// 返回命令码
	lRetCmdCode = lCmdCode + COMMAND_CODE_GAP_REQANDRET;
	
	// 4. 解析摄像头ID
	long lCamID = VIDEO_ID_INVALIDATION;
	char szCamName[VIDEO_NAME_MAXSIZE];
	memset(szCamName, 0x00, sizeof(szCamName));

	long lRet = ICV_SUCCESS;
	switch(lCmdCode) 
	{
	// 对摄像头的控制类操作，需要解析出摄像头
	case VIDEO_CMD_ADD_PSP:
	case VIDEO_CMD_APPLY_PSP:
	case VIDEO_CMD_LENS_CONTROL: // 镜头的六种控制
	case VIDEO_CMD_AUX_DEVICE_CONTROL: // 辅助设备的控制
	case VIDEO_CMD_ORIENT_CONTROL: // 云台控制
		lRet = VIDEO_PARSER->ParseBufferToInteger(szReqBuff, &lOffSet, lReqBufLen, VIDEO_CAMERA_ID_SIZE, lCamID);
		if(lRet != ICV_SUCCESS)
		{
			CVLog.LogErrMessage(lRet, _("Device driver(%s) get command, illegal parsed camera id."), m_szDrvDllName);
			return lRet;
		}
		break;
	case VIDEO_CMD_SWITCH_BY_ID: // 切换摄像头（按照摄像头ID）
		{
			// 解析设备ID
			long nDevID = 0;
			char szInChannel[VIDEO_CHANNEL_MAXSIZE];
			memset(szInChannel, 0x00, sizeof(szInChannel));
			char szOutChannel[VIDEO_CHANNEL_MAXSIZE];
			memset(szOutChannel, 0x00, sizeof(szOutChannel));

			lRet = VIDEO_PARSER->ParseBufferToInteger(szReqBuff, &lOffSet, lReqBufLen, VIDEO_DEVICE_ID_SIZE, nDevID);
			if(ICV_SUCCESS != lRet)
			{
				CVLog.LogErrMessage(lRet, _("Device driver(%s) get command, when video switch by name mode, illegal parsed device id."), m_szDrvDllName);
				return lRet;
			}
			// 解析输入通道
			lRet = VIDEO_PARSER->ParseBufferToChannel(szReqBuff, &lOffSet, lReqBufLen, szInChannel);
			if(ICV_SUCCESS != lRet)
			{
				CVLog.LogErrMessage(lRet, _("Device driver(%s) get command, when video switch by name mode, illegal parsed device in channel."), m_szDrvDllName);
				return lRet;
			}
			// 解析输出通道
			lRet = VIDEO_PARSER->ParseBufferToChannel(szReqBuff, &lOffSet, lReqBufLen, szOutChannel);
			if(ICV_SUCCESS != lRet)
			{
				CVLog.LogErrMessage(lRet, _("Device driver(%s) get command, when video switch by name mode, illegal parsed device out channel."), m_szDrvDllName);
				return lRet;
			}
			return VideoSwitchById(nDevID, szInChannel, szOutChannel);
		}
		break;

	case VIDEO_CMD_CYCLE_SWITCH_BY_NAME:
	case VIDEO_CMD_CYCLE_SWITCH_BY_BUFFER:
	case VIDEO_CMD_LOCK_MONITOR:
		lRet = EC_ICV_CCTV_NOTIMPLEMENTEDCMDCODE;
		// 本版本不实现
		break;
	default:
		lRet = EC_ICV_CCTV_UNKWNCMDCODE;
		CVLog.LogErrMessage(lRet, _("Device driver(%s) get unknown command code(%d)."), m_szDrvDllName, lCmdCode);
		return lRet;
		break;
	} // switch(nCmdCode) 

	// 5. 验证是否具有权限并自动锁定
	lRet = VerifyAndLock(lCamID, szUserName, szGroupName, LOCK_OBJECT_TYPE_CAMERA);
	if(lRet != ICV_SUCCESS)
		return lRet;
	
	// 5. 查找摄像头对应的控制设备信息
	CVideoDevice* pDev = NULL;
	char szChannel[VIDEO_CHANNEL_MAXSIZE];
	memset(szChannel, 0x00, sizeof(szChannel));

	lRet = VIDEO_CONFIG->GetCtrlDeviceByCamID(lCamID, pDev, szChannel);
	if(ICV_SUCCESS != lRet)
		return lRet;

	// 6. 登录设备
	if (0 > pDev->GetLoginID())
	{
		lRet = VideoLogin(pDev);
		if (ICV_SUCCESS != lRet)
			return lRet;
		
		m_theDevList.push_back(pDev);
	}
	
	// 缓冲区从摄像头ID后开始
	char *pszLeftBuffer = szReqBuff + lOffSet;
	long lLeftBufLen = lReqBufLen - lOffSet;
	lOffSet = 0;
	switch(lCmdCode) 
	{
		// 对摄像头的控制类操作，需要解析出摄像头
		// 预置位管理
		case VIDEO_CMD_ADD_PSP:
			return AddPSP(szUserName, szGroupName, lCamID, pDev, szChannel, pszLeftBuffer, lLeftBufLen);

		case VIDEO_CMD_APPLY_PSP:
			return PSPControl(lCamID, pDev, szChannel, pszLeftBuffer, lLeftBufLen);

		case VIDEO_CMD_LENS_CONTROL: // 镜头的六种控制
			return LensControl(pDev, szChannel, pszLeftBuffer, lLeftBufLen);

		case VIDEO_CMD_AUX_DEVICE_CONTROL: // 辅助设备的控制
			return AuxControl(pDev, szChannel, pszLeftBuffer, lLeftBufLen);
			break;
		case VIDEO_CMD_ORIENT_CONTROL: // 云台控制
			return PanControl(pDev, szChannel,  pszLeftBuffer, lLeftBufLen);
			break;
		case VIDEO_CMD_GENERAL_CONTROL:
			return GeneralControl(pDev, szReqBuff, lReqBufLen);
		default:
			break;
	}

	return lRet;
}


/**
 *  操作时验证是否具有某项权限.
 *  如果获取到权限，则自动锁定lAutoLockTime时间
 *
 *  @param  -[in,out]  long  lCamID: [comment]
 *  @param  -[in,out]  long  lLockResType: [comment]
 *
 *  @version     09/15/2008  shijunpu  Initial Version.
 */
long CDevCtrlTask::VerifyAndLock(long lCamID, const char *szUserName, const char *szLoginGrpName, long lLockResType)
{
	long lRet = ICV_SUCCESS;
	CVideoCameraEx *pCamEx = NULL;
	lRet = VIDEO_CONFIG->GetCameraByCamID(lCamID, pCamEx);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("User(%s)of group(%s), want to get camemra(%d) infomation failed."), 
					szUserName, szLoginGrpName, lCamID);
		return lRet;
	}

	lRet = AcquireLock((char *)szUserName, (char *)szLoginGrpName, lCamID, LOCK_OBJECT_TYPE_CAMERA, pCamEx->GetAuthLabel());
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("User(%s)of group(%s) has no right to control camera(%d)."), 
					szUserName, szLoginGrpName, lCamID);
	}

	return lRet;
}

/**
 *  登录设备
 *
 *  @param  -[in]  CVideoDevice*  pDev: [连接的控制设备(往往是矩阵)]
 *
 *  @version     09/15/2008  shijunpu  Initial Version.
 */
long CDevCtrlTask::VideoLogin(CVideoDevice *pDev)
{
	long lRet = ICV_SUCCESS;

	long lLoginID = VIDEO_ID_INVALIDATION;
	// 调用驱动切换
	PFN_Login Login = (PFN_Login)m_hDrvDll.symbol(VIDEO_FUNC_LOGIN_DEV);
	if(Login)
	{
		CVLog.LogMessage(LOG_LEVEL_DEBUG, _("VideoSwitch  control device(%s), Login IP:%s,PORT:%d,EXTENT:%s"), 
				pDev->GetName(), pDev->GetIP(), pDev->GetPort(), pDev->GetExtent());
		lRet = Login(pDev->GetIP(), strlen(pDev->GetIP()), pDev->GetPort(), pDev->GetExtent(), strlen(pDev->GetExtent()), lLoginID);
		CVLog.LogMessage(LOG_LEVEL_DEBUG, _("VideoSwitch  control device(%s), RetCode:%d"), pDev->GetName(), lRet);
	}
	else
	{
		lRet = EC_ICV_CCTV_DRIVER_ERROR;
		CVLog.LogErrMessage(lRet, _("Fail to get device loging in fucntion pointer."));//获取设备登录函数指针失败！
	}

	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Fail to log in device."));//登陆驱动失败！
		return EC_ICV_CCTV_DRIVER_ERROR;
	}

	pDev->SetLoginID(lLoginID);

	return lRet;
}

/**
 *  登出设备
 *
 *
 *  @version     09/15/2008  shijunpu  Initial Version.
 */
long CDevCtrlTask::VideoLogout()
{
	long lRet = ICV_SUCCESS;
	
	for (vector<CVideoDevice*>::iterator it = m_theDevList.begin(); it != m_theDevList.end(); it++)
	{
		// 调用驱动切换
		PFN_Logout Logout = (PFN_Logout)m_hDrvDll.symbol(VIDEO_FUNC_LOGOUT_DEV);
		if(Logout)
		{
			CVLog.LogMessage(LOG_LEVEL_DEBUG, _("VideoSwitch  control device(%s), Logout DeviceID:%d,size:%d"), 
				(*it)->GetName(), (*it)->GetLoginID(), m_theDevList.size());
			lRet = Logout((*it)->GetLoginID());
			CVLog.LogMessage(LOG_LEVEL_DEBUG, _("VideoSwitch  control device(%s), RetCode:%d"), (*it)->GetName(), lRet);
		}
		else
		{
			lRet = EC_ICV_CCTV_DRIVER_ERROR;
			CVLog.LogErrMessage(lRet, _("Fail to get device loging out fucntion pointer."));//获取设备登出函数指针失败！
		}
	}

	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Fail to log out device."));//登出驱动失败！
		return EC_ICV_CCTV_DRIVER_ERROR;
	}

	return lRet;
}

/**
 *  切换摄像头对应的监视器
 *
 *  @param  -[in]  long nDevID: [矩阵ID]
 *  @param  -[in]  long nInChannel: [矩阵输入通道]
 *  @param  -[in]  long nOutChannel: [矩阵输出通道]
 *
 *  @version     09/15/2008  shijunpu  Initial Version.
 */
long CDevCtrlTask::VideoSwitchById(long nDevID, char* szInChannel, char* szOutChannel)
{
	long lRet = ICV_SUCCESS;

	// 查找摄像头对应的控制设备信息
	CVideoDevice* pDev = VIDEO_CONFIG->GetDeviceList()->FindDevicebyID(nDevID);
	if(!pDev)
		return EC_ICV_CCTV_DEVNOTEXIST;

	// 登录设备
	if (0 > pDev->GetLoginID())
	{
		lRet = VideoLogin(pDev);
		if (ICV_SUCCESS != lRet)
			return lRet;
		
		m_theDevList.push_back(pDev);
	}

	// 调用驱动切换
	PFN_SwitchCameraToMonitor Switch = (PFN_SwitchCameraToMonitor)m_hDrvDll.symbol( VIDEO_FUNC_SWITCH);
	if(Switch)
	{
		//字符串转为整型
		long lInChannel = std::atoi(szInChannel);
		long lOutChannel = std::atoi(szOutChannel);

		CVLog.LogMessage(LOG_LEVEL_DEBUG, _("VideoSwitch  control device(%s),CameraLinkChannel:%d,MonitorLinkChannel:%d"), 
				pDev->GetName(), lInChannel, lOutChannel);

		lRet = Switch(pDev->GetLoginID(), lInChannel, lOutChannel);
		CVLog.LogMessage(LOG_LEVEL_DEBUG, _("VideoSwitch  control device(%s), RetCode:%d"), pDev->GetName(), lRet);
	}
	else
	{
		lRet = EC_ICV_CCTV_DRIVER_ERROR;
		CVLog.LogErrMessage(lRet, _("Fail to get device switching function pointer!"));//获取设备切换函数指针失败！
	}
	
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Fail to switch video!"));//视频切换失败！
		return EC_ICV_CCTV_DRIVER_ERROR;
	}

	return lRet;
}

/**
 *  镜头的控制动作
 *
 *  @param  -[in]  CVideoDevice*  pDev: [连接的控制设备(往往是矩阵)]
 *  @param  -[in]  long lChannelCam: [连接的控制设备对应的控制通道号]
 *  @param  -[in]  char*  pMsgBuf: [请求的其他信息]
 *  @param  -[in]  long  nBufLen: [请求信息的长度]
 *
 *  @version     09/15/2008  shijunpu  Initial Version.
 */
long CDevCtrlTask::LensControl(CVideoDevice *pDev, char* szChannelCam, char* pMsgBuf, long nBufLen)
{
	long lRet = ICV_SUCCESS;
	long lOffset = 0;

	long lControlCode = VIDEO_ID_INVALIDATION; 
	long lControlType = VIDEO_ID_INVALIDATION; 
	long lSpeed = VIDEO_ID_INVALIDATION;
	long lElapseTime = 0; // 持续时间（毫秒为单位）
	
	lRet = VIDEO_PARSER->ParseBufferToInteger(pMsgBuf, &lOffset, nBufLen, VIDEO_CMD_LENS_SIZE, lControlCode);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("LensControl() control device(%s), error to parse command code."), pDev->GetName());
		return lRet;
	}

	lRet = VIDEO_PARSER->ParseBufferToInteger(pMsgBuf, &lOffset, nBufLen, VIDEO_CMD_LENS_SIZE, lControlType);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("LensControl() control device(%s), error to parse control type."), pDev->GetName());
		return lRet;
	}

	// parse speed
	VIDEO_PARSER->ParseBufferToInteger(pMsgBuf, &lOffset, nBufLen, VIDEO_CMD_LENS_SIZE, lSpeed);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("LensControl() control device(%s), error to parse speed."), pDev->GetName());
		return lRet;
	}

	// parse 持续时间
	VIDEO_PARSER->ParseBufferToInteger(pMsgBuf, &lOffset, nBufLen, VIDEO_ELAPSECTRLTIME_LENGTH_SIZE, lElapseTime);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("LensControl() control device(%s), error to parse elapse time"), pDev->GetName());
		return lRet;
	}

	long nCtrlPort = pDev->GetCtrlPort();

	// 调用驱动控制镜头
	PFN_LensControl LensControl = (PFN_LensControl)m_hDrvDll.symbol(VIDEO_FUNC_LENS_CONTROL);
	if(LensControl)
	{
		//字符串转化为整数
		long lChannelCam = std::atoi(szChannelCam);

		CVLog.LogMessage(LOG_LEVEL_DEBUG, _("LensControl  control device(%s), CtrlPort:%d,lControlCode:%d,lSpeed:%d,lElapseTime:%d"), 
						pDev->GetName(), nCtrlPort, lControlCode, lSpeed, lElapseTime);

		lRet = LensControl(pDev->GetLoginID(), nCtrlPort, lChannelCam, lControlCode, lSpeed, lElapseTime);
		CVLog.LogMessage(LOG_LEVEL_DEBUG, _("LensControl  control device(%s), RetCode:%d"), pDev->GetName(), lRet);
	}
	else
	{
		lRet = EC_ICV_CCTV_DRIVER_ERROR;
		CVLog.LogErrMessage(lRet, _("Fail to get device control lens function pointer!"));//获取设备控制镜头函数指针失败！
	}

	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Fail to control lens for driver!"));//
		return EC_ICV_CCTV_DRIVER_ERROR;
	}
	
	return lRet;
}

/**
 *  云台控制动作
 *
 *  @param  -[in]  CVideoDevice*  pDev: [连接的控制设备(往往是矩阵)]
 *  @param  -[in]  long lChannelCam: [连接的控制设备对应的控制通道号]
 *  @param  -[in]  char*  pMsgBuf: [请求的其他信息]
 *  @param  -[in]  long  nBufLen: [请求信息的长度]
 *
 *  @version     09/15/2008  shijunpu  Initial Version.
 */
long CDevCtrlTask::PanControl(CVideoDevice *pDev, char* szChannelCam, char* pMsgBuf, long nBufLen)
{
	long lRet = ICV_SUCCESS;
	long lOffset = 0;

	long lControlCode = VIDEO_ID_INVALIDATION; 
	long lControlType = VIDEO_ID_INVALIDATION; 
	long lSpeed = VIDEO_ID_INVALIDATION;
	
	lRet = VIDEO_PARSER->ParseBufferToInteger(pMsgBuf, &lOffset, nBufLen, VIDEO_CMD_LENS_SIZE, lControlCode);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("PanControl() control device(%s), error to parse command code."), pDev->GetName());
		return lRet;
	}

	// 0停止控制动作，1开始控制动作
	VIDEO_PARSER->ParseBufferToInteger(pMsgBuf, &lOffset, nBufLen, VIDEO_CMD_LENS_SIZE, lControlType);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("PanControl() control device(%s), error to parse control type."), pDev->GetName());
		return lRet;
	}

	// parse speed
	VIDEO_PARSER->ParseBufferToInteger(pMsgBuf, &lOffset, nBufLen, VIDEO_CMD_LENS_SIZE, lSpeed);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("PanControl() control device(%s), error to parse speed."), pDev->GetName());
		return lRet;
	}

	// parse 持续时间
	long lElpaseTime = 0;
	VIDEO_PARSER->ParseBufferToInteger(pMsgBuf, &lOffset, nBufLen, VIDEO_CMD_LENS_SIZE, lElpaseTime);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("PanControl() control device(%s), error to parse elapse time."), pDev->GetName());
		return lRet;
	}

	long nCtrlPort = pDev->GetCtrlPort();

	// 调用驱动控制云台
	PFN_PanControl PanControl = (PFN_PanControl)m_hDrvDll.symbol(VIDEO_FUNC_PAN_CONTROL);
	if(PanControl)
	{
		//ChannelCam转为整型
		long lChannelCam = std::atoi(szChannelCam);

		CVLog.LogMessage(LOG_LEVEL_DEBUG, _("PanControl  control device(%s), CtrlPort:%d,lControlCode:%d,lSpeed:%d"), 
						pDev->GetName(), nCtrlPort, lControlCode, lSpeed);
		lRet = PanControl(pDev->GetLoginID(), nCtrlPort, lChannelCam, lControlCode, lSpeed, lElpaseTime);
		CVLog.LogMessage(LOG_LEVEL_DEBUG, _("PanControl  control device(%s), RetCode:%d"), pDev->GetName(), lRet);
	}
	else
	{
		lRet = EC_ICV_CCTV_DRIVER_ERROR;
		CVLog.LogErrMessage(lRet, _("Fail to get device control pan function pointer!"));//获取设备控制云台函数指针失败！
	}

	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Fail to control pan for driver!"));//云台控制失败！
		return EC_ICV_CCTV_DRIVER_ERROR;
	}

	return lRet;
}

/**
 *  预置位相关操作.
 *  包括增/删/改/应用预置位四种处理.
 *
 *  @param  -[in]  char* pszUserName: [用户名]
 *  @param  -[in]  char* pszGroupName: [用户组]
 *  @param  -[in]  CVideoDevice*  pDev: [连接的控制设备(往往是矩阵)]
 *  @param  -[in]  long lChannelCam: [连接的控制设备对应的控制通道号]
 *  @param  -[in]  char*  pMsgBuf: [请求的其他信息]
 *  @param  -[in]  long  nBufLen: [请求信息的长度]
 *
 *  @version     09/15/2008  shijunpu  Initial Version.
 */
long CDevCtrlTask::AddPSP(char* pszUserName, char* pszGroupName, long lCamID, CVideoDevice *pDev, char* szChannelCam, char* pMsgBuf, long nBufLen)
{
	long lRet = ICV_SUCCESS;
	long lOffset = 0;

	// 得到预置位信息
	char szPsPName[VIDEO_NAME_MAXSIZE];
	memset(szPsPName, 0, sizeof(szPsPName));
	lRet = VIDEO_PARSER->ParseSubBuffer(pMsgBuf, &lOffset, szPsPName, nBufLen, VIDEO_NAME_LENGTH_SIZE, VIDEO_CAMERA_ID_SIZE);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("AddPSP  control device(%s), get error information of psp."), pDev->GetName());
		return lRet;
	}

	// 得到预置位描述
	char szPsPDesc[VIDEO_DESCRIPTION_MAXSIZE];
	memset(szPsPDesc, 0, sizeof(szPsPDesc));
	lRet = VIDEO_PARSER->ParseSubBuffer(pMsgBuf, &lOffset, szPsPDesc, nBufLen, VIDEO_DESP_LENGTH_SIZE, sizeof(szPsPDesc));
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("AddPSP  control device(%s), get error description of psp."), pDev->GetName());
		return lRet;
	}

	// 解析预置位索引
	long nPresetIndex = 0;
	lRet = VIDEO_PARSER->ParseBufferToInteger(pMsgBuf, &lOffset, nBufLen, VIDEO_PSP_INDEX_SIZE, nPresetIndex);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("AddPSP  control device(%s), get error index of psp."), pDev->GetName());
		return lRet;
	}

	long nCtrlPort = pDev->GetCtrlPort();

	// 调用驱动添加预置位
	PFN_PSPControl PSPControl = (PFN_PSPControl)m_hDrvDll.symbol(VIDEO_FUNC_PSP_CONTROL);
	if(PSPControl)
	{
		//ChannelCam转为整型
		long lChannelCam = std::atoi(szChannelCam);

		CVLog.LogMessage(LOG_LEVEL_DEBUG, _("AddPSP  control device(%s), CtrlPort:%d, lChannelCam:%d, PSPNo:%d"), 
						pDev->GetName(), nCtrlPort, lChannelCam, nPresetIndex);

		lRet = PSPControl(pDev->GetLoginID(), nCtrlPort, lChannelCam, VIDEO_CTRL_PSP_ADD, nPresetIndex);
		if (ICV_SUCCESS != lRet)
		{
			CVLog.LogErrMessage(lRet, _("Fail to control preset!"));//预置位控制失败！
			return EC_ICV_CCTV_DRIVER_ERROR;
		}
		
		// 添加
		CVideoPSP psp(szPsPName, nPresetIndex, szPsPDesc);
		lRet = VIDEO_CONFIG->AddPSPOfCameraByUserInfo(lCamID, &psp, pszUserName, pszGroupName);
		if(lRet != ICV_SUCCESS)
		{
			CVLog.LogErrMessage(lRet, _("Failed to add psp information to database."));
			return lRet;
		}

		CVLog.LogMessage(LOG_LEVEL_DEBUG, _("AddPSP  control device(%s), RetCode:%d"), pDev->GetName(), lRet);
	}
	else
	{
		lRet = EC_ICV_CCTV_DRIVER_ERROR;
		CVLog.LogErrMessage(lRet, _("Fail to get setting preset function pointer!"));//获取设置预置位函数指针失败！
	}

	return lRet;
}

/**
 *  预置位相关操作.
 *  包括增/删/改/应用预置位四种处理.
 *
 *  @param  -[in]  CVideoDevice*  pDev: [连接的控制设备(往往是矩阵)]
 *  @param  -[in]  long lChannelCam: [连接的控制设备对应的控制通道号]
 *  @param  -[in]  char*  pMsgBuf: [请求的其他信息]
 *  @param  -[in]  long  nBufLen: [请求信息的长度]
 *
 *  @version     09/15/2008  shijunpu  Initial Version.
 */
long CDevCtrlTask::PSPControl(long lCamID, CVideoDevice *pDev, char* szChannelCam, char* pMsgBuf, long nBufLen)
{
	long lRet = ICV_SUCCESS;
	long lOffset = 0;

	// 得到预置位信息
	char szPsPName[VIDEO_NAME_MAXSIZE];
	memset(szPsPName, 0, sizeof(szPsPName));
	lRet = VIDEO_PARSER->ParseSubBuffer(pMsgBuf, &lOffset, szPsPName, nBufLen, VIDEO_NAME_LENGTH_SIZE, VIDEO_CAMERA_ID_SIZE);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("PSPControl  control device(%s), error to get psp information."), pDev->GetName());
		return lRet;
	}

	CVideoPSP thePsp;
	lRet = VIDEO_CONFIG->GetPSPInfoByNameAndCamID(&thePsp, szPsPName, lCamID);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("PSPControl  control device(%s), failed to find psp information by name(%s) and camera id(%d)."), 
							pDev->GetName(), szPsPName, lCamID);
		return lRet;
	}

	long nCtrlPort = pDev->GetCtrlPort();

	// 调用驱动设置预置位
	PFN_PSPControl PSPControl = (PFN_PSPControl)m_hDrvDll.symbol(VIDEO_FUNC_PSP_CONTROL);
	if(PSPControl)
	{
		//ChannelCam转为整型
		long lChannelCam = std::atoi(szChannelCam);

		CVLog.LogMessage(LOG_LEVEL_DEBUG, _("PSPControl  control device(%s), CtrlPort:%d,lChannelCam:%d, PSPNo:%d"), 
						pDev->GetName(), nCtrlPort, lChannelCam, thePsp.GetPresetIndex());

		lRet = PSPControl(pDev->GetLoginID(), nCtrlPort, lChannelCam, VIDEO_CTRL_PSP_APPLY, thePsp.GetPresetIndex());
		CVLog.LogMessage(LOG_LEVEL_DEBUG, _("PSPControl  control device(%s), RetCode:%d"), pDev->GetName(), lRet);
	}
	else
	{
		lRet = EC_ICV_CCTV_DRIVER_ERROR;
		CVLog.LogErrMessage(lRet, _("Fail to get setting preset function pointer!"));//获取设置预置位函数指针失败！
	}

	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Fail to control preset for driver!"));//驱动控制预置位失败！
		return EC_ICV_CCTV_DRIVER_ERROR;
	}

	return lRet;
}

/**
 *  辅助设备控制
 *
 *  @param  -[in]  CVideoDevice*  pDev: [连接的控制设备(往往是矩阵)]
 *  @param  -[in]  long lChannelCam: [连接的控制设备对应的控制通道号]
 *  @param  -[in]  char*  pMsgBuf: [请求的其他信息]
 *  @param  -[in]  long  nBufLen: [请求信息的长度]
 *
 *  @version     09/15/2008  shijunpu  Initial Version.
 */
long CDevCtrlTask::AuxControl(CVideoDevice *pDev, char* szChannelCam, char* pMsgBuf, long nBufLen)
{
	long lRet = ICV_SUCCESS;
	long lControlCode = VIDEO_ID_INVALIDATION; 
	long lControlType = VIDEO_ID_INVALIDATION; 
	long lSpeed = VIDEO_ID_INVALIDATION;
	long lElapseTime = 0;
	long lOffset = 0;

	lRet = VIDEO_PARSER->ParseBufferToInteger(pMsgBuf, &lOffset, nBufLen, VIDEO_CMD_LENS_SIZE, lControlCode);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("AuxControl() control device(%s), failed to parse command code."), pDev->GetName());
		return lRet;
	}

	VIDEO_PARSER->ParseBufferToInteger(pMsgBuf, &lOffset, nBufLen, VIDEO_CMD_LENS_SIZE, lControlType);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("AuxControl() control device(%s), failed to parse control type."), pDev->GetName());
		return lRet;
	}

	// parse speed
	VIDEO_PARSER->ParseBufferToInteger(pMsgBuf, &lOffset, nBufLen, VIDEO_CMD_LENS_SIZE, lSpeed);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("AuxControl() control device(%s), failed to parse speed."), pDev->GetName());
		return lRet;
	}

	// parse 持续时间
	VIDEO_PARSER->ParseBufferToInteger(pMsgBuf, &lOffset, nBufLen, VIDEO_ELAPSECTRLTIME_LENGTH_SIZE, lElapseTime);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("AuxControl() control device(%s), failed to parse elapse time."), pDev->GetName());
		return lRet;
	}

	long nCtrlPort = pDev->GetCtrlPort();

	// 调用驱动控制附加设备
	PFN_AuxiliaryDeviceControl AuxControl = (PFN_AuxiliaryDeviceControl)m_hDrvDll.symbol(VIDEO_FUNC_AUX_DEV_CONTROL);
	if(AuxControl)
	{
		long lChannelCam = std::atoi(szChannelCam);

		CVLog.LogMessage(LOG_LEVEL_DEBUG, _("AuxControl  control device(%s), CtrlPort:%d,lControlCode:%d,lSpeed:%d,lElapseTime:%d"), 
						pDev->GetName(), nCtrlPort, lControlCode, lSpeed, lElapseTime);

		lRet = AuxControl(pDev->GetLoginID(), nCtrlPort, lChannelCam, lControlCode, lSpeed, lElapseTime);
		CVLog.LogMessage(LOG_LEVEL_DEBUG, _("AuxControl  control device(%s), RetCode:%d"), 
						pDev->GetName(), lRet);
	}
	else
	{
		lRet = EC_ICV_CCTV_DRIVER_ERROR;
		CVLog.LogErrMessage(lRet, _("Fail to get device control additional device function pointer!"));//获取设备控制附加设备函数指针失败！
	}

	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Fail to control additional device."));//控制附加设备失败！
		return EC_ICV_CCTV_DRIVER_ERROR;
	}

	return lRet;
}

/**
 *  通用类控制动作
 *
 *  @param  -[in]  CVideoDevice*  pDev: [连接的控制设备(往往是矩阵)]
 *  @param  -[in]  char*  pMsgBuf: [请求的其他信息]
 *  @param  -[in]  long  nBufLen: [请求信息的长度]
 *
 *  @version     09/15/2008  shijunpu  Initial Version.
 */
long CDevCtrlTask::GeneralControl(CVideoDevice *pDev, char* pMsgBuf, long nBufLen)
{
	long lRet = ICV_SUCCESS;

	PFN_GeneralControl GeneralControl = (PFN_GeneralControl)m_hDrvDll.symbol(VIDEO_FUNC_GENERAL_CONTROL);
	if (GeneralControl)
	{
		CVLog.LogMessage(LOG_LEVEL_DEBUG, _("GeneralControl  control device(%s), MsgBuf:%s"), pDev->GetName(), pMsgBuf);
		lRet = GeneralControl(pDev->GetLoginID(), pMsgBuf, nBufLen);
		CVLog.LogMessage(LOG_LEVEL_DEBUG, _("AuxControl  control device(%s)"), pDev->GetName());
	}
	else
	{
		lRet = EC_ICV_CCTV_DRIVER_ERROR;
		CVLog.LogErrMessage(lRet, _("Fail to get device conmmon control action function pointer!"));//获取设备通用类控制动作函数指针失败！
	}

	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Fail to common cotrol!"));//通用控制失败！
		return EC_ICV_CCTV_DRIVER_ERROR;
	}

	return lRet;
}

/**
 *  返回控制命令结果给客户端(请求方).
 *
 *  @param  -[in]  HQUEUE  hCliQue: [客户端队列]
 *  @param  -[in]  long  lRetCtrl: [返回的控制命令结果]
 *  @param  -[in]  long  lStampID: [客户端请求的StampID(序列号)]
 *  @param  -[in]  long  lRetCmdCode: [返回的控制序列号]
 *
 *  @version     09/15/2008  shijunpu  Initial Version.
 */
long CDevCtrlTask::RetControlCommand(HQUEUE hCliQue, long lStampID, long lRetCtrl, long lRetCmdCode)
{
	if (NULL == hCliQue)
		return ICV_SUCCESS;

	char szRetMsg[CTRLCOMMAND_RETMSG_MAXLEN];
	memset(szRetMsg, 0, sizeof(szRetMsg));
	sprintf(szRetMsg, "%03d%010d%010d", (int)lRetCmdCode, (int)lStampID, (int)lRetCtrl);
	long lRet = NET_WRAPPER->SendToClient(hCliQue, szRetMsg, strlen(szRetMsg));
	CVLog.LogMessage(LOG_LEVEL_DEBUG, _("DriverTaskThread(%s) send to client return information(%s), RetCode: %d"), m_szDrvDllName, szRetMsg, lRet);
	
	return lRet;
}
