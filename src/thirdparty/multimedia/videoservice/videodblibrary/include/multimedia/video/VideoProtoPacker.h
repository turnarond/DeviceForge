/**************************************************************
 *  Filename:    VideoProtocolPacker.h
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: 视频监控系统协议解析库头文件.
 *
 *  @author:     xulizai
 *  @version     2008/05/16  xulizai  Initial Version
 *  @version     2009/04/17  chenzhan  添加摄像头自定义分区和排序的打包函数
 *  @version     2009/05/04	 zhucongfeng	添加根据分区编号获取自定义分区摄像头
 *  @version     2009/05/31	 zhucongfeng	添加组织抓拍编号
 *  @version     2009/06/09	 zhucongfeng	添加删除抓拍图片的消息组织
 *  @version     2009/06/11	 zhucongfeng	获取抓拍图片内容时删除参数摄像头编号
 *  @version     2009/06/17	 chenzhan	服务端协议打包函数组织
 			     09/08/2009  chenzhan     添加录像抓拍、图片累加功能
				 14/04/2010  chenzhan  增加设备级连控制服务端返回给客户端的编解码器列表
**************************************************************/
#if !defined(AFX_VIDEOPROTOCOLPACKER_H__9BBBBD8B_CA7C_45FF_A14C_23441BD658EC__INCLUDED_)
#define AFX_VIDEOPROTOCOLPACKER_H__9BBBBD8B_CA7C_45FF_A14C_23441BD658EC__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "VideoStructDef.h"

// 协议解析中缓冲区大小定义
#define VIDEO_ZONE_INFO_MAXSIZE				128*2		// 分区信息的最大buffer长度(参考协议)
#define VIDEO_PRODUCT_INFO_MAXSIZE			96*2		// 产品型号信息的最大buffer长度(参考协议)
#define VIDEO_DEVICE_INFO_MAXSIZE			160*2		// 设备信息的最大buffer长度(参考协议)
#define VIDEO_LINKDEVICE_INFO_MAXSIZE		16*2		// 连接设备信息的最大buffer长度(参考协议)
#define VIDEO_CAMERA_INFO_MAXSIZE			160*2		// 摄像头信息的最大buffer长度(参考协议)
#define VIDEO_SIMPLE_CAMERA_INFO_MAXSIZE	64*2		// 摄像头简易信息的最大buffer长度(参考协议)
#define VIDEO_MONITOR_INFO_MAXSIZE			128*2		// 监视器分区信息的最大buffer长度(参考协议)
#define VIDEO_MODE_INFO_MAXSIZE				128*2		// 模式(不包括参数)信息的最大buffer长度(参考协议)
#define VIDEO_PSP_INFO_MAXSIZE				128*2		// 预置位信息的最大buffer长度(参考协议)
#define VIDEO_SNAP_INFO_MAXSIZE				128*2		// 抓拍信息的最大buffer长度(参考协议,不包含抓拍图片内容)
#define VIDEO_LOCK_INFO_MAXSIZE				64*2		// 锁定信息的最大buffer长度(参考协议)

#define RETCMD_HEADERINFO_LENGTH 23

class CVideoProtoPacker  
{
public:
	CVideoProtoPacker();
	virtual ~CVideoProtoPacker();
// 打包为缓冲区类的方法
public:
    /**
	 *  发送消息头(需求用例ID:113179,113180,113181)
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nCmd:									命令码
	 *  @param  -[in] long nStampID:								消息戳
	 *  @param  -[in] char* pszUserName:							用户名
	 *  @param  -[in] char* pszGroup:								群组名
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdHeader(char* pszBuff, long nInLen, long nCmd, long nStampID, char* pszUserName, char* pszGroup, long &nOutLen);
	/**
	 *  返回消息头(需求用例ID:113179,113180,113181)
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nCmd:									命令码
	 *  @param  -[in] long nStampID:								消息戳
	 *  @param  -[in] long nRetCode:								返回错误码
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackRetHeader(char* pszBuff, long nInLen, long nCmd, long nStampID, long nRetCode, long &nOutLen);
	/**
	 *  返回获取所有的设备配置信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] CVideoProductList *plistProduct:				设备产品型号列表
	 *  @param  -[in] CVideoDeviceList *plistDev:					设备列表
	 *  @param  -[in] CVideoZoneList *plistZone:					固定分区列表
	 *  @param  -[in] CVideoCameraList *plistCam:					摄像头列表
	 *  @param  -[in] CVideoMonitorList *plistMon:					监视器列表
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackRetGetAllConfigInfo(char* pszBuff, long nInLen, CVideoProductList *plistProduct, CVideoDeviceList *plistDev, 
		CVideoZoneList *plistZone, CVideoCameraList *plistCam, CVideoMonitorList *plistMon, long &nOutLen);
	/**
	 *  返回获取所有的分区信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] CVideoZoneList* plistZone:					分区列表
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackRetGetZoneInfo(char* pszBuff, long nInLen, CVideoZoneList* plistZone, long &nOutLen);
	/**
	 *  返回获取所有的设备产品型号信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] CVideoProductList* plistProduct:				设备产品型号列表
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackRetGetProductInfo(char* pszBuff, long nInLen, CVideoProductList* plistProduct, long &nOutLen);
	/**
	 *  返回获取所有的视频设备信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] CVideoDeviceList* plistDev:					设备列表
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackRetGetDevInfo(char* pszBuff, long nInLen, CVideoDeviceList* plistDev, long &nOutLen);
	/**
	 *  返回获取所有的摄像头信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] CVideoCameraList* plistCam:					摄像头列表
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackRetGetCamInfo(char* pszBuff, long nInLen, CVideoCameraList* plistCam, long &nOutLen);
	/**
	 *  返回获取所有的摄像头简易信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] CVideoCameraList* plistCam:					摄像头列表
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackRetGetSimpleCamInfo(char* pszBuff, long nInLen, CVideoCameraList* plistCam, long &nOutLen);
	/**
	 *  发送获取某一分区的摄像头信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nZoneID:									分区ID
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdGetZoneCam(char* pszBuff, long nInLen, long nZoneID, long &nOutLen);
	/**
	 *  返回获取某一分区的摄像头信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] CVideoCameraList* plistCam:					摄像头列表
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackRetGetZoneCam(char* pszBuff, long nInLen, CVideoCameraList* plistCam, long &nOutLen);
	/**
	 *  发送获取某一分区的摄像头简易信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nZoneID:									分区ID
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdGetZoneSimpleCam(char* pszBuff, long nInLen, long nZoneID, long &nOutLen);
	/**
	 *  返回获取某一分区的摄像头简易信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] CVideoCameraList* plistCam:					摄像头列表
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackRetGetZoneSimpleCam(char* pszBuff, long nInLen, CVideoCameraList* plistCam, long &nOutLen);
	/**
	 *  发送根据摄像头名称获取摄像头信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] char* pszCamName:								摄像头名称
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdGetCamByName(char* pszBuff, long nInLen, char* pszCamName, long &nOutLen);
	/**
	 *  返回根据摄像头名称获取摄像头信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] CVideoCameraList* plistCam:					摄像头列表
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackRetGetCamByName(char* pszBuff, long nInLen, CVideoCameraList* plistCam, long &nOutLen);
	/**
	 *  返回获取所有的监视器信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] CVideoMonitorList* plistMon:					监视器列表
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackRetGetMonInfo(char* pszBuff, long nInLen, CVideoMonitorList* plistMon, long &nOutLen);
	/**
	 *  发送获取某一分区的监视器信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nZoneID:									分区ID
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdGetZoneMon(char* pszBuff, long nInLen, long nZoneID, long &nOutLen);
	/**
	 *  返回获取某一分区的监视器信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] CVideoMonitorList* plistMon:					监视器列表
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackRetGetZoneMon(char* pszBuff, long nInLen, CVideoMonitorList* plistMon, long &nOutLen);
	/**
	 *  发送获取所有的模式名称
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nTime:									信息更新时间
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdGetAllModeName(char* pszBuff, long nInLen, long nTime, long &nOutLen);
	/**
	 *  返回获取所有的模式名称
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nTime:									服务本次更新时间
	 *  @param  -[in] CVideoModeList* plistMode:					模式列表
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackRetGetAllModeName(char* pszBuff, long nInLen, long nTime, CVideoModeList* plistMode, long &nOutLen);
	/**
	 *  发送增加模式
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] CVideoMode* ptheMode:							模式信息
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdAddMode(char* pszBuff, long nInLen, CVideoMode* ptheMode, long &nOutLen);
	/**
	 *  发送删除模式
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] CVideoModeList* plistMode:					模式列表
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdDelMode(char* pszBuff, long nInLen, CVideoModeList* plistMode, long &nOutLen);
	/**
	 *  返回删除模式
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] CVideoModeList* plistMode:					模式列表
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackRetDelMode(char* pszBuff, long nInLen, CVideoModeList* plistMode, long &nOutLen);
	/**
	 *  发送修改模式
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] char *pszModeName:							旧模式名称
	 *  @param  -[in] CVideoMode* ptheMode:							新模式信息
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdModifyMode(char* pszBuff, long nInLen, char *pszModeName, CVideoMode* ptheMode, long &nOutLen);
	/**
	 *  发送获取模式的布局信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] char *pszModeName:							模式名称
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdGetModeLayout(char* pszBuff, long nInLen, char *pszModeName, long &nOutLen);
	/**
	 *  返回获取模式的布局信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] CVideoMode* ptheMode:							模式信息
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackRetGetModeLayout(char* pszBuff, long nInLen, CVideoMode* ptheMode, long &nOutLen);
	/**
	 *  发送获取摄像头的预置位信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nCamID:									摄像头ID
	 *  @param  -[in] long nTime:									信息更新时间
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdGetCamPSPInfo(char* pszBuff, long nInLen, long nCamID, long nTime, long &nOutLen);
	/**
	 *  返回获取摄像头的预置位信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nCamID:									摄像头ID
	 *  @param  -[in] long nTime:									服务本次更新时间
	 *  @param  -[in] CVideoPSPList* plistPSP:						预置位列表
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackRetGetCamPSPInfo(char* pszBuff, long nInLen, long nCamID, long nTime, CVideoPSPList* plistPSP, long &nOutLen);
	/**
	 *  发送增加预置位
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nCamID:									摄像头ID
	 *  @param  -[in] long nTime:									信息更新时间
	 *  @param  -[in] CVideoPSP *pthePSP:							预置位信息
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdAddPSP(char* pszBuff, long nInLen, long nCamID, long nTime, CVideoPSP *pthePSP, long &nOutLen);
	/**
	 *  发送删除预置位
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nCamID:									摄像头ID
	 *  @param  -[in] CVideoPSPList *plistPSP:						预置位列表
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	//2012.5.6 zhucongfeng 无使用，而且是错误的，打包与解包不一致。目前打包和解包直接写在VideoRAPI和VideoService中了
	//long PackCmdDelPSP(char* pszBuff, long nInLen, long nCamID, CVideoPSPList *plistPSP, long &nOutLen);
	/**
	 *  返回删除预置位
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] CVideoPSPList *plistPSP:						预置位列表
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	//2012.5.6 zhucongfeng 无使用，而且是错误的，打包与解包不一致。目前打包和解包直接写在VideoRAPI和VideoService中了
	//long PackRetDelPSP(char* pszBuff, long nInLen, CVideoPSPList *plistPSP, long &nOutLen);
	/**
	 *  发送修改预置位
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nCamID:									摄像头ID
	 *  @param  -[in] char *pszPSPName:								旧预置位名称
	 *  @param  -[in] CVideoPSP *pthePSP:							新预置位信息
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdModifyPSP(char* pszBuff, long nInLen, long nCamID, char *pszPSPName, CVideoPSP *pthePSP, long &nOutLen);
	/**
	 *  发送应用预置位
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nCamID:									摄像头ID
	 *  @param  -[in] char *pszPSPName:								预置位名称
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdAppPSP(char* pszBuff, long nInLen, long nCamID, char *pszPSPName, long &nOutLen);
	/**
	 *  返回获取抓拍图片的信息类型
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] CIDMapTextList *plistSnapType:				抓拍类型列表
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackRetGetSnapType(char* pszBuff, long nInLen, CIDMapTextList *plistSnapType, long &nOutLen);
	/**
	 *  发送抓拍查询
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nCamID:									摄像头ID
	 *  @param  -[in] char *pszPSPName:								预置位名称
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdQuerySnapInfo(char* pszBuff, long nInLen, long nCamID, long nStart, long nEnd, char *pszSnapType, long &nOutLen);
	/**
	 *  返回抓拍查询
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] CVideoSnapFileList *plistSnapInfo:			抓拍信息列表
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackRetQuerySnapInfo(char* pszBuff, long nInLen, CVideoSnapFileList *plistSnapInfo, long &nOutLen);
	/**
	 *  发送获取指定抓拍图片的图片内容(返回时图片内容需额外添加)
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
     *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nSnapID:								    抓拍ID
     *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdGetPicBuffer(char* pszBuff, long nInLen, long nSnapID, long &nOutLen);
	/**
	 *  发送增加抓拍图片(图片内容需额外添加)
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
     *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] CVideoSnapFile *ptheSnapInfo:				    抓拍信息
     *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdAddPicInfo(char* pszBuff, long nInLen, CVideoSnapFile *ptheSnapInfo, long &nOutLen);
	/**
	 *  返回增加抓拍图片
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
     *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nSnapID:								    抓拍ID
     *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackRetAddPicInfo(char* pszBuff, long nInLen, long nSnapID, long &nOutLen);
	/**
	 *  发送删除抓拍图片信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
     *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] CVideoSnapFileList *plistSnapInfo:			抓拍信息列表
     *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdDelSnapInfo(char* pszBuff, long nInLen, CVideoSnapFileList *plistSnapInfo, long &nOutLen);
	/**
	 *  发送修改抓拍图片信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
     *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nSnapID:								    抓拍ID
	 *  @param  -[in] CVideoSnapFileList *plistSnapInfo:			抓拍信息列表
     *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdModifySnapInfo(char* pszBuff, long nInLen, long nSnapID, CVideoSnapFile *ptheSnapInfo, long &nOutLen);
	/**
	 *  发送视频切换--逻辑ID
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
     *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nCamID:								    摄像头ID
	 *  @param  -[in] long nMonID:								    监视器ID
     *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdSwitchByID(char* pszBuff, long nInLen, long nCamID, long nMonID, long &nOutLen);
	/**
	 *  发送视频切换--名称
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
     *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] char *pszCamName:							    摄像头名称
	 *  @param  -[in] char *pszMonName:							    监视器名称
     *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdSwitchByName(char* pszBuff, long nInLen, char *pszCamName, char *pszMonName, long &nOutLen);
	/**
	 *  发送镜头控制
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
     *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nCamID:								    摄像头ID
	 *  @param  -[in] long nCtrCode:								控制码(1-6)占1位
	 *  @param  -[in] long nSpeed:								    速度(0-5)占1位
     *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdLensControl(char* pszBuff, long nInLen, long nCamID, long nCtrCode, long nSpeed, long &nOutLen);
	/**
	 *  发送附加设备控制
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
     *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nCamID:								    摄像头ID
	 *  @param  -[in] long nCtrCode:								控制码(1-2)占1位
	 *  @param  -[in] long nSpeed:								    速度(0-5)占1位
     *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdAuxControl(char* pszBuff, long nInLen, long nCamID, long nCtrCode, long nSpeed, long &nOutLen);
	/**
	 *  发送云台控制
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
     *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nCamID:								    摄像头ID
	 *  @param  -[in] long nCtrCode:								控制码(1-4)占1位
	 *  @param  -[in] long nSpeed:								    速度(0-5)占1位
     *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdPanControl(char* pszBuff, long nInLen, long nCamID, long nCtrCode, long nSpeed, long &nOutLen);
	/**
	 *  返回获取自定义分区(需求用例ID:113179,113180,113181)
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] CVideoCustomZoneList* plistCustZone:			自定义分区列表
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackRetGetCustZone(char* pszBuff, long nInLen, CVideoCustomZoneList* plistCustZone, long &nOutLen);
	/**
	 *  发送添加自定义分区(需求用例ID:113179,113180,113181)
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] char* pszCustZoneName:						自定义分区名称
	 *  @param  -[in] char* pszCustZoneDesc:						自定义分区名称
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdAddCustZone(char* pszBuff, long nInLen, char* pszCustZoneName, char* pszCustZoneDesc, long &nOutLen);
	/**
	 *  发送删除自定义分区(需求用例ID:113179,113180,113181)
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] char* pszCustZoneName:						自定义分区名称
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdDelCustZone(char* pszBuff, long nInLen, char* pszCustZoneName, long &nOutLen);
	/**
	 *  发送获取自定义分区摄像头(需求用例ID:113179,113180,113181)
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] char* pszCustZoneName:						自定义分区名称
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdGetCustZoneCam(char* pszBuff, long nInLen, char* pszCustZoneName, long &nOutLen);
	/**
	 *  发送获取自定义分区摄像头(需求用例ID:113179,113180,113181)
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long lCustZoneID:								自定义分区编号
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdGetCustZoneCam(char* pszBuff, long nInLen, long lCustZoneID, long &nOutLen);
	/**
	 *  返回获取自定义分区摄像头(需求用例ID:113179,113180,113181)
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] CVideoCameraList *plistCustZoneCam:			摄像头名称列表
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackRetGetCustZoneCam(char* pszBuff, long nInLen, CVideoCameraList *plistCustZoneCam, long &nOutLen);
	/**
	 *  发送复制自定义分区摄像头(需求用例ID:113179,113180,113181)
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] char* pszCustZoneName:						自定义分区名称(摄像头拷贝进此自定义分区)
	 *  @param  -[in] CVideoList<CStrName> *plistCustZoneCamName:	摄像头名称列表
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdCpyCustZoneCam(char* pszBuff, long nInLen, char* pszCustZoneName, CVideoList<CStrName> *plistCustZoneCamName, long &nOutLen);
	/**
	 *  发送删除自定义分区摄像头(需求用例ID:113179,113180,113181)
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] char* pszCustZoneName:						自定义分区名称(只删除此自定义分区摄像头)
	 *  @param  -[in] CVideoList<CStrName> *plistCustZoneCamName:	摄像头名称列表
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdDelCustZoneCam(char* pszBuff, long nInLen, char* pszCustZoneName, CVideoList<CStrName> *plistCustZoneCamName, long &nOutLen);
	/**
	 *  发送累加抓拍图片
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nCaptureID:								抓拍信息ID
	 *  @param  -[in] char *pPicBuf:								图片内容
	 *  @param  -[in] long nPicSize:								图片内容长度
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdCapturePic(char* pszBuff, long nInLen, long nCaptureID, char* pPicBuf, long nPicSize, long &nOutLen);
	/**
	 *  发送添加录像信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] CRecord *pRec:								录像信息
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdAddRecord(char* pszBuff, long nInLen, CRecord *pRec, char* pRecordBuf, long nRecSize, long &nOutLen);
	/**
	 *  发送查询录像信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nCamID:									摄像头ID
	 *  @param  -[in] long nStartTime:								开始时间
	 *  @param  -[in] long nEndTime:								结束时间
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdQueryRecord(char* pszBuff, long nInLen, long nCamID, time_t tStartTime, time_t tEndTime, long &nOutLen);
	/**
	 *  返回查询录像信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] CRecordList* pListRecord:						录像信息列表
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackRetQueryRecord(char* pszBuff, long nInLen, CRecordList* pListRecord, long &nOutLen);
	/**
	 *  发送修改录像信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nRecordID:								录像信息ID
	 *  @param  -[in] CRecord *pRec:								录像信息
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdModifyRecord(char* pszBuff, long nInLen, long nRecordID, CRecord *pRec, long &nOutLen);
	/**
	 *  发送下载录像信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nRecordID:								录像信息ID
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdDownloadRecord(char* pszBuff, long nInLen, long nRecordID, long &nOutLen);
	/**
	 *  发送删除录像信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nRecordID:								录像信息ID
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdDelRecord(char* pszBuff, long nInLen, long nRecordID, long &nOutLen);
	/**
	 *  发送删除抓拍图片信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
     *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long nCamID:									摄像机ID
	 *  @param  -[in] long nMonID:									监视器ID
     *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCmdVideoSwitch(char* pszBuff, long nInLen, long nCamID, long nMonID, long &nOutLen);
	/**
	 *  返回查询录像信息
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] CReSwitchChannelList* pListSwitch:			视频切换信息列表
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackRetVideoSwitch(char* pszBuff, long nInLen, CReSwitchChannelList* pListSwitch, long &nOutLen);
private:
	/**
	 *  将输入的监视器列表打包为缓冲区
	 *
	 *  @param  -[in] const bool bZone:					是否仅仅打包某个分区
	 *  @param  -[in] long nZoneID:						分区ID(如果查所有分区则为-1)
	 *  @param  -[in] CVideoMonitorList* plistMonitor:	监视器列表
	 *  @param  -[out] char* szOutBuf:					输出缓冲区
	 *  @param  -[in] long lOutBufSize:					输出缓冲区的总长度
	 *  @param  -[out] long* plActOutBufLen:			实际输出的缓冲区长度,从0开始计数
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackGeneralMonitorListToBuffer(CVideoMonitorList* plistMonitor, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen, 
											const bool bZone, const long nZoneID);

	/**
	 *  将输入的摄像头列表打包为缓冲区
	 *
	 *  @param  -[in] const bool bZone:					是否仅仅打包某个分区
	 *  @param  -[in] long nZoneID:						分区ID(如果查所有分区则为-1)
	 *  @param  -[in] CVideoCamera* pCamera:			摄像头
	 *  @param  -[out] char* szOutBuf:					输出缓冲区
	 *  @param  -[in] long lOutBufSize:					输出缓冲区的总长度
	 *  @param  -[out] long* plActOutBufLen:			实际输出的缓冲区长度,从0开始计数
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackGeneralCameraToBuffer(CVideoCamera* pCamera, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen, 
		const bool bZone, const long nZoneID);
	
	/**
	 *  将输入的摄像头列表打包为缓冲区
	 *
	 *  @param  -[in] const bool bZone:					是否仅仅打包某个分区
	 *  @param  -[in] long nZoneID:						分区ID(如果查所有分区则为-1)
	 *  @param  -[in] CVideoCameraList* plistCamera:	摄像头列表
	 *  @param  -[out] char* szOutBuf:					输出缓冲区
	 *  @param  -[in] long lOutBufSize:					输出缓冲区的总长度
	 *  @param  -[out] long* plActOutBufLen:			实际输出的缓冲区长度,从0开始计数
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackGeneralCameraListToBuffer(CVideoCameraList* plistCamera, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen, 
											const bool bZone, const long nZoneID);
	
	/**  将输入的摄像头列表打包为缓冲区(简要的摄像头信息)
	 *
	 *  @param  -[in] const bool bZone:					是否仅仅打包某个分区
	 *  @param  -[in] long nZoneID:						分区ID(如果查所有分区则为-1)
	 *  @param  -[in] CVideoCameraList* plistCamera:	摄像头列表
	 *  @param  -[out] char* szOutBuf:					输出缓冲区
	 *  @param  -[in] long lOutBufSize:					输出缓冲区的总长度
	 *  @param  -[out] long* plActOutBufLen:			实际输出的缓冲区长度,从0开始计数
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackGeneralSimpleCameraListToBuffer(CVideoCameraList* plistCamera, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen, 
											const bool bZone, const long nZoneID);

// 打包对象
public:
	/**
	 *  将输入的分区列表打包为缓冲区
	 *
	 *  @param  -[in] CVideoZoneList* plistZone:		分区列表
	 *  @param  -[out] char* szOutBuf:					输出缓冲区
	 *  @param  -[in] long lOutBufSize:					输出缓冲区的总长度
	 *  @param  -[out] long* plActOutBufLen:			实际输出的缓冲区长度,从0开始计数
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackZoneListToBuffer(IN CVideoZoneList* plistZone, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen);

	/**
	 *  将输入的设备列表打包为缓冲区
	 *
	 *  @param  -[in] CVideoDeviceList* plistDevice:	设备列表
	 *  @param  -[out] char* szOutBuf:					输出缓冲区
	 *  @param  -[in] long lOutBufSize:					输出缓冲区的总长度
	 *  @param  -[out] long* plActOutBufLen:			实际输出的缓冲区长度,从0开始计数
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackDeviceListToBuffer(IN CVideoDeviceList* plistDevice, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen);

	/**
	 *  将输入的设备产品型号信息打包为缓冲区
	 *
	 *  @param  -[in] CVideoProductList* plistProduct:	设备产品型号列表
	 *  @param  -[out] char* szOutBuf:					输出缓冲区
	 *  @param  -[in] long lOutBufSize:					输出缓冲区的总长度
	 *  @param  -[out] long* plActOutBufLen:			实际输出的缓冲区长度,从0开始计数
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackProductListToBuffer(CVideoProductList* plistProduct, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen);

	/**
	 *  将输入的摄像头列表打包为缓冲区
	 *
	 *  @param  -[in] CVideoCamera* pCamera:			摄像头
	 *  @param  -[out] char* szOutBuf:					输出缓冲区
	 *  @param  -[in] long lOutBufSize:					输出缓冲区的总长度
	 *  @param  -[out] long* plActOutBufLen:			实际输出的缓冲区长度,从0开始计数
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCameraToBuffer(IN CVideoCamera* pCamera, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen);

	/**
	 *  将输入的摄像头列表打包为缓冲区(所有分区）
	 *
	 *  @param  -[in] CVideoCameraList* plistCamera:	摄像头列表
	 *  @param  -[out] char* szOutBuf:					输出缓冲区
	 *  @param  -[in] long lOutBufSize:					输出缓冲区的总长度
	 *  @param  -[out] long* plActOutBufLen:			实际输出的缓冲区长度,从0开始计数
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCameraListToBuffer(IN CVideoCameraList* plistCamera, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen);

	/**
	 *  将输入的摄像头列表打包为缓冲区(简要信息, 所有分区)
	 *
	 *  @param  -[in] CVideoCameraList* plistCamera:	摄像头列表
	 *  @param  -[out] char* szOutBuf:					输出缓冲区
	 *  @param  -[in] long lOutBufSize:					输出缓冲区的总长度
	 *  @param  -[out] long* plActOutBufLen:			实际输出的缓冲区长度,从0开始计数
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackSimpleCameraListToBuffer(IN CVideoCameraList* plistCamera, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen);

	/**
	 *  将输入的摄像头列表打包为缓冲区（某个分区）
	 *
	 *  @param  -[in] long nZoneID:						分区ID(如果查所有分区则为-1)
	 *  @param  -[in] CVideoCameraList* plistCamera:	摄像头列表
	 *  @param  -[out] char* szOutBuf:					输出缓冲区
	 *  @param  -[in] long lOutBufSize:					输出缓冲区的总长度
	 *  @param  -[out] long* plActOutBufLen:			实际输出的缓冲区长度,从0开始计数
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackCameraListOfZoneToBuffer(IN const long nZoneID, IN CVideoCameraList* plistCamera,
									  char* szOutBuf, const long lOutBufSize, long* plActOutBufLen);

	/**
	 *  将输入某个模式打包为缓冲区
	 *
	 *  @param  -[in] CVideoMode* pMode:				模式
	 *  @param  -[in] const bool bPara:					是否打包参数
	 *  @param  -[out] char* szOutBuf:					输出缓冲区
	 *  @param  -[in] long lOutBufSize:					输出缓冲区的总长度
	 *  @param  -[out] long* plActOutBufLen:			实际输出的缓冲区长度,从0开始计数
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackModeToBuffer(IN CVideoMode* pMode, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen, IN const bool bPara);

	/**
	 *  将输入模式列表打包为缓冲区
	 *
	 *  @param  -[in] CVideoMode* pMode:				模式列表
	 *  @param  -[in] const bool bPara:					是否打包参数
	 *  @param  -[out] char* szOutBuf:					输出缓冲区
	 *  @param  -[in] long lOutBufSize:					输出缓冲区的总长度
	 *  @param  -[out] long* plActOutBufLen:			实际输出的缓冲区长度,从0开始计数
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackModeListToBuffer(IN CVideoModeList* plistMode, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen, IN const bool bPara);

	/**
	 *  将输入的监视器列表打包为缓冲区(所有分区)
	 *
	 *  @param  -[in] CVideoMonitorList* plistMonitor:	监视器列表
	 *  @param  -[out] char* szOutBuf:					输出缓冲区
	 *  @param  -[in] long lOutBufSize:					输出缓冲区的总长度
	 *  @param  -[out] long* plActOutBufLen:			实际输出的缓冲区长度,从0开始计数
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackMonitorListToBuffer(IN CVideoMonitorList* plistMonitor, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen);

	/**
	 *  将输入的监视器列表打包为缓冲区(某个分区)
	 *
	 *  @param  -[in] CVideoMonitorList* plistMonitor:	监视器列表
	 *  @param  -[out] char* szOutBuf:					输出缓冲区
	 *  @param  -[in] long lOutBufSize:					输出缓冲区的总长度
	 *  @param  -[out] long* plActOutBufLen:			实际输出的缓冲区长度,从0开始计数
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackMonitorListOfZoneToBuffer(IN const long nZoneID, IN CVideoMonitorList* plistMonitor, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen);

	/**
	 *  将输入的摄像头预置位打包为缓冲区
	 *
	 *  @param  -[in] CVideoPSP* pPSP:					某个摄像头的某个预置位
	 *  @param  -[out] char* szOutBuf:					输出缓冲区
	 *  @param  -[in] long lOutBufSize:					输出缓冲区的总长度
	 *  @param  -[out] long* plActOutBufLen:			实际输出的缓冲区长度,从0开始计数
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackPSPToBuffer(IN CVideoPSP* pPSP, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen);

	/**
	 *  将输入的摄像头预置位列表打包为缓冲区
	 *
	 *  @param  -[in] CVideoPSP* pPSP:					某个摄像头的预置位列表
	 *  @param  -[out] char* szOutBuf:					输出缓冲区
	 *  @param  -[in] long lOutBufSize:					输出缓冲区的总长度
	 *  @param  -[out] long* plActOutBufLen:			实际输出的缓冲区长度,从0开始计数
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 *
	 */
	long PackPSPListToBuffer(IN CVideoPSPList* plistPSP, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen);

	/**
	 *  将输入的抓拍文件信息列表打包为缓冲区
	 *
	 *  @param  -[in] CVideoSnapFile* pSnapFile:		抓拍文件信息
	 *  @param  -[out] char* pszBuffer:					输出缓冲区
	 *  @param  -[in] long lBufferSize:					输出缓冲区的总长度
	 *  @param  -[out] long* plOutBufferSize:			实际输出的缓冲区长度,从0开始计数
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackSnapFileToBuffer(IN CVideoSnapFile* pSnapFile, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen);

	/**
	 *  将输入的抓拍文件信息列表打包为缓冲区
	 *
	 *  @param  -[in] CVideoSnapFileList* plistSnapFile: 抓拍文件信息列表
	 *  @param  -[out] char* szOutBuf:					输出缓冲区
	 *  @param  -[in] long lOutBufSize:					输出缓冲区的总长度
	 *  @param  -[out] long* plActOutBufLen:			实际输出的缓冲区长度,从0开始计数
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackSnapFileListToBuffer(IN CVideoSnapFileList* plistSnapFile, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen);

	/**
	 *  将输入的摄像头列表打包为缓冲区(简要信息, 某个分区)
	 *
	 *  @param  -[in] CVideoCameraList* plistCamera:	摄像头列表
	 *  @param  -[out] char* szOutBuf:					输出缓冲区
	 *  @param  -[in] long lOutBufSize:					输出缓冲区的总长度
	 *  @param  -[out] long* plActOutBufLen:			实际输出的缓冲区长度,从0开始计数
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackSimpleCameraListOfZoneToBuffer(IN const long nZoneID, IN CVideoCameraList* plistCamera, 
									char* szOutBuf, const long lOutBufSize, long* plActOutBufLen);

	/**
	 *  将抓怕ID组织到消息中
	 *
	 *  @param  -[out] char* pszBuff:								消息缓冲区(从此指针的开始处存放信息)
	 *  @param  -[in] long nInLen:									消息缓冲区长度
	 *  @param  -[in] long lSnapID:									抓拍编号
	 *  @param  -[out] long &nOutLen:								返回信息长度
	 *
	 *  @return 
	 *	- ==0 success
	 *	- !=0 failure
	 */
	long PackSnapPicID(char* pszBuff, long nInLen, long lSnapID, long &nOutLen);

	/**
	 *  打包删除抓拍图片.
	 *
	 *  @param  -[in, out]  char*  pszBuff: [缓冲区]
	 *  @param  -[in]  long  nInLen: [缓冲区长度]
	 *  @param  -[in]  long  lCamID: [摄像头ID]
	 *  @param  -[in]  long  lSnapID: [抓拍图片ID]
	 *  @param  -[in]  long&  nOutLen: [返回的长度]
	 *
	 *  @version     06/09/2009  zhucongfeng  Initial Version.
	 */
	long PackDelSnapPic(char* pszBuff, long nInLen, long lCamID, long lSnapID, long &nOutLen);

	/**
	 *  打包发送摄像头对应的实时图像连接的设备.
	 *
	 *  @param  -[in, out]  char*  pszBuff: [缓冲区]
	 *  @param  -[in]  long  nInLen: [缓冲区长度]
	 *  @param  -[in]  CVideoLinkDevice *pLinkDev: [设备信息]
	 *  @param  -[in]  long&  nOutLen: [返回的长度]
	 *
	 *  @version     01/17/2011  fengjuanyong  Initial Version.
	 */
	long PackRetGetDevToCam(char* pszBuff, long nInLen, long lDevType, CVideoLinkDevice *pLinkDev, long &nOutLen);

	/**
	 *  打包发送摄像头对应的监视器设备.
	 *
	 *  @param  -[in, out]  char*  pszBuff: [缓冲区]
	 *  @param  -[in]  long  nInLen: [缓冲区长度]
	 *  @param  -[in]  CVideoMonitor *pMon: [监视器信息]
	 *  @param  -[in]  long&  nOutLen: [返回的长度]
	 *
	 *  @version     03/01/2011  fengjuanyong  Initial Version.
	 */
	long PackRetGetMonToCam(char* pszBuff, long nInLen, CVideoMonitor *pMon, long &nOutLen);
};

#endif // !defined(AFX_VIDEOPROTOCOLPACKER_H__9BBBBD8B_CA7C_45FF_A14C_23441BD658EC__INCLUDED_)
