/**************************************************************
 *  Filename:    VideoDBOperation.h
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: 视频监控系统数据库操作头文件.
 *
 *  @author:     xulizai
 *  @version     2008/05/16  xulizai  Initial Version
 *				 08/31/2009  chenzhan  增加录像片段管理.
			     09/08/2009  chenzhan     添加录像抓拍、图片累加功能
				 05/03/2010  chenzhan  优化摄像机列表添加函数.
				 14/04/2010  chenzhan  增加设备级连控制当前通道号数据库表.
**************************************************************/

#if !defined(AFX_VIDEODBLIBRARY_H__59BB89C6_FBC2_42C5_95CB_4D3D5910AA4D__INCLUDED_)
#define AFX_VIDEODBLIBRARY_H__59BB89C6_FBC2_42C5_95CB_4D3D5910AA4D__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <map>
#include "VideoDefine.h"
#include "VideoStructDef.h"
#ifdef _WIN32
#	ifdef videodblibrary_EXPORTS
#		define VIDEODB_API __declspec(dllexport)
#	else
#		define VIDEODB_API __declspec(dllimport)
#	endif
#else//_WIN32
#	define VIDEODB_API
#endif//_WIN32

class CppSQLite3DB;
class VIDEODB_API CVideoDBLibrary  
{
private:
	//为国际化而定义,之前在VideoDefineMacro.h中定义,被注释掉的为未使用的宏，暂时保留
	//string m_strVideoSystemTips;
	string m_strVideoDefaultZoneName;
	//string m_strVideoAllZoneDefaultName;
	//string m_strVideoSystemGlobalZone;
	//string m_strVideoSystemUserZone;
	//string m_strAllSnaptype;
	//string m_strAllCamera;

	string m_strInsertMatrixType;
	string m_strInsertDvrType;
	string m_strInsertEncoderType;
	string m_strInsertDecoderType;
	string m_strInsertCameraType;
	string m_strInsertMonitorType;
	string m_strInsertNvrType;

	string m_strHaikangWeishi1;
	string m_strHaikangWeishi2;
	string m_strHaikangRecorder1;
	string m_strHaikangRecorder2;
	string m_strADMatrix;
	string m_strHaikangrRcorder3;
	string m_strHuasanRecorder;
	string m_strDahuaRecorder;
private:
	CppSQLite3DB* m_pdbCfg;
	CppSQLite3DB* m_pdbInfo;
	bool CreateCfgTable(const char *szSQL, const char *szTable);
	bool CreateInfoTable(const char *szSQL, const char *szTable);
	bool CreateDBCfg();
	bool CreateDBInfo();
	bool IsServerExist(const long nServerID);
	bool IsZoneExist(const long nZoneID);
	bool IsDeviceTypeExist(const long nDeviceTypeID);
	bool IsProductExist(const long nProductID);
	bool IsDeviceExist(const long nDeviceID);
	bool IsPspExist(const long nCamID, const char* pszPSPName);
	long GetChannelID(const long nDeviceID, const char* szChannel);
	long AddCameraLinkDevice(const long nChannelID, const long nCameraID, const bool bCtrl, const bool bSrc, const bool bHis);
	long AddChannel(const long nDeviceID, const char* szChannel);
	long SetLinkChannel(const long nChannelID, const long nLinkChannelID);
	long DeleteCameraLinkDeviceByCameraID(const long nCameraID);
	long DeleteCameraLinkDeviceByChannelID(const long nChannelID);
	long SetMonitorLinkDeviceNullByChannelID(const long nChannelID);
	long SetDeviceLinkChannelNullByLinkChannelID(const long nChannelID);
	long SetDeviceLinkChannelNullByDeviceID(const long nDeviceID);
	long DeleteChannelByChannelID(const long nChannelID);
	long DeleteChannelByDeviceID(const long nDeviceID);
	long SetCameraZoneDefaultByZoneID(const long nZoneID);
	long SetMonitorZoneDefaultByZoneID(const long nZoneID);
	// 带事务处理的函数
	long AddCameraList(CVideoCameraList* pCamList);
	long AddCamera(CVideoCamera* pCamera);
	long UpdateCamera(CVideoCamera* pNewCamera, const long nOldCameraID, bool bLinkChange);
	long UpdateCameraZone(CVideoList<long>* plistCameraID, const long nZoneID);
	long DeleteCamera(const long nCameraID);
	long UpdateMonitorZone(CVideoList<long>* plistMonitorID, const long nZoneID);
	long DeleteServer(const long nServerID);
	long DeleteZone(const long nZoneID);
	long AddDevice(CVideoDevice* pDevice);
	long UpdateDevice(CVideoDevice* pNewDevice, const long nOldDeviceID, bool bLinkChange);
	long DeleteProduct(const long nProductID);
    long AddNVRDeviceType();
public:
	CVideoDBLibrary();
	virtual ~CVideoDBLibrary();
	/*///////////////////////////////////////////////////////
	初始化字符串，为国际化使用
	参数说明:
	[in]const vector<string>& strVector			字符串数组
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoInitString(IN const vector<string>& strVector);
	/*///////////////////////////////////////////////////////
	初始化配置数据库
	参数说明:
	[in]const char* pszDBFileName			数据库文件名(包含路径)
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoInitCfgDB(IN const char* pszDBFileName);

	/*///////////////////////////////////////////////////////
	初始化过程信息数据库
	参数说明:
	[in]const char* pszDBFileName			数据库文件名(包含路径)
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoInitInfoDB(IN const char* pszDBFileName);

	/*///////////////////////////////////////////////////////
	退出配置信息数据库
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoExitCfgDB();

	/*///////////////////////////////////////////////////////
	退出过程信息数据库
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoExitInfoDB();

	/*///////////////////////////////////////////////////////
	读取摄像头信息
	参数说明:
	[out]CVideoCameraList* plistCamera		摄像头列表
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoReadCameraList(OUT CVideoCameraList* plistCamera);

	/*///////////////////////////////////////////////////////
	添加摄像头列表
	参数说明:
	[in]CVideoCameraList* pCamList			摄像头信息
	返回值:
	ICV_SUCCESS(成功)
	说明: 摄像头ID在添加成功后自动更新
	*////////////////////////////////////////////////////////
	long VideoAddCameraList(IN CVideoCameraList* pCamList);

	/*///////////////////////////////////////////////////////
	添加摄像头
	参数说明:
	[in]CVideoCamera* pCamera				摄像头信息
	返回值:
	ICV_SUCCESS(成功)
	说明: 摄像头ID在添加成功后自动更新
	*////////////////////////////////////////////////////////
	long VideoAddCamera(IN CVideoCamera* pCamera);

	/*///////////////////////////////////////////////////////
	修改摄像头
	参数说明:
	[in]CVideoCamera* pNewCamera			新的摄像头信息
	[in]const long nOldCameraID				被修改的摄像头ID
	[in]bool bLinkChange=false				连接信息是否发生改变
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoUpdateCamera(IN CVideoCamera* pNewCamera, IN const long nOldCameraID, IN bool bLinkChange=false);

	/*///////////////////////////////////////////////////////
	修改摄像头的分区
	参数说明:
	[in]CVideoList<long>* plistCameraID		被修改的摄像头ID列表
	[in]const long nZoneID					分区ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoUpdateCameraZone(IN CVideoList<long>* plistCameraID, IN const long nZoneID);

	/*///////////////////////////////////////////////////////
	删除摄像头
	参数说明:
	[in]const long nCameraID				被删除的摄像头ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoDeleteCamera(IN const long nCameraID);

	/*///////////////////////////////////////////////////////
	删除某一视频服务下的所有摄像头
	参数说明:
	[in]const long nServerID				视频服务ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoDeleteCameraByServerID(IN const long nServerID);

	/*///////////////////////////////////////////////////////
	读取监视器
	参数说明:
	[out]CVideoMonitorList* plistMonitor	监视器列表
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoReadMonitorList(OUT CVideoMonitorList* plistMonitor);

	/*///////////////////////////////////////////////////////
	添加监视器
	参数说明:
	[in/out]CVideoMonitor* pMonitor			监视器信息
	返回值:
	ICV_SUCCESS(成功)
	说明: 监视器ID在添加成功后自动更新
	*////////////////////////////////////////////////////////
	long VideoAddMonitor(OPT CVideoMonitor* pMonitor);

	/*///////////////////////////////////////////////////////
	修改监视器
	参数说明:
	[in]CVideoMonitor* pNewMonitor			新的监视器信息
	[in]const long nOldMonitorID			被修改的监视器ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoUpdateMonitor(IN CVideoMonitor* pNewMonitor, IN const long nOldMonitorID);

	/*///////////////////////////////////////////////////////
	修改监视器的分区
	参数说明:
	[in]CVideoList<long>* plistMonitorID	被修改的监视器ID列表
	[in]const long nZoneID					分区ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoUpdateMonitorZone(IN CVideoList<long>* plistMonitorID, IN const long nZoneID);
	
	/*///////////////////////////////////////////////////////
	删除监视器
	参数说明:
	[in]const long nMonitorID				被删除的监视器ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoDeleteMonitor(IN const long nMonitorID);
	
	/*///////////////////////////////////////////////////////
	删除某一视频服务下的所有监视器
	参数说明:
	[in]const long nServerID				视频服务ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoDeleteMonitorByServerID(IN const long nServerID);

	/*///////////////////////////////////////////////////////
	读取视频服务
	参数说明:
	[out]CVideoServerList* plistServer		视频服务列表
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoReadServerList(OUT CVideoServerList* plistServer);

	/*///////////////////////////////////////////////////////
	添加视频服务
	参数说明:
	[in/out]CVideoServer* pServer			视频服务信息
	返回值:
	ICV_SUCCESS(成功)
	说明: 视频服务ID在添加成功后自动更新
	*////////////////////////////////////////////////////////
	long VideoAddServer(OPT CVideoServer* pServer);

	/*///////////////////////////////////////////////////////
	修改视频服务
	参数说明:
	[in]CVideoServer* pNewServer			新的视频服务信息
	[in]const long nOldServerID				被修改的视频服务ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoUpdateServer(IN CVideoServer* pNewServer, IN const long nOldServerID);

	/*///////////////////////////////////////////////////////
	删除视频服务
	参数说明:
	[in]const long nServerID				被删除的视频服务ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoDeleteServer(IN const long nServerID);

	/*///////////////////////////////////////////////////////
	读取分区
	参数说明:
	[out]CVideoZoneList* plistZone			分区列表
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoReadZoneList(OUT CVideoZoneList* plistZone);
	
	/*///////////////////////////////////////////////////////
	添加分区
	参数说明:
	[in/out]CVideoZone* pZone				分区信息
	返回值:
	ICV_SUCCESS(成功)
	说明: 分区ID在添加成功后自动更新
	*////////////////////////////////////////////////////////
	long VideoAddZone(OPT CVideoZone* pZone);

	/*///////////////////////////////////////////////////////
	修改分区
	参数说明:
	[in]CVideoZone* pNewZone				新的分区信息
	[in]const long nOldZoneID				被修改的分区ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoUpdateZone(IN CVideoZone* pNewZone, IN const long nOldZoneID);	

	/*///////////////////////////////////////////////////////
	删除分区
	参数说明:
	[in]const long nZoneID					被删除的分区ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoDeleteZone(IN const long nZoneID);

	/*///////////////////////////////////////////////////////
	读取设备
	参数说明:
	[out]CVideoDeviceList* plistDevice		设备列表
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoReadDeviceList(OUT CVideoDeviceList* plistDevice);
	
	/*///////////////////////////////////////////////////////
	添加设备
	参数说明:
	[in/out]CVideoDevice* pDevice			设备信息
	返回值:
	ICV_SUCCESS(成功)
	说明: 设备ID在添加成功后自动更新
	*////////////////////////////////////////////////////////
	long VideoAddDevice(OPT CVideoDevice* pDevice);

    ///////以下3个函数为NVR功能而新加的 huangming 2012-05-16//////////
    long VideoAddNVRDeviceType();            //添加“网络视频服务器”（NVR）设备类型
    long GetCfgDBVersion(int& nCfgVersion);  //获取数据库文件版本号
    long UpdateCfgDBVersion(int nCfgVersion);//更新包含“网络视频服务器”（NVR）设备类型的数据库文件版本
    long UpdateCfgDB();//默认升级：加入默认支持的设备类型信息

	/*///////////////////////////////////////////////////////
	修改设备
	参数说明:
	[in]CVideoDevice* pNewDevice			新的设备信息
	[in]const long nOldDeviceID				被修改的设备ID
	[in]bool bLinkChange=false				连接设备信息是否发生改变
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoUpdateDevice(IN CVideoDevice* pNewDevice, IN const long nOldDeviceID, IN bool bLinkChange=false);
	
	/*///////////////////////////////////////////////////////
	删除设备
	参数说明:
	[in]const long nDeviceID				被删除的设备ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoDeleteDevice(IN const long nDeviceID);

	/*///////////////////////////////////////////////////////
	删除某一视频服务下的所有设备
	参数说明:
	[in]const long nServerID				视频服务ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoDeleteDeviceByServerID(IN const long nServerID);
	
	/*///////////////////////////////////////////////////////
	删除某一产品型号的所有设备
	参数说明:
	[in]const long nProductID					设备产品型号ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoDeleteDeviceByProductID(IN const long nProductID);

	/*///////////////////////////////////////////////////////
	读取设备类型
	参数说明:
	[out]CIDMapTextList* plistDeviceType	设备类型list
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoReadDeviceTypeList(OUT CVideoDevTypeList* plistDeviceType);

	/*///////////////////////////////////////////////////////
	读取设备产品型号
	参数说明:
	[out]CVideoProductList* plistProduct		产品型号列表
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoReadProductList(OUT CVideoProductList* plistProduct);
	
	/*///////////////////////////////////////////////////////
	添加设备产品型号
	参数说明:
	[in/out]CVideoProduct* pProduct				设备产品型号信息
	返回值:
	ICV_SUCCESS(成功)
	说明: 设备产品型号ID在添加成功后自动更新
	*////////////////////////////////////////////////////////
	long VideoAddProduct(OPT CVideoProduct* pProduct);

	/*///////////////////////////////////////////////////////
	修改设备产品型号
	参数说明:
	[in]CVideoProduct* pNewProduct				新的设备产品型号信息
	[in]const long nOldProductID				被修改的设备产品型号ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoUpdateProduct(IN CVideoProduct* pNewProduct, IN const long nOldProductID);

	/*///////////////////////////////////////////////////////
	删除设备产品型号
	参数说明:
	[in]const long nProductID					被删除的设备产品型号ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoDeleteProduct(IN const long nProductID);

	/*///////////////////////////////////////////////////////
	读取抓拍信息
	参数说明:
	[out]CIDMapTextList* plistSnapTypeInfo	抓拍信息list
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoReadSnapTypeInfoList(OUT CIDMapTextList* plistSnapTypeInfo);

	/*///////////////////////////////////////////////////////
	添加抓拍信息
	参数说明:
	[in]const char* pszSnapTypeInfo			抓拍信息
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoAddSnapTypeInfo(IN const char* pszSnapTypeInfo);
	
	/*///////////////////////////////////////////////////////
	修改抓拍信息
	参数说明:
	[in]const char* pszNewSnapTypeInfo		新的抓拍信息
	[in]const char* pszOldSnapTypeInfo		被修改的抓拍信息
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoUpdateSnapTypeInfo(IN const char* pszNewSnapTypeInfo, IN const char* pszOldSnapTypeInfo);
	
	/*///////////////////////////////////////////////////////
	删除抓拍信息
	参数说明:
	[in]const char* pszSnapTypeInfo			被删除的抓拍信息
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoDeleteSnapTypeInfo(IN const char* pszSnapTypeInfo);	

	/*///////////////////////////////////////////////////////
	读取某个摄像头的预置位
	参数说明:
	[out]CVideoPSPList* plistPSP			预置位列表
	[in]const long nCameraID				摄像头ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoReadPSPListOfCamera(OUT CVideoPSPList* plistPSP, IN const long nCameraID);

	/*///////////////////////////////////////////////////////
	添加预置位
	参数说明:
	[in]CVideoPSP* pPSP						预置位信息
	[in]const long nCameraID				摄像头ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoAddPSPOfCamera(IN CVideoPSP* pPSP, IN const long nCameraID);
	
	/*///////////////////////////////////////////////////////
	修改预置位
	参数说明:
	[in]CVideoPSP* pNewPSP					新的预置位信息
	[in]const char* pszOldPSPName			被修改的预置位名称
	[in]const long nCameraID				摄像头ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoUpdatePSPOfCamera(IN CVideoPSP* pNewPSP, IN const char* pszOldPSPName, IN const long nCameraID);	

	/*///////////////////////////////////////////////////////
	删除预置位
	参数说明:
	[in]const char* pszPSPName				被删除的预置位名称
	[in]const long nCameraID				摄像头ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoDeletePSPOfCamera(IN const char* pszPSPName, IN const long nCameraID);	

	/*///////////////////////////////////////////////////////
	读取模式
	参数说明:
	[out]CVideoModeList* plistMode			模式列表
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoReadModeList(OUT CVideoModeList* plistMode);

	/*///////////////////////////////////////////////////////
	根据模式名称读取模式信息
	参数说明:
	[in]const char* pszModeName		模式名称
	[out]CVideoMode* pMode			模式信息
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoReadModeByName(IN const char* pszModeName, OUT CVideoMode* pMode);

	/*///////////////////////////////////////////////////////
	添加模式
	参数说明:
	[in]CVideoMode* pMode			模式信息
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoAddMode(IN CVideoMode* pMode);

	/*///////////////////////////////////////////////////////
	修改模式
	参数说明:
	[in]CVideoMode* pNewMode				新的模式信息
	[in]const char* pszOldModeName			被修改的模式名称
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoUpdateMode(IN CVideoMode* pNewMode, IN const char* pszOldModeName, const bool bParaChange);

	/*///////////////////////////////////////////////////////
	删除模式
	参数说明:
	[in]const char* pszModeName				被删除的模式名称
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoDeleteMode(IN const char* pszModeName);

	/*///////////////////////////////////////////////////////
	添加抓拍图片
	参数说明:
	[in]CVideoSnapFile* pSnapPic			抓拍图片信息
	[in]const char* pszPicBuf				图片buffer
	[in]const long nSize					图片buffer长度
	[in]CIDMapTextList* plistSnapType		抓拍类型列表
	[out]long &nSnapID						抓拍ID
	返回值:
	ICV_SUCCESS(成功)
	说明: 若为连续抓拍图片,则图片buffer按照如下格式:
	      长度内容长度内容长度内容...
	*////////////////////////////////////////////////////////
	long VideoAddSnapPicture(IN CVideoSnapFile* pSnapPic, IN const char* pszPicBuf, IN const long nSize, IN CIDMapTextList* plistSnapType, OUT long &nSnapID);

	/*///////////////////////////////////////////////////////
	// 读取抓拍图片信息
	参数说明:
	[out]CVideoSnapFileList* plistSnapPic	抓拍图片信息列表
	[in]CIDMapTextList* plistSnapType		抓拍类型列表
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoReadSnapPictureList(OUT CVideoSnapFileList* plistSnapPic, IN CIDMapTextList* plistSnapType);

	/*///////////////////////////////////////////////////////
	// 读取抓拍图片内容
	参数说明:
	[in]const long nSnapID					抓拍ID
	[out]long &nCamID						摄像头ID
	[out]long &nCount						抓拍张数
	[out]char *&pBuffer						图片内容
	[out]long &nPicLen						内容长度
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoReadSnapPicBuffer(IN const long nSnapID, OUT long &nCamID, OUT long &nCount, OUT char *&pBuffer, OUT long &nPicLen);

	/*///////////////////////////////////////////////////////
	// 修改抓拍图片
	参数说明:
	[in]CVideoSnapFile* pNewSnapPic			新抓拍图片信息
	[in]const long nSnapPicID				图片ID
	[in]CIDMapTextList* plistSnapType		抓拍类型
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoUpdateSnapPicture(IN CVideoSnapFile* pNewSnapPic, IN const long nSnapPicID, IN CIDMapTextList* plistSnapType);
	
	/*///////////////////////////////////////////////////////
	// 删除抓拍图片
	参数说明:
	[in]const long nSnapPicID				图片ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoDeleteSnapPicture(IN const long nSnapPicID);

	/*///////////////////////////////////////////////////////
	// 累加抓拍图片
	参数说明:
	[in]const long nCaptureID				抓拍ID
	[in]const char* pszPicBuff				图片buffer
	[in]const long nSize					图片buffer长度
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoCapturePicAddTo(IN const long nCaptureID, IN const char* pszPicBuff, IN const long nSize);
		
	/*///////////////////////////////////////////////////////
	// 读取自定义分区列表(需求用例ID:113179,113180,113181)
	参数说明:
	[in]const char* pszUserName				自定义分区名称
	[out]CVideoCustomZoneList *plistCustZone	自定义分区列表
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoReadCustomZone(IN const char* pszUserName, OUT CVideoCustomZoneList *plistCustZone);
		
	/*///////////////////////////////////////////////////////
	// 添加自定义分区(需求用例ID:113179,113180,113181)
	参数说明:
	[in]const char* pszUserName				用户名称
	[in]const char* pszCustZoneName			自定义分区名称
	[in]const char* pszCustZoneDesc			自定义分区描述
	[out]long &nID							返回的自定义分区ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoAddCustomZone(IN const char* pszUserName, IN const char* pszCustZoneName, IN const char* pszCustZoneDesc, OUT long &nID);
		
	/*///////////////////////////////////////////////////////
	// 修改自定义分区(需求用例ID:113179,113180,113181)
	参数说明:
	[in]CVideoCustomZone* pNewCustZone		新的自定义分区
	[in]const long nCustZoneID				自定义分区ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoUpdateCustomZone(IN CVideoCustomZone* pNewCustZone, IN const long nCustZoneID);
		
	/*///////////////////////////////////////////////////////
	// 删除自定义分区(需求用例ID:113179,113180,113181)
	参数说明:
	[in]const long nCustZoneID				自定义分区ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoDeleteCustomZone(IN const long nCustZoneID);
		
	/*///////////////////////////////////////////////////////
	// 读取摄像头索引(需求用例ID:113179,113180,113181)
	参数说明:
	[in]const long nCustZoneID				自定义分区ID
	[out]CVideoList<long>* plistCameraID	摄像头ID列表
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoReadCamIndex(OUT CVideoList<long>* plistCameraID, IN const long nCustZoneID);
		
	/*///////////////////////////////////////////////////////
	// 添加摄像头索引(需求用例ID:113179,113180,113181)
	参数说明:
	[in]const long nCustZoneID				自定义分区ID
	[in]CVideoList<long>* plistCameraID		摄像头ID列表
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoAddCamIndex(IN CVideoList<long>* plistCameraID, IN const long nCustZoneID);
		
	/*///////////////////////////////////////////////////////
	// 删除摄像头索引(需求用例ID:113179,113180,113181)
	参数说明:
	[in]const long nCustZoneID				自定义分区ID
	[in]CVideoList<long>* plistCameraID		摄像头ID列表
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoDeleteCamIndex(IN CVideoList<long>* plistCameraID, IN const long nCustZoneID);

	/*///////////////////////////////////////////////////////
	// 添加录像
	参数说明:
	[in]CRecord *pRec						录像内容
	[out]long &nRecordID					录像ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoAddRecord(IN CRecord *pRec, OUT long &nRecordID);

	/*///////////////////////////////////////////////////////
	// 获取录像中摄像头ID
	参数说明:
	[in]const long nRecordID				录像ID
	[out]CRecord* pRec						录像信息
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoGetRecord(IN const long nRecordID, OUT CRecord* pRec);

	/*///////////////////////////////////////////////////////
	// 修改录像信息
	参数说明:
	[in]const long nRecordID				录像ID
	[in]CRecord* pRec						录像信息
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoModifyRecord(IN const long nRecordID, IN CRecord* pRec);

	/*///////////////////////////////////////////////////////
	// 查询录像信息
	参数说明:
	[out]CRecordList *plistRec				录像信息列表
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoReadRecordList(OUT CRecordList *plistRec);

	/*///////////////////////////////////////////////////////
	// 修改录像信息
	参数说明:
	[in]const long nRecordID				录像ID
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoDeleteRecord(IN const long nRecordID);

	/*///////////////////////////////////////////////////////
	// 查询上次使用的通道
	参数说明:
	[in]const long nNearCamDevID			离摄像机近端的设备ID
	[in]const long nNearMonDevID			离监视器近端的设备ID
	[out]char* szNearMonChannel				离监视器近端的输入通道 VIDEO_CHANNEL_MAXSIZE
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoQueryLinkChannel(IN const long nNearCamDevID, IN const long nNearMonDevID, OUT char* szNearMonChannel);
	
	/*///////////////////////////////////////////////////////
	// 更新正在使用的通道
	参数说明:
	[in]const long nNearCamDevID			离摄像机近端的设备ID
	[in]const long nNearMonDevID			离监视器近端的设备ID
	[in]const char* szNearMonChannel			离监视器近端的输入通道 VIDEO_CHANNEL_MAXSIZE
	返回值:
	ICV_SUCCESS(成功)
	*////////////////////////////////////////////////////////
	long VideoUpdateLinkChannel(IN const long nNearCamDevID, IN const long nNearMonDevID, IN const char* szNearMonChannel);
};

#endif // !defined(AFX_VIDEODBLIBRARY_H__59BB89C6_FBC2_42C5_95CB_4D3D5910AA4D__INCLUDED_)
