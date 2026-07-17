/**************************************************************
 *  Filename:    VideoCfg.h
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: 配置信息读写文件.
 *
 *  @author:     chenzhan
 *  @version     04/21/2009  chenzhan  Initial Version 
 *  @version     04/21/2009  chenzhan  增加摄像头分组.
 *				 08/31/2009  chenzhan  增加录像片段管理.
				 10/23/2009  chenzhan  解析带逗号的字符串.
				 14/04/2010  chenzhan  设备级连视频切换.
				 04/20/2010  chenzhan  修改设备级连算法,增加视频切换的数据结构
**************************************************************/
// VideoCfg.h: interface for the CVideoCfg class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_VIDEOCFG_H__EB621F0D_775C_415A_B0A2_B5810E6B3945__INCLUDED_)
#define AFX_VIDEOCFG_H__EB621F0D_775C_415A_B0A2_B5810E6B3945__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "VideoSvcDef.h"
#include "multimedia/video/VideoDBLibrary.h"

class CVideoCfg  
{
public:
	CVideoCfg();
	virtual ~CVideoCfg();

private:
	long m_lAutoLockSeconds;		// 自动锁定时间
	CVideoDBLibrary m_videoDB;		// 视频数据库操作
	
	time_t m_lLastAllCfgChgTime;		// 数据库中所有设备配置最新修改过时间
	time_t m_lLastModeChgTime;		// 数据库中模式最新修改过时间
	time_t m_lLastPspChgTime;			// 数据库中预置位最新修改时间
	unsigned short m_uListenPort;	// 视频服务的侦听端口

// 内存中保留的各种配置信息, 在运行过程中这些信息都是不会改变的（和数据库文件中时刻一致）
private:
	CVideoServerList m_listSvr;					// 视频服务列表
	CVideoZoneList m_listZone;					// 分区列表
	CVideoProductList m_listProduct;			// 设备产品型号列表
	CVideoDeviceList m_listDev;					// 视频设备列表
	CVideoCameraMap m_mapCam;					// 摄像头列表
	CVideoMonitorList m_listMon;				// 监视器列表
	CIDMapTextList m_listSnapType;				// 抓拍类型列表
	CVideoMatrixList m_listMatrix;				// 矩阵列表
	
private:
	char m_szLocalIP[ICV_IPSTRING_MAXLEN];		// 本机IP地址，用于据IP得到本地服务号,在发送指令时比较该指令是远程还是本地执行
	long m_lLocalSvrID;							// 本地服务的编号
	bool m_bBak;								// 是否是备机

private:
	long LoadFixConfig();						// 加载固定配置信息,包括:摄像头\监视器等由配置下发的\在运行过程中不会改变的信息
	long LoadXmlConfigInfo();					// 读取XML配置文件的信息,得到本地IP地址
	long InitVideoConfigDB();					// 初始化配置数据库
	long InitVideoInfoDB();						// 初始化过程数据库
	long ExitVideoConfigDB();					// 退出配置数据库	
	void UpdateLastModeChgTime();				// 更新模式最后改变时间
	void UpdateLastPspChgTime();				// 更新预置位最后改变时间
	void UpdateLastAllCfgChgTime();				// 更新全局配置最后改变时间
	// 验证用户模式的权限
	long VerifyUserModeRight(const char *pszUserName, const char *pszGroupName, CVideoMode *pMode);
	// 验证用户对摄像头的权限
	long VerifyUserCamRight(const char *pszUserName, const char *pszGroupName, const long lCamID);
	// 验证用户分区的权限
	long VerifyUserZoneRight(const char *pszUserName, const char *pszGroupName, const long lZoneID);

// Get/Set method
public:
	// 获取默认锁定时间
	long GetDefaultLockTime()  { return m_lAutoLockSeconds;}
	char* GetLocalIP() { return m_szLocalIP; }
	long GetVideoRMStatus();										// 是否是冗余机器
	long GetCtrlDeviceByCamID(const long nCameraID, 
		CVideoDevice* &pCtrlDev, char* szChannel);					// 根据摄像头ID得到控制该摄像头的设备
	long GetCameraByCamID(long lCamID, CVideoCameraEx *&pCamEx);	// 得到摄像头信息及是否是本地摄像头信息
	long GetCameraByCamName(const char *szCamName, CVideoCameraEx *&pCamEx);	// 得到摄像头信息及是否是本地摄像头信息
	long GetCameraIDByCamName(const char *szCamName, long &nCamID); // 得到摄像头ID及是否是本地摄像头信息
	long GetProductByCamID(long lCamID, CVideoProduct *&pProduct);	// 得到摄像头信息及是否是本地摄像头信息
	long GetMonitorIDByMonName(const char *pMonName, long &nMonID); // 根据名称获取监视器ID
	CIDMapTextList* GetListSnapType() { return &m_listSnapType; }
	time_t GetLastModeChgTime(){return m_lLastModeChgTime;}			// 模式发生了改变
	time_t GetLastPspChgTime(){return m_lLastPspChgTime;}				// 预置位发生了改变
	time_t GetLastAllCfgChgTime(){return m_lLastAllCfgChgTime;}		// 全局配置发生了改变
	CVideoProductList* GetProducts() { return &m_listProduct; }
	CVideoCameraMap* GetCameraMap() { return &m_mapCam; }
	CVideoDeviceList * GetDeviceList(){return &m_listDev;}			// 返回所有设备列表
	CVideoProductList * GetProductList(){return &m_listProduct;}	// 返回所有设备列表
	unsigned short GetListenPort(){return m_uListenPort;}
	CVideoServerList *GetVideoServerList(){return &m_listSvr;}
	CVideoMonitorList *GetMonitorList(){return &m_listMon;} // 返回监视器列表
	CVideoMatrixList *GetMatrixList() {return &m_listMatrix;} // 返回矩阵列表
// 录像片段管理
public:
	// 增加录像记录
	long AddRecordOfCameraByUserInfo(const char* pszUserName, const char* pszGroupName, const long nCamID, CRecord *pRec, long &nRecordID);
	// 修改录像记录信息
	long ModifyRecordByUserInfo(const char* pszUserName, const char* pszGroupName, const long nRecordID, CRecord* pRec);
	// 查询录像
	long QueryRecordByUserInfo(const char* pszUserName, const char* pszGroupName, const long nCamID, const long nTimeStart, const long nTimeEnd, CRecordList *plistRec);
	// 下载录像
	long DownloadRecordByUserInfo(const char* pszUserName, const char* pszGroupName, const long nRecordID, const char* pszFileName);
	// 删除录像
	long DeleteRecordByUserInfo(const char* pszUserName, const char* pszGroupName, const long nRecordID);
	// 保存录像
	long SaveRecord(const long nRecordID, const char* pszRecBuff, const long nRecSize);
// 抓拍图片累加
public:
	// 抓拍图片累加
	long CapturePicAddTo(const char* pszUserName, const char* pszGroupName, const long nCaptureID, const char* pszPicBuff, const long nSize);
public:
	// 加载所有的配置信息
	long LoadAllConfig();
	// 删除所有的配置信息
	long UnLoadAllConfig();	
	// 根据用户信息以及模式名称获取模式信息
	long GetModeInfoByNameAndUserInfo(const char* pszModeName, CVideoMode* pMode, 
											 const char* pszUserName, const char* pszGroupName);
	// 根据用户信息修改模式
	long ModifyModeByUserInfo(const char* pszOldModeName, CVideoMode* pNewMode, 
										   const char* pszUserName, const char* pszGroupName);
	// 根据用户信息删除模式
	long DeleteModeByUserInfo(const char* pszModeName, const char* pszUserName, const char* pszGroupName);
	// 根据用户信息添加模式
	long AddModeByUserInfo(CVideoMode* pMode, const char* pszUserName, const char* pszGroupName);
	// 根据用户信息获取模式信息列表
	long GetModeListByUserInfo(CVideoModeList* plistMode, const char* pszUserName, const char* pszGroupName);
	// 根据用户信息获取摄像头信息列表
	long GetCameraListByZoneAndUserInfo(CVideoCameraList* plistCamera, 
		const char* pszUserName, const char* pszGroupName, long lZoneID);
	// 根据用户信息获取某一分区下的监视器信息列表
	long GetMonitorListByZoneAndUserInfo(CVideoMonitorList* plistMonitor,  const char* pszUserName, 
													 const char* pszGroupName, const long lZoneID);
	// 根据用户信息获取分区信息列表
	long GetZoneCamMonListByUserInfo(CVideoZoneList* plistZone, CVideoCameraList* plistCam,
											CVideoMonitorList* plistMon, const char* pszUserName, const char* pszGroupName);
	// 根据用户信息获取分区信息列表								
	long GetZoneListByUserInfo(CVideoZoneList* plistZone, const char* pszUserName, const char* pszGroupName);
	// 根据用户信息添加抓拍图片
	long AddSnapPictureByUserInfo(const long lCamID, CVideoSnapFile* pSnapFIle, const char* pszPicBuf, const long nPicBufSize,
										 const char* pszUserName, const char* pszGroupName, long &nSnapID);
	// 根据用户信息删除抓拍信息
	long DelSnapPicInfoByUser(const long lCamID, const long lSnapID, const char* pszUserName, const char* pszGroupName);
	// 根据用户信息修改抓拍信息
	long ModifySnapPicInfoByUser(const long lCamID, const long lSnapID, CVideoSnapFile* pSnapFile, const char* pszUserName, const char* pszGroupName);
	// 根据用户信息查询抓拍图片信息
	long QuerySnapPictureByUserInfo(const char* pZone, const long lCamID, time_t tStart, time_t tEnd, const char* pSnapType,
										   CVideoSnapFileList* pSnapFileList, const char* pszUserName, const char* pszGroupName);
	// 根据抓拍ID获取摄像头ID
	long GetCameraIDBySnapID(long nSnapID, long &nCamID);
	// 根据用户信息获取抓拍图片内容
	long GetSnapPicBufByUser(const long nSnapID, char *&pBuffer, long &nPicLen, 
										const char* pszUserName, const char* pszGroupName);
	// 根据用户信息修改预置位
	long ModifyPSPOfCameraByUserInfo(const long lCamID, const char* pszOldPSPName, CVideoPSP* pNewPSP, 
												  const char* pszUserName, const char* pszGroupName);
	// 根据用户信息删除预置位
	long DeletePSPOfCameraByUserInfo(const long lCamID, const char* pszPSPName, 
											const char* pszUserName, const char* pszGroupName);
	// 根据用户信息添加预置位
	long AddPSPOfCameraByUserInfo(const long lCamID, CVideoPSP* pPSP, 
										 const char* pszUserName, const char* pszGroupName);
	// 根据用户信息以及摄像头ID获取预置位信息
	long GetPSPListOfCameraByUserInfo(const long lCameraID, CVideoPSPList* plistPSP, 
											 const char* pszUserName, const char* pszGroupName);
	// 根据预置位名称获取预置位信息
	long GetPSPInfoByNameAndCamID(CVideoPSP* pPSP, const char* pszPSPName, long nCameraID);
	// 根据监视器ID得到连接该监视器的设备
	long GetLinkDeviceByMonitorID(const long lMonitorID, CVideoDevice* &pLinkDev, char* szChannel);
	// 获取用户有权限看到的所有自定义分区信息
	long GetCustZoneByUser(const char* pszUserName, CVideoCustomZoneList *plistCustZone);
	// 添加自定义分组
	long AddCustZoneByUser(const char* pszUserName, const char* pszCustZoneName, const char* pszCustZoneDesc);
	// 删除自定义分区
	long DelCustZone(long nCustZoneID);
	// 获取用户有权限看到的某一自定义分区摄像头信息
	long GetCustZoneCamByUser(const char* pszUserName, long nCustZoneID, CVideoCameraList* plistCam);
	// 复制摄像头到自定义分区
	long CpyCustZoneCamByUser(const char* pszUserName, long nCustZoneID, CVideoList<long>* plistCamIndex);
	// 删除自定义分区中的摄像头
	long DelCustZoneCamByUser(const char* pszUserName, const char* pszGroupName, long nCustZoneID, CVideoList<long>* plistCamIndex);
	//解析带逗号的字符串
	char* GetNextToken(char* pszSrc, char* pszDst, long nDstBuffSize, char token=',');
	// 获取矩阵ID
	long GetMatrixIDByMatrixName(const char *szMatrixName, long &lMatrixID);
	// 根据矩阵获取关联的摄像头简单信息
	long GetCameraListByMatrixAndUserInfo(CVideoCameraList* plistCamera, const char* pszUserName, const char* pszGroupName, long lMatrixID);
// 设备级连用到的函数
public:
	// 获取连接本设备输入通道的连接信息
	long GetInputDevLinkList(long nLocalDevID, CReSwitchChannelList &theNextLinkList);
	// 根据两个设备的ID获取他们之间接下来使用的通道
	long GetCurrSwitchChannel(long nNearCamDevID, long nNearMonDevID, char* szNearCamChannel, char* szNearMonChannel);
};

typedef ACE_Singleton<CVideoCfg, ACE_Null_Mutex> CVideoCfgSingleton;
#define VIDEO_CONFIG CVideoCfgSingleton::instance()

#endif // !defined(AFX_VIDEOCFG_H__EB621F0D_775C_415A_B0A2_B5810E6B3945__INCLUDED_)
