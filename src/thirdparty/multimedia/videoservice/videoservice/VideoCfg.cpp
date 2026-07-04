/**************************************************************
 *  Filename:    VideoCfg.cpp
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: 配置信息读写文件.
 *
 *  @author:     chenzhan
 *  @version     04/21/2009  chenzhan  Initial Version
 *  @version     04/21/2009  chenzhan  增加摄像头分组.
 *				 08/31/2009  chenzhan  增加录像片段管理.
				 09/28/2009  chenzhan  数据库的返回码不在服务里面处理，所有的数据库错误码以EC_ICV_CCTV_SQLITE_ERROR代替,数据库执行成功必须以0返回
				 10/23/2009  chenzhan  解析带逗号的字符串.
				 14/04/2010  chenzhan  设备级连视频切换.
				 04/20/2010  chenzhan  修改设备级连算法,增加视频切换的数据结构
				 06/23/2010  zhucongfeng  删除CVCommon的声明，直接用后台导出的定义
 * @version      06/22/2011  chenzhiquan   修改IP长度判断，解决ip为xxx.xxx.xxx.xxx时判断IP不合法问题
**************************************************************/
// VideoCfg.cpp: implementation of the CVideoCfg class.
//
//////////////////////////////////////////////////////////////////////

#include "VideoCfg.h"
#include "VideoSvcDef.h"
#include "multimedia/video/VideoAuth.h"
#include "gettext/libintl.h"

#define _(STRING) gettext(STRING)

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CVideoCfg::CVideoCfg()
{
	m_uListenPort = ICV_VIDEO_DEFAULT_TCPPORT;
	m_lLocalSvrID = VIDEO_ID_INVALIDATION;
	m_listSvr.clear();
	m_listZone.clear();
	m_listProduct.clear();
	m_listDev.clear();
	m_listMon.clear();
	m_lAutoLockSeconds = VIDEO_DEFAULT_LOCK_TIME;
	memset(m_szLocalIP, 0x00, sizeof(m_szLocalIP));
	CVStringHelper::Safe_StrNCpy(m_szLocalIP, "127.0.0.1", sizeof(m_szLocalIP));

	// 下面三个最近时间都是从当前开始计时
	time(&m_lLastModeChgTime);	// // 数据库中模式最新修改过时间。刚刚启动时为系统时间，便于客户端查询是否需要获取所有配置
	m_lLastAllCfgChgTime = m_lLastPspChgTime = m_lLastModeChgTime;		// 数据库中预置位最新修改时间
}

CVideoCfg::~CVideoCfg()
{
	m_listSvr.clear();
	m_listZone.clear();
	m_listProduct.clear();
	m_listDev.clear();
	m_listMon.clear();

	// UnLoadAllConfig();
}

/**
 *  读取XML配置文件的信息，得到本地IP地址
<?xml version="1.0" encoding="GB2312" ?>
<VideoServer LocalIP="127.0.0.1" />
 *
 *	@return $(bool) true成功，false失败.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CVideoCfg::LoadXmlConfigInfo()
{
	long lRet = ICV_SUCCESS;
	char szXmlCfgPath[ICV_LONGFILENAME_MAXLEN];
	memset(szXmlCfgPath, 0, sizeof(szXmlCfgPath));

	// 获取配置目录
	CVStringHelper::Safe_StrNCpy(szXmlCfgPath, CVComm.GetCVProjCfgPath(), sizeof(szXmlCfgPath));
	
	// 获取配置文件名称
	if(strlen(szXmlCfgPath) + strlen(VIDEO_LOCAL_SERVER_FILE_NAME) >= ICV_LONGFILENAME_MAXLEN - 1)
	{
		lRet = EC_ICV_CCTV_FILEPATHNAMETOOLONG;
		CVLog.LogErrMessage(lRet, _("Config file name<%s\\%s> length exceed maximum length<%d>."),
									szXmlCfgPath, VIDEO_LOCAL_SERVER_FILE_NAME, ICV_LONGFILENAME_MAXLEN - 1);
		return lRet;
	}
	strcat(szXmlCfgPath, ACE_DIRECTORY_SEPARATOR_STR);
	strcat(szXmlCfgPath, VIDEO_LOCAL_SERVER_FILE_NAME);

	// 建立tinyxml的文档对象
	TiXmlDocument doc;

	// 解析xml文件名
	if(!doc.LoadFile(szXmlCfgPath))
	{
		lRet = EC_ICV_CCTV_FAILTOLOADXMLCFGFILE;
		CVLog.LogErrMessage(lRet, _("Load config file(%s) error."), szXmlCfgPath);
		return lRet;
	}

	// 解析文件
	TiXmlElement *pRootElem = doc.RootElement();
	// 确认节点在xml文件里存在
	if(pRootElem == NULL)
	{
		lRet = EC_ICV_CCTV_FAILTOPARSEXMLCFGFILE;
		CVLog.LogErrMessage(lRet, _("Parse config file(%s) information error, cann't find node 'VideoServer'."), szXmlCfgPath);
		return lRet;
	}

	// 获取本机IP地址，验证合法性
	const char* szLocalIP = pRootElem->Attribute(VIDEO_SERVER_NODE_LOCALIP);
	// * @version 06/22/2011   chenzhiquan   修改IP长度判断，解决ip为xxx.xxx.xxx.xxx时判断IP不合法问题
	if(!szLocalIP || strcmp(szLocalIP, "") == 0 || strlen(szLocalIP) >= ICV_IPSTRING_MAXLEN)
	{
		lRet = EC_ICV_CCTV_FAILTOPARSEXMLCFGFILE;
		CVLog.LogErrMessage(lRet, _("Get local machine IP(%s) failed, cann't find %s in config file."), 
							VIDEO_STRING(szLocalIP), VIDEO_SERVER_NODE_LOCALIP);
		return lRet;
	}
	
	// 若IP地址不为空
	CVStringHelper::Safe_StrNCpy(m_szLocalIP, szLocalIP, sizeof(m_szLocalIP));

	CVLog.LogMessage(LOG_LEVEL_INFO, _("Read config files(%s) successfully."), szXmlCfgPath);
	
	return lRet;
}

/**
 *  $(删除所有的配置信息).
 *
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CVideoCfg::UnLoadAllConfig()
{
	m_listSvr.clear();					// 视频服务列表
	m_listZone.clear();					// 分区列表
	m_listProduct.clear();				// 设备产品型号列表
	m_listDev.clear();					// 视频设备列表
	
	m_listMon.clear();				// 监视器列表
	m_listSnapType.clear();				// 抓拍类型列表
	
	// 形成Camera map结构
	for(CVideoCameraMap::iterator itCam = m_mapCam.begin(); itCam != m_mapCam.end(); itCam ++)
	{
		CVideoCameraEx *pCamEx = itCam->second;
		SAFE_DELETE(pCamEx);
	}
	m_mapCam.clear();

	long lRet = m_videoDB.VideoExitInfoDB();
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogMessage(LOG_LEVEL_CRITICAL, _("VideoExitInfoDB failed!"));
		return EC_ICV_CCTV_SQLITE_ERROR;
	}
	
	lRet = ExitLockAuth();
	if(lRet != ICV_SUCCESS)
		CVLog.LogMessage(LOG_LEVEL_CRITICAL, _("ExitLockAuth failed!"));

	return lRet;
}

/**
 *  $(读取所有的配置信息).
 *
 *	@return $(bool) true成功，false失败.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CVideoCfg::LoadAllConfig()
{
	// 读取缺省端口
	m_uListenPort = CVComm.GetServicePort(VIDEO_SERVICE_NAME, ICV_VIDEO_DEFAULT_TCPPORT);

	long lRet = ICV_SUCCESS;
	lRet = LoadXmlConfigInfo();
	if(lRet != ICV_SUCCESS)
		return lRet;

	lRet = InitVideoConfigDB();
	if(lRet != ICV_SUCCESS)
		return lRet;

	lRet = InitVideoInfoDB();
	if(lRet != ICV_SUCCESS)
		return lRet;

	lRet = LoadFixConfig();
	if(lRet != ICV_SUCCESS)
	{
		lRet = ExitVideoConfigDB();
		return lRet;
	}
	
	CVLog.LogMessage(LOG_LEVEL_INFO, _("Read all config information successfully!"));
	// 读取完信息之后关闭数据库
	lRet = ExitVideoConfigDB();
	if(lRet != ICV_SUCCESS)
		CVLog.LogMessage(LOG_LEVEL_INFO, _("ExitVideoConfigDB failed!"));

	lRet = InitLockAuth();
	if(lRet != ICV_SUCCESS)
		CVLog.LogMessage(LOG_LEVEL_INFO, _("InitLockAuth failed!"));

	// 设置自动锁定时间
	SetAutoUnLockTime(m_lAutoLockSeconds);
	
	UpdateLastModeChgTime();

	UpdateLastPspChgTime();

	UpdateLastAllCfgChgTime();
	
	return lRet;
}


/**
 *  $(初始化配置数据库).
 *
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CVideoCfg::InitVideoConfigDB()
{
	long lRet = ICV_SUCCESS;
	
	// 获取配置目录
	char szVideoDBPath[ICV_LONGFILENAME_MAXLEN];
	memset(szVideoDBPath, 0, sizeof(szVideoDBPath));
	CVStringHelper::Safe_StrNCpy(szVideoDBPath, CVComm.GetCVProjCfgPath(), sizeof(szVideoDBPath));
	
	// 获取配置文件名称
	if(strlen(szVideoDBPath) + strlen(VIDEO_LOCAL_SERVER_FILE_NAME) >= ICV_LONGFILENAME_MAXLEN - 1)
	{
		lRet = EC_ICV_CCTV_FILEPATHNAMETOOLONG;
		CVLog.LogErrMessage(lRet, _("Config file name<%s\\%s> length exceed the maximum length<%d>."),
									szVideoDBPath, VIDEO_LOCAL_SERVER_FILE_NAME, ICV_LONGFILENAME_MAXLEN - 1);
		return lRet;
	}
	strcat(szVideoDBPath, ACE_DIRECTORY_SEPARATOR_STR);
	strcat(szVideoDBPath, VIDEO_DB_CONFIG_FILE_NAME);

	lRet = m_videoDB.VideoInitCfgDB(szVideoDBPath);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Initialize config database."));
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	return lRet;
}

/**
 *  $(退出配置数据库).
 *
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CVideoCfg::ExitVideoConfigDB()
{
	long lRet = m_videoDB.VideoExitCfgDB();
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Quit config database Error."));
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	CVLog.LogMessage(LOG_LEVEL_INFO, _("Quit config database."));
	return ICV_SUCCESS;
}

/**
 *  $(初始化过程数据库).
 *
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CVideoCfg::InitVideoInfoDB()
{
	long lRet = ICV_SUCCESS;
	
	// 获取配置目录
	char szVideoDBPath[ICV_LONGFILENAME_MAXLEN];
	memset(szVideoDBPath, 0, sizeof(szVideoDBPath));
	CVStringHelper::Safe_StrNCpy(szVideoDBPath, CVComm.GetCVProjCfgPath(), sizeof(szVideoDBPath));
	
	// 获取配置文件名称
	if(strlen(szVideoDBPath) + strlen(VIDEO_LOCAL_SERVER_FILE_NAME) >= ICV_LONGFILENAME_MAXLEN - 1)
	{
		lRet = EC_ICV_CCTV_FILEPATHNAMETOOLONG;
		CVLog.LogErrMessage(lRet, _("Config file name<%s\\%s> length exceed the maximum length<%d>."),
									szVideoDBPath, VIDEO_LOCAL_SERVER_FILE_NAME, ICV_LONGFILENAME_MAXLEN - 1);
		return lRet;
	}
	strcat(szVideoDBPath, ACE_DIRECTORY_SEPARATOR_STR);
	strcat(szVideoDBPath, VIDEO_DB_INFO_FILE_NAME);

	lRet = m_videoDB.VideoInitInfoDB(szVideoDBPath);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Initialize process database Error."));
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	CVLog.LogMessage(LOG_LEVEL_INFO, _("Initialize process database."));

	return ICV_SUCCESS;
}

/**
 *  加载固定配置信息，包括：摄像头、监视器等由配置下发的、在运行过程中不会改变的信息.
 *
 *
 *  @version     09/14/2008  shijunpu  Initial Version.
 */
long CVideoCfg::LoadFixConfig()
{
	// 固定配置在系统各个线程启动前已经启动，因此不需要加锁
	// 读取各个服务器的信息
	long lRet = m_videoDB.VideoReadServerList(&m_listSvr);
	if(ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Read all service information error."));
		return EC_ICV_CCTV_SQLITE_ERROR;
	}
	
	// 找到本地服务器ID
	bool bFindLocalSvr = false;
	for(CVideoServerList::iterator itSvr = m_listSvr.begin(); itSvr != m_listSvr.end(); itSvr ++)
	{
		CVideoServer & vServer = *itSvr;
		if((strcmp(vServer.GetIP(), m_szLocalIP) == 0) 
			|| (vServer.GetIsBak() && strcmp(vServer.GetBakIP(), m_szLocalIP) == 0)
			|| (strcmp(vServer.GetIP(), VIDEO_DEFAULT_IP) == 0))
		{
			m_bBak = vServer.GetIsBak();
			m_lLocalSvrID = vServer.GetID();
			bFindLocalSvr = true;
			m_lAutoLockSeconds = vServer.GetLockSeconds();
			break;
		}
	}
	if(!bFindLocalSvr)
	{
		lRet = EC_ICV_CCTV_LOCALSVRIPNOTMATCH;
		CVLog.LogErrMessage(lRet, _("There is no server information in config database that matching local IP(%s)."), m_szLocalIP);
		return lRet;
	}

	// 读取各个分区的信息
	lRet = m_videoDB.VideoReadZoneList(&m_listZone);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Read each of all zone information."));
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	// 读取各个设备产品型号的信息
	lRet = m_videoDB.VideoReadProductList(&m_listProduct);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Read each of all Product information."));
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	// 读取各个设备的信息
	lRet = m_videoDB.VideoReadDeviceList(&m_listDev);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Read each of all device information."));
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	// 填充cvideomatrixlist的内容
	m_listMatrix.clear();
	for(CVideoDeviceList::iterator itDev = m_listDev.begin(); itDev != m_listDev.end(); itDev ++)
	{
		CVideoDevice &videoDev = *itDev;
		long lDevType = VIDEO_CONFIG->GetProducts()->FindProductbyID(videoDev.GetProductID())->GetDeviceType();
		if (lDevType != VIDEO_DEVICE_TYPE_MATRIX)
			continue;
		// 如果设备是矩阵则查找矩阵关联的设备
		CVideoMatrix mat;
		mat.SetMatrixID(videoDev.GetID());
		mat.InitMatLinkDevList();
		for(CVideoLinkDeviceList::iterator itLinkDev = videoDev.GetLinkDev()->begin(); itLinkDev != videoDev.GetLinkDev()->end(); itLinkDev ++)
		{
			CVideoLinkDevice &linkDev = *itLinkDev;
			long lProductID = VIDEO_CONFIG->GetDeviceList()->FindDevicebyID(linkDev.GetDeviceID())->GetProductID();
			long lLinkDevType = VIDEO_CONFIG->GetProducts()->FindProductbyID(lProductID)->GetDeviceType();
			if (lLinkDevType == VIDEO_DEVICE_TYPE_ENCODER)
			{
				CVideoMatLinkDev *pMatLinkDev = mat.GeMatLinkDevList()->FindMatLinkDevByChannel(linkDev.GetLocalChannel());
				if (pMatLinkDev != NULL)
					pMatLinkDev->GetLinkDeviceList()->InsertObject(linkDev);
				else
				{
					CVideoMatLinkDev matLinkDev;
					matLinkDev.SetLocalChannel(linkDev.GetLocalChannel());
					matLinkDev.SetCamID(VIDEO_ID_INVALIDATION);
					matLinkDev.SetHasEncodeDev(true);
					matLinkDev.SetHasMonitorDev(false);
					matLinkDev.SetIsUsed(false);
					matLinkDev.SetResCount(0);
					matLinkDev.InitLinkDeviceList();
					matLinkDev.InitMonitorList();
					matLinkDev.GetLinkDeviceList()->InsertObject(linkDev);
					mat.GeMatLinkDevList()->InsertObject(matLinkDev);
				}
			}
		}
		m_listMatrix.InsertObject(mat);
	}

	// 读取各个监视器的信息
	lRet = m_videoDB.VideoReadMonitorList(&m_listMon);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Read each of all monitor information."));
		return EC_ICV_CCTV_SQLITE_ERROR;
	}
	// 继续填充cvideomatrixlist的内容
	for(CVideoMonitorList::iterator itMon = m_listMon.begin(); itMon != m_listMon.end(); itMon ++)
	{
		CVideoMonitor &videoMon = *itMon;
		CVideoLinkDevice *pLinkDev = videoMon.GetLinkDev();
		if (pLinkDev->GetDeviceID() != VIDEO_ID_INVALIDATION)
		{
			long lProductID = VIDEO_CONFIG->GetDeviceList()->FindDevicebyID(pLinkDev->GetDeviceID())->GetProductID();
			long lLinkDevType = VIDEO_CONFIG->GetProducts()->FindProductbyID(lProductID)->GetDeviceType();
			if (lLinkDevType == VIDEO_DEVICE_TYPE_MATRIX)
			{
				// 寻找matrix
				CVideoMatrix *pMat = m_listMatrix.FindMatByID(pLinkDev->GetDeviceID());
				if (pMat != NULL)
				{
					CVideoMatLinkDev *pMatLinkDev = pMat->GeMatLinkDevList()->FindMatLinkDevByChannel(pLinkDev->GetChannel());
					if (pMatLinkDev != NULL)
					{
						pMatLinkDev->SetHasMonitorDev(true);
						pMatLinkDev->GetMonitorList()->InsertObject(videoMon);
					}
					else
					{
						CVideoMatLinkDev matLinkDev;
						matLinkDev.SetLocalChannel(pLinkDev->GetChannel());
						matLinkDev.SetCamID(VIDEO_ID_INVALIDATION);
						matLinkDev.SetHasEncodeDev(false);
						matLinkDev.SetHasMonitorDev(true);
						matLinkDev.SetIsUsed(false);
						matLinkDev.SetResCount(0);
						matLinkDev.InitLinkDeviceList();
						matLinkDev.InitMonitorList();
						matLinkDev.GetMonitorList()->InsertObject(videoMon);
						pMat->GeMatLinkDevList()->InsertObject(matLinkDev);
					}
				}
				// 没找到则重新建立一个
				else
				{
					CVideoMatrix mat;
					mat.SetMatrixID(pLinkDev->GetDeviceID());
					mat.InitMatLinkDevList();
					CVideoMatLinkDev matLinkDev;
					matLinkDev.SetLocalChannel(pLinkDev->GetChannel());
					matLinkDev.SetCamID(VIDEO_ID_INVALIDATION);
					matLinkDev.SetHasEncodeDev(false);
					matLinkDev.SetHasMonitorDev(true);
					matLinkDev.SetIsUsed(false);
					matLinkDev.SetResCount(0);
					matLinkDev.InitLinkDeviceList();
					matLinkDev.InitMonitorList();
					matLinkDev.GetMonitorList()->InsertObject(videoMon);
					mat.GeMatLinkDevList()->InsertObject(matLinkDev);
					m_listMatrix.InsertObject(mat);
				}
			}
		}
	}

	// 读取各个抓拍类型的信息
	lRet = m_videoDB.VideoReadSnapTypeInfoList(&m_listSnapType);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Read each of all snap type information."));
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	// 读取各个摄像头的信息。摄像头本身是固定的，但是摄像头对应的预置位是动态改变的
	CVideoCameraList camList;
	lRet = m_videoDB.VideoReadCameraList(&camList);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Read each of all camera information."));
		return EC_ICV_CCTV_SQLITE_ERROR;
	}
	
	// 形成Camera map结构
	for(CVideoCameraList::iterator itCam = camList.begin(); itCam != camList.end(); itCam ++)
	{
		CVideoCamera &camera = *itCam;
		CVideoCameraEx *pCamEx = new CVideoCameraEx(camera);
		pCamEx->SetIsLocalCtrlSvr(false);
		pCamEx->SetCtrlSvr(NULL);
		pCamEx->SetDevCtrlTask(NULL);
		
		// 查找控制设备
		long nCtrlDevID = VIDEO_ID_INVALIDATION;
		// 查找显示设备
		long nSrcDevID = VIDEO_ID_INVALIDATION;
		/*long lChannelID = VIDEO_ID_INVALIDATION;*/
		for(CVideoLinkDeviceList::iterator itLinkDev = camera.GetLinkDev()->begin(); itLinkDev != camera.GetLinkDev()->end(); itLinkDev ++)
		{
			CVideoLinkDevice &linkDev = *itLinkDev;
			if(linkDev.GetIsCtrlDevice())
			{
				nCtrlDevID = linkDev.GetDeviceID();
				break;
			}
			else if (linkDev.GetIsVideoSourceDevice())
			{
				nSrcDevID = linkDev.GetDeviceID();
				break;
			}
			else
				continue;
		} // for(CVideoLinkDeviceList::iterator itLink

		if(nCtrlDevID < 0)
			pCamEx->SetCanCtrl(false);
		else
			pCamEx->SetCanCtrl(true);
		
		// 控制设备信息, 填充摄像头对应的服务器信息
		CVideoDevice *pDev = m_listDev.FindDevicebyID(nCtrlDevID);
		if(pDev)
		{
			int nDevSvrID = pDev->GetServerID();
			// 设置摄像头对应的服务器IP
			pCamEx->SetCtrlSvr(m_listSvr.FindServerbyID(nDevSvrID));
			if(nDevSvrID == m_lLocalSvrID)
			{
				pCamEx->SetIsLocalCtrlSvr(true);
			}
		} // if(pDev)
		// 显示设备信息, 填充摄像头对应的服务器信息
		pDev = m_listDev.FindDevicebyID(nSrcDevID);
		if(pDev)
		{
			int nDevSvrID = pDev->GetServerID();
			// 设置摄像头对应的服务器IP
			pCamEx->SetCtrlSvr(m_listSvr.FindServerbyID(nDevSvrID));
			if(nDevSvrID == m_lLocalSvrID)
			{
				pCamEx->SetIsLocalCtrlSvr(true);
			}
		} // if(pDev)

		// 查找分区对应的授权标签
		CVideoZone *pZone = m_listZone.FindZonebyID(pCamEx->GetZoneID());
		if(pZone)
			pCamEx->SetAuthLabel(pZone->GetAuthLabel());
		else
			pCamEx->SetAuthLabel("");
		
		m_mapCam[pCamEx->GetID()] = pCamEx;
	}

	return lRet;
}

/**
 *  根据摄像头ID得到控制该摄像头的设备.
 *
 *  @param  -[in,out]  const long  nCameraID: [comment]
 *  @param  -[in,out]  CVideoDevice*  pCtrlDev: [comment]
 *  @param  -[in,out]  long&  nChannel: [comment]
 *
 *  @version     09/15/2008  shijunpu  Initial Version.
 */
long CVideoCfg::GetCtrlDeviceByCamID(const long lCamID, CVideoDevice* &pCtrlDev, char* szChannel)
{
	long lRet = ICV_SUCCESS;
	CVideoCameraMap::iterator itCtrDev = m_mapCam.find(lCamID);
	CVideoCameraEx *pCamEx = itCtrDev->second;
	if(itCtrDev == m_mapCam.end() || !pCamEx || !pCamEx->GetLinkDev())
	{
		lRet = EC_ICV_CCTV_CANNOTFINDCAMBYID;
		CVLog.LogErrMessage(lRet, _("Cann't find camera information by camera id(%d)."), lCamID);
		return lRet;
	}

	// 查找控制设备
	long nCtrlDevID = VIDEO_ID_INVALIDATION;
	CVideoLinkDeviceList *pLinkDevList = pCamEx->GetLinkDev();
	for(CVideoLinkDeviceList::iterator itLinkDev = pLinkDevList->begin(); itLinkDev != pLinkDevList->end(); itLinkDev ++)
	{
		CVideoLinkDevice &linkDev = *itLinkDev;
		if(linkDev.GetIsCtrlDevice())
		{
			Safe_CopyString(szChannel, linkDev.GetChannel(), VIDEO_CHANNEL_MAXSIZE);
			nCtrlDevID = linkDev.GetDeviceID();
			break;
		}
	}

	pCtrlDev = m_listDev.FindDevicebyID(nCtrlDevID);
	if(!pCtrlDev)
		return EC_ICV_CCTV_NOCTRLDEVWITHCAM;

	return lRet;
}

/**
 *  冗余中的活动机器状态.
 *
	//RETURN CODE OF REDUNDANCY MANAGEMENT
	#define RM_STATUS_INACTIVE		0
	#define RM_STATUS_ACTIVE		1
	#define RM_STATUS_UNAVALIBLE	2
 *
 *  @version     09/15/2008  shijunpu  Initial Version.
 */
long CVideoCfg::GetVideoRMStatus() // 是否是冗余机器
{
    if (!m_bBak)
        return RM_STATUS_ACTIVE;

	long lStatus = 0;  
	long lRet = GetRMStatus(&lStatus);
	
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("GetRMStatus Error!"));
		return RM_STATUS_UNAVALIBLE;
	}

	return lStatus;
}

// 得到摄像头信息及是否是本地摄像头信息
long CVideoCfg::GetCameraByCamID(long lCamID, CVideoCameraEx *&pCamEx)
{
	long lRet = ICV_SUCCESS;
	pCamEx = NULL;
	CVideoCameraMap::iterator itCam = m_mapCam.find(lCamID);
	if(itCam == m_mapCam.end())
		return EC_ICV_CCTV_CAMERANOTFOUND;

	pCamEx = itCam->second;
	return lRet;
}

// 得到摄像头信息及是否是本地摄像头信息
long CVideoCfg::GetCameraByCamName(const char *szCamName, CVideoCameraEx *&pDestCamEx)
{
	long lRet = ICV_SUCCESS;
	pDestCamEx = NULL;
	for(CVideoCameraMap::iterator itCam = m_mapCam.begin(); itCam != m_mapCam.end(); itCam ++)
	{
		CVideoCameraEx *pCamEx = itCam->second;
		if(strcmp(pCamEx->GetName(), szCamName) == 0)
		{
			pDestCamEx = pCamEx;
			break;
		}
	}

	if(!pDestCamEx)
		return EC_ICV_CCTV_CAMERANOTFOUND;

	return lRet;
}

// 得到摄像头ID
long CVideoCfg::GetCameraIDByCamName(const char *szCamName, long &nCamID)
{
	long lRet = ICV_SUCCESS;
	for(CVideoCameraMap::iterator itCam = m_mapCam.begin(); itCam != m_mapCam.end(); itCam ++)
	{
		CVideoCameraEx *pCamEx = itCam->second;
		if(strcmp(pCamEx->GetName(), szCamName) == 0)
		{
			nCamID = pCamEx->GetID();
			break;
		}
	}

	if(nCamID == VIDEO_ID_INVALIDATION)
		return EC_ICV_CCTV_CAMERANOTFOUND;

	return lRet;
}

// 得到监视器ID
long CVideoCfg::GetMonitorIDByMonName(const char *pMonName, long &nMonID)
{
	long lRet = ICV_SUCCESS;
	for(CVideoMonitorList::iterator itMon = m_listMon.begin(); itMon != m_listMon.end(); itMon ++)
	{
		if(strcmp(itMon->GetName(), pMonName) == 0)
		{
			nMonID = itMon->GetID();
			break;
		}
	}

	if(nMonID == VIDEO_ID_INVALIDATION)
		return EC_ICV_CCTV_MONITORNOTFOUND;

	return lRet;
}

// 得到摄像头信息及是否是本地摄像头信息
long CVideoCfg::GetProductByCamID(long lCamID, CVideoProduct *&pProduct)
{
	long lRet = ICV_SUCCESS;
	CVideoDevice *pCtrlDev = NULL;

	char szTmpChannel[VIDEO_CHANNEL_MAXSIZE];
	memset(szTmpChannel, 0x00, sizeof(szTmpChannel));

	lRet = GetCtrlDeviceByCamID(lCamID, pCtrlDev, szTmpChannel);
	if(lRet != ICV_SUCCESS)
		return lRet;

	long lProductID = pCtrlDev->GetProductID();
	pProduct = m_listProduct.FindProductbyID(lProductID);
	if(!pProduct)
		lRet = EC_ICV_CCTV_CANNOTFINDPRODUCT;
	return lRet;
}

/**
 *  $(根据用户信息获取分区信息列表).
 *
 *  @param  -[out] CVideoZoneList* plistZone: 分区列表.
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CVideoCfg::GetZoneListByUserInfo(CVideoZoneList* plistZone, const char* pszUserName, const char* pszGroupName)
{
	long lRet = ICV_SUCCESS;
	plistZone->clear();
	// 根据权限确定实际的分区列表信息
	for(CVideoZoneList::iterator itZone = m_listZone.begin(); itZone != m_listZone.end(); itZone ++)
	{
		CVideoZone &zone = *itZone;
		long lAuth = 0;
		lRet = VerifyUserRight((char *)pszUserName, (char *)pszGroupName, zone.GetAuthLabel(), &lAuth);
		if(lRet != ICV_SUCCESS || lAuth == 0) // 如果获取授权失败, 或者授权项为0
		{
			CVLog.LogErrMessage(lRet, _("GetZoneListByUserInfo,user:<%s> group:<%s> authlabel:<%s>."),
				pszUserName, pszGroupName, zone.GetAuthLabel());
			continue;
		}

		plistZone->InsertObject(zone);
	}

	return ICV_SUCCESS;
}


/**
 *  $(根据用户信息获取分区信息列表).
 *
 *  @param  -[out] CVideoZoneList* plistZone: 分区列表.
 *  @param  -[out] CVideoCameraList* plistCam: 摄像头列表.
 *  @param  -[out] CVideoMonitorList* plistMon: 监视器列表.
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CVideoCfg::GetZoneCamMonListByUserInfo(CVideoZoneList* plistZone, CVideoCameraList* plistCam,
											CVideoMonitorList* plistMon, const char* pszUserName, const char* pszGroupName)
{
	plistZone->clear();
	plistCam->clear();
	plistMon->clear();
	
	long lAuth = 0;
	int i = 0;
	for(CVideoZoneList::iterator itZone = m_listZone.begin(); itZone != m_listZone.end(); itZone ++)
	{
		CVideoZone &zone = *itZone;
		if (1 == zone.GetID())
			continue;

		long lRet = VerifyUserRight((char *)pszUserName, (char *)pszGroupName, zone.GetAuthLabel(), &lAuth);
		if(lRet != ICV_SUCCESS || lAuth == 0) // 如果获取授权失败, 或者授权项为0
		{
			CVLog.LogErrMessage(lRet, _("GetZoneCamMonListByUserInfo,user:<%s> group:<%s> authlabel:<%s>."),
				pszUserName, pszGroupName, zone.GetAuthLabel());
			continue;
		}

		plistZone->InsertObject(zone);
	}

	VIDEO_CHECKFAIL_RETURN(GetCameraListByZoneAndUserInfo(plistCam, pszUserName, pszGroupName, VIDEO_ID_INVALIDATION));
	VIDEO_CHECKFAIL_RETURN(GetMonitorListByZoneAndUserInfo(plistMon, pszUserName, pszGroupName, VIDEO_ID_INVALIDATION));

	return ICV_SUCCESS;
}

/**
 *  $(根据用户信息获取某一分区下的监视器信息列表).
 *
 *  @param  -[out] CVideoMonitorList* plistMonitor: 监视器列表.
 *  @param  -[in] const long nZoneID: 分区ID.
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CVideoCfg::GetMonitorListByZoneAndUserInfo(CVideoMonitorList* plistMonitor,  const char* pszUserName, 
													 const char* pszGroupName, const long lZoneID)
{
	long lRet = ICV_SUCCESS;
	
	plistMonitor->clear();
	for(CVideoMonitorList::iterator itMon = m_listMon.begin(); itMon != m_listMon.end(); itMon ++)
	{
		CVideoMonitor &monitor = *itMon;
		long lThisZoneID = monitor.GetZoneID();
		
		// 如果需要比较ZoneID, 并且两者不等, 则不插入
		if(lZoneID > 0 && lThisZoneID != lZoneID)
			continue;

		long lAuth = 0;
		CVideoZone *pZone = m_listZone.FindZonebyID(lThisZoneID);
		if(!pZone)
			return EC_ICV_CCTV_ZONENOTEXIST;
		lRet = VerifyUserRight((char *)pszUserName, (char *)pszGroupName, pZone->GetAuthLabel(), &lAuth);
		if(lRet != ICV_SUCCESS || lAuth == 0) // 如果获取授权失败, 或者授权项为0
		{
			CVLog.LogErrMessage(lRet, _("GetMonitorListByZoneAndUserInfo,user:<%s> group:<%s> authlabel:<%s>."),
				pszUserName, pszGroupName, pZone->GetAuthLabel());
			continue;
		}
			
		plistMonitor->InsertObject(monitor);
	}

	return ICV_SUCCESS;
}

/**
 *  $(根据用户信息获取摄像头信息列表).
 *
 *  @param  -[out] CVideoCameraList* plistCamera: 摄像头列表.
 *  @param  -[in] const long nZoneID: 分区ID. 为负数则表示获取所有分区
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CVideoCfg::GetCameraListByZoneAndUserInfo(CVideoCameraList* plistCamera, const char* pszUserName, const char* pszGroupName, long lZoneID)
{
	long lRet = ICV_SUCCESS;
	plistCamera->clear();
	for(CVideoCameraMap::iterator itCam = m_mapCam.begin(); itCam != m_mapCam.end(); itCam ++)
	{
		CVideoCameraEx *pCamEx = itCam->second;
		long lThisZoneID = pCamEx->GetZoneID();

		// 如果需要比较ZoneID, 并且两者不等, 则不插入
		if(lZoneID > 0 && lThisZoneID != lZoneID)
			continue;

		CVideoZone *pZone = m_listZone.FindZonebyID(lThisZoneID);
		if(!pZone)
			return EC_ICV_CCTV_ZONENOTEXIST;

		long lAuth = 0;
		lRet = VerifyUserRight((char *)pszUserName, (char *)pszGroupName, pZone->GetAuthLabel(), &lAuth);
		if(lRet != ICV_SUCCESS || lAuth == 0) // 如果获取授权失败, 或者授权项为0
		{
			CVLog.LogErrMessage(lRet, _("GetCameraListByZoneAndUserInfo,user:<%s> group:<%s> authlabel:<%s>."),
				pszUserName, pszGroupName, pZone->GetAuthLabel());
			continue;
		}

		plistCamera->InsertObject(*(CVideoCamera *)pCamEx);
	}
	return ICV_SUCCESS;
}

/**
 *  $(根据用户信息获取模式信息列表).
 *
 *  @param  -[out] CVideoModeList* plistMode: 模式列表.
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CVideoCfg::GetModeListByUserInfo(CVideoModeList* plistMode, const char* pszUserName, const char* pszGroupName)
{
	plistMode->clear();
	
	// 从数据库读取所有的模式信息
	CVideoModeList listAllMode;
	long lRet = m_videoDB.VideoReadModeList(&listAllMode);
	if(ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Fail to read mode information!"));//读取模式信息失败！
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	// 根据用户权限确定模式 未完成 chenzhan
	for(CVideoModeList::iterator itMod = listAllMode.begin(); itMod != listAllMode.end(); itMod ++)
	{
		plistMode->InsertObject(*itMod);
	}

	return ICV_SUCCESS;
}

/**
 *  $(根据用户信息添加模式).
 *
 *  @param  -[in] CVideoMode* pMode: 模式信息.
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *	@return $(bool) true成功，false失败.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CVideoCfg::AddModeByUserInfo(CVideoMode* pMode, const char* pszUserName, const char* pszGroupName)
{
	long lRet = VerifyUserModeRight(pszUserName, pszGroupName, pMode);
	if(lRet != ICV_SUCCESS) // 如果获取授权失败, 或者授权项为0
		return lRet;


	lRet = m_videoDB.VideoAddMode(pMode);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Add mode to database failed."));
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	
	UpdateLastModeChgTime();
	return ICV_SUCCESS;

}


// 验证用户模式的权限
// 如果用户对所有的模式内的分区都有权限,才认为有权限
long CVideoCfg::VerifyUserModeRight(const char *pszUserName, const char *pszGroupName, CVideoMode *pMode)
{
	long lRet = ICV_SUCCESS;

	CVideoList<long>* pZoneIDList = pMode->GetListZone();
	for (CVideoList<long>::iterator itZoneID = pZoneIDList->begin(); itZoneID!= pZoneIDList->end(); itZoneID ++)
	{
		long lZoneID = *itZoneID;
		lRet = VerifyUserZoneRight(pszUserName, pszGroupName, lZoneID);
		if(lRet != ICV_SUCCESS)
			return lRet;
	}

	return lRet;
}

// 验证用户模式的权限
long CVideoCfg::VerifyUserZoneRight(const char *pszUserName, const char *pszGroupName, const long lZoneID)
{
	long lRet = ICV_SUCCESS;
	long lAuth = 1;

	CVideoZone *pZone = m_listZone.FindZonebyID(lZoneID);
	if(!pZone)
		return EC_ICV_CCTV_ZONENOTEXIST;

	//默认分区不验证
	if (0 != strcmp(_("Default partion"), pZone->GetName()))
		lRet = VerifyUserRight((char *)pszUserName, (char *)pszGroupName, pZone->GetAuthLabel(), &lAuth);

	if(lRet != ICV_SUCCESS || lAuth == 0) // 如果获取授权失败, 或者授权项为0
	{
		CVLog.LogErrMessage(lRet, _("VerifyUserZoneRight,user:<%s> group:<%s> authlabel:<%s>."),
			pszUserName, pszGroupName, pZone->GetAuthLabel());
		if (lAuth == 0)
			lRet = EC_ICV_CCTV_NOAUTH;
	}

	return lRet;
}

// 验证用户对摄像头的权限
long CVideoCfg::VerifyUserCamRight(const char *pszUserName, const char *pszGroupName, const long lCamID)
{
	long lRet = ICV_SUCCESS;

	CVideoCameraEx *pCamEx = NULL;
	lRet = GetCameraByCamID(lCamID,pCamEx); // 得到摄像头信息及是否是本地摄像头信息
	if(lRet != ICV_SUCCESS)
		return lRet;

	long lZoneID = pCamEx->GetZoneID();
	lRet = VerifyUserZoneRight(pszUserName, pszGroupName, lZoneID);
	
	return lRet;
}

/**
 *  $(根据用户信息删除模式).
 *
 *  @param  -[in] const char* pszModeName: 模式名称.
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *	@return $(bool) true成功，false失败.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CVideoCfg::DeleteModeByUserInfo(const char* pszModeName, const char* pszUserName, const char* pszGroupName)
{
	// 读取模式
	CVideoMode theMode;
	long lRet = m_videoDB.VideoReadModeByName(pszModeName, &theMode);
	
	// 如果该模式已不存在,认为是删除成功了
	if( lRet != ICV_SUCCESS)
		return ICV_SUCCESS;

	lRet = VerifyUserModeRight(pszUserName, pszGroupName, &theMode);
	if(lRet != ICV_SUCCESS) // 如果获取授权失败
	{
		CVLog.LogErrMessage(lRet, _("User(%s) has no right to delete mode."), pszUserName);
		return lRet;
	}
	
	// 删除
	lRet = m_videoDB.VideoDeleteMode(pszModeName);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Delete mode from database failed."));
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	UpdateLastModeChgTime();
	return ICV_SUCCESS;
}


/**
 *  $(根据用户信息修改模式).
 *
 *  @param  -[in] const char* pszOldModeName: 旧模式名称.
 *  @param  -[in] CVideoMode* pNewMode: 新模式信息.
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *	@return $(bool) true成功，false失败.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CVideoCfg::ModifyModeByUserInfo(const char* pszOldModeName, CVideoMode* pNewMode, 
										   const char* pszUserName, const char* pszGroupName)
{
	long lRet = VerifyUserModeRight(pszUserName, pszGroupName, pNewMode);
	if(lRet != ICV_SUCCESS) // 如果获取授权失败
	{
		CVLog.LogErrMessage(lRet, _("User(%s) has no right to modify mode."), pszUserName);
		return lRet;
	}
	
	lRet = m_videoDB.VideoUpdateMode(pNewMode, pszOldModeName, pNewMode->GetType()!=VIDEO_MODETYPE_SHOW);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Modify mode from database failed."));
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	
	UpdateLastModeChgTime();
	return ICV_SUCCESS;
}


/**
 *  $(根据用户信息以及模式名称获取模式信息).
 *
 *  @param  -[in] const char* pszModeName:模式名称.
 *  @param  -[out] CVideoMode* pNewMode: 模式信息.
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *	@return $(bool) true成功，false失败.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CVideoCfg::GetModeInfoByNameAndUserInfo(const char* pszModeName, CVideoMode* pMode, 
											 const char* pszUserName, const char* pszGroupName)
{
	long lRet = VerifyUserModeRight(pszUserName, pszGroupName, pMode);
	if(lRet != ICV_SUCCESS) // 如果获取授权失败
	{
		CVLog.LogErrMessage(lRet, _("User(%s) has no right to get mode."), pszUserName);
		return lRet;
	}
	
	lRet = m_videoDB.VideoReadModeByName(pszModeName, pMode);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Get mode from database failed."));
		return EC_ICV_CCTV_SQLITE_ERROR;
	}
	
	return ICV_SUCCESS;
}


/**
 *  $(根据用户信息以及摄像头ID获取预置位信息).
 *
 *  @param  -[in] const long nCameraID: 摄像头ID.
 *  @param  -[out] CVideoPSPList* plistPSP: 预置位列表.
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CVideoCfg::GetPSPListOfCameraByUserInfo(const long lCameraID, CVideoPSPList* plistPSP, 
											 const char* pszUserName, const char* pszGroupName)
{
	plistPSP->clear();

	// 验证用户是否可以操作摄像头
	long lRet = VerifyUserCamRight(pszUserName, pszGroupName, lCameraID);
	if(lRet != ICV_SUCCESS) // 如果获取授权失败
		return lRet;
	
	// 从数据库获取预置位信息
	lRet = m_videoDB.VideoReadPSPListOfCamera(plistPSP, lCameraID);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Fail to read preset position information!"));//获取预置位信息失败！
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	return ICV_SUCCESS;
}

/**
 *  $(根据用户信息添加预置位).
 *
 *  @param  -[in] const long nCameraID: 摄像头ID.
 *  @param  -[in] const char* pszPSPName: 预置位名称.
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *	@return $(bool) true成功，false失败.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CVideoCfg::AddPSPOfCameraByUserInfo(const long lCamID, CVideoPSP* pPSP, 
										 const char* pszUserName, const char* pszGroupName)
{
	// 验证用户是否可以操作摄像头
	long lRet = VerifyUserCamRight(pszUserName, pszGroupName, lCamID);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("User(%s) has no right to add psp."), pszUserName);
		return lRet;
	}

	lRet = m_videoDB.VideoAddPSPOfCamera(pPSP, lCamID);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Add psp to database failed."));
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	UpdateLastPspChgTime();
	return ICV_SUCCESS;
}


/**
 *  $(根据用户信息删除预置位).
 *
 *  @param  -[in] const long nCameraID: 摄像头ID.
 *  @param  -[in] const char* pszPSPName: 预置位名称.
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *	@return $(bool) true成功，false失败.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CVideoCfg::DeletePSPOfCameraByUserInfo(const long lCamID, const char* pszPSPName, 
											const char* pszUserName, const char* pszGroupName)
{
	// 验证用户是否可以操作摄像头
	long lRet = VerifyUserCamRight(pszUserName, pszGroupName, lCamID);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("User(%s) has no right to delete psp."), pszUserName);
		return lRet;
	}

	lRet = m_videoDB.VideoDeletePSPOfCamera(pszPSPName, lCamID);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Delete psp from database failed."));
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	UpdateLastPspChgTime();
	return ICV_SUCCESS;
}


/**
 *  $(根据用户信息修改预置位).
 *
 *  @param  -[in] const long nCameraID: 摄像头ID.
 *  @param  -[in] const char* pszOldPSPName: 旧预置位名称.
 *  @param  -[in] CVideoPSP* pNewPSP: 新预置位信息.
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *	@return $(bool) true成功，false失败.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CVideoCfg::ModifyPSPOfCameraByUserInfo(const long lCamID, const char* pszOldPSPName, CVideoPSP* pNewPSP, 
												  const char* pszUserName, const char* pszGroupName)
{
	// 验证用户是否可以操作摄像头
	long lRet = VerifyUserCamRight(pszUserName, pszGroupName, lCamID);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("User(%s) has no right to modify psp."), pszUserName);
		return lRet;
	}

	lRet = m_videoDB.VideoUpdatePSPOfCamera(pNewPSP, pszOldPSPName, lCamID);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Modify psp from database failed."));
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	UpdateLastPspChgTime();
	return ICV_SUCCESS;
}


/**
 *  $(根据用户信息添加抓拍图片).
 *
 *  @param  -[in] CVideoSnapFile* pSnapFIle: 抓拍信息.
 *  @param  -[in] const char* pszPicBuf: 图片buffer.
 *  @param  -[in] const long nPicBufSize: 图片buffer长度.
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *  @param  -[out] long &nSnapID: 抓拍ID.
 *	@return $(bool) true成功，false失败.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CVideoCfg::AddSnapPictureByUserInfo(const long lCamID, CVideoSnapFile* pSnapFIle, const char* pszPicBuf, const long nPicBufSize,
										 const char* pszUserName, const char* pszGroupName, long &nSnapID)
{
	// 验证用户是否可以操作摄像头
	long lRet = VerifyUserCamRight(pszUserName, pszGroupName, lCamID);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("User(%s) has no right to add snap pictrue."), pszUserName);
		return lRet;
	}

	// 验证用户是否可以操作摄像头
	lRet = m_videoDB.VideoAddSnapPicture(pSnapFIle, pszPicBuf, nPicBufSize, &m_listSnapType, nSnapID);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Add snap pictrue to database failed."));
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	return ICV_SUCCESS;

}

// 根据用户信息删除抓拍信息
long CVideoCfg::DelSnapPicInfoByUser(const long lCamID, const long lSnapID, const char* pszUserName, const char* pszGroupName)
{
	// 验证用户是否可以操作摄像头
	long lRet = VerifyUserCamRight(pszUserName, pszGroupName, lCamID);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("User(%s) has no right to delete snap pictrue."), pszUserName);
		return lRet;
	}

	// 删除
	lRet = m_videoDB.VideoDeleteSnapPicture(lSnapID);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Delete snap pictrue to database failed."));
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	return ICV_SUCCESS;
}

// 根据用户信息修改抓拍信息
long CVideoCfg::ModifySnapPicInfoByUser(const long lCamID, const long lSnapID, CVideoSnapFile* pSnapFile, const char* pszUserName, const char* pszGroupName)
{
	// 验证用户是否可以操作摄像头
	long lRet = VerifyUserCamRight(pszUserName, pszGroupName, lCamID);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("User(%s) has no right to modify snap pictrue."), pszUserName);
		return lRet;
	}

	// 更新
	lRet = m_videoDB.VideoUpdateSnapPicture(pSnapFile, lSnapID, &m_listSnapType);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Modify snap pictrue information to database failed."));
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	return ICV_SUCCESS;
}

// 根据用户信息查询抓拍图片
long CVideoCfg::QuerySnapPictureByUserInfo(const char* pZone, const long lCamID, time_t tStart, time_t tEnd, const char* pSnapType,
										   CVideoSnapFileList* pSnapFileList, const char* pszUserName, const char* pszGroupName)
{
	long lRet = EC_ICV_CCTV_FUNCPARAMINVALID;
	if ((NULL == pZone) || (NULL == pSnapType) || (NULL == pSnapFileList) || (NULL == pszUserName) || (NULL == pszGroupName))
		return lRet;

	// 获取抓拍信息
	lRet = m_videoDB.VideoReadSnapPictureList(pSnapFileList, &m_listSnapType);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("QuerySnapPictureByUserInfo read snap picture list error."));
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	// 查询抓拍信息
	int i = 0;
	for (i=0; i<pSnapFileList->GetSize(); i++)
	{
		// 验证用户是否可以操作摄像头
		lRet = VerifyUserCamRight(pszUserName, pszGroupName, pSnapFileList->GetAt(i)->GetCameraID());
		if (ICV_SUCCESS != lRet)
		{
			pSnapFileList->RemoveAt(i);
			i--;
			continue;
		}
		// 摄像头信息是否在摄像头列表中
		CVideoCameraEx *pCamEx = NULL;
		lRet = GetCameraByCamID(pSnapFileList->GetAt(i)->GetCameraID(), pCamEx);
		if (ICV_SUCCESS != lRet)
		{
			pSnapFileList->RemoveAt(i);
			i--;
			continue;
		}
		// 获取某一分区摄像头的抓拍信息
		CVideoZone *ptheZone = NULL;
		if (0 != strcmp(pZone, _("All partions")))
		{
			ptheZone = m_listZone.FindZonebyName(pZone);
			if (NULL == ptheZone)
				return EC_ICV_CCTV_ZONENOTEXIST;
			if (ptheZone->GetID() != pCamEx->GetZoneID()) 
			{
				pSnapFileList->RemoveAt(i);
				i--;
				continue;
			}
		}
		// 获取某一摄像头的抓拍信息
		if (VIDEO_ID_DEFAULT != lCamID)
		{
			if (lCamID != pCamEx->GetID())
			{
				pSnapFileList->RemoveAt(i);
				i--;
				continue;
			}
		}
		// 获取某一时间段的抓拍信息
		if ((tStart > pSnapFileList->GetAt(i)->GetSnapTime()) || (tEnd < pSnapFileList->GetAt(i)->GetSnapTime()))
		{
			pSnapFileList->RemoveAt(i);
			i--;
			continue;
		}
		// 获取某一抓拍类型的抓拍信息
		if (0 != strcmp(pSnapType, _("All types")))
		{
			if (0 != strcmp(pSnapType, pSnapFileList->GetAt(i)->GetInfoType()))
			{
				pSnapFileList->RemoveAt(i);
				i--;
				continue;
			}
		}
	}

	return ICV_SUCCESS;
}

/**
 *  根据抓拍ID获取摄像头ID.
 *
 *  @param  -[in]  long  nSnapID: [抓拍ID]
 *  @param  -[out]  long&  nCamID: [摄像头ID]
 *  @return 成功：ICV_SUCCESS
 *    失败：错误码.
 *
 *  @version     06/02/2009  chenzhan  Initial Version.
 */
long CVideoCfg::GetCameraIDBySnapID(long nSnapID, long &nCamID)
{
	if (0 > nSnapID)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	// 获取抓拍信息
	CVideoSnapFileList theSnapFileList;
	theSnapFileList.clear();
	long lRet = m_videoDB.VideoReadSnapPictureList(&theSnapFileList, &m_listSnapType);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("GetCameraIDBySnapID read snap picture list error."));
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	for (int i=0; i<theSnapFileList.GetSize(); i++)
	{
		CVideoSnapFile *pSnapFile = theSnapFileList.GetAt(i);
		if (NULL == pSnapFile)
			return EC_ICV_CCTV_SNAPPICNOTEXIST;

		if (pSnapFile->GetID() == nSnapID)
		{
			nCamID = pSnapFile->GetCameraID();
			return ICV_SUCCESS;
		}
	}

	return EC_ICV_CCTV_SNAPPICNOTEXIST;
}

long CVideoCfg::GetSnapPicBufByUser(const long nSnapID, char *&pBuffer, long &nPicLen, const char* pszUserName, const char* pszGroupName)
{
	long lRet = EC_ICV_CCTV_FUNCPARAMINVALID;
	if ((0 >= nSnapID) || (NULL == pszUserName) || (NULL == pszGroupName))
		return lRet;

	// 获取抓拍图片内容
	long nCamID = VIDEO_ID_INVALIDATION;
	long nCount = 0;
	lRet = m_videoDB.VideoReadSnapPicBuffer(nSnapID, nCamID, nCount, pBuffer, nPicLen);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("GetSnapPicBufByUser read snap picture buffer error."));
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	// 验证用户是否可以对应摄像头
	lRet = VerifyUserCamRight(pszUserName, pszGroupName, nCamID);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("GetSnapPicBufByUser read snap picture buffer error."));
		return lRet;
	}

	return lRet;
}

/**
 *  $(根据预置位名称获取预置位信息).
 *
 *  @param  -[out]  CVideoPSP*  pPSP: 预置位信息
 *  @param  -[in]  const char*  pszPSPName: 预置位名称
 *  @param  -[in]  long nCameraID: 摄像头ID
 *
 *  @version     08/28/2008  chenzhan  Initial Version.
 */
long CVideoCfg::GetPSPInfoByNameAndCamID(CVideoPSP* pPSP, const char* pszPSPName, long nCameraID)
{
	CVideoPSPList pspList;
	
	long lRet = m_videoDB.VideoReadPSPListOfCamera(&pspList, nCameraID);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Fail to read a camera preset list!"));//读取某一摄像头的预置位列表失败！
		return EC_ICV_CCTV_SQLITE_ERROR;
	}
	
	for (CVideoPSPList::iterator itPsp = pspList.begin(); itPsp != pspList.end(); itPsp ++)
	{
		CVideoPSP &psp = *itPsp;
		
		if (0 == strcmp(pszPSPName, psp.GetName()))
		{
			pPSP->SetName(psp.GetName());
			pPSP->SetPresetIndex(psp.GetPresetIndex());
			pPSP->SetDescription(psp.GetDescription());
			break;
		}
	}
	return lRet;
}

// 根据监视器ID得到连接该监视器的设备
long CVideoCfg::GetLinkDeviceByMonitorID(const long lMonitorID, CVideoDevice* &pLinkDev, char* szChannel)
{
	CVideoMonitor* pMonitor = m_listMon.FindMonitorbyID(lMonitorID);
	if(!pMonitor)
		return EC_ICV_CCTV_MONITORNOTFOUND;
	
	CVideoLinkDevice* pMonLinkDev = pMonitor->GetLinkDev();
	if(!pMonLinkDev)
		return EC_ICV_CCTV_LINKDEVICENOTFOUND;

	// 查找连接设备
	Safe_CopyString(szChannel, pMonLinkDev->GetChannel(), sizeof(szChannel));

	long nCtrlDevID = pMonLinkDev->GetDeviceID();
	pLinkDev = m_listDev.FindDevicebyID(nCtrlDevID);
	if(!pLinkDev)
		return EC_ICV_CCTV_LINKDEVICENOTFOUND;

	return ICV_SUCCESS;
}

/**
 *  获取用户有权限看到的所有自定义分区信息.(需求用例ID:113179,113180,113181)
 *
 *  @param  -[in]  const char*  pszUserName:			[用户名]
 *  @param  -[in]  CVideoCustomZoneList *plistCustZone:	[分区列表]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     04/21/2009  chenzhan  Initial Version.
 */
long CVideoCfg::GetCustZoneByUser(const char* pszUserName, CVideoCustomZoneList *plistCustZone)
{
	// 入口检测
	if ((NULL == pszUserName) || (NULL == plistCustZone))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = m_videoDB.VideoReadCustomZone(pszUserName, plistCustZone);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Fail to add custom partition."));//读取自定义分区失败！
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	return ICV_SUCCESS;
}

/**
 *  添加自定义分组.(需求用例ID:113179,113180,113181)
 *
 *  @param  -[in]  const char*  pszUserName:		[用户名]
 *  @param  -[in]  const char*  pszCustZoneName:	[分区名称]
 *  @param  -[in]  const char*  pszCustZoneDesc:	[分区描述]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     04/21/2009  chenzhan  Initial Version.
 */
long CVideoCfg::AddCustZoneByUser(const char* pszUserName, const char* pszCustZoneName, const char* pszCustZoneDesc)
{
	// 入口检测
	if ((NULL == pszUserName) || (NULL == pszCustZoneName) || (NULL == pszCustZoneDesc))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	long nCustZoneID = VIDEO_ID_INVALIDATION;
	long lRet = m_videoDB.VideoAddCustomZone(pszUserName, pszCustZoneName, pszCustZoneDesc, nCustZoneID);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Fail to add custom partition."));//添加自定义分区失败！
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	return ICV_SUCCESS;
}

/**
 *  删除自定义分区.(需求用例ID:113179,113180,113181)
 *
 *  @param  -[in]  long nCustZoneID:	[分区ID]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     04/21/2009  chenzhan  Initial Version.
 */
long CVideoCfg::DelCustZone(long nCustZoneID)
{
	// 入口检测
	if (0 >= nCustZoneID)
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	long lRet = m_videoDB.VideoDeleteCustomZone(nCustZoneID);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Fail to delete custom partition."));//删除自定义分区失败！
		return EC_ICV_CCTV_SQLITE_ERROR;
	}
	
	return ICV_SUCCESS;
}

/**
 *  获取用户有权限看到的某一自定义分区摄像头信息.(需求用例ID:113179,113180,113181)
 *
 *  @param  -[in]  const char*  pszUserName:	[用户名]
 *  @param  -[in]  long			nCustZoneID:	[分区ID]
 *  @param  -[in]  CVideoCameraList* plistCam:	[摄像头列表]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     04/21/2009  chenzhan  Initial Version.
 */
long CVideoCfg::GetCustZoneCamByUser(const char* pszUserName, long nCustZoneID, CVideoCameraList* plistCam)
{
	// 入口检测
	if ((NULL == pszUserName) || (NULL == plistCam) || (0 >= nCustZoneID))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	CVideoList<long> theCamIndexList;
	long lRet = m_videoDB.VideoReadCamIndex(&theCamIndexList, nCustZoneID);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Fail to read camera index."));//读取摄像头索引失败！
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	for (int i=0; i<theCamIndexList.GetSize(); i++)
	{
		CVideoCameraMap::iterator itCam = m_mapCam.find(*theCamIndexList.GetAt(i));
		plistCam->InsertObject(*(CVideoCamera *)(itCam->second));
	}

	return ICV_SUCCESS;
}

/**
 *  复制摄像头到自定义分区.(需求用例ID:113179,113180,113181)
 *
 *  @param  -[in]  const char*  pszUserName:		[用户名]
 *  @param  -[in]  long			nCustZoneID:		[分区ID]
 *  @param  -[in]  CVideoList<long>* plistCamIndex: [摄像头ID列表]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     04/21/2009  chenzhan  Initial Version.
 */
long CVideoCfg::CpyCustZoneCamByUser(const char* pszUserName, long nCustZoneID, CVideoList<long>* plistCamIndex)
{
	// 入口检测
	if ((NULL == pszUserName) || (NULL == plistCamIndex) || (0 >= nCustZoneID))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	long lRet = m_videoDB.VideoAddCamIndex(plistCamIndex, nCustZoneID);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Fail to add camera index."));//添加摄像头索引失败！
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	return ICV_SUCCESS;
}

/**
 *  删除自定义分区中的摄像头.(需求用例ID:113179,113180,113181)
 *
 *  @param  -[in]  const char*  pszUserName:		[用户名]
 *  @param  -[in]  const char*  pszGroupName:		[用户组]
 *  @param  -[in]  long			nCustZoneID:		[分区ID]
 *  @param  -[in]  CVideoList<long>* plistCamIndex: [摄像头ID列表]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     04/21/2009  chenzhan  Initial Version.
 */
long CVideoCfg::DelCustZoneCamByUser(const char* pszUserName, const char* pszGroupName, long nCustZoneID, CVideoList<long>* plistCamIndex)
{
	// 入口检测
	if ((NULL == pszUserName) || (NULL == pszGroupName) || (NULL == plistCamIndex) || (0 >= nCustZoneID))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	for (int i=0; i < plistCamIndex->GetSize(); i++)
		VIDEO_CHECKFAIL_RETURN(VerifyUserCamRight(pszUserName, pszGroupName, *plistCamIndex->GetAt(i)));

	long lRet = m_videoDB.VideoDeleteCamIndex(plistCamIndex, nCustZoneID);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Fail to delete camera index."));//删除摄像头索引失败！
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	return ICV_SUCCESS;
}

// 更新模式最后改变时间
void CVideoCfg::UpdateLastModeChgTime()
{
	m_lLastModeChgTime ++;
}

// 更新预置位最后改变时间
void CVideoCfg::UpdateLastPspChgTime()
{
	m_lLastPspChgTime ++;
}

// 更新预置位最后改变时间
void CVideoCfg::UpdateLastAllCfgChgTime()
{
	m_lLastAllCfgChgTime ++;
}

/**
 *  抓拍图片累加
 *
 *  @param  -[in]  const char*  pszUserName:		[用户名]
 *  @param  -[in]  const char*  pszGroupName:		[用户组]
 *  @param  -[in]  const long	nCaptureID:			[抓拍ID]
 *  @param  -[in]  const char*  pszPicBuff:			[图片缓冲区]
 *  @param  -[in]  const long	nSize:				[缓冲区大小]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     08/31/2009  chenzhan  Initial Version.
 */
long CVideoCfg::CapturePicAddTo(const char* pszUserName, const char* pszGroupName, const long nCaptureID, const char* pszPicBuff, const long nSize)
{
	if ((NULL == pszUserName) || (NULL == pszGroupName) || (NULL == pszPicBuff) || (0 >= nCaptureID) || (0 >= nSize))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long nCamID = 0;
	long nCount = 0;
	long nPicSize = 0;
	char *pBuffer = NULL;
	long lRet = m_videoDB.VideoReadSnapPicBuffer(nCaptureID, nCamID, nCount, pBuffer, nPicSize);
	delete pBuffer;
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Fail to read capture picture content."));//读取抓拍图片内容失败！
		return EC_ICV_CCTV_SQLITE_ERROR;
	}
	
	if (VIDEO_SERIES_SNAP_MAX_COUNT <= nCount)
	{
		CVLog.LogErrMessage(lRet, _("The count of capture achieve <%d>."), VIDEO_SERIES_SNAP_MAX_COUNT);//抓拍图片个数已经达到<%d>个！
		return EC_ICV_CCTV_ISMAXPICCOUNT;
	}

	// 验证用户是否可以对应摄像头
	lRet = VerifyUserCamRight(pszUserName, pszGroupName, nCamID);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CVideoCfg::CapturePicAddTo fail to verify permission."));//CVideoCfg::CapturePicAddTo权限验证失败！
		return lRet;
	}

	lRet = m_videoDB.VideoCapturePicAddTo(nCaptureID, pszPicBuff, nSize);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CVideoCfg::CapturePicAddTo Fail to add picture to database."));//CVideoCfg::CapturePicAddTo向数据库中添加图片失败！
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	return lRet;
}

/**
 *  添加录像片段信息
 *
 *  @param  -[in]  const char*  pszUserName:		[用户名]
 *  @param  -[in]  const char*  pszGroupName:		[用户组]
 *  @param  -[in]  const long	nCamID:				[摄像头ID]
 *  @param  -[in]  CRecord *pRec:					[录像信息]
 *  @param  -[in]  long &nRecordID:					[录像记录ID]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     08/31/2009  chenzhan  Initial Version.
 */
long CVideoCfg::AddRecordOfCameraByUserInfo(const char* pszUserName, const char* pszGroupName, const long nCamID, CRecord *pRec, long &nRecordID)
{
	if ((0 >= nCamID) || (NULL == pRec) || (NULL == pszUserName) || (NULL == pszGroupName))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = ICV_SUCCESS;
	
	// 验证用户是否可以对应摄像头
	lRet = VerifyUserCamRight(pszUserName, pszGroupName, nCamID);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CVideoCfg::AddRecordOfCameraByUserInfo fail to verify permission."));//CVideoCfg::AddRecordOfCameraByUserInfo权限验证失败！
		return lRet;
	}
	
	lRet = m_videoDB.VideoAddRecord(pRec, nRecordID);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CVideoCfg::AddRecordOfCameraByUserInfo Fail to add video to database."));//CVideoCfg::AddRecordOfCameraByUserInfo向数据库添加录像信息失败！
		return EC_ICV_CCTV_SQLITE_ERROR;
	}
	
	return lRet;
}

/**
 *  修改录像记录
 *
 *  @param  -[in]  const char*  pszUserName:		[用户名]
 *  @param  -[in]  const char*  pszGroupName:		[用户组]
 *  @param  -[in]  const long	nRecordID:			[录像记录ID]
 *  @param  -[in]  CRecord* pRec:					[录像信息]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     08/31/2009  chenzhan  Initial Version.
 */
long CVideoCfg::ModifyRecordByUserInfo(const char* pszUserName, const char* pszGroupName, const long nRecordID, CRecord* pRec)
{
	if ((NULL == pszUserName) || (NULL == pszGroupName) || (0 >= nRecordID) || (NULL == pRec))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = ICV_SUCCESS;

	// 获取录像信息
	CRecord rec;
	lRet = m_videoDB.VideoGetRecord(nRecordID, &rec);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CVideoCfg::ModifyRecordByUserInfo fail to get video information."));//CVideoCfg::ModifyRecordByUserInfo获取录像信息失败！
		return EC_ICV_CCTV_SQLITE_ERROR;
	}
	
	// 验证用户是否可以对应摄像头
	lRet = VerifyUserCamRight(pszUserName, pszGroupName, rec.GetCameraID());
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CVideoCfg::ModifyRecordByUserInfo fail to verify permission."));//CVideoCfg::ModifyRecordByUserInfo权限验证失败！
		return lRet;
	}
	
	// 修改录像
	lRet = m_videoDB.VideoModifyRecord(nRecordID, pRec);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CVideoCfg::ModifyRecordByUserInfo fail to modify video information."));//CVideoCfg::ModifyRecordByUserInfo修改录像信息失败！
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	//lRet = 未完成 chenzhan 修改文件名称

	return ICV_SUCCESS;
}

/**
 *  查询录像
 *
 *  @param  -[in]  const char*  pszUserName:		[用户名]
 *  @param  -[in]  const char*  pszGroupName:		[用户组]
 *  @param  -[in]  const long	nCamID:				[摄像头ID]
 *  @param  -[in]  const long	nTimeStart:			[起始时间]
 *  @param  -[in]  const long	nTimeEnd:			[结束时间]
 *  @param  -[in]  CRecordList &thelistRec:			[录像列表]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     08/31/2009  chenzhan  Initial Version.
 */
long CVideoCfg::QueryRecordByUserInfo(const char* pszUserName, const char* pszGroupName, const long nCamID, const long nTimeStart, const long nTimeEnd, CRecordList *plistRec)
{
	if ((NULL == pszUserName) || (NULL == pszGroupName) || (0  >= nTimeStart) || (nTimeStart >= nTimeEnd) || (NULL == plistRec))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	// 查询录像
	long lRet = m_videoDB.VideoReadRecordList(plistRec);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CVideoCfg::QueryRecordByUserInfo fail to query video information."));//CVideoCfg::QueryRecordByUserInfo查询录像信息失败！
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	// 查询信息
	for (int i=0; i<plistRec->GetSize(); i++)
	{
		// 摄像头信息是否在摄像头列表中
		CVideoCameraEx *pCamEx = NULL;
		lRet = GetCameraByCamID(plistRec->GetAt(i)->GetCameraID(), pCamEx);
		if (ICV_SUCCESS != lRet)
		{
			plistRec->RemoveAt(i);
			i--;
			continue;
		}
		// 获取某一摄像头的抓拍信息
		if (VIDEO_ID_DEFAULT != nCamID)
		{
			if (nCamID != pCamEx->GetID())
			{
				plistRec->RemoveAt(i);
				i--;
				continue;
			}
		}
		// 获取某一时间段的抓拍信息
		if ((nTimeStart > plistRec->GetAt(i)->GetStartTime()) || (nTimeEnd < plistRec->GetAt(i)->GetEndTime()))
		{
			plistRec->RemoveAt(i);
			i--;
			continue;
		}
		// 验证用户是否可以操作摄像头
		lRet = VerifyUserCamRight(pszUserName, pszGroupName, plistRec->GetAt(i)->GetCameraID());
		if (ICV_SUCCESS != lRet)
		{
			plistRec->RemoveAt(i);
			i--;
			CVLog.LogErrMessage(lRet, _("CVideoCfg::QueryRecordByUserInfo fail to verify permission."));//CVideoCfg::QueryRecordByUserInfo权限验证失败！
			continue;
		}
	}

	return ICV_SUCCESS;
}

/**
 *  下载录像
 *
 *  @param  -[in]  const char*  pszUserName:		[用户名]
 *  @param  -[in]  const char*  pszGroupName:		[用户组]
 *  @param  -[in]  const long	nRecordID:			[录像记录ID]
 *  @param  -[in]  const char*  pszFileName:		[录像文件名称]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     08/31/2009  chenzhan  Initial Version.
 */
long CVideoCfg::DownloadRecordByUserInfo(const char* pszUserName, const char* pszGroupName, const long nRecordID, const char* pszFileName)
{
	if ((NULL == pszUserName) || (NULL == pszGroupName) || (0 >= nRecordID) || (NULL == pszFileName))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	// 获取录像信息
	CRecord rec;
	long lRet = m_videoDB.VideoGetRecord(nRecordID, &rec);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CVideoCfg::DownloadRecordByUserInfo fail to get video information."));//CVideoCfg::DownloadRecordByUserInfo获取录像信息失败！
		return EC_ICV_CCTV_SQLITE_ERROR;
	}
	
	// 文件名称
	memcpy((void*)pszFileName, rec.GetFileName(), strlen(rec.GetFileName()));

	// 验证用户是否可以对应摄像头
	lRet = VerifyUserCamRight(pszUserName, pszGroupName, rec.GetCameraID());
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CVideoCfg::DownloadRecordByUserInfo fail to verify permission."));//CVideoCfg::DownloadRecordByUserInfo权限验证失败！
		return lRet;
	}

	return ICV_SUCCESS;
}

/**
 *  删除录像
 *
 *  @param  -[in]  const char*  pszUserName:		[用户名]
 *  @param  -[in]  const char*  pszGroupName:		[用户组]
 *  @param  -[in]  const long	nRecordID:			[录像记录ID]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     08/31/2009  chenzhan  Initial Version.
 */
long CVideoCfg::DeleteRecordByUserInfo(const char* pszUserName, const char* pszGroupName, const long nRecordID)
{
	if ((NULL == pszUserName) || (NULL == pszGroupName) || (0 >= nRecordID))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	// 获取录像信息
	CRecord rec;
	long lRet = m_videoDB.VideoGetRecord(nRecordID, &rec);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CVideoCfg::DeleteRecordByUserInfo fail to get video information."));//CVideoCfg::DeleteRecordByUserInfo获取录像信息失败！
		return EC_ICV_CCTV_SQLITE_ERROR;
	}
	
	// 验证用户是否可以对应摄像头
	lRet = VerifyUserCamRight(pszUserName, pszGroupName, rec.GetCameraID());
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CVideoCfg::DeleteRecordByUserInfo fail to verify permission."));//CVideoCfg::DeleteRecordByUserInfo权限验证失败！
		return lRet;
	}
	
	// 删除录像
	lRet = m_videoDB.VideoDeleteRecord(nRecordID);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CVideoCfg::DeleteRecordByUserInfo fail to delete video information."));//CVideoCfg::DeleteRecordByUserInfo删除录像信息失败！
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	char szFullPath[VIDEO_DEFAULTINFO_MAXSIZE];
	memset(szFullPath, 0x00, sizeof(szFullPath));
	sprintf(szFullPath, "%s\\VideoServer", CVComm.GetProjectDataPath());
	lRet = CVFileHelper::DeleteAFile(rec.GetFileName(), szFullPath);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CVideoCfg::DeleteRecordByUserInfo fail to get video file."));//CVideoCfg::DeleteRecordByUserInfo删除录像文件失败！
		return lRet;
	}

	return ICV_SUCCESS;
}

/**
 *  保存录像
 *
 *  @param  -[in]  const long	nRecordID:			[录像记录ID]
 *  @param  -[in]  const char*  pszRecBuff:			[用户名]
 *  @param  -[in]  const long	nRecSize:			[录像大小]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     08/31/2009  chenzhan  Initial Version.
 */
long CVideoCfg::SaveRecord(const long nRecordID, const char* pszRecBuff, const long nRecSize)
{
	// 获取录像信息
	CRecord rec;
	long lRet = m_videoDB.VideoGetRecord(nRecordID, &rec);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CVideoCfg::SaveRecord fail to get video information."));//CVideoCfg::SaveRecord获取录像信息失败！
		return EC_ICV_CCTV_SQLITE_ERROR;
	}

	char szFullName[VIDEO_DEFAULTINFO_MAXSIZE];
	memset(szFullName, 0x00, sizeof(szFullName));
	sprintf(szFullName, "%s\\rtd\\videoserver\\%s", CVComm.GetProjectDataPath(), rec.GetFileName());
	lRet = CVFileHelper::WriteToFile(szFullName, (char*)pszRecBuff, nRecSize);
	if(ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CVideoCfg::SaveRecord fail to save video file."));//CVideoCfg::SaveRecord保存录像文件失败！
		return lRet;
	}

	return ICV_SUCCESS;
}

/**
 *  解析带逗号的字符串
 *
 *  @param  -[in]  char* pszSrc:			[原字符串]
 *  @param  -[in]  char* pszDst:			[子字符串]
 *  @param  -[in]  long nDstBuffSize:		[子字符串缓冲区大小]
 *  @param  -[in]  char token:				[分隔符]
 *  @return
 *	- char *子字符串
 *
 *  @version     10/23/2009  chenzhan  Initial Version.
 */
char* CVideoCfg::GetNextToken(char* pszSrc, char* pszDst, long nDstBuffSize, char token)
{
	long nCount = strlen(pszSrc);
	if (0 == nCount)
		return NULL;
	
	long nSlash = 0;
	for (nSlash=0; nSlash<nCount; nSlash++)
	{
		if (token == *(pszSrc+nSlash))
			break;
	}
	
	if (0 == nSlash)
	{
		memmove(pszSrc, pszSrc+nSlash+1, nCount-nSlash-1);
		*(pszSrc+nCount-nSlash-1) = '\0';
		return NULL;
	}
	else
	{
		if (nDstBuffSize < (nSlash+1))
			return NULL;
		
		memcpy(pszDst, pszSrc, nSlash);
		if (nSlash == nCount)
			memset(pszSrc, 0x00, nCount);
		else
		{
			memmove(pszSrc, pszSrc+nSlash+1, nCount-nSlash-1);
			*(pszSrc+nCount-nSlash-1) = '\0';
		}
	}
	
	return pszDst;
}

// 获取连接本设备输入通道的连接信息
long CVideoCfg::GetInputDevLinkList(long nLocalDevID, CReSwitchChannelList &theNextLinkList)
{
	if (0 >= nLocalDevID)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	theNextLinkList.clear();
	CVideoDeviceList::iterator it = m_listDev.begin();
	for (; it!=m_listDev.end(); it++)
	{
		CVideoLinkDeviceList::iterator itLink = it->GetLinkDev()->begin();
		for (; itLink!=it->GetLinkDev()->end(); itLink++)
		{
			if (itLink->GetDeviceID() == nLocalDevID)
			{
				CReSwitchChannel theLink;
				theLink.SetDev(&(*it));
				theLink.SetInChannel(itLink->GetChannel());
				theLink.SetOutChannel(itLink->GetLocalChannel());
				theNextLinkList.InsertObject(theLink);
			}
		}
	}
	
	return ICV_SUCCESS;
}

/**
 *  $(根据两个设备的ID获取他们之间接下来使用的通道).
 *
 *  @param  -[in] long nNearCamDevID: 摄像头id
 *  @param  -[in] long nNearMonDevID: 监视器id
 *  @param  -[out] char* szNearCamChannel: 摄像头通道
 *  @param  -[out] char* szNearMonChannel: 监视器通道
 *
 *  @version  04/18/2012 gaoliang modify Version.
 */
long CVideoCfg::GetCurrSwitchChannel(long nNearCamDevID, long nNearMonDevID, char* szNearCamChannel, char* szNearMonChannel)
{
	if (0 >= nNearCamDevID)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	if (0 >= nNearMonDevID)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	CVideoDevice *pDev = m_listDev.FindDevicebyID(nNearCamDevID);
	if (!pDev)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	// 顺序循环使用这两个设备之间的通道,此处为计算下一条应该使用的通道
	// 计算方式：如果当前有未使用的通道，则其为下一使用通道；
	//                 否则取时间戳最小的通道为下一使用通道
	//返回通道前，更新时间戳和使用标志位
	CVideoLinkDeviceList::iterator it = pDev->GetLinkDev()->begin();
	CVideoLinkDevice *pNext = &(*it); //下一个要使用的通道
	//CVideoLinkDevice *pFirst = &(*it);//最小的通道号
	CVideoLinkDevice *pOldestUsed = &(*it); //最早使用的通道号
	for (; it!=pDev->GetLinkDev()->end(); it++)
	{
		pNext = &(*it);                  //重新初始化pNext
		if (! pNext->GetIsUsed() ) //当前VideoLinkDevice未被使用
		{
			break;
		}
		
		if ( pOldestUsed->GetTimeStamp() > it->GetTimeStamp() ) //find older one
		{
			pOldestUsed = &(*it);
		}
		pNext = pOldestUsed;
	}

	pNext->SetIsUsed(true);
	time_t tempTime;
	time(&tempTime);
	long lTime = static_cast<long>(tempTime);
	pNext->SetTimeStamp(lTime);

	// 返回值
	Safe_CopyString(szNearMonChannel, pNext->GetChannel(), sizeof(szNearMonChannel));
	Safe_CopyString(szNearCamChannel, pNext->GetLocalChannel(), sizeof(szNearCamChannel));

	// 更新数据库
	m_videoDB.VideoUpdateLinkChannel(nNearCamDevID, nNearMonDevID, szNearMonChannel);

	return ICV_SUCCESS;
}

/**
 *  $(得到矩阵ID).
 *
 *  @param  -[in] const char* szMatrixName: 矩阵名称.
 *  @param  -[out] long &lMatrixID: 矩阵ID.
 *
 *  @version  02/28/2011  fengjuanyong  Initial Version.
 */
long CVideoCfg::GetMatrixIDByMatrixName(const char *szMatrixName, long &lMatrixID)
{
	long lRet = ICV_SUCCESS;
	for(CVideoDeviceList::iterator itDev = m_listDev.begin(); itDev != m_listDev.end(); itDev ++)
	{
		CVideoDevice &videoDev = *itDev;
		long lDevType = VIDEO_CONFIG->GetProducts()->FindProductbyID(videoDev.GetProductID())->GetDeviceType();
		if (lDevType == VIDEO_DEVICE_TYPE_MATRIX)
		{
			if (strcmp(videoDev.GetName(), szMatrixName) == 0)
			{
				lMatrixID = videoDev.GetID();
				break;
			}
		}
	}

	if (lMatrixID == VIDEO_ID_INVALIDATION)
		return EC_ICV_CCTV_DEVNOTEXIST;

	return lRet;
}

/**
 *  $(根据矩阵获取关联的摄像头信息列表).
 *
 *  @param  -[out] CVideoCameraList* plistCamera: 摄像头列表.
 *  @param  -[in] const long nMatrixID: 分区ID. 为负数则表示获取所有分区
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *
 *  @version  02/28/2011  fengjuanyong  Initial Version.
 */
long CVideoCfg::GetCameraListByMatrixAndUserInfo(CVideoCameraList* plistCamera, const char* pszUserName, const char* pszGroupName, long lMatrixID)
{
	long lRet = ICV_SUCCESS;
	plistCamera->clear();
	for(CVideoCameraMap::iterator itCam = m_mapCam.begin(); itCam != m_mapCam.end(); itCam ++)
	{
		// 获取摄像头关联的设备列表
		CVideoCameraEx *pCamEx = itCam->second;

		CVideoLinkDeviceList *pLinkDevList = pCamEx->GetLinkDev();
		if (pLinkDevList == NULL)
			continue;

		// 查找列表中是否有符合的matrixid
		for(CVideoLinkDeviceList::iterator itLinkDev = pLinkDevList->begin(); itLinkDev != pLinkDevList->end(); itLinkDev ++)
		{
			CVideoLinkDevice &linkDev = *itLinkDev;
			CVideoDevice *pDev = VIDEO_CONFIG->GetDeviceList()->FindDevicebyID(linkDev.GetDeviceID());
			long lProductID = pDev->GetProductID();
			long lLinkDevType = VIDEO_CONFIG->GetProducts()->FindProductbyID(lProductID)->GetDeviceType();
			// 连接的设备是矩阵且矩阵ID符合
			if ((lLinkDevType == VIDEO_DEVICE_TYPE_MATRIX) && (pDev->GetID() == lMatrixID))
			{
				plistCamera->InsertObject(*(CVideoCamera *)pCamEx);
				break;
			}
		}
	}
	return lRet;
}
