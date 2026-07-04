// TimerTask.h: interface for the CTimerTask class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_TIMERTASK_H__2927D0BA_A33A_4E87_AF23_0403EE0FFB04__INCLUDED_)
#define AFX_TIMERTASK_H__2927D0BA_A33A_4E87_AF23_0403EE0FFB04__INCLUDED_

#include <ace/Null_Mutex.h>
#include <ace/Singleton.h>
#include <ace/Task.h>
#include <map>
#include "VideoTimeSynchCfg.h"
using namespace std;

#ifndef __stdcall
#define __stdcall
#endif

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

//初始化
typedef long    (__stdcall *DllFuncVideoInitSDK)  ();

//登陆
typedef  long   (__stdcall *DllFuncVideoLogin)  (char* pszDeviceIP, long nIPSize, long nDevicePort, 
												 char* pszExtent, long nExtentSize, long &nLoginID);
//退出
typedef  long   (__stdcall *DllFuncVideoLogout)  (long nLoginID);
//对时
typedef  long   (__stdcall *DllFuncVideoSetDeviceTime)  (long nLoginID, time_t tmDevice);

//驱动（插件定义）
typedef struct _VIDEO_DRIVER_DLL
{
	//在配置库中的编号
	long lProductID;
	//编号
	long lDeviceType;
	//dll名称
	char szDLLName[VIDEO_NAME_MAXSIZE];
	//初始化
	DllFuncVideoInitSDK pfInitSDK;
	//登陆
	DllFuncVideoLogin pfLogin;
	//退出
	DllFuncVideoLogout pfLogout;
	//对时
	DllFuncVideoSetDeviceTime pfSetDeviceTime;

	_VIDEO_DRIVER_DLL()
	{
		lProductID = -1;
		lDeviceType = -1;
		memset(szDLLName, 0, sizeof(szDLLName));
		pfInitSDK = NULL;
		pfLogin = NULL;
		pfLogout = NULL;
		pfSetDeviceTime = NULL;
	}
}VIDEO_DRIVER_DLL, *PVIDEO_DRIVER_DLL;

//设备驱动map
typedef std::map<long, VIDEO_DRIVER_DLL> VIDEODEVTYPEMAP;



class CTimerTask  : public ACE_Task<ACE_NULL_SYNCH>
{
public:
	CTimerTask();
	virtual ~CTimerTask();
public:
	// 启动线程
	long StartTask();
	// 结束线程
	long StopTask();
	// 线程处理函数
	virtual int	svc();

	//获取设备信息
	long GetTimeSynchDevInfo();

private:
	long m_lShouldExit;
	ACE_DLL	m_hDrvDll;

	CVideoDeviceList m_listDevice;
	VIDEODEVTYPEMAP m_mapVideoDevType;

	long GetAllDeviceInfo(CVideoDeviceList* pDevList, long lTimeOut);
	long GetAllDevTypeInfo(CVideoProductList* pDevTypeList, long lTimeOut);
	long SendMsgToServer(char* pMsgBuffer, long lMsgLength, char **pRecvBuf, long &lRecvLen, long lTimeOut);
	long GetDriverDLL(CVideoProduct* pProduct, VIDEO_DRIVER_DLL &driver);
	bool IsNeedTimeSynchDev(CVideoDevice* pDevice);
	long GetDevTypeByID(long lID, PVIDEO_DRIVER_DLL &pDriver);
	void ProcessTimeSynch();
	long DeviceLogout(PVIDEO_DRIVER_DLL pDriver, long lLoginID);
};

typedef ACE_Singleton<CTimerTask, ACE_Null_Mutex> CTimerTaskSingleton;
#define TIMER_TASK CTimerTaskSingleton::instance()

#endif // !defined(AFX_TIMERTASK_H__2927D0BA_A33A_4E87_AF23_0403EE0FFB04__INCLUDED_)
