/**************************************************************
 *  Filename:    InfoMgrCmd.h
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: 消息处理头文件，主要处理在本地处理数据库.
 *
 *  @author:     chenzhan
 *  @version     04/21/2009  chenzhan  Initial Version
 *  @version     04/21/2009  chenzhan  增加摄像头分组.
 *				 08/31/2009  chenzhan  增加录像片段管理.
				 04/20/2010  chenzhan  增加视频设备级连切换
**************************************************************/
// InfoMgrCmd.h: interface for the CInfoMgrCmd class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_INFOMGRCMD_H__EFED3B14_7B3A_48A6_8B13_5D241CB67BE5__INCLUDED_)
#define AFX_INFOMGRCMD_H__EFED3B14_7B3A_48A6_8B13_5D241CB67BE5__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "VideoSvcDef.h"

class CInfoMgrCmd  
{
public:
	CInfoMgrCmd();
	virtual ~CInfoMgrCmd();

public:
	// 处理客户端发送的增删改查类信息管理请求
	long ProcessInfoMgrCmd(long lCamID, char *szCmdMsg, long lCmdMsgLen, long lCmdCode, long lStampID,
								char *szUserName, char *szGrpName, HQUEUE hCliQue);
	// 返回结果缓冲区给客户端
	long RetCmdBuffer(HQUEUE hCliQue, char *szCliBuf, long lBufSize);

// 与摄像头无关的信息管理类请求
public:
	// 获取所有的设备配置信息buffer		
	long GetAllConfigInfoBuffer(const char* pszUserName, const char* pszGroupName, 
										 const char* pInBuf, const long nInBufSize,
										 char *& pMsgBuff, long* plOutSize);
	// 获取所有分区信息buffer
    long GetAllZoneBuffer(const char* pszUserName, const char* pszGroupName, char* szRetBuf, 
							const long nRetBufSize, long* pnOutSize);
	// 获取所有产品型号信息buffer
	long GetAllProductBuffer(char* szRetBuf, const long nRetBufSize, long* pnOutSize);
	// 获取所有设备信息buffer
	long GetAllDeviceBuffer(char* szRetBuf, const long nRetBufSize, long* pnOutSize);
	// 获取所有摄像头信息buffer
	long GetAllCameraBuffer(const char* pszUserName, const char* pszGroupName, char* szRetBuf, 
							const long nRetBufSize, long* pnOutSize, bool bSimple, long lZoneID);
	// 获取所有摄像头简易信息buffer
	long GetAllSimpleCameraBuffer(const char* pszUserName, const char* pszGroupName, char* szRetBuf, 
									const long nRetBufSize, long* pnOutSize);
	// 获取某一分区所有摄像头信息buffer
	long GetAllCameraBufferOfZone(const char* pszUserName, const char* pszGroupName, const char* pInBuf, 
						const long nInBufSize, char* szRetBuf, const long nRetBufSize, long* pnOutSize, bool bSimple=false);
	// 获取某一分区所有摄像头简易信息buffer
	long GetAllSimpleCameraBufferOfZone(const char* pszUserName, const char* pszGroupName, const char* pInBuf, 
										const long nInBufSize, char* szRetBuf, const long nRetBufSize, long* pnOutSize);
	// 根据摄像头名称获取摄像头信息buffer
	long GetCameraBufferByName(const char* pszUserName, const char* pszGroupName, const char* pInBuf, 
								const long nInBufSize, char* szRetBuf, const long nRetBufSize, long* pnOutSize);
	// 获取所有的监视器信息buffer
	long GetAllMonitorBuffer(const char* pszUserName, const char* pszGroupName, char* szRetBuf, const long nRetBufSize, 
							long* pnOutSize, long lZoneID = VIDEO_ID_INVALIDATION);
	// 模式管理
	// 获取所有模式信息(模式名称和描述)buffer
	long GetAllModeNameBuffer(const char* pszUserName, const char* pszGroupName, const char* pInBuf, const long nInBufSize,
								char* szRetBuf, const long nRetBufSize, long* pnOutSize);
	// 添加模式
	long AddMode(const char* pszUserName, const char* pszGroupName, const char* pInBuf, 
					const long nInBufSize);
	// 删除模式
	long DeleteMode(const char* pszUserName, const char* pszGroupName, const char* pInBuf, 
						const long nInBufSize);
	// 更新模式
	long ModifyMode(const char* pszUserName, const char* pszGroupName, const char* pInBuf, 
						const long nInBufSize);
	// 获取模式的布局buffer
	long GetModeLayoutBuffer(const char* pszUserName, const char* pszGroupName, const char* pInBuf, 
								const long nInBufSize, char* szRetBuf, const long nRetBufSize, long* pnOutSize);

	///////////////////////////////预置位管理///////////////////////////////////////////
	// 获取预置位信息buffer
	long GetPSP(const long lCamID, const char* pszUserName, const char* pszGroupName, const char* pInBuf, 
				const long nInBufSize, char* szRetBuf, const long nRetBufSize, long* pnOutSize);
	// 删除预置位
	long DeletePSP(const long lCamID, const char* pszUserName, const char* pszGroupName, 
							const char* pInBuf, const long nInBufSize);
	// 修改预置位信息
	long ModifyPSP(const long lCamID, const char* pszUserName, const char* pszGroupName, 
							const char* pInBuf, const long nInBufSize);


	////////////////////////////////////抓拍图片管理//////////////////////////////////////
	// 获取所有抓拍类型信息buffer
	long GetAllSnapType(char* szRetBuf, const long nRetBufSize, long* pnOutSize);
	// 查询抓拍信息
	long QuerySnapInfo(const char* pszUserName, const char* pszGroupName, const char* pInBuf, 
						const long nInBufSize, char* szRetBuf, const long nRetBufSize, long* pnOutSize);
	// 获取指定抓拍图片的图片内容
	long GetSnapPic(const char* pszUserName, const char* pszGroupName, const char* pInBuf, 
						const long nInBufSize, char *&pRetBuf, long &nOutSize);
	// 下载抓拍图片
	long DownloadSnapPic(const char* pszUserName, const char* pszGroupName, const char* pInBuf, const long nInBufSize,
								char *&pRetBuf, long &nOutSize);
	// 添加抓拍图片
	long AddSnapPicture(const long lCamID, const char* pszUserName, const char* pszGroupName, 
						const char* pInBuf, const long nInBufSize, char *&pRetBuf, long &nOutSize);
	// 累加抓拍图片
	long CapturePicAddTo(const char* pszUserName, const char* pszGroupName, const char* pInBuf, const long nInBufSize);
	// 删除抓拍图片
	long DelSnapInfo(const char* pszUserName, const char* pszGroupName, const char* pInBuf, const long nInBufSize);
	// 修改抓拍信息
	long ModifySnapInfo(const long nCamID, const char* pszUserName, const char* pszGroupName, const char* pInBuf, 
								const long nInBufSize);
	// 添加录像片段信息
	long AddRecord(const char* szUserName, const char* szGroupName, const long lCamID,  
						const char* pInBuf, const long nInBufSize, char *&pRetBuf, long &nOutSize);
	// 修改录像片段信息
	long ModifyRecordInfo(const char* szUserName, const char* szGroupName, const char* pInBuf, const long nInBufSize);
	// 获取录像片段信息
	long QueryRecordInfo(const char* szUserName, const char* szGroupName, const char* pInBuf, const long nInBufSize, char *&pRetBuf, long &nOutSize);
	// 下载录像片段信息
	long DownloadRecord(const char* szUserName, const char* szGroupName, const char* pInBuf, const long nInBufSize, char *&pRetBuf, long &nOutSize);
	// 删除录像片段信息
	long DeleteRecord(const char* szUserName, const char* szGroupName, const char* pInBuf, const long nInBufSize);
	// 获取服务的冗余状态
	long GetServiceRMStauts(char* szRetBuf, const long nRetBufSize, long* pnOutSize);
	// 锁定摄像头
	long LockCamera(const long lCamID, const char* pszUserName, const char* pszGroupName, 
						const char* pInBuf, const long nInBufSize);
	// 获取配置状态是否改变信息
	long GetCfgChangeInfo(const char* pInBuf, const long nInBufSize, char* szRetBuf, const long nRetBufSize, long* pnOutSize);
	
	////////////////////////////////////自定义分区管理//////////////////////////////////////
	// 获取用户有权限看到的所有自定义分区信息
	long GetCustZone(const char* pszUserName, char* pszRetBuf, long nRetBufSize, long &lOutSize);
	// 添加自定义分组
	long AddCustZone(const char* pszUserName, const char* pszCliBuf, char* pszRetBuf, long nRetBufSize);
	// 删除自定义分区
	long DelCustZone(const char* pszUserName, const char* pszCliBuf, char* pszRetBuf, long nRetBufSize);
	// 获取用户有权限看到的某一自定义分区摄像头信息
	long GetCustZoneCam(const char* pszUserName, const char* pszCliBuf, char* pszRetBuf, long nRetBufSize, long &lOutSize);
	// 复制摄像头到自定义分区
	long CpyCustZoneCam(const char* pszUserName, const char* pszCliBuf, char* pszRetBuf, long nRetBufSize);
	// 删除自定义分区中的摄像头
	long DelCustZoneCam(const char* pszUserName, const char* pszGroupName, const char* pszCliBuf, char* pszRetBuf, long nRetBufSize);
    // 预置位相关操作
	long AddPSP(char* pszUserName, char* pszGroupName, long lCamID, char* pMsgBuf, long nBufLen);

	////////////////////////////////////获取摄像头连接的设备//////////////////////////////////////
	// 获取摄像头连接的设备-实时图像对应的设备
	long GetRealDevToCam(const long lCamID, const char* pszUserName, const char* pszGroupName, const char* pszCliBuf, const long nInBufSize, const long lStampID, 
							char* pszRetBuf, long nRetBufSize, long &lOutSize, HQUEUE hCliQue, long lCmdID);
	// 释放摄像头连接的设备-如果实时图像对应的设备是矩阵对应的编码器的话
	long FreeEncodeDevToCam(const long lCamID, const char* pszUserName, const char* pInBuf, const long nInBufSize);
	// 获取摄像头连接的设备-历史录像对应的设备
	long GetHisDevToCam(const long lCamID, const char* pszUserName, const char* pszCliBuf, char* pszRetBuf, long nRetBufSize, long &lOutSize);
	// 获取摄像头连接的设备-控制设备
	long GetCtlDevToCam(const long lCamID, const char* pszUserName, const char* pszCliBuf, char* pszRetBuf, long nRetBufSize, long &lOutSize);
	// 释放摄像头连接的监视器
	long FreeMonitorDevToCam(const long lCamID, const char* pszUserName, const char* pInBuf, const long nInBufSize);
	// 获取矩阵连接的摄像头
	long GetCameraBufferByMatrix(const char* pszUserName, const char* pszGroupName, const char* pszCliBuf, const long nInBufSize, char* szRetBuf, const long nRetBufSize, long* pnOutSize);
	// 计算视频切换的设备
public:
	long CallSwitchDriverTask(const char* pInBuf, const long nInBufSize, char *&pRetBuf, long &nOutSize, HQUEUE hCliQue);
	long CalcSwitchDev(CVideoDevice *pDevLinkCam, char* szCamChannel, CVideoDevice *pDevFromMon, char* szFromMonChannel, CReSwitchChannelList &theSwitchList);
};

#endif // !defined(AFX_INFOMGRCMD_H__EFED3B14_7B3A_48A6_8B13_5D241CB67BE5__INCLUDED_)
