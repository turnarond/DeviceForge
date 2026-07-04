/**************************************************************
 *  Filename:    ReqFetchTask.h
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: interface for the CDevCtrlTask class. This class get a CVNDK-request from CVNDK queue and put to queue of Request Handler Task
 *
 *  @author:     shijunpu
 *  @version     05/29/2008  shijunpu  Initial Version
				 04/20/2010  chenzhan  修改视频单个设备的切换为设备级连切换
**************************************************************/

#ifndef _DEVICE_CTRL_TASK_
#define _DEVICE_CTRL_TASK_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "VideoSvcDef.h"
#include <vector>

using namespace std;

// 驱动接口定义
#define VIDEO_FUNC_GET_VERSION			"VideoGetVersionNumber"
#define VIDEO_FUNC_INIT_DRIVER			"VideoInitDriver"
#define VIDEO_FUNC_EXIT_DRIVER			"VideoExitDriver"
#define VIDEO_FUNC_LOGIN_DEV			"VideoLogin"
#define VIDEO_FUNC_LOGOUT_DEV			"VideoLogout"
#define VIDEO_FUNC_LENS_CONTROL			"VideoLensControl"
#define VIDEO_FUNC_PSP_CONTROL			"VideoPresetPositionControl"
#define VIDEO_FUNC_AUX_DEV_CONTROL		"VideoAuxiliaryDeviceControl"
#define VIDEO_FUNC_PAN_CONTROL			"VideoPanControl"
#define VIDEO_FUNC_SWITCH				"VideoSwitchCameraToMonitor"
#define VIDEO_FUNC_GENERAL_CONTROL		"VideoGeneralControl"
#define VIDEO_FUNC_TIMERINTERVAL_MS		"VideoGetTimerIntvMS"
#define VIDEO_FUNC_TIMER_FUNC			"VideoTimerFunc"

typedef long (*PFN_GetVersionNumber)();
typedef long (*PFN_InitDriver)();
typedef long (*PFN_ExitDriver)();
typedef long (*PFN_Login)(char*, long, unsigned short, char*, long, long&);
typedef long (*PFN_Logout)(long);
typedef long (*PFN_LensControl)(long, long, long, long, long, long);
typedef long (*PFN_PSPControl)(long, long, long, long, long);
typedef long (*PFN_AuxiliaryDeviceControl)(long, long, long, long, long, long);
typedef long (*PFN_PanControl)(long, long, long, long, long, long);
typedef long (*PFN_SwitchCameraToMonitor)(long, long, long);
typedef long (*PFN_GeneralControl)(long, char*, long);
typedef long (*PFN_GetTimerIntvMS)(unsigned long &);
typedef long (*PFN_TimerFunc)();

class CDevCtrlTask : public ACE_Task<ACE_MT_SYNCH>  
{
friend class ACE_Singleton<CDevCtrlTask, ACE_Null_Mutex>;
UNITTEST(CDevCtrlTask);
public:
	CDevCtrlTask();
	virtual ~CDevCtrlTask();

// 驱动DLL的相关信息
private:
	char	m_szDrvDllName[ICV_LONGFILENAME_MAXLEN];	// 驱动名称
	long	m_nVersion;									// 驱动版本号
	ACE_DLL	m_hDrvDll;
	long	LoadDriverDLL();							// 加载驱动dll
	unsigned long	m_ulTimerIntvMS;					// 调用驱动定时函数的周期
	vector<CVideoDevice*> m_theDevList;					// 当前线程控制的设备列表

protected:
	// 处理驱动控制类命令分发
	long ProcessDriverCtrlCmd(char *pReqBuff, long lReqBufLen, long &nRetCmdCode, long &lStampID);
	// 预置位应用
	long PSPControl(long lCamID, CVideoDevice *pDev, char* szChannelCam, char* pMsgBuf, long nBufLen);
	// 镜头控制
	long LensControl(CVideoDevice *pDev, char* szChannelCam, char* pMsgBuf, long nBufLen);
	// 附加设备控制
	long AuxControl(CVideoDevice *pDev, char* szChannelCam, char* pMsgBuf, long nBufLen);
	// 云台控制
	long PanControl(CVideoDevice *pDev, char* szChannelCam, char* pMsgBuf, long nBufLen);
	// 通用接口
	long GeneralControl(CVideoDevice *pDev, char* pMsgBuf, long nBufLen);
	// 切换摄像头对应的监视器
	long VideoSwitchById(long nDevID, char* szInChannel, char* szOutChannel);
	// 预置位相关操作
	long AddPSP(char* pszUserName, char* pszGroupName, long lCamID, CVideoDevice *pDev, char* szChannelCam, char* pMsgBuf, long nBuflen);
	// 登录
	long VideoLogin(CVideoDevice *pDev);
	// 登出
	long VideoLogout();

protected:
	// 权限验证和缩控制
	long VerifyAndLock(long lCamID, const char *szUserName, const char *szLoginGrpName, long lLockResType);
	// 向客户端发送消息
	long RetControlCommand(HQUEUE hCliQue, long lStampID, long lRetCtrl, long lRetCmdCode);
	// 获取驱动名称
	char* GetDllName() { return m_szDrvDllName; }
public:
	// 启动线程
	int	 StartTask();
	// 结束线程
	int	StopTask();
	// 线程处理函数
	virtual int	svc();
public:
	// 设置线程对应的驱动名称
	void SetDllName(const char* szDevDllName) { CVStringHelper::Safe_StrNCpy(m_szDrvDllName, szDevDllName, sizeof(m_szDrvDllName)); }

};
typedef ACE_Singleton<CDevCtrlTask, ACE_Null_Mutex> CDevCtrlTaskSingleton;
#define DEVCTRL_TASK CDevCtrlTaskSingleton::instance()

#endif // _DEVICE_CTRL_TASK_
