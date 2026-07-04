/**************************************************************
 *  Filename:    TimerTask.cpp
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: $(定时任务).
 *
 *  @author:     zhucongfeng
 *  @version     08/28/2009  zhucongfeng  Initial Version
 *  @version     08/30/2009  zhucongfeng  添加设备时钟同步处理
 *  @version     09/17/2009  zhucongfeng  添加dll的initsdk接口处理
**************************************************************/

// TimerTask.cpp: implementation of the CTimerTask class.
//
//////////////////////////////////////////////////////////////////////

#include "common/OS.h"
#include "TimerTask.h"
#include "NetWrapper.h"
#include "gettext/libintl.h"

#define _(STRING) gettext(STRING)
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
#define DEFAULT_WAIT_INTERVAL_TIME	1000
#define GET_INFO_FROM_SERVER_WAIT_TIME	5000

CTimerTask::CTimerTask()
{
	m_lShouldExit = 0;
}

CTimerTask::~CTimerTask()
{

}

/**
 *  $(获取所有设备信息).
 *  $(Detail).
 *
 *  @param  -[out]  CVideoDeviceList*  pDevList: [获取到的设备信息]
 *  @param  -[in]  long  lTimeOut: [超时时间]
 *  @return $(Return).
 *
 *  @version     08/28/2009  zhucongfeng  Initial Version.
 */

long CTimerTask::GetAllDeviceInfo(CVideoDeviceList* pDevList, long lTimeOut)
{
	long lRet = ICV_SUCCESS;
	
	if(pDevList == NULL)
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	//申请缓冲区
	long lBufLen = VIDEO_MESSAGE_MAX_SIZE;
	char* szBuffer = new char[lBufLen];
	memset(szBuffer, 0, lBufLen);
	
	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(szBuffer, lBufLen, VIDEO_CMD_GET_ALL_DEVICE, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		SAFE_DELETE_ARRAY(szBuffer);
		return lRet;
	}
	
	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	
	lRet = SendMsgToServer(szBuffer, lOutLen, &pRecvBuf, lRecvLen, lTimeOut);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}
	
	//解析消息头
	long lResult;
	lRet = NET_WRAPPER->ParserMsgHeader(pRecvBuf, lResult, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}
	
	if(lResult != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lResult;
	}
	
	long lRemainLen = lRecvLen - lOutLen;
	char* pCharRemain = pRecvBuf + lOutLen;
	
	//解析设备列表
	lOutLen = 0;
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToDeviceList(pCharRemain, &lOutLen, lRemainLen, pDevList);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}
	
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;	
}

/**
 *  $(获取所有设备类型信息).
 *  $(Detail).
 *
 *  @param  -[out]  CVideoProduct*  pDevTypeList: [获取到的设备类型信息]
 *  @param  -[in]  long  lTimeOut: [超时时间]
 *  @return $(Return).
 *
 *  @version     08/28/2009  zhucongfeng  Initial Version.
 */

long CTimerTask::GetAllDevTypeInfo(CVideoProductList* pDevTypeList, long lTimeOut)
{
	long lRet = ICV_SUCCESS;
	
	if(pDevTypeList == NULL)
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	//申请缓冲区
	long lBufLen = VIDEO_MESSAGE_MAX_SIZE;
	char* szBuffer = new char[lBufLen];
	memset(szBuffer, 0, lBufLen);
	
	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(szBuffer, lBufLen, VIDEO_CMD_GET_ALL_PRODUCT, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		SAFE_DELETE_ARRAY(szBuffer);
		return lRet;
	}
	
	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	
	lRet = SendMsgToServer(szBuffer, lOutLen, &pRecvBuf, lRecvLen, lTimeOut);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}
	
	//解析消息头
	long lResult;
	lRet = NET_WRAPPER->ParserMsgHeader(pRecvBuf, lResult, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}
	
	if(lResult != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lResult;
	}
	
	long lRemainLen = lRecvLen - lOutLen;
	char* pCharRemain = pRecvBuf + lOutLen;
	
	//解析设备类型列表
	lOutLen = 0;
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToProductList(pCharRemain, &lOutLen, lRemainLen, pDevTypeList);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}
	
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;	
}

//向服务器端发送消息
long CTimerTask::SendMsgToServer(char* pMsgBuffer, long lMsgLength, char **pRecvBuf, long &lRecvLen, long lTimeOut)
{
	long lRet = NET_WRAPPER->SendAndReceiveMsg(pMsgBuffer, lMsgLength, (void**)pRecvBuf, lRecvLen, lTimeOut);
	
	SAFE_DELETE_ARRAY(pMsgBuffer);
	
	return lRet;
}

/**
 *  $(获取需要同步的设备信息，获取地址指针).
 *  $(Detail).
 *
 *  @return $(Return).
 *
 *  @version     08/28/2009  zhucongfeng  Initial Version.
 */

long CTimerTask::GetTimeSynchDevInfo()
{
	long lRet = ICV_SUCCESS;

	//获取所有设备信息
	CVideoDeviceList listDev;
	lRet = GetAllDeviceInfo(&listDev, GET_INFO_FROM_SERVER_WAIT_TIME);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR,_("GetAllDeviceInfo failed, RetCode: %d"), lRet);
		return lRet;
	}

	//获取所有设备类型信息
	CVideoProductList listDevType;
	lRet = GetAllDevTypeInfo(&listDevType, GET_INFO_FROM_SERVER_WAIT_TIME);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR,_("GetAllDevTypeInfo failed, RetCode: %d"), lRet);
		return lRet;
	}

	//设置设备信息map
	m_mapVideoDevType.clear();
	int nCount = listDevType.GetSize();
	int i=0;
	for(i=0;i<nCount;i++)
	{
		CVideoProduct* pProduct = listDevType.GetAt(i);
		if(pProduct == NULL)
			continue;

		//获取dll地址指针
		VIDEO_DRIVER_DLL dllDriver;
		if(GetDriverDLL(pProduct, dllDriver) == ICV_SUCCESS)
			m_mapVideoDevType.insert(VIDEODEVTYPEMAP::value_type(dllDriver.lProductID, dllDriver));
	}

	//获取设备信息list
	m_listDevice.clear();
	nCount = listDev.GetSize();
	for(i=0;i<nCount;i++)
	{
		CVideoDevice* pDevice = listDev.GetAt(i);
		if(pDevice == NULL)
			continue;

		//如果是硬盘录像机，就进行同步，其它的暂时不做同步
		if(IsNeedTimeSynchDev(pDevice))
			m_listDevice.InsertObject(*pDevice);
	}

	return lRet;
}

/**
 *  $(检测是否需要进行时钟同步).
 *  $(Detail).
 *
 *  @param  -[in]  CVideoDevice*  pDevice: [设备信息]
 *  @return $(需要同步，true；否则false).
 *
 *  @version     08/31/2009  zhucongfeng  Initial Version.
 */

bool CTimerTask::IsNeedTimeSynchDev(CVideoDevice* pDevice)
{
	bool bSynch = false;

	if(pDevice == NULL)
		bSynch = false;

	//获取设备类型信息
	PVIDEO_DRIVER_DLL pDriver = NULL;
	long lRet = GetDevTypeByID(pDevice->GetProductID(), pDriver);
	if(lRet != ICV_SUCCESS)
		bSynch = false;
	else
	{
		if(pDriver == NULL)
			bSynch = false;
		else
		{
			//检测设备类型，决定是否进行时钟同步
			switch(pDriver->lDeviceType)
			{
			case VIDEO_DEVICE_TYPE_DVR:
			case VIDEO_DEVICE_TYPE_ENCODER:
				bSynch = true;
				break;

			default:
				bSynch = false;
				break;
			}
		}
	}

	return bSynch;
}

/**
 *  $(根据设备类型编号获取设备驱动信息).
 *  $(Detail).
 *
 *  @param  -[in]  long  lID: [设备类型编号]
 *  @param  -[out]  PVIDEO_DRIVER_DLL &pDriver: [驱动信息]
 *  @return $(Return).
 *
 *  @version     08/31/2009  zhucongfeng  Initial Version.
 */

long CTimerTask::GetDevTypeByID(long lID, PVIDEO_DRIVER_DLL &pDriver)
{
	long lRet = ICV_SUCCESS;

	VIDEODEVTYPEMAP::iterator it = m_mapVideoDevType.find(lID);
	if(it == m_mapVideoDevType.end() )
		lRet = EC_ICV_CCTV_DEVTYPENOTEXIST;
	else
	{
		pDriver = &(it->second);
	}

	return lRet;
}

/**
 *  $(获取驱动或插件dll).
 *  $(Detail).
 *
 *  @param  -[in]  CVideoProduct* pProduct: [设备类型]
 *  @param  -[in]  VIDEO_DRIVER_DLL&  driver: [设备驱动dll]
 *  @return $(Return).
 *
 *  @version     08/31/2009  zhucongfeng  Initial Version.
 */

long CTimerTask::GetDriverDLL(CVideoProduct* pProduct, VIDEO_DRIVER_DLL &driver)
{
	long lRet = ICV_SUCCESS;

	char szName[MAXNAMELEN];
	memset(szName, 0, sizeof(szName));
	sprintf(szName, "%s.dll", pProduct->GetDllName());

	//取名称及ID
	strcpy(driver.szDLLName, pProduct->GetDllName());
	driver.lProductID = pProduct->GetID();
	driver.lDeviceType = pProduct->GetDeviceType();

	//打开dll
	lRet = m_hDrvDll.open(szName, RTLD_LAZY, 0);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}

	//获取初始化
	driver.pfInitSDK = (DllFuncVideoInitSDK)m_hDrvDll.symbol("VideoInitSDK");
	if(driver.pfInitSDK == NULL)
	{
		m_hDrvDll.close();
		return EC_ICV_CCTV_LOADDLLFAILED;
	}

	//获取登陆
	driver.pfLogin = (DllFuncVideoLogin)m_hDrvDll.symbol("VideoLogin");
	if(driver.pfLogin == NULL)
	{
		m_hDrvDll.close();
		return EC_ICV_CCTV_LOADDLLFAILED;
	}

	//获取退出
	driver.pfLogout = (DllFuncVideoLogout)m_hDrvDll.symbol("VideoLogout");
	if(driver.pfLogout == NULL)
	{
		m_hDrvDll.close();
		return EC_ICV_CCTV_LOADDLLFAILED;
	}

	//获取时钟同步
	driver.pfSetDeviceTime = (DllFuncVideoSetDeviceTime)m_hDrvDll.symbol("VideoSetDeviceTime");
	if(driver.pfSetDeviceTime == NULL)
	{
		m_hDrvDll.close();
		return EC_ICV_CCTV_LOADDLLFAILED;
	}

	//关闭dll
	m_hDrvDll.close();

	//调用初始化
	if(driver.pfInitSDK != NULL)
	{
		lRet = driver.pfInitSDK();
		
		if(lRet != ICV_SUCCESS)
		{
			CVLog.LogMessage(LOG_LEVEL_ERROR, _("Drivere(%s.dll) init sdk error, return code: %d"), driver.szDLLName, lRet);
			return lRet;
		}
	}
	else
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, _("Drivere(%s.dll) init sdk error, can't find init sdk port"), driver.szDLLName);
		return EC_ICV_CCTV_LOADDLLFAILED;
	}


	return lRet;
}

// 启动线程
long CTimerTask::StartTask()
{
	long lRet = ICV_SUCCESS;

	//获取设备信息
	lRet = GetTimeSynchDevInfo();
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR,_("GetTimeSynchDevInfo failed, startTask Failed, RetCode: %d"), lRet);
		return lRet;
	}

	m_lShouldExit = 0;
	int nRet = (long)this->activate();
	if(nRet != 0)
	{
		lRet = -1;
		CVLog.LogMessage(LOG_LEVEL_ERROR,_("StartTask Failed, RetCode: %d"), lRet);
	}	
	return lRet;
}
// 结束线程
long CTimerTask::StopTask()
{
	m_lShouldExit = 1;
	CVLog.LogMessage(LOG_LEVEL_ERROR,_("timer task received stop msg"));
	return ICV_SUCCESS;	
}
// 线程处理函数
int	CTimerTask::svc()
{
	ACE_Time_Value tvTimeout;
	tvTimeout.msec(DEFAULT_WAIT_INTERVAL_TIME);
	while(!m_lShouldExit)
	{
		//每1秒等一次，循环，防止退出时等待时间超长
		for(int i=0;i<VIDEO_TIME_SYNCH_CONFIG->m_lIntervalTime;i++)
		{
			if(m_lShouldExit)
				break;

			ACE_OS::sleep(tvTimeout);
		}

		if(m_lShouldExit)
				break;		

		//逐个处理定时任务
		ProcessTimeSynch();
	}
	CVLog.LogMessage(LOG_LEVEL_ERROR,_("timer task stoped"));
	return ICV_SUCCESS;
}

/**
 *  $(处理时钟同步).
 *  $(Detail).
 *
 *  @return $(Return).
 *
 *  @version     08/31/2009  zhucongfeng  Initial Version.
 */

void CTimerTask::ProcessTimeSynch()
{
	int nCount = m_listDevice.GetSize();
	for(int i=0;i<nCount;i++)
	{
		CVideoDevice* pDevice = m_listDevice.GetAt(i);
		if(pDevice == NULL)
			continue;

		//获取驱动信息
		PVIDEO_DRIVER_DLL pDriver = NULL;
		long lRet = GetDevTypeByID(pDevice->GetProductID(), pDriver);
		if(lRet != ICV_SUCCESS || pDriver == NULL)
		{
			CVLog.LogMessage(LOG_LEVEL_ERROR, _("Device(%s) time synch error, RetCode:%d, can't find device type"), pDevice->GetName(), lRet);
			continue;
		}

		long lLoginID;
		//调用登陆接口
		if(pDriver->pfLogin != NULL)
		{
			lRet = pDriver->pfLogin(pDevice->GetIP(), strlen(pDevice->GetIP()), pDevice->GetPort(), 
				pDevice->GetExtent(), strlen(pDevice->GetExtent()), lLoginID);

			if(lRet != ICV_SUCCESS)
			{
				CVLog.LogMessage(LOG_LEVEL_ERROR, _("Device(%s) time synch error, (%s.dll) login in error, return code: %d"), pDevice->GetName(), pDriver->szDLLName, lRet);
				continue;
			}
		}
		else
		{
			CVLog.LogMessage(LOG_LEVEL_ERROR, _("Device(%s) time synch error, (%s.dll) can't find login in port"), pDevice->GetName(), pDriver->szDLLName);
			continue;
		}

		//调用同步接口
		if(pDriver->pfSetDeviceTime != NULL)
		{
			time_t timeNow;
			time(&timeNow);
			lRet = pDriver->pfSetDeviceTime(lLoginID, timeNow);
			
			if(lRet != ICV_SUCCESS)
			{
				CVLog.LogMessage(LOG_LEVEL_ERROR, _("Device(%s) time synch error, (%s.dll) set device time error, return code: %d"), pDevice->GetName(), pDriver->szDLLName, lRet);
				DeviceLogout(pDriver, lLoginID);
				continue;
			}
		}
		else
		{
			CVLog.LogMessage(LOG_LEVEL_ERROR, _("Device(%s) time synch error, (%s.dll) can't find set device time port"), pDevice->GetName(), pDriver->szDLLName);
			DeviceLogout(pDriver, lLoginID);
			continue;
		}

		//调用注销接口
		lRet = DeviceLogout(pDriver, lLoginID);
		if(lRet != ICV_SUCCESS)
			continue;

		CVLog.LogMessage(LOG_LEVEL_INFO, _("Device(%s) time synch success"), pDevice->GetName());
	}
}

/**
 *  $(设备注销).
 *  $(Detail).
 *
 *  @param  -[in]  PVIDEO_DRIVER_DLL  pDriver: [驱动信息]
 *  @param  -[in]  long  lLoginID: [登陆ID]
 *  @return $(Return).
 *
 *  @version     08/31/2009  zhucongfeng  Initial Version.
 */

long CTimerTask::DeviceLogout(PVIDEO_DRIVER_DLL pDriver, long lLoginID)
{
	long lRet = ICV_SUCCESS;

	if(pDriver == NULL)
		return EC_ICV_CCTV_DEVTYPENOTEXIST;

	if(pDriver->pfLogout == NULL)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, _("DeviceType(%s.dll) logout error, can't find login out port"), pDriver->szDLLName);
		return EC_ICV_CCTV_LOADDLLFAILED;
	}
	else
	{
		long lRet = pDriver->pfLogout(lLoginID);
		if(lRet != ICV_SUCCESS)
		{
			CVLog.LogMessage(LOG_LEVEL_ERROR, _("DeviceType(%s.dll) logout error, return code: %d"), pDriver->szDLLName, lRet);
			return lRet;
		}
	}

	return lRet;
}

