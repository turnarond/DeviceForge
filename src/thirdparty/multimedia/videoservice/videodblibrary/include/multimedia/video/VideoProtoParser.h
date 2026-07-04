/**************************************************************
 *  Filename:    VideoProtocolParser.h
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: 视频监控系统协议解析库头文件.
 *
 *  @author:     xulizai
 *  @version     2008/05/16  xulizai  Initial Version
 *  @version     2009/04/17  chenzhan  添加摄像头自定义分区和排序的解析函数
 *  @version     2009/06/01  zhucongfeng 添加获取图片内容的解析函数
 *  @version     2009/06/17	 chenzhan	服务端协议打包函数组织
 			     09/08/2009  chenzhan     添加录像抓拍、图片累加功能
				 14/04/2010  chenzhan  增加设备级连控制服务端返回给客户端的编解码器列表
**************************************************************/
#if !defined(AFX_VIDEOPROTOCOLPARSER_H__9BBBBD8B_CA7C_45FF_A14C_23441BD658EC__INCLUDED_)
#define AFX_VIDEOPROTOCOLPARSER_H__9BBBBD8B_CA7C_45FF_A14C_23441BD658EC__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "VideoStructDef.h"

class CVideoProtoParser  
{
public:
	CVideoProtoParser();
	virtual ~CVideoProtoParser();
// 解析为缓冲区类的方法
public:
	/**
	 *  发送消息头(需求用例ID:113179,113180,113181)
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] long &nCmd:									命令码
	 *  @param  -[out] long &nStampID:								消息戳
	 *  @param  -[out] char* pszUserName:							用户名
	 *  @param  -[in] long nNameBufLen:								用户名缓冲区长度
	 *  @param  -[out] char* pszGroup:								群组名
	 *  @param  -[in] long nGroupBufLen:							群组名缓冲区长度
	 *  @param  -[out] long &nOutLen:								已经解析信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PaseCmdHeader(char* pszBuff, long &nCmd, long &nStampID, char* pszUserName, long nNameBufLen, char* pszGroup, long nGroupBufLen, long &nOutLen);
	/**
	 *  返回消息头(需求用例ID:113179,113180,113181)
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] long &nCmd:									命令码
	 *  @param  -[out] long &nStampID:								消息戳
	 *  @param  -[out] long &nOutLen:								已经解析信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PaseRetHeader(char* pszBuff, long &nCmd, long &nStampID, long &nRetCode, long &nOutLen);
	/**
	 *  返回获取所有的设备配置信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] CVideoProductList *plistProduct:				设备产品型号列表
	 *  @param  -[out] CVideoDeviceList *plistDev:					设备列表
	 *  @param  -[out] CVideoZoneList *plistZone:					固定分区列表
	 *  @param  -[out] CVideoCameraList *plistCam:					摄像头列表
	 *  @param  -[out] CVideoMonitorList *plistMon:					监视器列表
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseRetGetAllConfigInfo(char* pszBuff, CVideoProductList *plistProduct, CVideoDeviceList *plistDev, 
		CVideoZoneList *plistZone, CVideoCameraList *plistCam, CVideoMonitorList *plistMon);
	/**
	 *  返回获取所有的分区信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] CVideoZoneList* plistZone:					分区列表
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseRetGetZoneInfo(char* pszBuff, CVideoZoneList* plistZone);
	/**
	 *  返回获取所有的设备产品型号信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] CVideoProductList* plistProduct:				设备产品型号列表
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseRetGetProductInfo(char* pszBuff, CVideoProductList* plistProduct);
	/**
	 *  返回获取所有的视频设备信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] CVideoDeviceList* plistDev:					设备列表
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseRetGetDevInfo(char* pszBuff, CVideoDeviceList* plistDev);
	/**
	 *  返回获取所有的摄像头信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] CVideoCameraList* plistCam:					摄像头列表
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseRetGetCamInfo(char* pszBuff, CVideoCameraList* plistCam);
	/**
	 *  返回获取所有的摄像头简易信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] CVideoCameraList* plistCam:					摄像头列表
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseRetGetSimpleCamInfo(char* pszBuff, CVideoCameraList* plistCam);
	/**
	 *  发送获取某一分区的摄像头信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] long &nZoneID:								分区ID
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdGetZoneCam(char* pszBuff, long &nZoneID);
	/**
	 *  返回获取某一分区的摄像头信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] CVideoCameraList* plistCam:					摄像头列表
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseRetGetZoneCam(char* pszBuff, CVideoCameraList* plistCam);
	/**
	 *  发送获取某一分区的摄像头简易信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] long &nZoneID:								分区ID
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdGetZoneSimpleCam(char* pszBuff, long &nZoneID);
	/**
	 *  返回获取某一分区的摄像头简易信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] CVideoCameraList* plistCam:					摄像头列表
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseRetGetZoneSimpleCam(char* pszBuff, CVideoCameraList* plistCam);
	/**
	 *  发送根据摄像头名称获取摄像头信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] char* pszCamName:							摄像头名称
	 *  @param  -[in] long nCamNameLen:								摄像头名称缓冲区长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdGetCamByName(char* pszBuff, char* pszCamName, long nCamNameLen);
	/**
	 *  返回根据摄像头名称获取摄像头信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] CVideoCamera* ptheCam:					    摄像头信息
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseRetGetCamByName(char* pszBuff, CVideoCamera* ptheCam);
	/**
	 *  返回获取所有的监视器信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] CVideoMonitorList* plistMon:					监视器列表
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseRetGetMonInfo(char* pszBuff, CVideoMonitorList* plistMon);
	/**
	 *  发送获取某一分区的监视器信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] long &nZoneID:								分区ID
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdGetZoneMon(char* pszBuff, long &nZoneID);
	/**
	 *  返回获取某一分区的监视器信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] CVideoMonitorList* plistMon:					监视器列表
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseRetGetZoneMon(char* pszBuff, CVideoMonitorList* plistMon);
	/**
	 *  发送获取所有的模式名称
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] long &nTime:									信息更新时间
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdGetAllModeName(char* pszBuff, long &nTime);
	/**
	 *  返回获取所有的模式名称
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] long &nTime:									服务本次更新时间
	 *  @param  -[out] CVideoModeList* plistMode:					模式列表
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseRetGetAllModeName(char* pszBuff, long &nTime, CVideoModeList* plistMode);
	/**
	 *  发送增加模式
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] CVideoMode* ptheMode:						模式信息
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdAddMode(char* pszBuff, CVideoMode* ptheMode);
	/**
	 *  发送删除模式
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] CVideoModeList* plistMode:					模式列表
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdDelMode(char* pszBuff, CVideoModeList* plistMode);
	/**
	 *  返回删除模式
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] CVideoModeList* plistMode:					模式列表
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseRetDelMode(char* pszBuff, CVideoModeList* plistMode);
	/**
	 *  发送修改模式
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] char *pszModeName:							旧模式名称
	 *  @param  -[in] long nModeNameLen:							旧模式名称缓冲区长度
	 *  @param  -[out] CVideoMode* ptheMode:						新模式信息
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdModifyMode(char* pszBuff, char *pszModeName, long nModeNameLen, CVideoMode* ptheMode);
	/**
	 *  发送获取模式的布局信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] char *pszModeName:							模式名称
	 *  @param  -[in] long nModeNameLen:							模式名称缓冲区长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdGetModeLayout(char* pszBuff, char *pszModeName, long nModeNameLen);
	/**
	 *  返回获取模式的布局信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] CVideoMode* ptheMode:						模式信息
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseRetGetModeLayout(char* pszBuff, CVideoMode* ptheMode);
	/**
	 *  发送获取摄像头的预置位信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] long &nCamID:								摄像头ID
	 *  @param  -[out] long &nTime:									信息更新时间
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdGetCamPSPInfo(char* pszBuff, long &nCamID, long &nTime);
	/**
	 *  返回获取摄像头的预置位信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] long &nCamID:								摄像头ID
	 *  @param  -[out] long &nTime:									服务本次更新时间
	 *  @param  -[out] CVideoPSPList* plistPSP:						预置位列表
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseRetGetCamPSPInfo(char* pszBuff, long &nCamID, long &nTime, CVideoPSPList* plistPSP);
	/**
	 *  发送增加预置位
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] long &nCamID:								摄像头ID
	 *  @param  -[out] long &nTime:									信息更新时间
	 *  @param  -[out] CVideoPSP *pthePSP:							预置位信息
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdAddPSP(char* pszBuff, long &nCamID, long &nTime, CVideoPSP *pthePSP);
	/**
	 *  发送删除预置位
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] long &nCamID:								摄像头ID
	 *  @param  -[out] CVideoPSPList *plistPSP:						预置位列表
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	//2012.5.6 zhucongfeng 无使用，而且是错误的，打包与解包不一致。目前打包和解包直接写在VideoRAPI和VideoService中了
	//long ParseCmdDelPSP(char* pszBuff, long &nCamID, CVideoPSPList *plistPSP);
	/**
	 *  返回删除预置位
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] CVideoList<CStrName> *plistCStrName:			预置位列表
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	//2012.5.6 zhucongfeng 无使用，而且是错误的，打包与解包不一致。目前打包和解包直接写在VideoRAPI和VideoService中了
	//long ParseRetDelPSP(char* pszBuff, CVideoList<CStrName> *plistCStrName);
	/**
	 *  发送修改预置位
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] long &nCamID:								摄像头ID
	 *  @param  -[out] char *pszPSPName:							旧预置位名称
	 *  @param  -[in] long nPSPNameLen:								旧预置位名称缓冲区长度
	 *  @param  -[out] CVideoPSP *pthePSP:							新预置位信息
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdModifyPSP(char* pszBuff, long &nCamID, char *pszPSPName, long nPSPNameLen, CVideoPSP *pthePSP);
	/**
	 *  发送应用预置位
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] long &nCamID:								摄像头ID
	 *  @param  -[out] char *pszPSPName:							预置位名称
	 *  @param  -[in] long nPSPNameLen:								预置位名称缓冲区长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdAppPSP(char* pszBuff, long &nCamID, char *pszPSPName, long nPSPNameLen);
	/**
	 *  返回获取抓拍图片的信息类型
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] CVideoList<CStrName> *plistCStrName:			抓拍类型列表
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseRetGetSnapType(char* pszBuff, CVideoList<CStrName> *plistCStrName);
	/**
	 *  发送抓拍查询信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] long &nCamID:								摄像头ID
	 *  @param  -[out] long &nStart:								开始时间
	 *  @param  -[out] long &nEnd:									结束时间
	 *  @param  -[out] char *pszSnapType:							抓拍类型
	 *  @param  -[in] long nSnapTypeLen:							抓拍类型缓冲区长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdQuerySnapInfo(char* pszBuff, long &nCamID, long &nStart, long &nEnd, char *pszSnapType, long nSnapTypeLen);
	/**
	 *  返回抓拍查询信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] CVideoSnapFileList *plistSnapInfo:			抓拍信息列表
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseRetQuerySnapInfo(char* pszBuff, CVideoSnapFileList *plistSnapInfo);
	/**
	 *  发送获取指定抓拍图片的图片内容(返回时图片内容需额外添加)
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] long &nSnapID:								抓拍ID
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdGetPicBuffer(char* pszBuff, long &nSnapID);
	/**
	 *  发送增加抓拍图片(图片内容需额外添加)
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] CVideoSnapFile *ptheSnapInfo:				抓拍信息
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdAddPicInfo(char* pszBuff, CVideoSnapFile *ptheSnapInfo);
	/**
	 *  返回增加抓拍图片
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] long &nSnapID:								抓拍ID
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseRetAddPicInfo(char* pszBuff, long &nSnapID);
	/**
	 *  发送删除抓拍图片信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] CVideoSnapFileList *plistSnapInfo:			抓拍信息列表
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdDelSnapInfo(char* pszBuff, CVideoSnapFileList *plistSnapInfo);
	/**
	 *  发送修改抓拍图片信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] long &nSnapID:								抓拍ID
	 *  @param  -[out] CVideoSnapFileList *plistSnapInfo:			抓拍信息列表
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdModifySnapInfo(char* pszBuff, long &nSnapID, CVideoSnapFile *ptheSnapInfo);
	/**
	 *  发送视频切换--逻辑ID
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] long &nCamID:								摄像头ID
	 *  @param  -[out] long &nMonID:								监视器ID
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdSwitchByID(char* pszBuff, long &nCamID, long &nMonID);
	/**
	 *  发送视频切换--名称
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] char *pszCamName:							摄像头名称
	 *  @param  -[in] long nCamNameLen:								摄像头名称缓冲区长度
	 *  @param  -[out] char *pszMonName:							监视器名称
	 *  @param  -[in] long nMonNameLen:								监视器名称缓冲区长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdSwitchByName(char* pszBuff, char *pszCamName, long nCamNameLen, char *pszMonName, long nMonNameLen);
	/**
	 *  发送镜头控制
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] long &nCamID:								摄像头ID
	 *  @param  -[out] long &nCtrCode:								控制码(1-6)占1位
	 *  @param  -[out] long &nSpeed:								速度(0-5)占1位
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdLensControl(char* pszBuff, long &nCamID, long &nCtrCode, long &nSpeed);
	/**
	 *  发送附加设备控制
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] long &nCamID:								摄像头ID
	 *  @param  -[out] long &nCtrCode:								控制码(1-2)占1位
	 *  @param  -[out] long &nSpeed:								速度(0-5)占1位
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdAuxControl(char* pszBuff, long &nCamID, long &nCtrCode, long &nSpeed);
	/**
	 *  发送云台控制
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] long &nCamID:								摄像头ID
	 *  @param  -[out] long &nCtrCode:								控制码(1-4)占1位
	 *  @param  -[out] long &nSpeed:								速度(0-5)占1位
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdPanControl(char* pszBuff, long &nCamID, long &nCtrCode, long &nSpeed);
	/**
	 *  返回获取自定义分区(需求用例ID:113179,113180,113181)
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] CVideoCustomZoneList* plistCustZone:			自定义分区列表
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PaseRetGetCustZone(char* pszBuff, CVideoCustomZoneList* plistCustZone);
	/**
	 *  发送添加自定义分区(需求用例ID:113179,113180,113181)
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] char* pszCustZoneName:						自定义分区名称
	 *  @param  -[in] long nNameBufLen:								自定义分区名称缓冲区长度
	 *  @param  -[out] char* pszCustZoneDesc:						自定义分区描述
	 *  @param  -[in] long nDescBufLen:								自定义分区描述缓冲区长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PaseCmdAddCustZone(char* pszBuff, char* pszCustZoneName, long nNameBufLen, char* pszCustZoneDesc, long nDescBufLen);
	/**
	 *  发送删除自定义分区(需求用例ID:113179,113180,113181)
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] char* pszCustZoneName:						自定义分区名称
	 *  @param  -[in] long nNameBufLen:								自定义分区名称缓冲区长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PaseCmdDelCustZone(char* pszBuff, char* pszCustZoneName, long nNameBufLen);
	/**
	 *  发送获取自定义分区摄像头(需求用例ID:113179,113180,113181)
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] char* pszCustZoneName:						自定义分区名称
	 *  @param  -[in] long nNameBufLen:								自定义分区名称缓冲区长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PaseCmdGetCustZoneCam(char* pszBuff, char* pszCustZoneName, long nNameBufLen);
	/**
	 *  返回获取自定义分区摄像头(需求用例ID:113179,113180,113181)
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] CVideoCameraList *plistCustZoneCam:			摄像头名称列表
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PaseRetGetCustZoneCam(char* pszBuff, CVideoCameraList *plistCustZoneCam);
	/**
	 *  发送复制自定义分区摄像头(需求用例ID:113179,113180,113181)
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] char* pszCustZoneName:						自定义分区名称(摄像头拷贝进此自定义分区)
	 *  @param  -[in] long nNameBufLen:								自定义分区名称缓冲区长度
	 *  @param  -[out] CVideoList<CStrName> *plistCustZoneCamName:	摄像头名称列表
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PaseCmdCpyCustZoneCam(char* pszBuff, char* pszCustZoneName, long nNameBufLen, CVideoList<CStrName> *plistCustZoneCamName);
	/**
	 *  发送删除自定义分区摄像头(需求用例ID:113179,113180,113181)
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] char* pszCustZoneName:						自定义分区名称(只删除此自定义分区摄像头)
	 *  @param  -[in] long nNameBufLen:								自定义分区名称缓冲区长度
	 *  @param  -[out] CVideoList<CStrName> *plistCustZoneCamName:	摄像头名称列表
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PaseCmdDelCustZoneCam(char* pszBuff, char* pszCustZoneName, long nNameBufLen, CVideoList<CStrName> *plistCustZoneCamName);
	/**
	 *  发送添加录像片段信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] CRecord *pRec:								录像信息
	 *  @param  -[in] char*& pRecordBuffer:							录像内容
	 *  @param  -[out] long &nRecSize:								录像内容长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdAddRecord(char* pszBuff, CRecord *pRec, char*& pRecordBuffer, long &nRecSize);
	/**
	 *  返回添加录像片段信息(返回ID)
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] long &nRecordID:								录像ID
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseRetAddRecord(char* pszBuff, long &nRecordID);
	/**
	 *  发送添加录像片段信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] long &nCamID:								摄像头ID
	 *  @param  -[out] long &nStartTime:							开始时间
	 *  @param  -[out] long &nEndTime:								终止时间
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdQueryRecord(char* pszBuff, long &nCamID, long &nStartTime, long &nEndTime);
	/**
	 *  返回查询录像片段信息(返回录像信息)
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] CRecordList* pListRecord:					录像信息
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseRetQueryRecord(char* pszBuff, CRecordList* pListRecord);
	/**
	 *  发送修改录像片段信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] CRecord* pRecord:							录像信息
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdModifyRecord(char* pszBuff, CRecord* pRecord);
	/**
	 *  返回下载录像片段信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] char* pszFileName:							文件名称
	 *  @param  -[out] long &nFileSize:								文件大小
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseRetDownloadRecord(char* pszBuff, char* pszFileName, long &nFileSize);
	/**
	 *  发送删除抓拍图片信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] long nCamID:									摄像机ID
	 *  @param  -[out] long nMonID:									监视器ID
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdVideoSwitch(char* pszBuff, long &nCamID, long &nMonID);
	/**
	 *  返回查询录像信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] CReSwitchChannelList* pListSwitch:			视频切换信息列表
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseRetVideoSwitch(char* pszBuff, CReSwitchChannelList* pListSwitch);
	/**
	 *  发送获取监视器信息
	 *
	 *  @param  -[in] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[out] char *pszMonitorName:						监视器名称
	 *  @param  -[in] long nMonitorNameLen:							监视器名称缓冲区长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long ParseCmdGetMonitorName(char* pszBuff, char *pszMonitorName, long nMonitorNameLen);
// 基本解析类方法
private:
	// 解析子字符串
	long ParseBufferToString(const char* pszBuffer, long* pnOffset, const long lBufferSize, const long lOutStringLenSize,
											  char* pszOutString, const long lOutStringBufSize);
	// 解析名称
	long ParseName(const char* pszBuffer, long* pnOff, char* pszName, const long nBufferSize);
	// 解析描述
	long ParseDescription(const char* pszBuffer, long* pnOff, char* pszDescription, const long nBufferSize);
	// 解析IP地址
	long ParseIPAddress(const char* pszBuffer, long* pnOff, char* pszIP, const long nBufferSize);

	long ParseBufferToIP(const char* pszBuffer, long* pnOff, const long nBufferSize, char* pszIP);
	long ParseToSubBuffer(const char* pszBuffer, long* pnOffset, const long lBufferSize, const long nOutSubBufferLengthSize, 
											   char* pszOutSubBuffer, const long nMaxOutSubBufferSize);
	long ParseBufferToDesc(const char* pszBuffer, long* pnOffset, const long lBufferSize, char* pszDescription);
	long ParseBufferToName(const char* pszBuffer, long* pnOffset, const long lBufferSize, char* pszName);

public:
    //通道解析
    long ParseBufferToChannel(const char* pszBuffer, long* plOffset, const long lBufferSize, char* pszChannel);

// 解析类对应方法
public:
	// 从buffer中解析出相应的子串
	long ParseSubBuffer(IN const char* pszBuffer, OPT long* pnOff, OUT char* pszOutSubBuffer, IN const long nBufferSize, 
						IN const long nOutSubBufferLengthSize, IN const long nMaxOutSubBufferSize);

	// 将buffer解析成摄像头实体
	long ParseBufferToCamera(const char* pszBuffer, long* pnOff, const long nBufferSize, CVideoCamera* pCamera);
	
	// 将buffer解析成监视器
	long ParseBufferToMonitor(const char* pszBuffer, long* pnOff, const long nBufferSize, CVideoMonitor* pMonitor);

	long ParseBufferToInteger(IN const char* pszBuffer, OPT long* pnOff, IN const long nBufferSize, 
		IN const long nIntBufferSize, OUT long &lValue);
	
	long ParseBufferToZoneList(const char* pszBuffer, long* pnOffset, const long lBufferSize,
								CVideoZoneList* plistZone);

	long ParseBufferToProductList(const char* pszBuffer, long* pnOffset, const long lBufferSize, 
								CVideoProductList* plistProduct);

		
	long ParseBufferToDeviceList(const char* pszBuffer, long* pnOffset, const long lBufferSize, 
							 CVideoDeviceList* plistDevice);



	long ParseBufferToCameraList(const char* pszBuffer, long* pnOffset, const long lBufferSize, CVideoCameraList* plistCamera);



	long ParseBufferToSimpleCameraList(IN const char* pszBuffer, IN const long nBufferSize, OUT CVideoCameraList* plistCamera);


	long ParseBufferToCameraListOfZone(IN const char* pszBuffer, IN const long nBufferSize, OUT CVideoCameraList* plistCamera);


	long ParseBufferToSimpleCameraListOfZone(IN const char* pszBuffer, IN const long nBufferSize, OUT CVideoCameraList* plistCamera);

	
	long ParseBufferToMonitorList(const char* pszBuffer, long* pnOffset, const long lBufferSize,
												   CVideoMonitorList* plistMonitor);
                   

	long ParseBufferToMonitorListOfZone(IN const char* pszBuffer, IN const long nBufferSize, OUT CVideoMonitorList* plistMonitor);


	long ParseBufferToMode(IN const char* pszBuffer, IN OUT long *pnOffSet, IN const long nBufferSize, OUT CVideoMode* pMode, IN const bool bPara);

	long ParseBufferToModeList(const char* pszBuffer, long *pnOffSet, const long lBufferSize, 
								CVideoModeList* plistMode, bool bIncPara);

	long ParseBufferToPSP(IN const char* pszBuffer, IN const long nBufferSize, OUT CVideoPSP* pPSP);

	long ParseBufferToPSPList(const char* pszBuffer, long* pnOffset, const long lBufferSize, CVideoPSPList* plistPSP);

	long ParseBufferToSnapFile(IN const char* pszBuffer, IN const long nBufferSize, OUT CVideoSnapFile* pSnapFile, OPT long* pnOff);

	long ParseBufferToSnapFileList(IN const char* pszBuffer, IN const long nBufferSize, OUT CVideoSnapFileList* plistSnapFile);

    long ParseBufferToCStrNameList(const char* pszBuffer, IN const long nBufferSize, OUT CVideoList<CStrName>* plistCStrName);

	// 解析出请求命令的头部信息, 我们需要的头部信息包括: 命令号/StampID/用户名/群组名
	long ParseRequestCmdHeaderInfo(IN const char* pszBuffer, IN const long nBufferSize, long &lOffset,
					OUT long &lCmdCode, OUT long &lStampID, 
					OUT char *szUserName, IN long lUserNameLen, OUT char *szGrpName, IN long lGrpNameLen);

	//解析获取图片内容
	long ParseGetSnapPicBuffer(const char* pszBuffer, const long nBufferSize, long &nCount, char *&pBuffer, long &nPicLen);
};


#endif // !defined(AFX_VIDEOPROTOCOLPARSER_H__9BBBBD8B_CA7C_45FF_A14C_23441BD658EC__INCLUDED_)
