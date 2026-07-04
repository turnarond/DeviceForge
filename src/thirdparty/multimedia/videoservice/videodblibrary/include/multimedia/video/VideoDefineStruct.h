/**************************************************************
 *  Filename:    VideoCommonDefine.h
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: 视频监控系统内部共用结构体定义.
 *
 *  @author:     louzhengqing
 *  @version     2011/01/14  louzhengqing		Initial Version
**************************************************************/
#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef VIDEO_DEFINE_STRUCT_H_
#define VIDEO_DEFINE_STRUCT_H_

#include "VideoDefineMacro.h"

#define   VIDEO_PSP_MAXSIZE      128    //预置位信息列表的最大位数
#define   VIDEO_LIST_MAXSIZE     512    //模式中摄像头所涉及的最多分区
///////////////////////// 视频服务实体定义 ////////////////////////
struct SVideoServer
{
public:
	long m_nID;											// 视频服务ID
	char m_szName[VIDEO_NAME_MAXSIZE];					// 视频服务名称
	char m_szIP[VIDEO_IPADDRESS_MAXSIZE];				// 视频服务主IP
	bool m_bBak;										// 是否有备机
	char m_szBakIP[VIDEO_IPADDRESS_MAXSIZE];			// 视频服务备机IP
	int  m_nLockSeconds;								// 设备锁定时间,单位(秒)
	char m_szDescription[VIDEO_DESCRIPTION_MAXSIZE];	// 视频服务描述
};

///////////////////////// 设备产品型号实体定义 ////////////////////////
struct SVideoProduct
{
public:
	long m_nDeviceType;						// 设备类型
	long m_nID;								// 产品型号ID
	char m_szName[VIDEO_NAME_MAXSIZE];		// 产品型号名称
	char m_szDllName[VIDEO_NAME_MAXSIZE];	// 插件/驱动的dll名称
};

///////////////////////// 分区实体定义 ////////////////////////
struct SVideoZone
{
public:
	long m_nID;											// 分区ID
	char m_szName[VIDEO_NAME_MAXSIZE];					// 分区名称
	char m_szDescription[VIDEO_DESCRIPTION_MAXSIZE];	// 分区描述
	char m_szAuthLabel[VIDEO_NAME_MAXSIZE];				// 分区对应的权限标签ID
	long m_nAuthField;									// 权限范围
};

///////////////////////// 连接设备实体定义 ////////////////////////
// //fengjuanyong 2011-1-12 增加矩阵下编码器的信息struct SVideoLinkDevice
// {
// public:
// 	long m_nDeviceID;			// 连接设备ID
// 	long m_nChannel;			// 连接设备的端口号/通道号
// 	long m_nLocalChannel;		// 本设备的输出端口号/通道号
// 	bool m_bCtrlDevice;			// 是否为控制设备
// 	bool m_bVideoSourceDevice;	// 是否为实时视频源设备
// 	bool m_bHistoryDevice;		// 是否为录像管理设备
// 	
// 	bool m_bEncodeDevice;		// 该设备是否是编码器
// 	bool m_bUsed;				// 编码器是否被占用
// 	time_t m_tmLinkTime;		// 编码器被占用时间
// 	char m_szUserName[VIDEO_NAME_MAXSIZE];		// 占用编码器的用户名
// 	long m_lUserPRI;			// 占用编码器的用户的优先级
// };


///////////////////////// 视频设备实体定义 ////////////////////////
struct SVideoDevice
{
public:
	long m_nServerID;									// 所属视频服务ID
	long m_nProductID;									// 设备的产品型号ID
	long m_nID;											// 设备ID
	char m_szName[VIDEO_NAME_MAXSIZE];					// 设备名称
	char m_szIP[VIDEO_IPADDRESS_MAXSIZE];				// 设备IP地址
	unsigned short m_nPort;								// 设备端口
	char m_szExtent[VIDEO_DEFAULTINFO_MAXSIZE];			// 设备自定义信息
	char m_szDescription[VIDEO_DESCRIPTION_MAXSIZE];	// 设备描述
	long m_nCtrlPort;									// 矩阵摄像头控制专用通道
	long m_nLoginID;									// 注册ID
};


///////////////////////// 设备类型实体定义 ////////////////////////
struct SVideoDevType
{
public:
	long m_nID;											// 设备类型ID
	char m_szName[VIDEO_NAME_MAXSIZE];					// 设备类型
};

///////////////////////// 预置位实体定义 ////////////////////////
struct SVideoPSP
{
public:
	char m_szName[VIDEO_NAME_MAXSIZE];					// 预置位名称
	long m_nPresetIndex;								// 预置点序号
	char m_szDescription[VIDEO_DESCRIPTION_MAXSIZE];	// 预置位描述
};

///////////////////////// 摄像头实体定义 ////////////////////////
struct SVideoCamera
{
public:
	long m_nServerID;									// 所属视频服务ID
	long m_nZoneID;										// 分区ID
	char m_szName[VIDEO_NAME_MAXSIZE];					// 摄像头名称
	long m_nID;											// 摄像头逻辑ID
	char m_szDescription[VIDEO_DESCRIPTION_MAXSIZE];	// 摄像头描述
	bool m_bRemoteFileCtrl;								// 远程文件操作
	bool m_bLocalFileCtrl;								// 本地文件操作
	bool m_bRemoteTimeCtrl;								// 时间回放操作
	bool m_bOrientCtrl;									// 云台控制
	bool m_bPSPCtrl;									// 预置位控制
	bool m_bLensCtrl;									// 镜头控制
	bool m_bQualityCtrl;								// 画面质量
	bool m_bSnapCtrl;									// 抓拍控制
	bool m_bHeaterCtrl;									// 加热器控制
	bool m_bRainBrushCtrl;								// 雨刷控制
	long m_nSnapWidth;									// 抓拍图片宽度
	long m_nSnapHeight;									// 抓拍图片高度
	long m_nSnapBitDeep;								// 抓拍位深度
	long m_nPSPMaxCount;								// 预置位的最大数量
	SVideoPSP m_listPSP[VIDEO_PSP_MAXSIZE];				// 预置位信息列表
};


///////////////////////// 监视器实体定义 ////////////////////////
struct SVideoMonitor
{
public:
	long m_nServerID;									// 所属视频服务ID
	long m_nZoneID;										// 分区ID
	char m_szName[VIDEO_NAME_MAXSIZE];					// 监视器名称
	long m_nID;											// 监视器逻辑ID
	char m_szDescription[VIDEO_DESCRIPTION_MAXSIZE];	// 监视器描述
	char m_szChannel[VIDEO_CHANNEL_MAXSIZE];			// 监视器通道，对于网络视频服务器，就是一个编码
	long m_nLinkDevID;									// 监视器连接设备编号
	long m_nLinkDevType;								// 连接设备的类型
};

///////////////////////// 模式实体定义 ////////////////////////
struct SVideoMode
{
public:
	long m_nType;										// 模式类型
	char m_szName[VIDEO_NAME_MAXSIZE];					// 模式名称
	char m_szPara[VIDEO_MODE_PARA_MAXSIZE];				// 模式参数,包括模式所涉及的分区，模式类型以及模式布局内容
	char m_szDescription[VIDEO_DESCRIPTION_MAXSIZE];	// 模式描述
	long m_listZone[VIDEO_LIST_MAXSIZE];				// 模式中摄像头所涉及的分区
	int  m_nZone;                                       // 模式中摄像头退涉及傻分区返个数
};

///////////////////////// 抓拍图片实体定义 ////////////////////////
struct SVideoSnapFile
{
public:
	long m_nID;											// 抓拍图片ID
	long m_nCameraID;									// 摄像头ID
	char m_szInfoType[VIDEO_NAME_MAXSIZE];				// 信息类型
	time_t m_tmSnap;									// 抓拍时间
	long m_nSnapCount;									// 连续抓拍张数
	char m_szExtent[VIDEO_DEFAULTINFO_MAXSIZE];			// 扩展属性
	char m_szDescription[VIDEO_DESCRIPTION_MAXSIZE];	// 抓拍描述
};


///////////////////////// 抓拍类型定义 ////////////////////////////
struct SIDMapText
{
public:
	long m_nID;								// ID
	char m_szText[VIDEO_NAME_MAXSIZE];		// Text
};

///////////////////////// 自定义分区定义 ////////////////////////////
struct SVideoCustomZone
{
public:
	long m_nID;									// 分区ID
	char m_szName[VIDEO_NAME_MAXSIZE];			// 分区名称
	char m_szUserName[VIDEO_NAME_MAXSIZE];		// 用户名
	char m_szDesc[VIDEO_DESCRIPTION_MAXSIZE];	// 描述
};

///////////////////////// 历史录像文件定义 ////////////////////////////
struct SHisFile
{
public:
	char m_szName[VIDEO_NAME_MAXSIZE];
	long m_nFileSize;
	long m_nStartTime;
	long m_nEndTime;
};


///////////////////////// 录像片段信息实体定义 ////////////////////////
struct SRecord
{
public:
	long m_nID;											// 抓拍图片ID
	long m_nCamID;										// 摄像头ID
	time_t m_tmStart;									// 起始时间
	time_t m_tmEnd;										// 结束时间
	char m_szExtent[VIDEO_EXTENT_MAXSIZE];				// 扩展属性
	char m_szFileName[VIDEO_FILE_NAME_MAXSIZE];			// 文件名称
	char m_szDesc[VIDEO_DESCRIPTION_MAXSIZE];			// 抓拍描述
	long m_nStartSnapID;								// 录像开始时抓拍ID
	long m_nMidSnapID;									// 录像播放中间抓拍ID，通过累加可以有多张抓拍图片
	long m_nEndSnapID;									// 录像结束时抓拍ID
	char m_szReserve1[VIDEO_DEFAULTINFO_MAXSIZE];		// 保留1
	char m_szReserve2[VIDEO_DEFAULTINFO_MAXSIZE];		// 保留2
	char m_szReserve3[VIDEO_DEFAULTINFO_MAXSIZE];		// 保留3
};

#endif //#ifndef VIDEO_DEFINE_STRUCT_H_
