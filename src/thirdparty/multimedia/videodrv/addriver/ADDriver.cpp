// ADDriver.cpp : Defines the initialization routines for the DLL.
//
#include "ADMatrixDriver.h"

CADMatrixDriver g_ADMatrixDriver;

#define VIDEO_DLL_EXPORT extern "C" __declspec(dllexport)

//获取驱动的版本号
VIDEO_DLL_EXPORT long VideoGetVersionNumber()
{
	return 5000;
}

//初始化驱动
VIDEO_DLL_EXPORT long VideoInitDriver()
{
	return g_ADMatrixDriver.InitDrive();
}

//退出驱动
VIDEO_DLL_EXPORT long VideoExitDriver()
{
	return g_ADMatrixDriver.ExitDriver();
}

//登录设备
VIDEO_DLL_EXPORT long VideoLogin(char* pszIP, long nIPSize, unsigned short nPort, char* pszExtent, long nExtentSize, long &nDevID)
{
	return g_ADMatrixDriver.Login(pszIP, nIPSize, nPort, pszExtent, nExtentSize, nDevID);
}

//退出设备
VIDEO_DLL_EXPORT long VideoLogout(long nDevID)
{
	return VIDEO_SUCCESS;
}

//镜头控制(放缩，远近，亮暗)
VIDEO_DLL_EXPORT long VideoLensControl(long nDevID, long nCtrlPort, long nChannel, long nControlCode, long nSpeed, long lElpaseTime)
{
	return g_ADMatrixDriver.LensControl(nDevID, nCtrlPort, nChannel, nControlCode, nSpeed, lElpaseTime);
}

//预置位管理(设置，调用)
VIDEO_DLL_EXPORT long VideoPresetPositionControl(long nDevID, long nCtrlPort, long nChannel, long nControlCode, long nPresetIndex)
{
	return g_ADMatrixDriver.PresetControl(nDevID, nCtrlPort, nChannel, nControlCode, nPresetIndex);
}

//附加设备控制
VIDEO_DLL_EXPORT long VideoAuxiliaryDeviceControl(long nDevID, long nCtrlPort, long nChannel, long nControlCode, long nSpeed, long lElpaseTime)
{
	return g_ADMatrixDriver.AuxControl(nDevID, nCtrlPort, nChannel, nControlCode, nSpeed); // 附加设备控制

}

//云台控制(方向：上下左右，速度：1-5)
VIDEO_DLL_EXPORT long VideoPanControl(long nDevID, long nCtrlPort, long nChannel, long nControlCode, long nSpeed, long lElpaseTime)
{
	return g_ADMatrixDriver.PanControl(nDevID, nCtrlPort, nChannel, nControlCode, nSpeed, lElpaseTime);
}

//视频切换
VIDEO_DLL_EXPORT long VideoSwitchCameraToMonitor(long nDevID, long nCamChannel, long nMonChannel)
{
	return g_ADMatrixDriver.SwitchCameratoMonitor(nDevID, nCamChannel, nMonChannel);
}

//万能操作(作为预留)
VIDEO_DLL_EXPORT long VideoGeneralControl(long nDevID, char* pszCtrlBuffer, long nSize)
{
	return EC_ICV_CCTV_NOTIMPLEMENTED;
}

//定时器调用,获取定时周期，以毫秒为单位，最小为50ms的周期，最大为500ms。缺省为每500ms进行定时器调用
VIDEO_DLL_EXPORT long VideoGetTimerIntvMS(unsigned long &lIntervalMS)
{
	lIntervalMS = 60; // 每200ms调用一次
	return VIDEO_SUCCESS;
}

//定时器调用,根据设置的定时周期（以毫秒为单位，缺省为每500ms调用一次）
VIDEO_DLL_EXPORT long VideoTimerFunc()
{
	return g_ADMatrixDriver.TimerFunc();
}
