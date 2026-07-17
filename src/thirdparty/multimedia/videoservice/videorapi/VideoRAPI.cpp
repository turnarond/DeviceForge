/**************************************************************
 *  Filename:    VideoRAPI.cpp
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: 视频远程API接口实现.
 *	用例编号：113206, 113208
 *
 *  @author:     zhucongfeng
 *  @version     04/20/2009  zhucongfeng  Initial Version
 *			     04/22/2009  zhucongfeng  添加根据摄像头名称模糊查询摄像头信息的接口及根据摄像头名称获取关联的硬盘录像机信息的接口
 *				 04/27/2009  zhucongfeng  修改获取硬盘录像机信息的接口，添加根据摄像头名称获取摄像头信息的接口及通过设备ID获取设备信息的接口
 *				 04/28/2009	 zhucongfeng  提供VRN接口
 *				 05/05/2009	 zhucongfeng  根据分区获取摄像头信息改为提供分区编号；修改VRN的参数；修改同行评审缺陷
 *				 05/05/2009	 zhucongfeng  修改测试缺陷，VR_GetCamInfoByCamName中解析错误时，返回查不到
 *				 06/01/2009	 zhucongfeng  添加获取图片内容、删除图片接口
 *				 06/04/2009	 zhucongfeng  获取硬盘录像机信息接口中返回添加：设备自定义信息;设备名称;设备描述
 *				 06/09/2009	 zhucongfeng  修改删除图片的接口，删除时传入删除的个数；获取硬盘录像机信息时，返回通道号
 *				 06/09/2009	 zhucongfeng  修改根据摄像头名称获取录像机信息的接口，返回的时候拷贝所有的信息
 *				 06/09/2009	 zhucongfeng  获取抓拍图片内容及删除抓拍图片，删除接口中的摄像头参数
 *				 06/23/2009	 zhucongfeng  增加接口VRN_GetCameras2，VRN_GetRecordInfoByCamName2，VR_GetSnapPicBuffer2，所需
                                          缓冲区长度由外部分配，(.net不方便调用**的参数)。长度不够时直接返回错误。
 *				 06/24/2009	 zhucongfeng  外部声明的缓冲区，用不完的置空
 *				 07/16/2009	 chenzhazhan  增加接口VR_DeletePsp，VR_ModifyPsp，预置位修改和删除
 *				 08/11/2009	 zhucongfeng  根据静态代码检查修改参数合法性判断
 *				 02/08/2010	 chenzhan	  批量删除图片和录像查询删除下载接口
 *				 03/08/2010	 chenzhan	  修改char[VIDEO_MESSAGE_MAX_SIZE]用new为使用数组
 *				 03/11/2010  chenzhan     修改历史录像路径合法判断
 *				 03/12/2010  chenzhan     修改历史录像文件合法判断
 *				 07/20/2010	 zhucongfeng  修改远程接口，添加句柄参数
 *				 07/24/2010  lixiaohao    配置信息远程接口，预置位管理远程接口
 *				 07/24/2010  lixiaohao    模式管理远程接口
 *				 08/14/2010  zhucongfeng  增加自定义分区远程接口，客户端管理
 *				 08/17/2010  lixiaohao    增加录像，抓拍，控制远程接口
 *               08/30/2010  lixiaohao    将作为输出参数的VideoList，修改为char*形式的buffer，缓冲区由调用方分配
 *               02/14/2011  louzhengqing 把字符串传递改为结构体传递，同时新增三个接口
 *               09/15/2011  zhucongfeng  注册客户端时，注册远程Queue，主备机只要有个成功就算成功
 *               05/06/2012  zhucongfeng  预置位序号长度由2位改为3位，原来设备的预置位是0～64，当前看有些设备预置位0～256

**************************************************************/

#include "ace/Mem_Map.h"
#include "multimedia/video/VideoRAPI.h"
#include "errcode/ErrCode_iCV_Common.hxx"
#include "NetWrapper.h"
#include "GlobalManager.h"
#include "common/cvGlobalHelper.h"
#include "common/cvcomm.hxx"
#include "gettext/libintl.h"

#define _(STRING) gettext(STRING)
#define TIME_BYTE_COUNT 6

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

namespace
{
	class ACEIniter
	{
	public:
		ACEIniter()
		{
			m_nInit = ACE::init();
		}
		~ACEIniter()
		{
			if (m_nInit == 0)
			{
				ACE::fini();
			}
		}
		int m_nInit;
	};
	
	static ACEIniter g_ACEIniter;
	
}

/***************************************************************
*私有函数
***************************************************************/
long SendMsgToServer(PCLIENT_INFO pClient, char* pMsgBuffer, long lMsgLength, char **pRecvBuf, long &lRecvLen, long lTimeOut);
char* GetNextToken(char* pszSrc, char* pszDst, long nDstBuffSize, char token = ',');
time_t StrToTime(char* pszTime);
bool IsValidTime(struct tm tmTime);
long SendAndRecvMsg(PCLIENT_INFO pClient,char* szBuffer,long lMsgLen,long lTimeOut,char* pCharRemain,long lRemainLen);

/***************************************************************
*转变list为strcut类型的数组
****************************************************************/
//转变list为strcut类型的数组
long TransProductList2ProdctArray(SVideoProduct** ppVideoProduct, int* pnVideoProduct,  CVideoProductList* plistProduct);
long TransDevList2DevArray(SVideoDevice** ppVideoDevice,  int* pnVideoDevice,  CVideoDeviceList* plistDev);
long TransZoneList2ZoneArray(SVideoZone** ppVideoZone, int* pnVideoZone, CVideoZoneList* plistZone);
long TransCamList2CamArray(SVideoCamera** ppVideoCamera,  int* pnVideoCamera, CVideoCameraList* plistCam);
long TransMonList2MonArray(SVideoMonitor** ppVideoMonitor, int* pnVideoMonitor, CVideoMonitorList* plistMon);
long TransPSPList2PSPArray(SVideoPSP** ppVideoPSP, int* pnVideoPSP, CVideoPSPList* plistPSP);
long TransModeList2ModeArray(SVideoMode** ppVideoMode, int* pnVideoMode, CVideoModeList* plistMode);
long TransSnapFileList2SnapFileArray(SVideoSnapFile** ppVideoSnapFile, int* pnVideoSnapFile,
									 CVideoSnapFileList* plistSnapFile);
long TransRecordList2RecordArray(SRecord** ppRecord, int* pnRecord, CRecordList* plistRecord);
long TransCusZoneList2CusZoneArray(SVideoCustomZone** ppCusZone, int* pnCusZone, CVideoCustomZoneList* plistCusZone);

/**
 *  设备产品型号列表LIST转变为设备产品型号数组
 *  @param  -[out] SVideoProduct** ppVideoProduct:	  设备产品型号数组指针
 *  @param  -[out] int* pnVideoProduct:	              设备产品型号个数
 *  @param  -[in]  CVideoProductList* plistProduct:   设备产品型号列表 
 *  @return  ==0 success  !=0 failure
 *  @version     01/19/2011  louzhengqing  Initial Version.
 */
long TransProductList2ProdctArray(SVideoProduct** ppVideoProduct, int* pnVideoProduct, CVideoProductList* plistProduct)
{
	if (plistProduct == NULL)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	
	//初始化
	*ppVideoProduct = NULL;
	*pnVideoProduct = 0;

	//List转化为数组
	*pnVideoProduct = plistProduct->GetSize();
	if (plistProduct->GetSize() == 0)
	{
		*ppVideoProduct = NULL;
		return ICV_SUCCESS;
	}
	*ppVideoProduct = new SVideoProduct[*pnVideoProduct];
	memset(*ppVideoProduct, 0x00, sizeof(ppVideoProduct)*(*pnVideoProduct));
	int nIndex = 0;
	for (CVideoProductList::iterator it = plistProduct->begin(); it != plistProduct->end(); it++)
	{
		(*(*ppVideoProduct + nIndex)).m_nDeviceType = it->GetDeviceType();
		(*(*ppVideoProduct + nIndex)).m_nID = it->GetID();
		strncpy((*(*ppVideoProduct + nIndex)).m_szName, it->GetName(), VIDEO_NAME_MAXSIZE);
		strncpy((*(*ppVideoProduct + nIndex)).m_szDllName, it->GetDllName(), VIDEO_NAME_MAXSIZE);
		nIndex ++;
	}
	
	return ICV_SUCCESS;
}

/**
 *  设备产品列表LIST转变为设备数组
 *  @param  -[out] SVideoDevice** ppVideoDevice:	设备指针
 *  @param  -[out] int* pnVideoDevice:	            设备个数
 *  @param  -[in]  CVideoDeviceList* plistDev:      设备列表 
 *  @return  ==0 success  !=0 failure
 *  @version     01/19/2011  louzhengqing  Initial Version.
 */
long TransDevList2DevArray(SVideoDevice** ppVideoDevice,  int* pnVideoDevice, CVideoDeviceList* plistDev)
{
	if (ppVideoDevice == NULL)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}

	//初始化
	*ppVideoDevice = NULL;
	*pnVideoDevice = 0;

	//List转化为数组
	*pnVideoDevice = plistDev->GetSize();
	if (plistDev->GetSize() == 0)
	{
		*ppVideoDevice = NULL;
		return ICV_SUCCESS;
	}
	*ppVideoDevice = new SVideoDevice[*pnVideoDevice];
	memset(*ppVideoDevice, 0x00, sizeof(ppVideoDevice)*(*pnVideoDevice));
	int nIndex = 0;
	for (CVideoDeviceList::iterator it = plistDev->begin(); it != plistDev->end(); it++)
	{
		(*(*ppVideoDevice + nIndex)).m_nServerID = (*it).GetServerID();
		(*(*ppVideoDevice + nIndex)).m_nProductID = (*it).GetProductID();
		(*(*ppVideoDevice + nIndex)).m_nID = (*it).GetID();
		strncpy((*(*ppVideoDevice + nIndex)).m_szName, (*it).GetName(), VIDEO_NAME_MAXSIZE);
		strncpy((*(*ppVideoDevice + nIndex)).m_szIP, (*it).GetIP(), VIDEO_IPADDRESS_MAXSIZE);
		(*(*ppVideoDevice + nIndex)).m_nPort = (*it).GetPort();
		strncpy((*(*ppVideoDevice + nIndex)).m_szExtent, (*it).GetExtent(), VIDEO_DEFAULTINFO_MAXSIZE);
		strncpy((*(*ppVideoDevice + nIndex)).m_szDescription, (*it).GetDescription(), VIDEO_DESCRIPTION_MAXSIZE);
		(*(*ppVideoDevice + nIndex)).m_nCtrlPort = (*it).GetCtrlPort();
		(*(*ppVideoDevice + nIndex)).m_nLoginID = (*it).GetLoginID();
		nIndex ++;
	}

	return ICV_SUCCESS;
}

/**
 *  设备产品列表LIST转变为设备数组
 *  @param  -[out] SVideoZone** ppVideoZone:		固定分区指针
 *  @param  -[out] int* pnVideoZone:		        固定分区个数
 *  @param  -[in]  CVideoZoneList* plistZone:       固定分区列表 
 *  @return  ==0 success  !=0 failure
 *  @version     01/19/2011  louzhengqing  Initial Version.
 */
long TransZoneList2ZoneArray(SVideoZone** ppVideoZone, int* pnVideoZone, CVideoZoneList* plistZone)
{
	if (plistZone == NULL)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}

	//初始化
	*ppVideoZone = NULL;
	*pnVideoZone = 0;

	//List转化为数组
	*pnVideoZone = plistZone->GetSize();
	if (plistZone->GetSize() == 0)
	{
		*ppVideoZone = NULL;
		return ICV_SUCCESS;
	}
	*ppVideoZone = new SVideoZone[*pnVideoZone];
	memset(*ppVideoZone, 0x00, sizeof(ppVideoZone)*(*pnVideoZone));
	int nIndex = 0;
	for (CVideoZoneList::iterator it = plistZone->begin(); it != plistZone->end(); it++)
	{
		(*(*ppVideoZone + nIndex)).m_nID = (*it).GetID();
		strncpy((*(*ppVideoZone + nIndex)).m_szName, (*it).GetName(), VIDEO_NAME_MAXSIZE);
		strncpy((*(*ppVideoZone + nIndex)).m_szDescription, (*it).GetDescription(), VIDEO_DESCRIPTION_MAXSIZE);
		strncpy((*(*ppVideoZone + nIndex)).m_szAuthLabel, (*it).GetAuthLabel(), VIDEO_NAME_MAXSIZE);
		(*(*ppVideoZone + nIndex)).m_nAuthField = (*it). GetAuthField();
		nIndex ++;
	}

	return ICV_SUCCESS;
}

/**
 *  设备产品列表LIST转变为设备数组
 *  @param  -[out] SVideoCamera** ppVideoCamera:	摄像头指针
 *  @param  -[out] int* pnVideoCamera:	            摄像头个数
 *  @param  -[in]  CVideoCameraList* plistCam:      摄像头列表 
 *  @return  ==0 success  !=0 failure
 *  @version     01/19/2011  louzhengqing  Initial Version.
 */
long TransCamList2CamArray(SVideoCamera** ppVideoCamera,  int* pnVideoCamera, CVideoCameraList* plistCam)
{
	if (plistCam == NULL)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	
	//初始化
	*ppVideoCamera = NULL;
	*pnVideoCamera = 0;
		
	//List转化为数组
	*pnVideoCamera = plistCam->GetSize();
	if (plistCam->GetSize() == 0)
	{
		*ppVideoCamera = NULL;
		return ICV_SUCCESS;
	}
	*ppVideoCamera = new SVideoCamera[*pnVideoCamera];
	memset(*ppVideoCamera, 0x00, sizeof(ppVideoCamera)*(*pnVideoCamera));
	int nIndex = 0;
	for (CVideoCameraList::iterator it = plistCam->begin(); it != plistCam->end(); it++)
	{
		(*(*ppVideoCamera + nIndex)).m_nServerID = (*it).GetServerID();			
		(*(*ppVideoCamera + nIndex)).m_nZoneID = (*it).GetZoneID();
		strncpy((*(*ppVideoCamera + nIndex)).m_szName, (*it).GetName(), VIDEO_NAME_MAXSIZE);
		(*(*ppVideoCamera + nIndex)).m_nID = (*it).GetID();
		strncpy((*(*ppVideoCamera + nIndex)).m_szDescription, (*it).GetDescription(), VIDEO_DESCRIPTION_MAXSIZE);
		(*(*ppVideoCamera + nIndex)).m_bRemoteFileCtrl = (*it).GetIsRemoteFileCtrl();			
		(*(*ppVideoCamera + nIndex)).m_bLocalFileCtrl = (*it).GetIsLocalFileCtrl();
		(*(*ppVideoCamera + nIndex)).m_bRemoteTimeCtrl = (*it).GetIsRemoteTimeCtrl();			
		(*(*ppVideoCamera + nIndex)).m_bOrientCtrl = (*it).GetIsOrientCtrl();
		(*(*ppVideoCamera + nIndex)).m_bPSPCtrl = (*it).GetIsPSPCtrl();			
		(*(*ppVideoCamera + nIndex)).m_bLensCtrl = (*it).GetIsLensCtrl();
		(*(*ppVideoCamera + nIndex)).m_bQualityCtrl = (*it).GetIsQualityCtrl();			
		(*(*ppVideoCamera + nIndex)).m_bSnapCtrl = (*it).GetIsSnapCtrl();
		(*(*ppVideoCamera + nIndex)).m_bHeaterCtrl = (*it).GetIsHeaterCtrl();			
		(*(*ppVideoCamera + nIndex)).m_bRainBrushCtrl = (*it).GetIsRainBrushCtrl();
		(*(*ppVideoCamera + nIndex)).m_nSnapWidth = (*it).GetSnapWidth();			
		(*(*ppVideoCamera + nIndex)).m_nSnapHeight = (*it).GetSnapHeight();
		(*(*ppVideoCamera + nIndex)).m_nSnapBitDeep = (*it).GetSnapBitDeep();			
		(*(*ppVideoCamera + nIndex)).m_nPSPMaxCount = (*it).GetPSPMaxCount();
		SVideoPSP tempSVideoPSP;
		CVideoPSPList tempCVideoPSPList;
		tempCVideoPSPList = *(it->GetPSP());
		int nIndexVideoPSP = 0;
		for (CVideoPSPList::iterator itPSPList = tempCVideoPSPList.begin(); itPSPList != tempCVideoPSPList.end(); itPSPList++)
		{
			tempSVideoPSP = (*(*ppVideoCamera + nIndex)).m_listPSP[nIndexVideoPSP];
			strncpy(tempSVideoPSP.m_szName, itPSPList->GetName(), VIDEO_NAME_MAXSIZE);
			strncpy(tempSVideoPSP.m_szDescription, itPSPList->GetDescription(), VIDEO_DESCRIPTION_MAXSIZE);
			tempSVideoPSP.m_nPresetIndex = itPSPList->GetPresetIndex();
			nIndexVideoPSP++;
		}
		nIndex ++;
		}

	return ICV_SUCCESS;
}

/**
 *  监视器列表LIST转变为监视器数组
 *  @param  -[out] SVideoMonitor** ppVideoMonitor:	监视器指针
 *  @param  -[out] int* pnVideoMonitor:	            监视器个数
 *  @param  -[in]  CVideoMonitorList* plistMon:     监视器列表 
 *  @return  ==0 success  !=0 failure
 *  @version     01/19/2011  louzhengqing  Initial Version.
 *  @version     05/17/2012  huangming 为支持新的NVR需要新加三个属性 
 */
long TransMonList2MonArray(SVideoMonitor** ppVideoMonitor, int* pnVideoMonitor, CVideoMonitorList* plistMon)
{
	if (plistMon == NULL)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	
	//初始化
	*ppVideoMonitor = NULL;
	*pnVideoMonitor = 0;

	//List转化为数组
	*pnVideoMonitor = plistMon->GetSize();
	if (plistMon->GetSize() == 0)
	{
		*ppVideoMonitor = NULL;
		return ICV_SUCCESS;
	}
	*ppVideoMonitor = new SVideoMonitor[*pnVideoMonitor];
	memset(*ppVideoMonitor, 0x00, sizeof(ppVideoMonitor)*(*pnVideoMonitor));
	int nIndex = 0;
	for (CVideoMonitorList::iterator it = plistMon->begin(); it != plistMon->end(); it++)
	{
		(*(*ppVideoMonitor + nIndex)).m_nServerID = (*it).GetServerID();
		(*(*ppVideoMonitor + nIndex)).m_nZoneID = (*it).GetZoneID();
		(*(*ppVideoMonitor + nIndex)).m_nID = (*it).GetID();
		strncpy((*(*ppVideoMonitor+nIndex)).m_szName, (*it).GetName(), VIDEO_NAME_MAXSIZE);
		strncpy((*(*ppVideoMonitor+nIndex)).m_szDescription, (*it).GetDescription(), VIDEO_DESCRIPTION_MAXSIZE);

        //以下三个属性为新加的,为支持新的NVR需要 huangming 2012-05-17
        strncpy((*(*ppVideoMonitor + nIndex)).m_szChannel, (*it).GetLinkDev()->GetChannel(), VIDEO_CHANNEL_MAXSIZE);
        (*(*ppVideoMonitor + nIndex)).m_nLinkDevID = (*it).GetLinkDev()->GetDeviceID();
        (*(*ppVideoMonitor + nIndex)).m_nLinkDevType = (*it).GetLinkDev()->GetLinkDevType();
		nIndex ++;
	}

	return ICV_SUCCESS;
}

/**
 *  预置位实体列表LIST转变为预置位实体数组
 *  @param  -[out] SVideoPSP** ppVideoPSP:	    预置位实体指针
 *  @param  -[out] int* pnVideoPSP:	            预置位实体个数
 *  @param  -[in]  CVideoPSPList* plistPSP:     预置位实体列表 
 *  @return  ==0 success  !=0 failure
 *  @version     01/19/2011  louzhengqing  Initial Version.
 */
long TransPSPList2PSPArray(SVideoPSP** ppVideoPSP, int* pnVideoPSP, CVideoPSPList* plistPSP)
{
	if (plistPSP == NULL)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	
	//初始化
	*ppVideoPSP = NULL;
	*pnVideoPSP = 0;
	
	//List转化为数组
	*pnVideoPSP = plistPSP->GetSize();
	if (plistPSP->GetSize() == 0)
	{
		*ppVideoPSP = NULL;
		return ICV_SUCCESS;
	}
	*ppVideoPSP = new SVideoPSP[*pnVideoPSP];
	memset(*ppVideoPSP, 0x00, sizeof(ppVideoPSP)*(*pnVideoPSP));
	int nIndex = 0;
	for (CVideoPSPList::iterator it = plistPSP->begin(); it != plistPSP->end(); it++)
	{
		(*(*ppVideoPSP + nIndex)).m_nPresetIndex = (*it).GetPresetIndex();
		strncpy((*(*ppVideoPSP+nIndex)).m_szName, (*it).GetName(), VIDEO_NAME_MAXSIZE);
		strncpy((*(*ppVideoPSP+nIndex)).m_szDescription, (*it).GetDescription(), VIDEO_DESCRIPTION_MAXSIZE);
		nIndex ++;
	}
	
	return ICV_SUCCESS;
}

/**
 *  模式实体列表LIST转变为模式实体数组
 *  @param  -[out] SVideoMode** ppVideoMode:	  模式实体指针
 *  @param  -[out] int* pnVideoMode:	          模式实体个数
 *  @param  -[in]  CVideoModeList* plistMode:     模式实体列表 
 *  @return  ==0 success  !=0 failure
 *  @version     01/19/2011  louzhengqing  Initial Version.
 */
long TransModeList2ModeArray(SVideoMode** ppVideoMode, int* pnVideoMode, CVideoModeList* plistMode)
{
	if (plistMode == NULL)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	
	//初始化
	*ppVideoMode = NULL;
	*pnVideoMode = 0;
	
	//List转化为数组
	*pnVideoMode = plistMode->GetSize();
	if (plistMode->GetSize() == 0)
	{
		*ppVideoMode = NULL;
		return ICV_SUCCESS;
	}
	*ppVideoMode = new SVideoMode[*pnVideoMode];
	memset(*ppVideoMode, 0x00, sizeof(ppVideoMode)*(*pnVideoMode));
	int nIndex = 0;
	for (CVideoModeList::iterator it = plistMode->begin(); it != plistMode->end(); it++)
	{
		(*(*ppVideoMode + nIndex)).m_nType = it->GetType();
		strncpy((*(*ppVideoMode + nIndex)).m_szName, it->GetName(), VIDEO_NAME_MAXSIZE);
		strncpy((*(*ppVideoMode + nIndex)).m_szPara, it->GetPara(), VIDEO_MODE_PARA_MAXSIZE);
		strncpy((*(*ppVideoMode + nIndex)).m_szDescription, it->GetDescription(), VIDEO_DESCRIPTION_MAXSIZE);
		int nIndexList = 0;
		for (CVideoList<long>::iterator itVideoList = ((it->GetListZone())->begin()); itVideoList != ((it->GetListZone())->end()); itVideoList++)
		{
			(*(*ppVideoMode + nIndex)).m_listZone[nIndexList] = *itVideoList;
			nIndexList++;
		}
		(*(*ppVideoMode + nIndex)).m_nZone = nIndexList;
		nIndex ++;
	}
	
	return ICV_SUCCESS;
}

/**
 *  抓拍图片实体列表LIST转变为抓拍图片实体数组
 *  @param  -[out] SVideoSnapFile** ppVideoSnapFile:	  抓拍图片实体指针
 *  @param  -[out] int* pnVideoSnapFile:	              抓拍图片实体个数
 *  @param  -[in]  CVideoSnapFileList* plistSnapFile:     抓拍图片实体列表 
 *  @return  ==0 success  !=0 failure
 *  @version     01/19/2011  louzhengqing  Initial Version.
 */
long TransSnapFileList2SnapFileArray(SVideoSnapFile** ppVideoSnapFile, int* pnVideoSnapFile,
									 CVideoSnapFileList* plistSnapFile)
{
	if (plistSnapFile == NULL)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	
	//初始化
	*ppVideoSnapFile = NULL;
	*pnVideoSnapFile = 0;
	
	//List转化为数组
	*pnVideoSnapFile = plistSnapFile->GetSize();
	if (plistSnapFile->GetSize() == 0)
	{
		*ppVideoSnapFile = NULL;
		return ICV_SUCCESS;
	}
	*ppVideoSnapFile = new SVideoSnapFile[*pnVideoSnapFile];
	memset(*ppVideoSnapFile, 0x00, sizeof(ppVideoSnapFile)*(*pnVideoSnapFile));
	int nIndex = 0;
	for (CVideoSnapFileList::iterator it = plistSnapFile->begin(); it != plistSnapFile->end(); it++)
	{
		(*(*ppVideoSnapFile + nIndex)).m_nID = it->GetID();
		(*(*ppVideoSnapFile + nIndex)).m_nCameraID = it->GetCameraID();
		strncpy((*(*ppVideoSnapFile + nIndex)).m_szInfoType, it->GetInfoType(), VIDEO_NAME_MAXSIZE);
		(*(*ppVideoSnapFile + nIndex)).m_tmSnap = it->GetSnapTime();
		(*(*ppVideoSnapFile + nIndex)).m_nSnapCount = it->GetSnapCount();
		strncpy((*(*ppVideoSnapFile + nIndex)).m_szExtent, it->GetExtent(), VIDEO_DEFAULTINFO_MAXSIZE);
		strncpy((*(*ppVideoSnapFile + nIndex)).m_szDescription, it->GetDescription(), VIDEO_DESCRIPTION_MAXSIZE);
		nIndex ++;
	}
	
	return ICV_SUCCESS;
}

/**
 *  录像片段信息实体列表LIST转变为录像片段信息实体数组
 *  @param  -[out] SRecord** ppRecord:	         录像片段信息实体指针
 *  @param  -[out] int* pnRecord:	             录像片段信息实体个数
 *  @param  -[in]  CRecordList* plistRecord:     录像片段信息实体列表 
 *  @return  ==0 success  !=0 failure
 *  @version     01/19/2011  louzhengqing  Initial Version.
 */
long TransRecordList2RecordArray(SRecord** ppRecord, int* pnRecord, CRecordList* plistRecord)
{
	if (plistRecord == NULL)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	
	//初始化
	*ppRecord = NULL;
	*pnRecord = 0;
	
	//List转化为数组
	*pnRecord = plistRecord->GetSize();
	if (plistRecord->GetSize() == 0)
	{
		*ppRecord = NULL;
		return ICV_SUCCESS;
	}
	*ppRecord = new SRecord[*pnRecord];
	memset(*ppRecord, 0x00, sizeof(ppRecord)*(*pnRecord));
	int nIndex = 0;
	for (CRecordList::iterator it = plistRecord->begin(); it != plistRecord->end(); it++)
	{
		(*(*ppRecord + nIndex)).m_nID = it->GetID();
		(*(*ppRecord + nIndex)).m_nCamID = it->GetCameraID();
		(*(*ppRecord + nIndex)).m_tmStart = it->GetStartTime();
		(*(*ppRecord + nIndex)).m_tmEnd = it->GetEndTime();
		strncpy((*(*ppRecord + nIndex)).m_szExtent, it->GetExtent(), VIDEO_EXTENT_MAXSIZE);
		strncpy((*(*ppRecord + nIndex)).m_szFileName, it->GetFileName(), VIDEO_FILE_NAME_MAXSIZE);
		strncpy((*(*ppRecord + nIndex)).m_szDesc, it->GetDesc(), VIDEO_DESCRIPTION_MAXSIZE);
		(*(*ppRecord + nIndex)).m_nStartSnapID = it->GetStartSnapID();
		(*(*ppRecord + nIndex)).m_nMidSnapID = it->GetMidSnapID();
		(*(*ppRecord + nIndex)).m_nEndSnapID = it->GetEndSnapID();
		strncpy((*(*ppRecord + nIndex)).m_szReserve1, it->GetReserve1(), VIDEO_DEFAULTINFO_MAXSIZE);
		strncpy((*(*ppRecord + nIndex)).m_szReserve2, it->GetReserve2(), VIDEO_DEFAULTINFO_MAXSIZE);
		strncpy((*(*ppRecord + nIndex)).m_szReserve3, it->GetReserve3(), VIDEO_DEFAULTINFO_MAXSIZE);
		nIndex ++;
	}
	
	return ICV_SUCCESS;
}

/**
 *   自定义分区实体列表LIST转变为自定义分区实体数组
 *  @param  -[out] SVideoCustomZone** ppCusZone:	  自定义分区实体指针
 *  @param  -[out] int* pnCusZone:	                  自定义分区实体个数
 *  @param  -[in]  SVideoCustomZone** ppCusZone:      自定义分区实体列表 
 *  @return  ==0 success  !=0 failure
 *  @version     01/19/2011  louzhengqing  Initial Version.
 */
long TransCustZoneList2CusZoneArray(SVideoCustomZone** ppCustZone, int* pnCustZone, CVideoCustomZoneList* plistCustZone)
{
	if (plistCustZone == NULL)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	
	//初始化
	*ppCustZone = NULL;
	*pnCustZone = 0;
	
	//List转化为数组
	*pnCustZone = plistCustZone->GetSize();
	if (plistCustZone->GetSize() == 0)
	{
		*ppCustZone = NULL;
		return ICV_SUCCESS;
	}
	*ppCustZone = new SVideoCustomZone[*pnCustZone];
	memset(*ppCustZone, 0x00, sizeof(ppCustZone)*(*pnCustZone));
	int nIndex = 0;
	for (CVideoCustomZoneList::iterator it = plistCustZone->begin(); it != plistCustZone->end(); it++)
	{
		(*(*ppCustZone + nIndex)).m_nID = it->GetID();
		strncpy((*(*ppCustZone + nIndex)).m_szName, it->GetName(), VIDEO_NAME_MAXSIZE);
		strncpy((*(*ppCustZone + nIndex)).m_szUserName, it->GetRelateUserName(), VIDEO_NAME_MAXSIZE);
		strncpy((*(*ppCustZone + nIndex)).m_szDesc, it->GetDesc(), VIDEO_DESCRIPTION_MAXSIZE);
		nIndex ++;
	}
	
	return ICV_SUCCESS;
}

/***************************************************************
*注册客户端
***************************************************************/

/**
 *  $(注册客户端信息).
 *  $(Detail).
 *
 *  @param  -[out]  HVideoClienT*  pHClient: [客户端句柄]
 *  @param  -[in]  const char*  szCliName: [客户端名称]
 *  @param  -[in]  char*  szServIp: [主机IP]
 *  @param  -[in]  int  nIsExistBak: [是否存在备机]
 *  @param  -[in]  char*  szBakIp: [备机IP]
 *  @param  -[in]  char* szUserName =  ICV_AUTH_DEFAULT_USER: [用户名称]
 *  @param  -[in]  char* szGroup =  "": [群组]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     04/21/2009  zhucongfeng  Initial Version.
 *  @version     07/20/2010  zhucongfeng  Initial Version.
 */

long VR_RegisterClient(HVideoClienT* pHClient, const char *szCliName, char *szServIp, int nIsExistBak, char *szBakIp, char* szUserName, char* szGroup)
{
	long lRet = ICV_SUCCESS;

	//检测参数合法性，用户名和群组暂时不检测
	if((szCliName == NULL) || (szCliName[0] == '\0'))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	if((szServIp == NULL) || (szServIp[0] == '\0') || ((nIsExistBak != 0) && ((szBakIp == NULL) || (szBakIp[0] == '\0'))))
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	
	CLIENT_INFO node;
	int nRegCliNumber = -1;
	nRegCliNumber = GM_HELPER->GetRegClientNum();
	//第一次注册,初始化该dll
	if(nRegCliNumber <= 0)
	{
		VIDEO_CHECK_FAIL_RETURN(NET_WRAPPER->InitializeNetwork());
		VIDEO_CHECK_FAIL_RETURN(GM_HELPER->Init());
	}
	
	//注册主机信息
	bool bServExist = false;
	VIDEO_CHECK_FAIL_RETURN(GM_HELPER->IsServerExist(szServIp, bServExist));
	HQUEUE hServQue = NULL;
	long lServPort = GM_HELPER->GetServPort();
	bool bMainSvrReg = true;
	if(!bServExist)
	{
		hServQue = NET_WRAPPER->RegRemoteServer(szServIp, lServPort);
		if(hServQue == NULL)
		{
			bMainSvrReg = false;
			if(!nIsExistBak)
				return EC_ICV_CCTV_FAILTOREGREMOTEQUE;
		}
		else
		{
			SERV_INFO serv;
			serv.hServerQ = hServQue;
			CVStringHelper::Safe_StrNCpy(serv.szServIp, szServIp, sizeof(serv.szServIp));
			VIDEO_CHECK_FAIL_RETURN(GM_HELPER->RegisterServer(serv));
		}
	}
	else
	{
		hServQue = GM_HELPER->GetServQue(szServIp);
		if(hServQue == NULL)
		{
			return EC_ICV_CCTV_GET_DATA_FAILED;
		}
	}

	if(bMainSvrReg)
		node.hServerQ = hServQue;
	

	//注册备机信息
	if(nIsExistBak)
	{
		bool bBakSvrReg = true;
		hServQue = NULL;
		VIDEO_CHECK_FAIL_RETURN(GM_HELPER->IsServerExist(szBakIp, bServExist));
		if(!bServExist)
		{
			hServQue = NET_WRAPPER->RegRemoteServer(szBakIp, lServPort);
			if(hServQue == NULL)
			{
				bBakSvrReg = false;

				if(!bMainSvrReg)
					return EC_ICV_CCTV_FAILTOREGREMOTEQUE;
			}
			else
			{
				SERV_INFO serv;
				serv.hServerQ = hServQue;
				CVStringHelper::Safe_StrNCpy(serv.szServIp, szBakIp, sizeof(serv.szServIp));
				VIDEO_CHECK_FAIL_RETURN(GM_HELPER->RegisterServer(serv));
			}
		}
		else
		{
			hServQue = GM_HELPER->GetServQue(szBakIp);
			if(hServQue == NULL)
			{
				return EC_ICV_CCTV_GET_DATA_FAILED;
			}
		}

		if(bBakSvrReg)
			node.hBakServQ = hServQue;
	}
	
	//注册客户端节点
	if((szCliName != NULL) && (szCliName[0] != '\0'))
		CVStringHelper::Safe_StrNCpy(node.szCliName, szCliName, sizeof(node.szCliName));
	else
		memset(node.szCliName, 0, sizeof(node.szCliName));
	
	node.bIsExistBak = nIsExistBak;

	if((szUserName != NULL) && (szUserName[0] != '\0'))
		CVStringHelper::Safe_StrNCpy(node.szUserName, szUserName, sizeof(node.szUserName));
	else
		memset(node.szUserName, 0, sizeof(node.szUserName));

	if((szGroup != NULL) && (szGroup[0] != '\0'))
		CVStringHelper::Safe_StrNCpy(node.szGroup, szGroup, sizeof(node.szGroup));
	else
		memset(node.szGroup, 0, sizeof(node.szGroup));
	
	VIDEO_CHECK_FAIL_RETURN(GM_HELPER->RegisterClient(node, pHClient));

	return lRet;
}

/**
 *  $(为.net提供的注册客户端接口).
 *  $(Detail).
 *
 *  @param  -[out]  HVideoClienT*  pHClient: [客户端句柄]
 *  @param  -[in]  const char*  szCliName: [客户端名称]
 *  @param  -[in]  char*  szMainServIP: [主服务器IP地址]
 *  @param  -[in]  bool  bIsExistBak: [是否存在备机]
 *  @param  -[in]  char*  szBakServIP: [备机IP地址]
 *  @param  -[in]  char* szUserName =  ICV_AUTH_DEFAULT_USER: [用户名]
 *  @param  -[in]  char* szGroup =  "": [群组]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     04/28/2009  zhucongfeng  Initial Version.
 *  @version     07/20/2010  zhucongfeng  Initial Version.
 */

long VRN_RegisterClient(HVideoClienT* pHClient, const char *szCliName, char* szMainServIP, bool bIsExistBak, char* szBakServIP, char* szUserName, char* szGroup)
{
	return VR_RegisterClient(pHClient, szCliName, szMainServIP, bIsExistBak, szBakServIP, szUserName, szGroup);
}

/**
*  注销客户端
*
*  @param  -[in]  HVideoClienT  hClient: [客户端句柄]
*  @return long.
*		- ==0 成功
*		- !=0 出现异常
*
*  @version     07/20/2010  zhucongfeng  Initial Version.
*/

long  VR_Release(HVideoClienT hClient)
{
	long lRet = ICV_SUCCESS;
	if(hClient == NULL)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}

	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;

	//注销
	GM_HELPER->UnRegisterClient(hClient);

	int nRegCliNumber = -1;
	nRegCliNumber = GM_HELPER->GetRegClientNum();
	if(nRegCliNumber <= 0)
	{
		GM_HELPER->UnInit(); 
		NET_WRAPPER->UnInitializeNetwork();		
		
	}
	return lRet;
}
/***************************************************************
*摄像头同步接口
***************************************************************/

/**
 *  获取分区列表。对于一般用户只能获取自己有权限的分区，无权限的分区不显示
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[out]  SVideoZone** ppVideoZone: [返列表回分区]
 *  @param  -[out]  int* pnVideoZone:         [分区个数]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     04/20/2009  zhucongfeng  Initial Version.
 */

long VR_GetAllZone(HVideoClienT hClient, SVideoZone** ppVideoZone, int* pnVideoZone, long lTimeOut)
{
	/************************************************************************/
	/* 接口说明：获取用户有权限看到的所有分区信息。
	命令码：  101
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名
	返回码：  501
	应答格式：返回码(3) 命令序列号(10) 分区个数(4) [分区ID(4) 分区名称长度(2) 分区名称 
	分区描述长度(2) 分区描述 权限范围(1)]
	*/
	/************************************************************************/
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	long lRet = ICV_SUCCESS;

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));

	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_GET_ALL_ZONE, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}

	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lOutLen, &pRecvBuf, lRecvLen, lTimeOut);
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

	//解析分区列表
	lOutLen = 0;
	CVideoZoneList listZone;
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToZoneList(pCharRemain, &lOutLen, lRemainLen, &listZone);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}

	//List转化为数组		
	lRet = TransZoneList2ZoneArray(ppVideoZone, pnVideoZone, &listZone);

	//释放Buffer
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;
}

/**
 *  获取自定义分区列表。对于一般用户只能获取自己有权限的分区，无权限的分区不显示
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[out]  SVideoCustomZone** ppVideoCustomZone: [返回的自定义分区指针]
 *  @param  -[out]  int* pnVideoCustomZone:               [返回的自定义分区个数]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     04/20/2009  zhucongfeng  Initial Version.
 */

long VR_GetAllCustZone(HVideoClienT hClient, SVideoCustomZone** ppVideoCustomZone, int* pnVideoCustomZone,
					   long lTimeOut)
{
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	long lRet = ICV_SUCCESS;

// 	if(plistCustomZone == NULL)
// 		return EC_ICV_CCTV_FUNCPARAMINVALID;

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));

	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_CUSTZONE_GET, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}

	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lOutLen, &pRecvBuf, lRecvLen, lTimeOut);
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

	// 	//直接返回分区Buffer  *  @version     08/28/2010  lixiaohao  $(Desp).
	// 	memcpy(plistCustomZone,pCharRemain,lRemainLen);

	//解析分区列表
	CVideoCustomZoneList listCustomZone;
	lOutLen = 0;
	lRet = NET_WRAPPER->m_pMsgParser->PaseRetGetCustZone(pCharRemain, &listCustomZone);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}

	//List转化为Array
	lRet = TransCustZoneList2CusZoneArray(ppVideoCustomZone, pnVideoCustomZone, &listCustomZone);
		
	//释放Buffer
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;
}

/**
 *  根据分区名称获取摄像头列表。对于一般用户只能获取自己有权限看到的摄像头，无权限的摄像头不显示
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[out]  SVideoCamera** ppVideoCamera:  [返回的摄像头指针]
 *  @param  -[out]  int* pnVideoCamera:            [返回的摄像头个数]
 *  @param  -[in]  long lZoneID: [分区编号]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     04/20/2009  zhucongfeng  Initial Version.
 */

long VR_GetAllCamInfoOfZone(HVideoClienT hClient, SVideoCamera** ppVideoCamera, int* pnVideoCamera,
							long lZoneID, long lTimeOut)
{
	/************************************************************************/
	/* 接口说明：获取某一分区用户有权限看到的摄像头设备信息。
	命令码：  112
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 分区ID(4)
	返回码：  512
	应答格式：返回码(3) 命令序列号(10) 分区ID(4) 摄像头个数(6) [摄像头逻辑ID(6) 摄像头名称长度(2) 
	摄像头名称 摄像头描述长度(2) 摄像头描述 远程文件操作(1) 本地文件操作(1) 时间回放操作(1) 
	云台控制(1) 预置位控制(1) 镜头控制(1) 画面质量(1) 抓拍控制(1) 加热器控制(1) 雨刷控制(1) 
	抓拍图片宽度(4) 抓拍图片高度(4) 抓拍图片位深度(2) 摄像头连接的设备个数(1) 
	[设备ID(4) 通道号(4) 是否控制设备(1) 是否实时视频源设备(1)]]
	*/
	/************************************************************************/
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	long lRet = ICV_SUCCESS;

	if((lZoneID <=0))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;

	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_GET_ZONE_CAMERA, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	//组织区名称
	lRet = NET_WRAPPER->m_pMsgPacker->PackCmdGetCustZoneCam(szBuffer+lMsgLen, sizeof(szBuffer)-lMsgLen, lZoneID, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
// 	//直接返回分区Buffer  *  @version     08/28/2010  lixiaohao  $(Desp).
// 	memcpy(plistCam,pCharRemain,lRemainLen);

	//=====解析摄像头列表=====
	CVideoCameraList plistCam;
	lOutLen = 0;
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToCameraList(pCharRemain, &lOutLen, lRemainLen, &plistCam);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}

	//List转化为数组
    lRet = TransCamList2CamArray(ppVideoCamera,  pnVideoCamera,  &plistCam);

	//释放Buffer
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;
}

/**
 *  根据用户自定义分区名称获取摄像头列表。对于一般用户只能获取自己有权限看到的摄像头，无权限的摄像头不显示
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[out]  SVideoCamera** ppVideoCamera: [返回的摄像头指针]
 *  @param  -[out]  int* pnVideoCamera:           [返回的摄像头个数]
 *  @param  -[in]  char *  pszCustZoneName: [分区名称]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     04/20/2009  zhucongfeng  Initial Version.
 */
long VR_GetAllCamInfoOfCustZone(HVideoClienT hClient, SVideoCamera** ppVideoCamera, int* pnVideoCamera,
								char * pszCustZoneName, long lTimeOut)
{
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	long lRet = ICV_SUCCESS;

	if((pszCustZoneName == NULL) || (pszCustZoneName[0] == '\0'))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;

	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_CUSTZONE_GETCAM, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	//组织区名称
	lRet = NET_WRAPPER->m_pMsgPacker->PackCmdGetCustZoneCam(szBuffer+lMsgLen, sizeof(szBuffer)-lMsgLen, pszCustZoneName, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
// 	//直接返回分区Buffer  *  @version     08/28/2010  lixiaohao  $(Desp).
// 	memcpy(plistCam,pCharRemain,lRemainLen);

	//解析摄像头列表
	lOutLen = 0;
	CVideoCameraList listCam;
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToCameraList(pCharRemain, &lOutLen, lRemainLen, &listCam);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}

	//List转化为数组		
	lRet = TransCamList2CamArray(ppVideoCamera,  pnVideoCamera,  &listCam);

	//释放Buffer
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;
}

/**
 *  根据提供的摄像头名称进行模糊查询，获取摄像头信息.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[out]  char*  plistCam: [返回的摄像头列表]
 *  @param  -[in]  char *  pszCamName: [摄像头模糊名称]
 *  @param  -[in]  long  lTimeOut: [超时时间]
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     04/22/2009  zhucongfeng  Initial Version.
 */

long VR_GetCameras (HVideoClienT hClient, char* plistCam, char * pszCamName, long lTimeOut) 
{
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	long lRet = ICV_SUCCESS;

	if(plistCam == NULL)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));

	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_GET_ALL_CAMERA, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}

	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lOutLen, &pRecvBuf, lRecvLen, lTimeOut);
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

	//解析摄像头
	CVideoCameraList listCam;
	lOutLen = 0;
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToCameraList(pCharRemain, &lOutLen, lRemainLen, &listCam);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}

	NET_WRAPPER->FreeRecvBuff(pRecvBuf);

	//根据名称进行模糊查询过滤
	int nCount = listCam.GetSize();
	string strCmpName = "";
	if((pszCamName != NULL) && (pszCamName[0] != '\0'))
		strCmpName = pszCamName;
	int nDelCount = 0;
	int* pnDelPos = new int[nCount];
	int i;

	//检测需要删除的摄像头
	for(i=0;i<nCount;i++)
	{
		CVideoCamera* pCamera = listCam.GetAt(i);
		string strName = pCamera->GetName();

		int nPos = strName.find(strCmpName);
		//根据名称查询
		if(nPos < 0)
		{
			pnDelPos[nDelCount] = i;
			nDelCount++;
		}
	}

	//删除需要删除的摄像头
	for(i=nDelCount-1;i>=0;i--)
	{
		listCam.RemoveAt(pnDelPos[i]);
	}
	lOutLen = 0;
	NET_WRAPPER->m_pMsgPacker->PackCameraListToBuffer(&listCam,plistCam,VIDEO_MESSAGE_MAX_SIZE,&lOutLen);
	
	return lRet;
}

/**
 *  为.net提供的根据摄像头名称模糊查询摄像头名称接口.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[out]  char*  pszCam: [返回的摄像头信息，中间以“;”分割]
 *  @param  -[in]  char *  pszCamName: [摄像头名称]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     04/28/2009  zhucongfeng  Initial Version.
 */

long VRN_GetCameras (HVideoClienT hClient, char* &pszCam, char * pszCamName, long lTimeOut)
{
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	long lRet = ICV_SUCCESS;

	char szListCam[VIDEO_MESSAGE_MAX_SIZE];
	memset(szListCam,0x00,VIDEO_MESSAGE_MAX_SIZE);
	lRet = VR_GetCameras(pClient, szListCam, pszCamName, lTimeOut);
	if(lRet != ICV_SUCCESS)
		return lRet;
	CVideoCameraList videoList;
	long lOffset=0;
	NET_WRAPPER->m_pMsgParser->ParseBufferToCameraList(szListCam,&lOffset,strlen(szListCam),&videoList);

	int nCount = videoList.GetSize();
	//将检出的名称以分隔符分割
	if(nCount <= 0)
	{
		pszCamName = NULL;
		return EC_ICV_CCTV_CAMERANOTFOUND;
	}
	
	pszCam = new char[nCount*VIDEO_NAME_MAXSIZE + nCount];
	memset(pszCam, 0, sizeof(pszCam));
	CVideoCamera* pCamear = NULL;
	for(int i=0;i<nCount-1;i++)
	{
		pCamear = videoList.GetAt(i);
		strcat(pszCam, pCamear->GetName());
		strcat(pszCam, VIDEO_NAME_SEPARATOR);
	}

	pCamear = videoList.GetAt(nCount-1);
	strcat(pszCam, pCamear->GetName());

	return lRet;
}

/**
 *  为.net提供的根据摄像头名称模糊查询摄像头名称接口.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[out]  char*  pszCam: [返回的摄像头信息，中间以“;”分割]
 *  @param  -[in]  long lBufLen: [分配的接收摄像头信息的Buffer长度]
 *  @param  -[in]  char *  pszCamName: [摄像头名称]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     06/23/2009  zhucongfeng  Initial Version.
 */
long VRN_GetCameras2 (HVideoClienT hClient, char* pszCam, long lBufLen, char * pszCamName, long lTimeOut)
{
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	if(pszCam == NULL || lBufLen <= 0)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	char* pszCamBuf = NULL;
	long lRet = VRN_GetCameras (pClient, pszCamBuf, pszCamName, lTimeOut);
	if(lRet != ICV_SUCCESS)
	{
		SAFE_DELETE_ARRAY(pszCamBuf);
		return lRet;
	}

	//检测长度是否超限
	long lCamBufLen = strlen(pszCamBuf);
	if(lBufLen <= lCamBufLen)
	{
		SAFE_DELETE_ARRAY(pszCamBuf);
		return EC_ICV_CCTV_PARSERBUFFERTOOSHORT;
	}

	memcpy(pszCam, pszCamBuf, lCamBufLen);
	SAFE_DELETE_ARRAY(pszCamBuf);

	//用不完的置空
	memset(pszCam+lCamBufLen, 0, lBufLen-lCamBufLen);

	return lRet;
}

/**
 *  根据摄像头名称获取摄像头信息.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[out]  CVideoCamera*  pCamera: [comment]
 *  @param  -[in]  char *  pszCamName: [摄像头名称]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     04/27/2009  zhucongfeng  Initial Version.
 */

long VR_GetCamInfoByCamName (HVideoClienT hClient, CVideoCamera* pCamera, char * pszCamName, long lTimeOut)
{
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}
	
	long lRet = ICV_SUCCESS;

	if((pCamera == NULL) || (pszCamName == NULL) || (pszCamName[0] == '\0'))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;

	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_GET_CAMERA_BY_NAME, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	//组织摄像头名称，都是名称，所以可以用组织区名称的方法
	lRet = NET_WRAPPER->m_pMsgPacker->PackCmdGetCustZoneCam(szBuffer+lMsgLen, sizeof(szBuffer)-lMsgLen, pszCamName, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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

	//如果消息剩余长度为0，说明没有查到
	if(lRemainLen <= 0)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return EC_ICV_CCTV_CAMERANOTFOUND;
	}

	//解析摄像头信息
	lOutLen = 0;
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToCamera(pCharRemain, &lOutLen, lRemainLen, pCamera);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}

	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;
}

/**
 *  获取所有设备信息.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[out]  SVideoDevice** ppVideoDevice:  [获取到的设备指针]
 *  @param  -[out]  int* pnVideoDevice:            [获取到的设备个数]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     04/27/2009  zhucongfeng  Initial Version.
 */

long VR_GetAllDeviceInfo (HVideoClienT hClient, SVideoDevice** ppVideoDevice, int* pnVideoDevice, long lTimeOut)
{
	/************************************************************************/
	/* 接口说明：获取除了摄像头和监视器的设备信息。
	命令码：  103
	请求格式：命令码(3) 命令序列号(10)
	返回码：  503
	应答格式：命令码(3) 命令序列号(10) 视频设备个数(4) [设备的品牌ID(4) 
	设备ID(4) 设备名称长度(2) 设备名称 IP地址长度(2) IP地址 设备端口(6) 
	设备描述长度(2) 设备描述 连接的设备个数(1)
	[本设备物理通道(4) 连接设备ID(4) 连接设备物理通道(4)]]                                                                     */
	/************************************************************************/
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}
	
	long lRet = ICV_SUCCESS;

// 	if(pDevList == NULL)
// 		return EC_ICV_CCTV_FUNCPARAMINVALID;

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));

	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_GET_ALL_DEVICE, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}

	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lOutLen, &pRecvBuf, lRecvLen, lTimeOut);
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
// 	//直接返回分区Buffer  *  @version     08/28/2010  lixiaohao  $(Desp).
// 	memcpy(pDevList,pCharRemain,lRemainLen);

	//=====解析设备列表=====
	CVideoDeviceList plistDev;
	lOutLen = 0;
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToDeviceList(pCharRemain, &lOutLen, lRemainLen, &plistDev);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}
	//List转化为数组
	lRet = TransDevList2DevArray(ppVideoDevice, pnVideoDevice, &plistDev);
	
	//释放Buffer
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;
}

/**
*  根据摄像头名称获取关联的硬盘录像机信息（IP地址、通道号、端口号）.
*
*  @param  -[in]  HVideoClienT hClient: [客户端句柄]
*  @param  -[out]  CVideoDevice*  pVideoDev: [硬盘录像机信息]
*  @param  -[out]  char* szChannel: [通道号]
*  @param  -[in]  char *  pszCamName: [摄像头名称]
*  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
*		- ==0 成功
*		- !=0 出现异常
*
*  @version     04/22/2009  zhucongfeng  Initial Version.
*/

long VR_GetRecordInfoByCamName (HVideoClienT hClient, CVideoDevice* pVideoDev, char* szChannel, char * pszCamName, long lTimeOut)
{
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}
	
	long lRet = ICV_SUCCESS;

	if((pVideoDev == NULL) || (pszCamName == NULL) || (pszCamName[0] == '\0'))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	//首先根据摄像头名称获取摄像头信息
	CVideoCamera videoCam;
	lRet = VR_GetCamInfoByCamName(pClient, &videoCam, pszCamName, lTimeOut);
	if(lRet != ICV_SUCCESS)
		return lRet;

	//从摄像头信息中取出录像机设备编号
	CVideoLinkDeviceList* pDevList = videoCam.GetLinkDev();
	if(pDevList == NULL)
		return EC_ICV_CCTV_LINKDEVICENOTFOUND;

	CVideoLinkDevice* pLinkDev = pDevList->FindSourceLinkDevice();
	if((pLinkDev == NULL) || (pLinkDev->GetIsVideoSourceDevice() == false)) 
		return EC_ICV_CCTV_LINKDEVICENOTFOUND;

	long lRecordID = pLinkDev->GetDeviceID();

	//取通道号
	Safe_CopyString(szChannel, pLinkDev->GetChannel(), sizeof(szChannel));

	//读取所有设备信息
	CVideoDeviceList videoDevList;

// 	char szDevList[VIDEO_MESSAGE_MAX_SIZE];
// 	memset(szDevList,0x00,VIDEO_MESSAGE_MAX_SIZE);
// 	lRet = VR_GetAllDeviceInfo(pClient, szDevList, lTimeOut);

	SVideoDevice* ppVideoDevice = NULL;
	int pnVideoDevice = 0;
	lRet = VR_GetAllDeviceInfo(pClient, &ppVideoDevice, &pnVideoDevice, lTimeOut);

	if(lRet != ICV_SUCCESS)
		return lRet;
// 	long lOffset = 0;
// 	NET_WRAPPER->m_pMsgParser->ParseBufferToDeviceList(szDevList,&lOffset,strlen(szDevList), &videoDevList);
	CVideoDevice tempVideoDevice;
	int nIndex = 0;
	for (CVideoDeviceList::iterator iter = videoDevList.begin(); nIndex < pnVideoDevice; nIndex ++)
	{
		tempVideoDevice.SetServerID((*(ppVideoDevice + nIndex)).m_nServerID);
		tempVideoDevice.SetProductID((*(ppVideoDevice + nIndex)).m_nProductID);
		tempVideoDevice.SetID((*(ppVideoDevice + nIndex)).m_nID);
		tempVideoDevice.SetName((*(ppVideoDevice + nIndex)).m_szName);
		tempVideoDevice.SetIP((*(ppVideoDevice + nIndex)).m_szIP);
		tempVideoDevice.SetPort((*(ppVideoDevice + nIndex)).m_nPort);
		tempVideoDevice.SetExtent((*(ppVideoDevice + nIndex)).m_szExtent);
		tempVideoDevice.SetDescription((*(ppVideoDevice + nIndex)).m_szDescription);
		tempVideoDevice.SetCtrlPort((*(ppVideoDevice + nIndex)).m_nCtrlPort);
		tempVideoDevice.SetLoginID((*(ppVideoDevice + nIndex)).m_nLoginID);
		iter = videoDevList.insert(iter, tempVideoDevice);
	}
	//根据编号查找设备信息
	CVideoDevice* pVideoDevTemp = videoDevList.FindDevicebyID(lRecordID);
	if(pVideoDevTemp == NULL)
		return EC_ICV_CCTV_LINKDEVICENOTFOUND;

	//拷贝设备信息
	pVideoDev->SetServerID(pVideoDevTemp->GetServerID());
	pVideoDev->SetProductID(pVideoDevTemp->GetProductID());
	pVideoDev->SetID(pVideoDevTemp->GetID());
	pVideoDev->SetName(pVideoDevTemp->GetName());
	pVideoDev->SetIP(pVideoDevTemp->GetIP());
	pVideoDev->SetPort(pVideoDevTemp->GetPort());	
	pVideoDev->SetExtent(pVideoDevTemp->GetExtent());
	pVideoDev->SetDescription(pVideoDevTemp->GetDescription());
	pVideoDev->SetCtrlPort(pVideoDevTemp->GetCtrlPort());
	pVideoDev->SetLoginID(pVideoDevTemp->GetLoginID());

	return lRet;
}

/**
 *  为.net提供的根据摄像头名称获取关联的硬盘录像机信息的接口.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[out]  char*  pszDev: [硬盘录像机信息，格式为“IP地址;通道号;端口号;设备自定义信息;设备名称;设备描述”]
 *  @param  -[in]  char *  pszCamName: [摄像头名称]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     04/28/2009  zhucongfeng  Initial Version.
 */

long VRN_GetRecordInfoByCamName (HVideoClienT hClient, char* &pszDev, char * pszCamName, long lTimeOut)
{
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}
	
	long lRet = ICV_SUCCESS;

	CVideoDevice videoDev;
	//long lChannel;
	char szChannel[VIDEO_CHANNEL_MAXSIZE];
	memset(szChannel, 0x00, sizeof(szChannel));

	lRet = VR_GetRecordInfoByCamName(pClient, &videoDev, szChannel, pszCamName, lTimeOut);

	if(lRet != ICV_SUCCESS)
		return lRet;

	//组织返回的信息
	pszDev = new char[VIDEO_NAME_MAXSIZE*3+VIDEO_DEFAULTINFO_MAXSIZE+VIDEO_NAME_MAXSIZE+VIDEO_DESCRIPTION_MAXSIZE];
	memset(pszDev, 0, sizeof(pszDev));

	//IP地址;
	strcat(pszDev, videoDev.GetIP());
	strcat(pszDev, VIDEO_NAME_SEPARATOR);

	//通道号;
	char szTemp[VIDEO_NAME_MAXSIZE];
	memset(szTemp, 0, sizeof(szTemp));
	sprintf(szTemp, "%s", szChannel);
	strcat(pszDev, szTemp);
	strcat(pszDev, VIDEO_NAME_SEPARATOR);

	//端口号
	memset(szTemp, 0, sizeof(szTemp));
	sprintf(szTemp, "%d", videoDev.GetPort());
	strcat(pszDev, szTemp);
	strcat(pszDev, VIDEO_NAME_SEPARATOR);

	//设备自定义信息
	char* pCharTemp = videoDev.GetExtent();
	if((pCharTemp != NULL) && (pCharTemp[0] != '\0'))
		strcat(pszDev, pCharTemp);
	strcat(pszDev, VIDEO_NAME_SEPARATOR);

	//设备名称
	pCharTemp = videoDev.GetName();
	if((pCharTemp != NULL) && (pCharTemp[0] != '\0'))
		strcat(pszDev, pCharTemp);
	strcat(pszDev, VIDEO_NAME_SEPARATOR);

	//设备描述
	pCharTemp = videoDev.GetDescription();
	if((pCharTemp != NULL) && (pCharTemp[0] != '\0'))
		strcat(pszDev, pCharTemp);

	return lRet;

}

/**
 *  为.net提供的根据摄像头名称获取关联的硬盘录像机信息的接口.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[out]  char*  pszDev: [硬盘录像机信息，格式为“IP地址;通道号;端口号;设备自定义信息;设备名称;设备描述”]
 *  @param  -[in]  long lBufLen: [分配的接收硬盘录像机信息的Buffer长度]
 *  @param  -[in]  char *  pszCamName: [摄像头名称]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     06/23/2009  zhucongfeng  Initial Version.
 */
long VRN_GetRecordInfoByCamName2 (HVideoClienT hClient, char* pszDev, long lBufLen, char * pszCamName, long lTimeOut)
{
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	if(pszDev == NULL || lBufLen <= 0)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	char* pszDevBuf = NULL;
	long lRet = VRN_GetRecordInfoByCamName(pClient, pszDevBuf, pszCamName, lTimeOut);
	if(lRet != ICV_SUCCESS)
	{
		SAFE_DELETE_ARRAY(pszDevBuf);
		return lRet;
	}

	//检测长度是否越界
	long lDevBufLen = strlen(pszDevBuf);
	if(lBufLen <= lDevBufLen)
	{
		SAFE_DELETE_ARRAY(pszDevBuf);
		return EC_ICV_CCTV_PARSERBUFFERTOOSHORT;
	}

	memcpy(pszDev, pszDevBuf, lDevBufLen);
	SAFE_DELETE_ARRAY(pszDevBuf);

	//用不完的置空
	memset(pszDev+lDevBufLen, 0, lBufLen-lDevBufLen);

	return lRet;
}

//-----------------抓拍--------------------------//

/**
 *  根据抓怕ID获取抓拍图片的内容.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[in]  long  nSnapID: [抓拍ID]
 *  @param  -[out]  long&  nCount: [抓拍个数]
 *  @param  -[out]  char*  &pBuffer: [图片buffer内容，格式为：图片内容图片长度图片内容......]
 *  @param  -[out]  long&  nPicLen: [buffer长度]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     05/31/2009  zhucongfeng  Initial Version.
 */

long VR_GetSnapPicBuffer(HVideoClienT hClient, long nSnapID, long &nCount, char *&pBuffer, long &nPicLen, long lTimeOut)
{

	/************************************************************************/
	/* 接口说明：获取指定抓拍图片的图片内容
	命令码：  152
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 摄像头ID(6) 抓拍图片ID(6)
	返回码：  552
	应答格式：命令码(3) 命令序列号(10) 错误码(10) 连续抓拍张数(2) [图片内容长度(8) 图片内容]
	*/
	/************************************************************************/
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	long lRet = ICV_SUCCESS;

	if(nSnapID <= 0)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;

	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_GET_PIC_BUFFER_BYID, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	//组织抓拍ID
	lRet = NET_WRAPPER->m_pMsgPacker->PackSnapPicID(szBuffer+lMsgLen, sizeof(szBuffer)-lMsgLen, nSnapID, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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

	//解析抓拍图片
	lOutLen = 0;
	lRet = NET_WRAPPER->m_pMsgParser->ParseGetSnapPicBuffer(pCharRemain, lRemainLen, nCount, pBuffer, nPicLen);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}

	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;
}

/**
 *  根据抓怕ID获取抓拍图片的内容.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[in]  long  nSnapID: [抓拍ID]
 *  @param  -[out]  long&  nCount: [抓拍个数]
 *  @param  -[out]  char*  &pBuffer: [图片buffer内容，格式为：图片内容图片长度图片内容......]
 *  @param  -[in]  long lBufLen: [外面分配的用于接收图片内容的缓冲区]
 *  @param  -[out]  long&  nPicLen: [buffer长度]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     06/23/2009  zhucongfeng  Initial Version.
 */

long VR_GetSnapPicBuffer2(HVideoClienT hClient, long nSnapID, long &nCount, char *pBuffer, long lBufLen, long &nPicLen, long lTimeOut)
{
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	if(pBuffer == NULL || lBufLen <= 0)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	char* pPicBuf = NULL;
	long lRet = VR_GetSnapPicBuffer(pClient, nSnapID, nCount, pPicBuf, nPicLen, lTimeOut);

	if(lRet != ICV_SUCCESS)
	{
		SAFE_DELETE_ARRAY(pPicBuf);
		return lRet;
	}

	//检测长度是否越界
	if(lBufLen < nPicLen)
	{
		SAFE_DELETE_ARRAY(pPicBuf);
		return EC_ICV_CCTV_PARSERBUFFERTOOSHORT;
	}
	
	memcpy(pBuffer, pPicBuf, nPicLen);
	SAFE_DELETE_ARRAY(pPicBuf);

	//用不完的置空
	memset(pBuffer+nPicLen, 0, lBufLen-nPicLen);
	
	return lRet;
}

/**
 *  根据抓拍ID删除抓拍的图片.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[in]  long  nSnapID: [抓拍ID]
 *  @param  -[in]  long  lCamID: [摄像头ID]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     05/31/2009  zhucongfeng  Initial Version.
 */

long VR_DelSnapPic(HVideoClienT hClient, long nSnapID, long lCamID, long lTimeOut)
{

	/************************************************************************/
	/* 命令码：  154
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 删除个数(6) [摄像头ID(6) 抓拍图片ID(6)]
	返回码：  554
	应答格式：命令码(3) 命令序列号(10) 错误码(10)
	*/
	/************************************************************************/
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}
	
	long lRet = ICV_SUCCESS;

	if(nSnapID <= 0)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;

	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_DELETE_SNAP_PIC, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	//组织抓拍ID
	lRet = NET_WRAPPER->m_pMsgPacker->PackDelSnapPic(szBuffer+lMsgLen, sizeof(szBuffer)-lMsgLen, lCamID, nSnapID, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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

	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lResult;
}

/**
 *  根据摄像头ID和预置位名称修改预置位.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[in]  long nCamID [摄像头ID]
 *  @param  -[in]  char *pOldPspName     [老预置位名]
 *  @param  -[in]  char *pNewPspName     [新预置位名]
 *  @param  -[in]  char *pNewPspDesc     [新预置位描述]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *  @return long.
 *		- ==0 成功    - !=0 出现异常
 *  @version     07/16/2009  chenzhan  Initial Version.
 */
long VR_ModifyPsp(HVideoClienT hClient, long nCamID, char *pOldPspName, char *pNewPspName, char *pNewPspDesc, long lTimeOut)
{
	/************************************************************************/
	/* 命令码：  143
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 摄像头逻辑ID 旧预置位名称长度(2) 旧预置位名称 新预置位名称长度(2) 新预置位名称 预置位描述长度(2) 预置位描述
	返回码：  543
	应答格式：命令码(3) 命令序列号(10) 错误码(10)
	*/
	/************************************************************************/
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	long lRet = ICV_SUCCESS;

	if (VIDEO_DESCRIPTION_MAXSIZE < (strlen(pNewPspDesc)+1))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	if (VIDEO_NAME_MAXSIZE < (strlen(pNewPspName)+1))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	if (VIDEO_NAME_MAXSIZE < (strlen(pOldPspName)+1))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	if (0 >= strlen(pNewPspName))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	if (0 >= strlen(pOldPspName))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	if(nCamID <= 0)
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_MODIFY_PSP, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;
	

	CVideoPSP psp;
	psp.SetName(pNewPspName);
	psp.SetDescription(pNewPspDesc);
	lRet = NET_WRAPPER->m_pMsgPacker->PackCmdModifyPSP(szBuffer+lMsgLen, sizeof(szBuffer)-lMsgLen, nCamID, pOldPspName, &psp, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;
	
	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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

	//释放Buffer
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lResult;
}

/**
 *  根据摄像头ID和预置位名称删除预置位.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[in]  long nCamID [摄像头ID]
 *  @param  -[in]  char* pPspName [预置位名称]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     07/16/2009  chenzhan  Initial Version.
 */
long VR_DeletePsp(HVideoClienT hClient, long nCamID, char *pPspName, long lTimeOut)
{
	/************************************************************************/
	/* 命令码：  142
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 摄像头逻辑ID(6)  [预置位名称长度(2) 预置位名称]
	返回码：  542
	应答格式：命令码(3) 命令序列号(10) 删除不成功的预置位个数(2) [预置位名称长度(2) 预置位名称]
	*/
	/************************************************************************/
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	long lRet = ICV_SUCCESS;
	
	if(nCamID <= 0)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	if (VIDEO_NAME_MAXSIZE < (strlen(pPspName)+1))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	if (0 >= strlen(pPspName))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_DELETE_PSP, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	lOutLen = sprintf(szBuffer+lMsgLen, "%06d%02d%s", nCamID, strlen(pPspName), pPspName);

	lMsgLen += lOutLen;
	
	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lResult;
}

/**
 *  删除某个时间点前的抓拍图片.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[in]  char* pszTime 时间点(格式"2010,01,01,08,00,00")
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     02/04/2010  chenzhan  Initial Version.
 */
long VRN_DelPrePicture(HVideoClienT hClient, char* pszTime, long lTimeOut)
{
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	if (!pszTime)
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	long lRet = ICV_SUCCESS;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_QUERY_SNAP_INFO, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;
	
	// 过滤条件
	long nCamID = VIDEO_ID_DEFAULT;
	time_t tTime = StrToTime(pszTime);
	if (0 >= tTime)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}

	lOutLen = sprintf(szBuffer+lMsgLen, _("08 all partitions %06d0000000000%010I64d08 all type"), nCamID, tTime);//08所有分区%06d0000000000%010I64d08所有类型
	lMsgLen += lOutLen;
	
	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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

	CVideoSnapFileList theSnapList;
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToSnapFileList(pRecvBuf+RETCMD_HEADERINFO_LENGTH, lRecvLen-RETCMD_HEADERINFO_LENGTH, &theSnapList);
	
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	
	for (int i=0; i<theSnapList.GetSize(); i++)
		VR_DelSnapPic(pClient, theSnapList.GetAt(i)->GetID(), nCamID);

	return lResult;
}

/**
 *  获取某个时间段的所有抓拍录像ID.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[in]  char* pszTimeStart 时间点(格式"2010,01,01,08,00,00")
 *  @param  -[in]  char* pszTimeEnd 时间点(格式"2010,01,01,10,00,00")
 *  @param  -[out] char* pszBuff ID列表(格式"1,2,3,4,"),此指针指向的内存由使用者申请
 *  @param  -[in]  long nBuffSize 内存长度
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     02/04/2010  chenzhan  Initial Version.
 */
long VRN_GetRecordID(HVideoClienT hClient, char* pszTimeStart, char* pszTimeEnd, char* pszBuff, long nBuffSize, long lTimeOut)
{
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}
	
	if ((!pszBuff) || (!pszTimeStart) || (!pszTimeEnd) || (VIDEO_NAME_MAXSIZE > nBuffSize))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	long lRet = ICV_SUCCESS;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, VIDEO_MESSAGE_MAX_SIZE);
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_QUERY_CAPTURE_INFO, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;
	
	time_t lStartTime = StrToTime(pszTimeStart);
	time_t lEndTime = StrToTime(pszTimeEnd);
	if ((lStartTime >= lEndTime) || (0 >= lStartTime))
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	
	// 过滤条件
	lRet = NET_WRAPPER->m_pMsgPacker->PackCmdQueryRecord(szBuffer+lMsgLen, sizeof(szBuffer), VIDEO_ID_DEFAULT, lStartTime, lEndTime, lOutLen);
	if (ICV_SUCCESS != lRet)
	{
		return lRet;
	}

	lMsgLen += lOutLen;
	
	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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

	if (ICV_SUCCESS != lResult)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lResult;
	}

	CRecordList theListRecord;
	lRet = NET_WRAPPER->m_pMsgParser->ParseRetQueryRecord(pRecvBuf+RETCMD_HEADERINFO_LENGTH, &theListRecord);
	if (ICV_SUCCESS != lRet)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}
	
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	
	long nOff = 0;
	for (int i=0; i<theListRecord.GetSize(); i++)
	{
		if (nOff > (nBuffSize+10))
			return EC_ICV_CCTV_FAILTONEWMEM;
		nOff += sprintf(pszBuff+nOff, "%d,", theListRecord.GetAt(i)->GetID());
	}

	return ICV_SUCCESS;
}

/**
 *  根据抓拍ID删除抓拍录像.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[in]  long nRecordID	[录像ID]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     02/04/2010  chenzhan  Initial Version.
 */
long VRN_DelRecord(HVideoClienT hClient, long nRecordID, long lTimeOut)
{	
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	if (0 >= nRecordID)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	long lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_DEL_CAPTURE_RECORD, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	lOutLen = sprintf(szBuffer+lMsgLen, "%06d", nRecordID);

	lMsgLen += lOutLen;
	
	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lResult;
}

/**
 *  下载抓拍录像片段.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[in]  char* pszFileName [文件名全路径并带扩展名]
 *  @param  -[in]  long nRecordID	[录像ID]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     02/04/2010  chenzhan  Initial Version.
 */
long VRN_DownloadRecord(HVideoClienT hClient, char* pszFileName, long nRecordID, long lTimeOut)
{
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	if ((NULL == pszFileName) || (0 >= nRecordID))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	long lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_GET_CAPTRUE_BUFFER, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	lOutLen = sprintf(szBuffer+lMsgLen, "%06d", nRecordID);

	lMsgLen += lOutLen;
	
	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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

	long nFileSize = 0;
	char szFileName[VIDEO_FILE_NAME_MAXSIZE];
	memset(szFileName, 0x00, sizeof(szFileName));
	lRet = NET_WRAPPER->m_pMsgParser->ParseRetDownloadRecord(pRecvBuf+RETCMD_HEADERINFO_LENGTH, szFileName, nFileSize);
	if (ICV_SUCCESS != lRet)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}

	// 查看文件是否存在
	if (CVFileHelper::IsFileExist(pszFileName))
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}

	char szSepPath[MAX_PATH];
	memset(szSepPath, 0x00, sizeof(szSepPath));
	char szSepName[VIDEO_NAME_MAXSIZE];
	memset(szSepName, 0x00, sizeof(szSepName));
	CVFileHelper::SeparatePathName(pszFileName, szSepPath, sizeof(szSepPath), szSepName, sizeof(szSepName));
	if (!CVFileHelper::IsDirectoryExist(szSepPath))
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return EC_ICV_CCTV_DIR_PATH_NOT_EXIST;
	}

	// 写文件
	lRet = CVFileHelper::WriteToFile(pszFileName, (char*)pRecvBuf+lRecvLen-nFileSize, nFileSize);
	if (ICV_SUCCESS != lRet)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}

	NET_WRAPPER->FreeRecvBuff(pRecvBuf);

	return lResult;
}

 /***************************************************************
*私有函数
***************************************************************/

 //向服务器端发送消息
long SendMsgToServer(PCLIENT_INFO pClient, char* pMsgBuffer, long lMsgLength, char **pRecvBuf, long &lRecvLen, long lTimeOut)
{
	if (lTimeOut<=0)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	long lRet = NET_WRAPPER->SendAndReceiveMsg(pClient, pMsgBuffer, lMsgLength, (void**)pRecvBuf, lRecvLen, lTimeOut);

// 	SAFE_DELETE_ARRAY(pMsgBuffer);

	return lRet;
}
 
 //解析带逗号的字符串
char* GetNextToken(char* pszSrc, char* pszDst, long nDstBuffSize, char token)
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

// 字符串时间转为整数
time_t StrToTime(char* pszTime)
{
	if (!pszTime)
		return 0;
	
	char szBuf[VIDEO_NAME_MAXSIZE];
	long nTime[TIME_BYTE_COUNT];
	struct tm theTime;
	memset(&theTime, 0x00, sizeof(theTime));
	for (int i=0; i<TIME_BYTE_COUNT; i++)
	{
		memset(szBuf, 0x00, sizeof(szBuf));
		nTime[i] = atoi(GetNextToken(pszTime, szBuf, sizeof(szBuf)));
	}
	
	// 由于tm结构体特殊结构,所以月份要-1年份要-1900
	theTime.tm_sec = nTime[5];
	theTime.tm_min = nTime[4];
	theTime.tm_hour = nTime[3];
	theTime.tm_mday = nTime[2];      // Day of the month
	theTime.tm_mon = nTime[1]-1;
	theTime.tm_year = nTime[0]-1900;

	// 判断时间是否标准
	if (!IsValidTime(theTime))
		return 0;
	
	return mktime(&theTime);
}

// 时间判断函数
bool IsValidTime(struct tm tmTime) 
{
    int year, month, day, hour, minute, second;
	
	year=tmTime.tm_year + 1900;
	month=tmTime.tm_mon + 1;
	day=tmTime.tm_mday;
	hour=tmTime.tm_hour;
	minute=tmTime.tm_min;
	second=tmTime.tm_sec;
	
    if (month<1 || month>12) 
        return false;
	
    switch (month)  
	{
	case 1:
	case 3:
	case 5:
	case 7:
	case 8:
	case 10:
	case 12:
		if (day<1 || day>31)
			return false;
		break;
	case 2:
		if (year%4==0 && (year%100!=0 || year%400==0)) 
		{
			if (day<1 || day>29) 
				return false;
		} else {
			if (day<1 || day>28) 
				return false;
		}
		break;
	default:
		if (day<1 || day>30) 
			return false;
    }

	if (hour<0 || hour>23)
		return false;
	
	if (minute<0 || minute>59)
		return false;

	if (second<0 || second>59)
		return false;
		
    return true;
}


/**
 * 获取所有的设备配置信息
 *  @param  -[in]  HVideoClienT hClient: 客户端句柄
 *  @param  -[in,out]  long& lLastTime:     当前最新所有配置信息的时间 
 *  @param  -[out] SVideoProduct** ppVideoProduct:	设备产品型号列表
 *  @param  -[out] int* pnVideoProduct:	            设备产品型号个数
 *  @param  -[out] SVideoDevice** ppVideoDevice:	设备列表
 *  @param  -[out] int* pnVideoDevice:	            设备个数
 *  @param  -[out] SVideoZone** ppVideoZone:		固定分区列表
 *  @param  -[out] int* pnVideoZone:		        固定分区个数
 *  @param  -[out] SVideoCamera** ppVideoCamera:	摄像头列表
 *  @param  -[out] int* pnVideoCamera:	            摄像头个数
 *  @param  -[out] SVideoMonitor** ppVideoMonitor:	监视器列表
 *  @param  -[out] int* pnVideoMonitor:	            监视器个数
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
 *  @return  ==0 success  !=0 failure
 *  @version     07/23/2010  lixiaohao  Initial Version.
 */
 long VR_GetAllConfigInfo(HVideoClienT hClient,long& lLastTime, SVideoProduct** ppVideoProduct, 
	                      int* pnVideoProduct, SVideoDevice** ppVideoDevice, int* pnVideoDevice,
                          SVideoZone** ppVideoZone, int* pnVideoZone, SVideoCamera** ppVideoCamera,
						  int* pnVideoCamera, SVideoMonitor** ppVideoMonitor, int* pnVideoMonitor,
		                  long lTimeOut)
 {
	 //检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

// 	//入口检测
// 	if((pszlistProduct == NULL) || (pszlistDev == NULL) || (pszlistZone== NULL) || (pszlistCam== NULL) || (pszlistMon == NULL))
// 		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = ICV_SUCCESS;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	/************************************************************************/
	/* 接口说明：获取用户有权限看到的所有设备配置信息信息。
	   命令码：  100
       请求格式：消息头 信息更新时间(10)
 	/************************************************************************/
	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_GET_ALL_CONFIG_INFO, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	//信息更新时间
	sprintf(szBuffer+lMsgLen, "%010d", lLastTime);
	lMsgLen += VIDEO_CHANGETIME_LENGTH_SIZE;


	
	char* pCharRemain = NULL;
	long lRemainLen = 0;
	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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

	lRemainLen = lRecvLen - lOutLen;
	pCharRemain = pRecvBuf + lOutLen;
	
	// 获取服务端模式的最新时间
	long lNewestCfgTime = 0;
	long lOffset = 0;
/************************************************************************/
/* 信息更新时间(10)设备品牌个数(4) [设备类型(1) 设备品牌ID(4) 设备品牌名称长度(2) 设备品牌名称 插件名称长度(2) 插件名称] 
   视频设备个数(4) [设备的品牌ID(4) 设备ID(4) 设备名称长度(2) 设备名称 IP地址长度(2) IP地址 设备端口(6) 设备描述长度(2) 设备描述 
   连接的设备个数(1)[本设备物理通道(4) 连接设备ID(4) 连接设备物理通道(4)]] 
   分区个数(4) [分区ID(4) 分区名称长度(2) 分区名称 分区描述长度(2) 分区描述 权限范围(1)] 
   摄像头个数(6) [摄像头所属分区ID(4) 摄像头逻辑ID(6) 摄像头名称长度(2) 摄像头名称 摄像头描述长度(2) 摄像头描述 远程文件操作(1) 
   本地文件操作(1) 时间回放操作(1) 云台控制(1) 预置位控制(1) 镜头控制(1) 画面质量(1) 抓拍控制(1) 加热器控制(1) 雨刷控制(1) 
   抓拍图片宽度(4) 抓拍图片高度(4) 抓拍图片位深度(2) 摄像头连接的设备个数(1) [设备ID(4) 通道号(4) 是否控制设备(1) 
   是否实时视频源设备(1)]] 监视器个数(4) [监视器所属分区ID(4) 监视器逻辑ID(4) 监视器名称长度(2) 监视器名称 监视器描述长度(2) 
   监视器描述 连接设备ID(4) 通道号(4)]                                                                     */
/************************************************************************/
	// 最新更新时间
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToInteger(pCharRemain, &lOffset, lRemainLen, VIDEO_CHANGETIME_LENGTH_SIZE, lNewestCfgTime);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lResult;
	}
	lLastTime = lNewestCfgTime;
	do
	{
		CVideoProductList plistProduct;
		CVideoDeviceList plistDev;
		CVideoZoneList plistZone;
		CVideoCameraList plistCam;
		CVideoMonitorList plistMon;
		
		long lstartPos = lOffset;
		int nIndex = 0;
		//===== 解析产品型号信息 ======
		lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToProductList(pCharRemain, &lOffset, lRemainLen, &plistProduct);
		if(lRet != ICV_SUCCESS)
			break;
		//List转化为Array
		lRet = TransProductList2ProdctArray(ppVideoProduct, pnVideoProduct, &plistProduct);
		if(lRet != ICV_SUCCESS)
			break;
		lstartPos = lOffset;

		//====== 解析设备信息======
		lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToDeviceList(pCharRemain, &lOffset, lRemainLen, &plistDev);
		if(lRet != ICV_SUCCESS)
			break;
		//List转化为数组		
		lRet = TransDevList2DevArray(ppVideoDevice,  pnVideoDevice,  &plistDev);
		if(lRet != ICV_SUCCESS)
			break;
		lstartPos = lOffset;

		//=====解析分区信息=====
		lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToZoneList(pCharRemain, &lOffset, lRemainLen, &plistZone);
		if(lRet != ICV_SUCCESS)
			break;
		//List转化为数组		
		lRet = TransZoneList2ZoneArray(ppVideoZone, pnVideoZone, &plistZone);
		if(lRet != ICV_SUCCESS)
			break;
		lstartPos = lOffset;

		// =====解析摄像头信息=====
		lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToCameraList(pCharRemain, &lOffset, lRemainLen, &plistCam);
		if(lRet != ICV_SUCCESS)
			break;
		//List转化为数组		
		lRet = TransCamList2CamArray(ppVideoCamera,  pnVideoCamera,  &plistCam);
		if(lRet != ICV_SUCCESS)
			break;
		lstartPos = lOffset;

		//=====解析监视器信息=====
		lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToMonitorList(pCharRemain, &lOffset, lRemainLen,&plistMon);
		if(lRet != ICV_SUCCESS)
			break;
		//List转化为数组
		lRet = TransMonList2MonArray(ppVideoMonitor, pnVideoMonitor,  &plistMon);
		if(lRet != ICV_SUCCESS)
			break;		
		lstartPos = lOffset;
	}while(0);

	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;
 }

/**
*获取所有的设备产品型号信息
* @param  -[in]  HVideoClienT hClient: 客户端句柄
* @param  -[out] SVideoProduct** ppVideoProduct:	设备产品型号指针
* @param  -[out] int* pnVideoProduct:				设备产品型号个数
* @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
* @return  ==0 success  !=0 failure
*
*  @version     07/24/2010  lixiaohao  Initial Version.
*/
long VR_GetAllProduct(HVideoClienT hClient, SVideoProduct** ppVideoProduct, int* pnVideoProduct, long lTimeOut)
{
	/************************************************************************
	接口说明：获取所有设备的品牌信息。
	命令码：  102
	请求格式：命令码(3) 命令序列号(10)
	返回码：  502
	应答格式：命令码(3) 命令序列号(10) 设备品牌个数(4) [设备类型(1) 设备品牌ID(4) 
	设备品牌名称长度(2) 设备品牌名称 插件名称长度(2) 插件名称]
 	/************************************************************************/
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

// 	//入口检测
// 	if(plistProduct == NULL) 
// 		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = ICV_SUCCESS;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;	
	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_GET_ALL_PRODUCT, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	char* pCharRemain = NULL;
	long lRemainLen = 0;

	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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

	lRemainLen = lRecvLen - lOutLen;
	pCharRemain = pRecvBuf + lOutLen;
// 	//直接返回Buffer  *  @version     08/28/2010  lixiaohao  $(Desp).
// 	memcpy(plistProduct,pCharRemain,lRemainLen);
				
	CVideoProductList plistProduct;
	long lOffset = 0;	
	// 解析产品型号信息
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToProductList(pCharRemain, &lOffset, lRemainLen, &plistProduct);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}

	//List转化为数组
    lRet = TransProductList2ProdctArray(ppVideoProduct, pnVideoProduct, &plistProduct);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}

	//释放Buffer
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;
	
} 

/**
* 获取所有的摄像头信息
* @param  -[in]  HVideoClienT hClient: 客户端句柄
* @param  -[out] SVideoCamera** ppVideoCamera:		[摄像头指针]
* @param  -[out] int* pnVideoCamera:				[摄像头个数]
* @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
* @return  ==0 success  !=0 failure
*
 *  @version     07/24/2010  lixiaohao  Initial Version.
 */
long VR_GetAllCam(HVideoClienT hClient, SVideoCamera** ppVideoCamera, int* pnVideoCamera, long lTimeOut)
{
	/************************************************************************/
	/*  接口说明：获取用户有权限看到的所有摄像头信息。
	命令码：  110
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名
	返回码：  510
	应答格式：返回码(3) 命令序列号(10) 摄像头个数(6) [摄像头所属分区ID(4) 
	摄像头逻辑ID(6) 摄像头名称长度(2) 摄像头名称 摄像头描述长度(2) 摄像头描述 
	远程文件操作(1) 本地文件操作(1) 时间回放操作(1) 云台控制(1) 预置位控制(1) 
	镜头控制(1) 画面质量(1) 抓拍控制(1) 加热器控制(1) 雨刷控制(1) 抓拍图片宽度(4) 
	抓拍图片高度(4) 抓拍图片位深度(2) 摄像头连接的设备个数(1) [设备ID(4) 通道号(4)
	是否控制设备(1) 是否实时视频源设备(1)]]
	/************************************************************************/

	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

// 	//入口检测
// 	if(plistCam == NULL) 
// 		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = ICV_SUCCESS;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;	
	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_GET_ALL_CAMERA, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	char* pCharRemain = NULL;
	long lRemainLen = 0;

	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	lRemainLen = lRecvLen - lOutLen;
	pCharRemain = pRecvBuf + lOutLen;
// 	//直接返回Buffer  *  @version     08/28/2010  lixiaohao  $(Desp).
// 	memcpy(plistCam,pCharRemain,lRemainLen);
	CVideoCameraList plistCam;
	long lOffset = 0;	
	// =====解析产品型号信息=====
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToCameraList(pCharRemain, &lOffset, lRemainLen, &plistCam);
	if(lResult != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lResult;
	}

	//List转化为数组
    lRet = TransCamList2CamArray(ppVideoCamera, pnVideoCamera, &plistCam);

	//释放Buffer
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;
	
}

/**
 * 获取所有的监视器信息
 * @param  -[in]  HVideoClienT hClient: 客户端句柄
 * @param  -[out] SVideoMonitor** ppVideoMonitor:	[监视器指针]
 * @param  -[out] int* pnVideoMonitor:				[监视器个数]
 * @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
 * @return  ==0 success  !=0 failure
 *
 *  @version     07/24/2010  lixiaohao  Initial Version.
 */
long VR_GetAllMon(HVideoClienT hClient, SVideoMonitor** ppVideoMonitor, int* pnVideoMonitor, long lTimeOut)
{
	/************************************************************************/
	/* 接口说明：获取用户有权限看到的所有监视器设备信息。
	命令码：  120
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名
	返回码：  520
	应答格式：返回码(3) 命令序列号(10) 监视器个数(4) [监视器所属分区ID(4) 监视器逻辑ID(4) 
	监视器名称长度(2) 监视器名称 监视器描述长度(2) 监视器描述 连接设备ID(4) 通道号(4)]
	*/
	/************************************************************************/
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

// 	//入口检测
// 	if(plistMon == NULL) 
// 		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = ICV_SUCCESS;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;	
	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_GET_ALL_MONITOR, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	char* pCharRemain = NULL;
	long lRemainLen = 0;

	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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

	lRemainLen = lRecvLen - lOutLen;
	pCharRemain = pRecvBuf + lOutLen;
// 	//直接返回Buffer  *  @version     08/28/2010  lixiaohao  $(Desp).
// 	memcpy(plistMon,pCharRemain,lRemainLen);
	CVideoMonitorList plistMon;
	long lOffset = 0;	
	// =====解析返回号信息=====
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToMonitorList(pCharRemain, &lOffset, lRemainLen, &plistMon);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}
	//List转化为数组
	lRet = TransMonList2MonArray(ppVideoMonitor, pnVideoMonitor,  &plistMon);

    //释放Buffer
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;
}

/**
 * 获取所有的摄像头简易信息
 *  @param  -[in]  HVideoClienT hClient: 客户端句柄
 *  @param  -[out] SVideoCamera** ppVideoCamera:  [摄像头指针]
 *  @param  -[out] int* pnVideoCamera:			  [摄像头个数]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
 *  @return  ==0 success  !=0 failure
 *
 *  @version     07/24/2010  lixiaohao  Initial Version.
 */

long VR_GetAllSimpleCam(HVideoClienT hClient, SVideoCamera** ppVideoCamera, int* pnVideoCamera, long lTimeOut)
{
	/************************************************************************/
	/* 接口说明：获取用户有权限看到的所有摄像头简易信息(所属分区、逻辑ID、名称)。
	命令码：  111
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名
	返回码：  511
	应答格式：返回码(3) 命令序列号(10) 摄像头个数(6) [摄像头所属分区ID(4) 
	摄像头逻辑ID(6) 摄像头名称长度(2) 摄像头名称]
	*/
	/************************************************************************/

	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

// 	//入口检测
// 	if(plistCam == NULL) 
// 		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = ICV_SUCCESS;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;	
	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_GET_ALL_SIMPLE_CAMERA, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	char* pCharRemain = NULL;
	long lRemainLen = 0;

	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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


	lRemainLen = lRecvLen - lOutLen;
	pCharRemain = pRecvBuf + lOutLen;
// 	//直接返回Buffer  *  @version     08/28/2010  lixiaohao  $(Desp).
// 	memcpy(plistCam,pCharRemain,lRemainLen);	
	CVideoCameraList plistCam;
	// 解析返回信息
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToSimpleCameraList(pCharRemain, lRemainLen, &plistCam);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}
	//List转化为数组
    lRet = TransCamList2CamArray(ppVideoCamera,  pnVideoCamera,  &plistCam);

	//释放Buffer
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;

}

/**
 *  获取某一分区的摄像头简易信息
 *  @param  -[in]  HVideoClienT hClient: 客户端句柄
 *  @param  -[out] SVideoCamera** ppVideoCamera:	[摄像头指针]
 *  @param  -[out] int* pnVideoCamera:			    [摄像头个数]
 *  @param  -[in] long lZoneID						分区ID
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
 *  @return  ==0 success  !=0 failure
 *
 *  @version     07/24/2010  lixiaohao  Initial Version.
 */
long VR_GetAllSimpleCamInfoOfZone(HVideoClienT hClient, SVideoCamera** ppVideoCamera, int* pnVideoCamera,
								  long lZoneID, long lTimeOut)
{
	/************************************************************************/
	/* 接口说明：获取某一分区用户有权限看到的摄像头设备简易信息(所属分区、逻辑ID、名称)。
	命令码：  113
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 分区ID(4)
	返回码：  513
	应答格式：返回码(3) 命令序列号(10) 分区ID(4) 摄像头个数(6) [摄像头逻辑ID(6) 摄像头名称长度(2) 摄像头名称]
	*/
	/************************************************************************/
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	long lRet = ICV_SUCCESS;

	if(lZoneID <=0)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;

	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_GET_ZONE_SIMPLE_CAMERA, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	//组织区名称
	lRet = NET_WRAPPER->m_pMsgPacker->PackCmdGetCustZoneCam(szBuffer+lMsgLen, sizeof(szBuffer)-lMsgLen, lZoneID, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
// 	//直接返回Buffer  *  @version     08/28/2010  lixiaohao  $(Desp).
// 	memcpy(plistCam,pCharRemain,lRemainLen);	

	//解析摄像头列表
	CVideoCameraList plistCam;
	lOutLen = 0;
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToSimpleCameraList(pCharRemain, lRemainLen, &plistCam);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}

	//List转化为数组		
	lRet = TransCamList2CamArray(ppVideoCamera,  pnVideoCamera,  &plistCam);

	//释放Buffer
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;
}


/**
 *  获取某一分区的监视器信息
 *  @param  -[in]  HVideoClienT hClient: 客户端句柄
 *  @param  -[out] SVideoMonitor** ppVideoMonitor:	监视器指针
 *  @param  -[out] int* pnVideoMonitor:	            监视器个数
 *  @param  -[in] long lZoneID						分区ID
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
 *  @return  ==0 success  !=0 failure
 *
 *  @version     07/24/2010  lixiaohao  Initial Version.
 */
long VR_GetAllMonInfoOfZone(HVideoClienT hClient, SVideoMonitor** ppVideoMonitor, int* pnVideoMonitor,
							long lZoneID, long lTimeOut)
{
	/************************************************************************/
	/* 接口说明：获取用户有权限看到的某一分区的监视器设备信息。
	命令码：  121
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 分区ID(4)
	返回码：  521
	应答格式：命令码(3) 命令序列号(10) 分区ID(4) 监视器个数(4) [监视器逻辑ID(4) 监视器名称长度(2) 
	监视器名称 监视器描述长度(2) 监视器描述 连接设备ID(4) 通道号(4)]
	*/
	/************************************************************************/
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	long lRet = ICV_SUCCESS;

	if(lZoneID <=0)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;

	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_GET_ZONE_MONITOR, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	//组织区格式
	lRet = NET_WRAPPER->m_pMsgPacker->PackCmdGetCustZoneCam(szBuffer+lMsgLen, sizeof(szBuffer)-lMsgLen, lZoneID, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
// 	//直接返回Buffer  *  @version     08/28/2010  lixiaohao  $(Desp).
// 	memcpy(plistMon,pCharRemain,lRemainLen);
	CVideoMonitorList plistMon;
	long lOffset = 0;
	//=====解析监视器列表=====
	lOutLen = 0;
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToMonitorList(pCharRemain,&lOffset,lRemainLen, &plistMon);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}

	//List转化为数组
	lRet = TransMonList2MonArray(ppVideoMonitor, pnVideoMonitor,  &plistMon);

	//释放Buffer
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;	
}

/**
*  获取摄像头的预置位信息
* @param  -[in]  HVideoClienT hClient: 客户端句柄
* @param  -[in,out] long& lCamID:		摄像头ID
* @param  -[in,out] long& lLastTime:     当前最新所有配置信息的时间 
* @param  -[out] SVideoPSP** ppVideoPSP:  预置位指针
* @param  -[out] int* pnVideoPSP:		  预置位个数
* @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     07/24/2010  lixiaohao  Initial Version.
*/
long VR_GetCamPSPInfo(HVideoClienT hClient, long lCamID, long& lLastTime, SVideoPSP** ppVideoPSP,
					  int* pnVideoPSP, long lTimeOut)
{
	/************************************************************************/
	/*	命令码：  140
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 摄像头逻辑ID(6) 信息更新时间(10)
	返回码：  540
	应答格式：命令码(3) 命令序列号(10) 错误码(10) 摄像头逻辑ID（6） 本次时间（10） 摄像头预置位个数(2) 
	[预置位名称长度(2) 预置位名称 预置位描述长度(2) 预置位描述 预置位序号(2)]
	*/
	/************************************************************************/
	 	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

// 	//入口检测
// 	if(plistPSP == NULL)
// 		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = ICV_SUCCESS;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;

	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_GET_PSP, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	//摄像头编号信息更新时间
	sprintf(szBuffer+lMsgLen, "%06d%010d",lCamID,lLastTime);
	lMsgLen += VIDEO_CAMERA_ID_SIZE;
	lMsgLen += VIDEO_CHANGETIME_LENGTH_SIZE;
	
	char* pCharRemain = NULL;
	long lRemainLen = 0;
	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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

	lRemainLen = lRecvLen - lOutLen;
	pCharRemain = pRecvBuf + lOutLen;
	
	// 获取服务端模式的最新时间
	long lNewestCfgTime = 0;
	long lOffset = 0;
	//摄像头ID
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToInteger(pCharRemain, &lOffset, lRemainLen, VIDEO_CAMERA_ID_SIZE, lCamID);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lResult;
	}

	// 最新更新时间
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToInteger(pCharRemain, &lOffset, lRemainLen, VIDEO_CHANGETIME_LENGTH_SIZE, lNewestCfgTime);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lResult;
	}
	
	lLastTime = lNewestCfgTime;
// 	//直接返回Buffer  *  @version     08/28/2010  lixiaohao  $(Desp).
// 	memcpy(plistPSP,pCharRemain+lOffset,lRemainLen-lOffset);		

	// 解析返回信息
	CVideoPSPList plistPSP;
    lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToPSPList(pCharRemain, &lOffset, lRemainLen, &plistPSP);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lResult;
	}

	//LIST转变为Array
	lRet = TransPSPList2PSPArray(ppVideoPSP, pnVideoPSP,  &plistPSP);

	//释放Buffer
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;
}


/**
*  增加预置位
* @param  -[in]  HVideoClienT hClient: 客户端句柄
* @param  -[in] long nCamID:							 摄像头ID
* @param  -[in] SVideoPSP *pthePSP:						 预置位信息
* @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     07/24/2010  lixiaohao  Initial Version.
*/
long VR_AddPSP(HVideoClienT hClient, long lCamID, SVideoPSP *pthePSP, long lTimeOut)
{
	/************************************************************************/
	/* 命令码：  141
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 
	摄像头逻辑ID 预置位名称长度(2) 预置位名称 预置位描述长度(2) 预置位描述 预置位序号(2)
	返回码：  541
	应答格式：命令码(3) 命令序列号(10) 错误码(10)
	/************************************************************************/
	 	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	//入口检测
	if(pthePSP == NULL || lCamID <=0)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = ICV_SUCCESS;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;

	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_ADD_PSP, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;
	
	//组织消息指令
	lOutLen = sprintf(szBuffer+lMsgLen, "%06d%02d%s%02d%s%03d",lCamID,strlen((*pthePSP).m_szName), (*pthePSP).m_szName,
		strlen((*pthePSP).m_szDescription), (*pthePSP).m_szDescription, (*pthePSP).m_nPresetIndex);
	lMsgLen += lOutLen;

	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	
    NET_WRAPPER->FreeRecvBuff(pRecvBuf);	
	//返回错误码
	return lResult;
}

long VR_AddPSP2(HVideoClienT hClient, long lCamID, SVideoPSP *pthePSP, long lTimeOut)
{
	/************************************************************************/
	/* 命令码：  145
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 
	摄像头逻辑ID 预置位名称长度(2) 预置位名称 预置位描述长度(2) 预置位描述 预置位序号(2)
	返回码：  545
	应答格式：命令码(3) 命令序列号(10) 错误码(10)
	/************************************************************************/
	 	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	//入口检测
	if(pthePSP == NULL || lCamID <=0)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = ICV_SUCCESS;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;

	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_ADD_PSP2, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;
	
	//组织消息指令
	lOutLen = sprintf(szBuffer+lMsgLen, "%06d%02d%s%02d%s%03d",lCamID,strlen((*pthePSP).m_szName), (*pthePSP).m_szName,
		strlen((*pthePSP).m_szDescription), (*pthePSP).m_szDescription, (*pthePSP).m_nPresetIndex);
	lMsgLen += lOutLen;

	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	
    NET_WRAPPER->FreeRecvBuff(pRecvBuf);	
	//返回错误码
	return lResult;
}

/**
*  应用预置位
*  @param  -[in]  HVideoClienT hClient: 客户端句柄
*  @param  -[in] long nCamID:							  摄像头ID
*  @param  -[in] SVideoPSP *pthePSP:					  预置位信息
*  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
*  @return ==0 success !=0 failure
*
*  @version     07/24/2010  lixiaohao  Initial Version.
*/
long VR_ApplyPSP(HVideoClienT hClient, long lCamID, SVideoPSP *pthePSP, long lTimeOut)
{
	/************************************************************************/
	/* 命令码：  144
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 摄像头逻辑ID 
	应用的预置位名称长度(2) 应用的预置位名称
	返回码：  544
	应答格式：命令码(3) 命令序列号(10) 错误码(10)
	/************************************************************************/

	 	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	//入口检测
	if(pthePSP == NULL || lCamID <=0)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = ICV_SUCCESS;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;

	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_APPLY_PSP, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;
	
	//组织消息指令
	lOutLen = sprintf(szBuffer+lMsgLen, "%06d%02d%s", lCamID, strlen((*pthePSP).m_szName),(*pthePSP).m_szName);
	lMsgLen += lOutLen;

	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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

	//释放Buffer
    NET_WRAPPER->FreeRecvBuff(pRecvBuf);	
	//返回错误码
	return lResult;
}

/**
*  获取所有的模式名称
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param  -[in,out]  long lLastTime:     当前最新所有配置信息的时间 
* @param -[out] SVideoMode** ppVideoMode: 模式指针
* @param -[out] int* pnVideoMode:         模式个数
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     07/24/2010  lixiaohao  Initial Version.
*/
long VR_GetAllModeName(HVideoClienT hClient,long& lLastTime, SVideoMode** ppVideoMode, int* pnVideoMode,
					   long lTimeOut)
{
	/************************************************************************/
	/* 接口说明：获取所有模式名称
	命令码：  130
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名信息更新时间(10)
	返回码：  530
	应答格式：命令码(3) 命令序列号(10) 错误码(10) 信息更新时间(10) 模式个数(4) [模式类型(1) 模式名称长度(2) 模式名称 模式描述长度(2) 模式描述]
 	/************************************************************************/
	 	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

// 	//入口检测
// 	if(plistMode == NULL)
// 		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = ICV_SUCCESS;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;

	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_GET_ALL_MODE_NAME, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	//信息更新时间
	sprintf(szBuffer+lMsgLen, "%010d", lLastTime);
	lMsgLen += VIDEO_CHANGETIME_LENGTH_SIZE;
	
	char* pCharRemain = NULL;
	long lRemainLen = 0;
	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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

	lRemainLen = lRecvLen - lOutLen;
	pCharRemain = pRecvBuf + lOutLen;
	
	// 获取服务端模式的最新时间
	long lNewestCfgTime = 0;
	long lOffset = 0;
	// 最新更新时间
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToInteger(pCharRemain, &lOffset, lRemainLen, VIDEO_CHANGETIME_LENGTH_SIZE, lNewestCfgTime);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lResult;
	}
	
	lLastTime = lNewestCfgTime;

	// 解析返回信息
// 	//直接返回Buffer  *  @version     08/28/2010  lixiaohao  $(Desp).
// 	memcpy(plistMode,pCharRemain+lOffset,lRemainLen-lOffset);	
	CVideoModeList plistMode;
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToModeList(pCharRemain, &lOffset, lRemainLen, &plistMode, false);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lResult;
	}

	//List转化为数组		
	lRet = TransModeList2ModeArray(ppVideoMode,  pnVideoMode,  &plistMode);

	//释放Buffer
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;
}
/**
*  增加模式
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[in] SVideoMode* ptheMode: 模式信息
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     07/28/2010  lixiaohao  Initial Version.
*/
long VR_AddMode(HVideoClienT hClient, SVideoMode* ptheMode, long lTimeOut)
{
	/************************************************************************/
	/*	接口说明：增加模式
	命令码：  131
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 
	模式类型(1) 模式名称长度(2) 模式名称 模式描述长度(2) 模式描述 模式参数长度(4) 模式参数
	返回码：  531
	应答格式：命令码(3) 命令序列号(10) 错误码(10)
	/************************************************************************/
	 	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	//入口检测
	if(ptheMode == NULL)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = ICV_SUCCESS;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;

	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_ADD_MODE, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;
	
	//组织消息指令
	lOutLen = sprintf(szBuffer+lMsgLen, "%01d%02d%s%02d%s%04d%s", (*ptheMode).m_nType, strlen((*ptheMode).m_szName), (*ptheMode).m_szName,
		strlen((*ptheMode).m_szDescription),(*ptheMode).m_szDescription,strlen((*ptheMode).m_szPara),(*ptheMode).m_szPara);
	lMsgLen += lOutLen;

	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	
    NET_WRAPPER->FreeRecvBuff(pRecvBuf);	
	//返回错误码
	return lResult;
	
}

/**
 *  删除模式
 * @param -[in] HVideoClienT hClient: 客户端句柄
 * @param -[in] SVideoMode* ptheMode: 模式信息
 * @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
 * @return ==0 success !=0 failure
 *
 *  @version     07/28/2010  lixiaohao  Initial Version.
 */
long VR_DelMode(HVideoClienT hClient, SVideoMode* ptheMode, long lTimeOut)
{
	/************************************************************************/
	/* 接口说明：删除若干个模式
	命令码：  132
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名  
	[模式名称长度(2) 模式名称]
	返回码：  532
	应答格式：命令码(3) 命令序列号(10) 删除不成功的个数(4) [模式名称长度(2) 模式名称]
	*/
	/************************************************************************/
	 	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	//入口检测
	if(ptheMode == NULL)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = ICV_SUCCESS;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;

	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_DELETE_MODE, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;
	
	//组织消息指令 //该接口只处理删除1个模式的情况
	lOutLen = sprintf(szBuffer+lMsgLen, "%02d%s%",strlen((*ptheMode).m_szName),(*ptheMode).m_szName);
	lMsgLen += lOutLen;

	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	
    NET_WRAPPER->FreeRecvBuff(pRecvBuf);	
	//返回错误码
	return lResult;	
	
}

/**
 *  修改模式
 * @param -[in] HVideoClienT hClient: 客户端句柄
 * @param -[in] char* szModeName: 旧模式名称
 * @param -[in] SVideoMode* ptheMode: 新模式信息
 * @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
 * @return ==0 success !=0 failure
 *  @version     07/28/2010  lixiaohao  Initial Version.
*/
long VR_ModifyMode(HVideoClienT hClient, char* szModeName, SVideoMode* ptheMode, long lTimeOut)
{
	/************************************************************************/
	/* 接口说明：修改模式
	命令码：  133
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 
	旧模式名称长度(2) 旧模式名称 新模式类型(1) 新模式名称长度(2) 新模式名称 新模式描述长度(2) 
	新模式描述 新模式参数长度(4) 新模式参数
	返回码：  533
	应答格式：命令码(3) 命令序列号(10) 错误码(10)
	/************************************************************************/
	 	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	//入口检测
	if(ptheMode == NULL)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = ICV_SUCCESS;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;

	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_MODIFY_MODE, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;
	
	//组织消息指令
	lOutLen = sprintf(szBuffer+lMsgLen, "%02d%s%01d%02d%s%02d%s%04d%s", strlen(szModeName), szModeName,
		(*ptheMode).m_nType, strlen((*ptheMode).m_szName),(*ptheMode).m_szName,
		strlen((*ptheMode).m_szDescription), (*ptheMode).m_szDescription, strlen((*ptheMode).m_szPara), (*ptheMode).m_szPara);
	lMsgLen += lOutLen;

	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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

	//释放Buffer
    NET_WRAPPER->FreeRecvBuff(pRecvBuf);	
	//返回错误码
	return lResult;
}


/**
*  获取模式的布局信息
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[in] char* szModeName: 模式名称
* @param -[out] char* sztheModePara: 模式布局信息
* @param -[out] &lSize: 模式布局信息的长度
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     07/28/2010  lixiaohao  Initial Version.
*/
long VR_GetModeLayout(HVideoClienT hClient, char* szModeName, char* sztheModePara,long& lSize, long lTimeOut)
{
	/************************************************************************/
	/* 接口说明：获取模式的布局信息
	命令码：  134
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 模式名称长度(2) 模式名称
	返回码：  534
	应答格式：命令码(3) 命令序列号(10) 模式参数长度(4) 模式参数
	/************************************************************************/
	 	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	//入口检测
	if(szModeName == NULL || lSize > VIDEO_MESSAGE_MAX_SIZE/4)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = ICV_SUCCESS;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;

	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_GET_MODE_LAYOUT, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;
	
	//组织消息指令
	lOutLen = sprintf(szBuffer+lMsgLen, "%02d%s%",strlen(szModeName),szModeName);
	lMsgLen += lOutLen;

	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	long lRemainLen = lRecvLen - lOutLen;
	char* pCharRemain = pRecvBuf + lOutLen;
	lRet = NET_WRAPPER->m_pMsgParser->ParseCmdGetModeLayout(pCharRemain,sztheModePara,lSize);
    NET_WRAPPER->FreeRecvBuff(pRecvBuf);	

	return lRet;
}

//---------------------自定义分区相关接口----------------------------------------

/**
 *  $(添加自定义分区).
 *  $(Detail).
 *
 * @param -[in] HVideoClienT hClient: 客户端句柄
 * @param -[in] const char *szCustZoneName： 自定义分区名称
 * @param -[in] const char *szCustZoneDesc： 自定义分区描述
 * @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
 * @return ==0 success !=0 failure
 *
 *  @version     08/01/2010  zhucongfeng  Initial Version.
 */

long VR_AddCustomZone(HVideoClienT hClient, const char *szCustZoneName, const char *szCustZoneDesc, long lTimeOut)
{
	/************************************************************************/
	/* 命令码：  VIDEO_CMD_CUSTZONE_ADD				211
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 
	分区名称名称长度(2) 分区名称 分区描述长度(2) 分区描述
	返回码：  611
	应答格式：命令码(3) 命令序列号(10) 错误码(10)
	/************************************************************************/

	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}
	
	//入口检测
	if((szCustZoneName == NULL) || (szCustZoneName[0] == '\0'))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	long lRet = ICV_SUCCESS;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_CUSTZONE_ADD, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;
	
	//组织消息指令
	lOutLen = sprintf(szBuffer+lMsgLen, "%02d%s%02d%s", strlen(szCustZoneName), szCustZoneName,
		strlen(szCustZoneDesc), szCustZoneDesc);
	lMsgLen += lOutLen;
	
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	
    NET_WRAPPER->FreeRecvBuff(pRecvBuf);	
	//返回错误码
	return lResult;
}

/**
 *  $(删除自定义分区).
 *  $(Detail).
 *
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[in] const char *szCustZoneName： 自定义分区名称
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
 *
 *  @version     08/01/2010  zhucongfeng  Initial Version.
 */

long VR_DeleteCustomZone(HVideoClienT hClient, const char *szCustZoneName, long lTimeOut)
{
	/************************************************************************/
	/* 命令码：  212
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 删除的分区名称长度(2) 删除的分区名称]
	返回码：  612
	应答格式：命令码(3) 命令序列号(10) 错误码(10)
	*/
	/************************************************************************/

	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	if((szCustZoneName == NULL) || (szCustZoneName[0] == '\0'))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	long lRet = ICV_SUCCESS;
	
	lRet = CVComm.ValidateNameEx(szCustZoneName, VIDEO_NAME_MAXSIZE+1);
	if (ICV_SUCCESS != lRet)
		return lRet;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_CUSTZONE_DEL, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;
	
	lOutLen = sprintf(szBuffer+lMsgLen, "%02d%s", strlen(szCustZoneName), szCustZoneName);
	
	lMsgLen += lOutLen;
	
	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lResult;
}

/**
 *  $(将摄像头拷贝到其他自定义分区).
 *  $(Detail).
 *
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[in] const char *szCustZoneName： 自定义分区名称
* @param -[in] CStrName** ppCStrName： 摄像头名指针
* @param -[in] int* pnCStrName： 摄像头名个数
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
 *
 *  @version     08/01/2010  zhucongfeng  Initial Version.
 */

long VR_CopyCamToCustomZone(HVideoClienT hClient, const char *szCustZoneName, CStrName** ppCStrName,
							int* pnCStrName, long lTimeOut)
{
	/************************************************************************/
	/* 命令码：  214
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 分区名称长度(2) 分区名称
	摄像头名称个数(6) 摄像头1～n名称长度(2) 摄像头1～n名称]
	返回码：  614
	应答格式：命令码(3) 命令序列号(10) 错误码(10)
	*/
	/************************************************************************/

	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	if((szCustZoneName == NULL) || (szCustZoneName[0] == '\0'))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	long lRet = ICV_SUCCESS;
	
	lRet = CVComm.ValidateNameEx(szCustZoneName, VIDEO_NAME_MAXSIZE+1);
	if (ICV_SUCCESS != lRet)
		return lRet;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_CUSTZONE_CPYCAM, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	//把数组的CSTrName转变为LIST
	CVideoList<CStrName> listCustZoneCamName;
	listCustZoneCamName.insert(listCustZoneCamName.end(), *ppCStrName, *ppCStrName + *pnCStrName);
	lRet = NET_WRAPPER->m_pMsgPacker->PackCmdCpyCustZoneCam(szBuffer+lMsgLen, sizeof(szBuffer)-lMsgLen, (char*)szCustZoneName,
														&listCustZoneCamName, lOutLen);
	if(lRet != ICV_SUCCESS)
		return lRet;
	
	lMsgLen += lOutLen;
	
	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lResult;
}

/**
 *  $(删除自定义分区的摄像头).
 *  $(Detail).
 *
 * @param -[in] HVideoClienT hClient: 客户端句柄
 * @param -[in] const char *szCustZoneName： 自定义分区名称
 * @param -[in] CStrName** ppCStrName： 摄像头名指针
 * @param -[in] int* pnCStrName：       摄像头名个数
 * @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
 * @return ==0 success !=0 failure
 *
 *  @version     08/01/2010  zhucongfeng  Initial Version.
 */

long VR_DeleteCustomZoneCam(HVideoClienT hClient, const char *szCustZoneName, CStrName** ppCStrName,
							int* pnCStrName, long lTimeOut)
{
	/************************************************************************/
	/* 命令码：  215
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 分区名称长度(2) 分区名称
	摄像头名称个数(6) 摄像头1～n名称长度(2) 摄像头1～n名称]
	返回码：  615
	应答格式：命令码(3) 命令序列号(10) 错误码(10)
	*/
	/************************************************************************/

	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	if((szCustZoneName == NULL) || (szCustZoneName[0] == '\0') || ppCStrName == NULL)
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	long lRet = ICV_SUCCESS;
	
	lRet = CVComm.ValidateNameEx(szCustZoneName, VIDEO_NAME_MAXSIZE+1);
	if (ICV_SUCCESS != lRet)
		return lRet;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_CUSTZONE_DELCAM, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;
	
	//把数组的CSTrName转变为LIST
	CVideoList<CStrName> listCustZoneCamName;
	listCustZoneCamName.insert(listCustZoneCamName.end(), *ppCStrName, *ppCStrName + *pnCStrName);

	lRet = NET_WRAPPER->m_pMsgPacker->PackCmdDelCustZoneCam(szBuffer+lMsgLen, sizeof(szBuffer)-lMsgLen, (char*)szCustZoneName,
														&listCustZoneCamName, lOutLen);

	if(lRet != ICV_SUCCESS)
		return lRet;
	
	lMsgLen += lOutLen;
	
	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lResult;
}


/**
*  场景 获取所有抓拍类型信息
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[out] ppIDMapText** ppVideoSnapFile: 抓拍类型指针
* @param -[out] int* pnIDMapText: 抓拍类型个数
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
long VR_GetAllSnapType(HVideoClienT hClient, SIDMapText** ppIDMapText, int *pnIDMapText, long lTimeOut)
{
	/************************************************************************/
	/*	接口说明：获取抓拍图片的信息类型，在抓拍图片保存时供选择
	命令码：  150
	命令格式：命令码(3) 命令序列号(10)
	返回码：  550
	应答格式：命令码(3) 命令序列号(10) 信息类型个数(2) [信息类型内容长度(2) 信息类型内容]
	*/
	/************************************************************************/
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

// 	if (pSnapTypeList == NULL)
// 		return EC_ICV_CCTV_FUNCPARAMINVALID;

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	long lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_GET_SNAP_TYPE, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;

	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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

    long  lRemainLen = lRecvLen - lOutLen;
	char* pCharRemain = pRecvBuf + lOutLen;
// 	//直接返回Buffer *  @version     08/28/2010  lixiaohao  $(Desp).
// 	memcpy(pSnapTypeList,pCharRemain,lRemainLen);

	long lOffset = 0,lCount = 0;	
	// 解析抓拍类型数量
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToInteger(pCharRemain, &lOffset, lRemainLen,VIDEO_SNAPTYPE_COUNT_SIZE, lCount);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}

	//初始化申请数组内存
	*ppIDMapText = NULL;
	*pnIDMapText = lCount;
	if (lCount == 0)
	{
		*ppIDMapText = NULL;
		return ICV_SUCCESS;
	}
	*ppIDMapText = new SIDMapText[*pnIDMapText];
	memset(*ppIDMapText, 0x00, sizeof(ppIDMapText)*(*pnIDMapText));


	char szTypeInfo[VIDEO_NAME_MAXSIZE];
	for(int i=0; i<lCount; i++)
	{
		memset(szTypeInfo, 0x00, sizeof(szTypeInfo));
		lRet = NET_WRAPPER->m_pMsgParser->ParseSubBuffer(pCharRemain, &lOffset, szTypeInfo, lRemainLen, VIDEO_NAME_LENGTH_SIZE, 
													sizeof(szTypeInfo));
		if(lRet != ICV_SUCCESS)
		{
			break;
		}
		CIDMapText tmp;
		tmp.SetID(i);
		tmp.SetText(szTypeInfo);
		//将数据成员拷贝到指针数组中
		(*(*ppIDMapText + i)).m_nID = tmp.GetID();
		strncpy((*(*ppIDMapText + i)).m_szText, tmp.GetText(), VIDEO_NAME_MAXSIZE);
	}

	//释放Buffer
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;
}

/**
* 获取抓拍图片信息
* @param -[in] HVideoClienT: hClient: 客户端句柄
* @param -[in] long nCamID: char* szZone:分区
* @param -[in] long nCamID: 摄像头ID
* @param -[in] char *szStart: 开始时间
* @param -[in] char *szEnd: 结束时间 时间格式为"2010,07,10,10,00,00"
* @param -[in] char *pszSnapType: 抓拍类型
* @param -[out] SVideoSnapFile** ppVideoSnapFile: 抓拍信息指针
* @param -[out] int* pnVideoSnapFile: 抓拍信息个数
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
long VR_GetSnapPicInfo(HVideoClienT hClient, char* szZone,long nCamID,char *szStart,char *szEnd,
					   char *pszSnapType, SVideoSnapFile** ppVideoSnapFile, int* pnVideoSnapFile, 
					   long lTimeOut)
{
	/************************************************************************/
	/* 接口说明：获取满足指定过滤条件的抓拍信息，不包括抓拍图片内容
	命令码：  151
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 分区名长度(2) 分区名 摄像头ID(6) 查询起始时间(10) 查询结束时间(10) 抓拍类型长度(2) 抓拍类型 
	返回码：  551
	应答格式：命令码(3) 命令序列号(10) 错误码(10) 抓拍图片数量(6) [抓拍图片ID(6) 摄像头逻辑ID(6) 信息类型长度(2) 信息类型 抓拍时间(10) 详细描述长度(2) 详细描述 连续抓拍张数(2)]
	*/
	/************************************************************************/
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	if ((pszSnapType == NULL) ||(szStart == NULL) ||(szEnd == NULL)||(szZone == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	long lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_QUERY_SNAP_INFO, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;
	time_t tStart = StrToTime(szStart);
	time_t tEnd = StrToTime(szEnd);
	
	lOutLen = sprintf(szBuffer+lMsgLen, "%02d%s%06d%010I64d%010I64d%02d%s", 
									strlen(szZone), szZone,nCamID, 
									tStart, tEnd, 
									strlen(pszSnapType), pszSnapType);
	lMsgLen += lOutLen;
	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;

	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
// 	//直接返回Buffer *  @version     08/28/2010  lixiaohao  $(Desp).
// 	memcpy(plistSnapInfo,pCharRemain,lRemainLen);

	// 解析返回信息
	CVideoSnapFileList plistSnapInfo;
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToSnapFileList(pCharRemain,lRemainLen, &plistSnapInfo);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}
    
	//List转化为Array
	lRet = TransSnapFileList2SnapFileArray(ppVideoSnapFile, pnVideoSnapFile, &plistSnapInfo);

	//释放Buffer
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;
}


/**
* 添加抓拍图片到服务端
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[in] long nCamID: 摄像头ID
* @param -[in] char* szSnapType 抓拍类型
* @param -[in] char* szDesc 抓拍描述
* @param -[in] char* szExt  扩展信息
* @param -[in] char* szTmSnap 抓拍时间
* @param -[in] bool bSnapMore 连续抓拍
* @param -[in] char * szPicBuffer 图片buffer
* @param -[in] long lBufferSize Buffer大小
* @param -[out] long &lSnapID  抓拍ID
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 Success !0 failure !=-1 
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
long VR_AddSnapPicture(HVideoClienT hClient, long nCamID, char* szSnapType, char* szDesc, char* szExt,int nSnapCount, char* szTmSnap, char* szPicBuffer,long lBufferSize,long& lSnapID, long lTimeOut)
{
	/************************************************************************/
	/* 命令码：  153
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 摄像头逻辑ID(6) 信息类型长度(2) 信息类型 抓拍时间(10) 详细描述长度(2) 详细描述 连续抓拍张数(2) [图片内容长度(6) 图片内容]
	返回码：  553
	应答格式：命令码(3) 命令序列号(10) 错误码(10) snapID (6)
	*/
	/************************************************************************/

	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	if ((szSnapType == NULL) ||(szDesc == NULL) ||(szTmSnap == NULL) ||(szPicBuffer == NULL)||(szExt == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE*20];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	long lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_ADD_SNAP_PIC, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;	
	//组织消息
	lRet = sprintf(szBuffer+lMsgLen,"%06d%02d%s%10I64d%03d%s%02d%s%02d%",
		nCamID,strlen(szSnapType),szSnapType,
		StrToTime(szTmSnap),strlen(szExt),szExt,
		strlen(szDesc),szDesc,nSnapCount);
	lMsgLen +=lRet;

	char* pSendInfo = new char[lMsgLen + lBufferSize];
	memcpy(pSendInfo, szBuffer, lMsgLen);
    memcpy(pSendInfo+lMsgLen,szPicBuffer,lBufferSize);
	lMsgLen +=lBufferSize;



	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	lRet = SendMsgToServer(pClient, pSendInfo, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
	delete []pSendInfo;
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
	long  lRemainLen = lRecvLen - lOutLen;
	char*  pCharRemain = pRecvBuf + lOutLen;
	long  lOffset = 0;
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToInteger(pCharRemain,&lOffset, lRemainLen,VIDEO_SNAP_ID_SIZE,lSnapID);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;
	
}


/**
* 
修改抓拍图片信息
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[in] long nCamID: 摄像头ID
* @param -[in] long nSnapID: 抓拍ID
* @param -[in] char* szSnapType 抓拍类型
* @param -[in] char* szDesc 抓拍描述
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
long VR_ModifyPicInfo(HVideoClienT hClient, long nCamID, long nSnapID, char* szSnapType, char* szDesc, long lTimeOut)
{
	/************************************************************************/
	/* 命令码：  155
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 摄像头ID(6) 抓拍图片ID(6) 新信息类型长度(2) 新信息类型 信息详细描述长度(2) 新详细描述
	返回码：  555
	应答格式：命令码(3) 命令序列号(10) 错误码(10)
	*/
	/************************************************************************/
	
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	if ((szSnapType == NULL) ||(szDesc == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	long lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_MODIFY_SNAP_INFO, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;	
	//组织消息
	lRet = sprintf(szBuffer+lMsgLen,"%06d%06d%02d%s%02d%s",
		nCamID,nSnapID,strlen(szSnapType),szSnapType,
		strlen(szDesc),szDesc);
	lMsgLen +=lRet;


	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lResult;
}

/**
* 云台控制
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[in] long nCamID: 摄像头ID
* @param -[in] long nCtrlCode: 控制码
* @param -[in] long nCtrlType: 控制类型
* @param -[in] long nCtrlSpeed: 控制速度
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_PanControl(HVideoClienT hClient, long nCameraID, long nCtrlCode, long nCtrlType, long nCtrlSpeed, long lTimeOut)
{
	/************************************************************************/
	/* 命令码：185
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 控制的摄像头逻辑ID(6) 控制码(1) 控制类型(1) 控制速度(1)
	返回码：585
	应答格式：返回码(3) 命令序列号(10) 错误码(10)
	*/
	/************************************************************************/

	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	if ((nCameraID <0) ||(nCtrlCode <0))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	long lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_ORIENT_CONTROL, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;	
	//组织消息
	lRet = sprintf(szBuffer+lMsgLen,"%06d%01d%01d%01d",nCameraID,nCtrlCode,nCtrlType,nCtrlSpeed);
	lMsgLen +=lRet;


	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lResult;
}

/**
*  镜头控制
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[in] long nCamID: 摄像头ID
* @param -[in] long nCtrlCode: 控制码
* @param -[in] long nCtrlType： 控制类型
* @param -[in] long nCtrlSpeed: 控制速度
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_LensControl(HVideoClienT hClient, long nCameraID, long nCtrlCode, long nCtrlType,long nCtrlSpeed, long lTimeOut)
{
	/************************************************************************/
	/* 命令码：183
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 控制的摄像头逻辑ID(6) 控制码(1) 控制类型(1) 控制速度(1)
	返回码：583
	应答格式：返回码(3) 命令序列号(10) 错误码(10)
	*/
	/************************************************************************/

	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	if ((nCameraID <0) ||(nCtrlCode <0))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	long lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_LENS_CONTROL, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;	
	//组织消息
	lRet = sprintf(szBuffer+lMsgLen,"%06d%01d%01d%01d",nCameraID,nCtrlCode,nCtrlType,nCtrlSpeed);
	lMsgLen +=lRet;


	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lResult;
}

/**
*  附加设备控制
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[in] long nCamID: 摄像头ID
* @param -[in] long nCtrlCode: 控制码
* @param -[in] long nCtrlType： 控制类型
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_AuxDevControl(HVideoClienT hClient, long nCameraID, long nCtrlCode, long nCtrlType, long lTimeOut)
{
	/************************************************************************/
	/* 命令码：184
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 控制的摄像头逻辑ID(6) 控制码(1) 控制类型(1) 控制速度(1)
	返回码：584
	应答格式：返回码(3) 命令序列号(10) 错误码(10)
	*/
	/************************************************************************/
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	if ((nCameraID <0) ||(nCtrlCode <0))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	long lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_AUX_DEVICE_CONTROL, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;	
	//组织消息
	lRet = sprintf(szBuffer+lMsgLen,"%06d%01d%01d%01d",nCameraID,nCtrlCode,nCtrlType,0);
	lMsgLen +=lRet;


	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lResult;
}

/**
*  视频切换
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[in] char* szCameraName： 摄像头名称
* @param -[in] char* szMonitorName： 监视器名称
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
long VR_SwitchVideoToMonitor(HVideoClienT hClient, char* szCameraName, char* szMonitorName, long lTimeOut)
{
	/************************************************************************/
	/* 命令码：182
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 摄像头名称长度(2) 摄像头名称 监视器名称长度(2) 监视器名称
	返回码：582
	应答格式：返回码(3) 命令序列号(10) 错误码(10)
	/************************************************************************/
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	if ((szCameraName == NULL) ||(szMonitorName == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	long lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_SWITCH_BY_NAME, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;	
	//组织消息
	lRet = sprintf(szBuffer+lMsgLen,"%02d%s%02d%s",strlen(szCameraName),szCameraName,strlen(szMonitorName),szMonitorName);
	lMsgLen +=lRet;


	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lResult;
}

/**
*  根据ID进行视频切换
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[in] long lCamID： 摄像头ID
* @param -[in] long lMonID： 监视器ID
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
long VR_SwitchVideoToMonitorByID(HVideoClienT hClient, long lCamID, long lMonID,  long lTimeOut)
{
	/************************************************************************/
	/*  命令码：181
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名摄像头逻辑ID(6) 监视器逻辑ID(4)
	返回码：581
	应答格式：命令码(3) 命令序列号(10) 错误码(10)
	*/
	/************************************************************************/
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	if ((lCamID <0) ||(lMonID <0))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	long lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_SWITCH_BY_ID, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;	
	//组织消息
	lRet = sprintf(szBuffer+lMsgLen,"%06d%04d",lCamID,lMonID);
	lMsgLen +=lRet;


	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lResult;
}

/**
* 轮询视频切换
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[in] char* szModeBuff： 轮询模式信息
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
long VR_CircularSwitchCamToMon(HVideoClienT hClient, const char* szModeBuff, long lTimeOut)
{
	/************************************************************************/
	/* 命令码：187
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 轮循参数长度(4) 轮循参数
	返回码：587
	应答格式：返回码(3) 命令序列号(10) 错误码(10)
	/************************************************************************/
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	if ((szModeBuff == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	long lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_CYCLE_SWITCH_BY_BUFFER, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;	
	//组织消息
	lRet = sprintf(szBuffer+lMsgLen,"%04d%s",strlen(szModeBuff),szModeBuff);
	lMsgLen +=lRet;


	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lResult;	
}

/**
* 停止轮询视频切换
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[in] char* szMonitorName： 监视器名称
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_StopCircularSwitchCamToMon(HVideoClienT hClient, const char* szMonitorName, long lTimeOut)
{
	/************************************************************************/
	/* 命令码：188
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 监视器轮循模式名称长度(2) 监视器轮循模式名称
	返回码：588
	应答格式：返回码(3) 命令序列号(10) 错误码(10)
	/************************************************************************/
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	if ((szMonitorName == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	long lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_STOP_CYCLE_SWITCH, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;	
	//组织消息
	lRet = sprintf(szBuffer+lMsgLen,"%02d%s",strlen(szMonitorName),szMonitorName);
	lMsgLen +=lRet;


	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lResult;	
}


/**
* * @param  -[in]  HVideoClienT hClient: 客户端句柄
* @param  -[in]  long nCamID: 		摄像头ID
* @param  -[in]  char* szStartTime   	开始时间(格式"2010,01,01,08,00,00")
* @param  -[in]  char* szEndTime    	结束时间(格式"2010,01,01,08,00,00")
* @param  -[in]  char* szExtent:		录像扩展信息
* @param  -[in]  char* szFileName:		录像文件名称
* @param  -[in]  char* szDesc:		    录像描述
* @param  -[in]  long lBufferSize:      录像片度长度
* @param  -[in]  char* pszBuffer:		录像片段Buffer
* @param  -[out] long & lRecordID:      录像片段ID
* @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
* @return  ==0 Success !=0 failure 
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
long VR_AddRecordInfo(HVideoClienT hClient, long nCamID, char* szStartTime, char* szEndTime, char* szExtent, char* szFileName, char* szDesc, char* pszBuffer,long lBufferSize,long & lRecordID, long lTimeOut)
{
	/************************************************************************/
	/* 	命令码：160
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 摄像头ID(6) 开始时间(10) 结束时间(10)
	录像扩展信息长度(3) 录像扩展信息 录像文件名长度(2) 录像文件名 录像描述长度(2) 录像描述 录像片段长度(10) 录像片度buffer
	返回码：560
	应答格式：返回码(3) 命令序列号(10) 错误码(10) 录像ID
	/************************************************************************/
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	if ((szStartTime == NULL)||(szEndTime == NULL)||(szExtent == NULL)||(szFileName == NULL)||(szDesc == NULL)||(pszBuffer == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	long lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_ADD_CAPTURE_RECORD, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;	
	//组织消息
	lRet = sprintf(szBuffer+lMsgLen, "%06d%010I64d%010I64d%03d%s%02d%s%02d%s%010d",
					nCamID,StrToTime(szStartTime),StrToTime(szEndTime),
					strlen(szExtent),szExtent,strlen(szFileName),szFileName,
					strlen(szDesc),szDesc,lBufferSize);
	lMsgLen +=lRet;

	char* pSendInfo = new char[lMsgLen + lBufferSize];
	memcpy(pSendInfo, szBuffer, lMsgLen);
	memcpy(pSendInfo+lMsgLen,pszBuffer,lBufferSize);
	lMsgLen +=lBufferSize;

	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	lRet = SendMsgToServer(pClient, pSendInfo, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
	delete []pSendInfo;
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

	long  lRemainLen = lRecvLen - lOutLen;
	char*  pCharRemain = pRecvBuf + lOutLen;
	long  lOffset = 0;
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToInteger(pCharRemain,&lOffset, lRemainLen,VIDEO_SNAP_ID_SIZE,lRecordID);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;

}

/**
*  查询录像片段信息
* @param  -[in]  HVideoClienT hClient: 客户端句柄
* @param  -[in]  long nCamID: 			摄像头ID
* @param  -[in]  char* szStartTime   	开始时间(格式"2010,01,01,08,00,00")
* @param  -[in]  char* szEndTime    	结束时间(格式"2010,01,01,08,00,00")
* @param  -[out] SRecord** ppRecord   	录像片段信息指针
* @param  -[out] int* pnRecord      	录像片段信息个数
* @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
long VR_QueryRecord(HVideoClienT hClient, long nCamID, char* szStartTime, char* szEndTime,
					SRecord** ppRecord, int* pnRecord, long lTimeOut)
{

	/************************************************************************/
	/* 	命令码：161
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 摄像头ID(6) 开始时间(10) 结束时间(10)
	返回码：561
	应答格式：返回码(3) 命令序列号(10) 错误码(10) 录像片度信息
	/************************************************************************/
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	if ((szStartTime == NULL)||(szEndTime == NULL)||(nCamID<=0))
		return EC_ICV_CCTV_FUNCPARAMINVALID;	

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	long lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_QUERY_CAPTURE_INFO, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;	
	//组织消息
	lRet = sprintf(szBuffer+lMsgLen, "%06d%010I64d%010I64d",nCamID,StrToTime(szStartTime),StrToTime(szEndTime));
	lMsgLen +=lRet;


	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	//直接返回Buffer
// 	memcpy(theListRecord,pCharRemain,lRemainLen);
	//解析返回信息
	CRecordList theListRecord;
	lRet = NET_WRAPPER->m_pMsgParser->ParseRetQueryRecord(pCharRemain, &theListRecord);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}

	//List转化为Array
	lRet = TransRecordList2RecordArray(ppRecord, pnRecord, &theListRecord);

    //释放Buffer
    NET_WRAPPER->FreeRecvBuff(pRecvBuf);	
	return lRet;
}

/**
*修改录像片段信息
* @param  -[in]  HVideoClienT hClient: 客户端句柄
* @param  -[in] long nRecordID:   		录像片段ID
* @param  -[in] SRecord* pRec:  	 	录像片段信息
* @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
long VR_ModifyRecord(HVideoClienT hClient, long nRecordID, CRecord* pRec, long lTimeOut)
{
	/************************************************************************/
	/* 	命令码：162
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 录像ID 录像信息
	返回码：562
	应答格式：返回码(3) 命令序列号(10) 错误码(10) 
	/************************************************************************/
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	if ((nRecordID <= 0)||(pRec ==NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;	

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	long lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_MDF_CAPTURE_RECORD, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;	
	//组织消息
	NET_WRAPPER->m_pMsgPacker->PackCmdModifyRecord(szBuffer+lMsgLen,VIDEO_MESSAGE_MAX_SIZE,nRecordID,pRec,lOutLen);
	lMsgLen +=lOutLen;


	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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

	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lResult;
}

/**
*  下载录像片段
* @param  -[in]  HVideoClienT hClient: 客户端句柄
* @param  -[in] long nRecordID:   		录像片段ID
* @param  -[out] char* szFileName:      录像名称
* @param  -[out] char* pszBuffer:  		录像片段Buffer
* @param  -[out] long& lBufferSize;     录像片段长度
* @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
long VR_DownloadRecord(HVideoClienT hClient, long nRecordID, char* szFileName,char* pszBuffer,long& lBufferSize, long lTimeOut)
{
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	if ((nRecordID<=0)||(pszBuffer ==NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;	

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	long lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_GET_CAPTRUE_BUFFER, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;	
	//组织消息
	lRet = sprintf(szBuffer+lMsgLen, "%06d",nRecordID);
	lMsgLen +=lRet;


	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	lRet = NET_WRAPPER->m_pMsgParser->ParseRetDownloadRecord(pCharRemain,szFileName,lBufferSize);
	memcpy(pszBuffer,pRecvBuf+lRecvLen-lBufferSize,lBufferSize);

    NET_WRAPPER->FreeRecvBuff(pRecvBuf);	
	return lRet;	
}

/**
*  删除录像片段
* @param  -[in]  HVideoClienT hClient: 客户端句柄
* @param  -[in] long nRecordID:   		录像片段ID
* @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_DeleteRecord(HVideoClienT hClient, long nRecordID, long lTimeOut)
{
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}

	if (nRecordID<=0)
		return EC_ICV_CCTV_FUNCPARAMINVALID;	

	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	long lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_DEL_CAPTURE_RECORD, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;	
	//组织消息
	lRet = sprintf(szBuffer+lMsgLen, "%06d",nRecordID);
	lMsgLen +=lRet;


	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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

	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lResult;
	
}

/**
*  获取摄像头对应的实时图像的设备
* @param    -[in]     HVideoClienT hClient: 客户端句柄
* @param    -[in]     long lCamID: [摄像头设备ID]
* @param    -[out]    long& lDeviceType: [实时图像的设备类型]
* @param    -[out]    long& lProductID: [实时图像的设备ID]
* @param    -[out]    char* szChannel: [实时图像的设备通道号]
* @param    -[in]     long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     02/10/2010  louzhengqing  Initial Version.
*/
VIDEO_REMOTE_API long VR_GetCurPicDeviceFromCam(HVideoClienT hClient, long lCamID, long& lDeviceType, long& lProductID,
												char* szChannel, long lTimeOut)
{
	/************************************************************************/
    /*命令码：230
    请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 摄像头逻辑ID(6)
    返回码：630
    应答格式：返回码(3) 命令序列号(10) 错误码(10) 设备类型(1) 设备ID(4) 通道号(4)
	*/
	/************************************************************************/
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}
	
	long lRet = ICV_SUCCESS;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;

	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_REALDEVICE_GET, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;
	lRet = sprintf(szBuffer+lMsgLen,"%06d", lCamID);
	lMsgLen +=lRet;	
	char* pCharRemain = NULL;
	long lRemainLen = 0;
	
	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	
	lRemainLen = lRecvLen - lOutLen;
	pCharRemain = pRecvBuf + lOutLen;

	long lOffset = 0;	
	// =====解析返回号信息=====
	// 设备类型
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToInteger(pCharRemain, &lOffset, lRemainLen, VIDEO_DEVICE_TYPE_SIZE, lDeviceType);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}
	// 设备ID
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToInteger(pCharRemain, &lOffset, lRemainLen, VIDEO_PRODUCT_ID_SIZE, lProductID);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}
    //设备通道
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToChannel(pCharRemain, &lOffset, lRemainLen, szChannel);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}
	
    //释放Buffer
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;
}


/**
*  获取取摄像头对应的历史图像的设备
* @param    -[in]     HVideoClienT hClient: 客户端句柄
* @param    -[in]     long lCamID: [摄像头设备ID]
* @param    -[out]    long& lDeviceType: [历史图像的设备类型]
* @param    -[out]    long& lProductID: [历史图像的设备ID]
* @param    -[out]    char* szChannel: [历史图像的设备通道号]
* @param    -[in]     long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     02/10/2010  louzhengqing  Initial Version.
*/
VIDEO_REMOTE_API long VR_GetHisPicDeviceFromCam(HVideoClienT hClient, long lCamID, long& lDeviceType, long& lProductID,
												char* szChannel, long lTimeOut)
{
	/************************************************************************/
    /*命令码：232
    请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名摄像头逻辑ID(6)
	返回码：632
	应答格式：返回码(3) 命令序列号(10) 错误码(10) 设备类型(1) 设备ID(4) 通道号(4)
	*/
	/************************************************************************/
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}
	
	long lRet = ICV_SUCCESS;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_HISDEVICE_GET, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;
	lRet = sprintf(szBuffer+lMsgLen,"%06d", lCamID);
	lMsgLen +=lRet;	
	char* pCharRemain = NULL;
	long lRemainLen = 0;
	
	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	
	lRemainLen = lRecvLen - lOutLen;
	pCharRemain = pRecvBuf + lOutLen;
	
	long lOffset = 0;	
	// =====解析返回号信息=====
	// 设备类型
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToInteger(pCharRemain, &lOffset, lRemainLen, VIDEO_DEVICE_TYPE_SIZE, lDeviceType);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}
	// 设备ID
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToInteger(pCharRemain, &lOffset, lRemainLen, VIDEO_PRODUCT_ID_SIZE, lProductID);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}
    //设备通道
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToChannel(pCharRemain, &lOffset, lRemainLen, szChannel);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}
	
    //释放Buffer
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;
}

/**
*  获取取摄像头对应的控制设备
* @param    -[in]     HVideoClienT hClient: 客户端句柄
* @param    -[in]     long lCamID: [摄像头设备ID]
* @param    -[out]    long& lDeviceType: [历史图像的设备类型]
* @param    -[out]    long& lProductID: [历史图像的设备ID]
* @param    -[out]    char* szChannel: [历史图像的设备通道号]
* @param    -[in]     long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     03/30/2011  fengjuanyong  Initial Version.
*/
VIDEO_REMOTE_API long VR_GetCtrlDeviceFromCam(HVideoClienT hClient, long lCamID, long& lDeviceType, long& lProductID,
												char* szChannel, long lTimeOut)
{
	/************************************************************************/
    /*命令码：236
    请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名摄像头逻辑ID(6)
	返回码：636
	应答格式：返回码(3) 命令序列号(10) 错误码(10) 设备类型(1) 设备ID(4) 通道号(4)
	*/
	/************************************************************************/
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}
	
	long lRet = ICV_SUCCESS;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_CTLDEVICE_GET, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;
	lRet = sprintf(szBuffer+lMsgLen,"%06d", lCamID);
	lMsgLen +=lRet;	
	char* pCharRemain = NULL;
	long lRemainLen = 0;
	
	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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

	lRemainLen = lRecvLen - lOutLen;
	pCharRemain = pRecvBuf + lOutLen;
	
	long lOffset = 0;	
	// =====解析返回号信息=====
	// 设备类型
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToInteger(pCharRemain, &lOffset, lRemainLen, VIDEO_DEVICE_TYPE_SIZE, lDeviceType);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}
	// 设备ID
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToInteger(pCharRemain, &lOffset, lRemainLen, VIDEO_PRODUCT_ID_SIZE, lProductID);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}
    //设备通道
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToChannel(pCharRemain, &lOffset, lRemainLen, szChannel);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}
	
    //释放Buffer
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;
}

/**
*  释放摄像头对应的编码器
* @param    -[in]     HVideoClienT hClient: 客户端句柄
* @param    -[in]     long lCamID: [摄像头设备ID]
* @param    -[in]     long lCodeID: [编码器ID]
* @param    -[in]     char* szChannel: [通道号]
* @param    -[in]     long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     02/10/2010  louzhengqing  Initial Version.
*/
VIDEO_REMOTE_API long VR_ReleaseCodeDeviceofCam(HVideoClienT hClient, long lCamID, long lCodeID, char* szChannel, long lTimeOut)
{
	/************************************************************************/
    /*
	命令码：231
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名群组名 摄像头逻辑ID(6) 编码器ID(4) 通道号(4)
	返回码：631
	应答格式：返回码(3) 命令序列号(10) 错误码(10)
	*/
	/************************************************************************/
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
	{
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	}
	
	long lRet = ICV_SUCCESS;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_ENCODEDEV_FREE, lOutLen);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}
	lMsgLen += lOutLen;
	//2013/4/27 lixiaohao  增加szchannel的长度 %04d
	lRet = sprintf(szBuffer+lMsgLen,"%06d%04d%04d%s", lCamID, lCodeID, strlen(szChannel),szChannel);
	lMsgLen +=lRet;	
	char* pCharRemain = NULL;
	long lRemainLen = 0;
	
	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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

	//释放Buffer
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lResult;	
}

/**
 *  根据摄像头名称获取分配的监视器信息.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[in]  char *  pszCamName: [摄像头名称]
 *  @param  -[in]  long lAuthMode: [权限占用模式 0 >=当前用户权限优先级 1 >当前用户权限优先级]
 *  @param  -[out]  char* pszMonitorName: [监视器名称]
 *  @param  -[out]  long& lSize: 监视器名称的长度
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     02/28/2011  fengjuanyong  Initial Version.
 */
VIDEO_REMOTE_API long VR_GetMonitorDevFromCam(HVideoClienT hClient, char* pszCamName, long lAuthMode, char* pszMonitorName, long& lSize, long lTimeOut)
{
	/************************************************************************/
    /*命令码：233
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 摄像头名称长度(2) 摄像头名称 权限模式(2)
	返回码：633
	应答格式：返回码(3) 命令序列号(10) 错误码(10) 监视器名称长度(2) 监视器名称*/
	/************************************************************************/
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	long lRet = ICV_SUCCESS;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;

	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_MONITORDEV_GET, lOutLen);
	if(lRet != ICV_SUCCESS)
		return lRet;
	lMsgLen += lOutLen;

	//组织摄像头名称，都是名称，所以可以用组织区名称的方法
	lRet = NET_WRAPPER->m_pMsgPacker->PackCmdGetCustZoneCam(szBuffer+lMsgLen, sizeof(szBuffer)-lMsgLen, pszCamName, lOutLen);
	if(lRet != ICV_SUCCESS)
		return lRet;
	lMsgLen += lOutLen;

	lRet = sprintf(szBuffer+lMsgLen,"%02d", lAuthMode);
	lMsgLen += lRet;	

	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	long lRemainLen = lRecvLen - lOutLen;
	char* pCharRemain = pRecvBuf + lOutLen;
	
	//解析监视器名称
	lRet = NET_WRAPPER->m_pMsgParser->ParseCmdGetMonitorName(pCharRemain,pszMonitorName,lSize);
    NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;
}

/**
 *  释放分配的监视器信息.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[in]  char *  pszCamName: [摄像头名称]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     02/28/2011  fengjuanyong  Initial Version.
 */
VIDEO_REMOTE_API long VR_ReleaseMonitorDevForCam(HVideoClienT hClient, char* pszCamName, long lTimeOut)
{
	/************************************************************************/
    /*
	命令码：234
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 摄像头名称长度(2) 摄像头名称
	返回码：634
	应答格式：返回码(3) 命令序列号(10) 错误码(10)
	*/
	/************************************************************************/
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	long lRet = ICV_SUCCESS;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;
	
	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_MONITORDEV_FREE, lOutLen);
	if(lRet != ICV_SUCCESS)
		return lRet;
	lMsgLen += lOutLen;

	//组织摄像头名称，都是名称，所以可以用组织区名称的方法
	lRet = NET_WRAPPER->m_pMsgPacker->PackCmdGetCustZoneCam(szBuffer+lMsgLen, sizeof(szBuffer)-lMsgLen, pszCamName, lOutLen);
	if(lRet != ICV_SUCCESS)
		return lRet;
	lMsgLen += lOutLen;
	
	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}

	//解析消息头
	long lResult;
	lRet = NET_WRAPPER->ParserMsgHeader(pRecvBuf, lResult, lOutLen);
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;
}

/**
 * 根据矩阵获取连接该矩阵的摄像头简易信息
 *  @param  -[in]  HVideoClienT hClient: 客户端句柄
 *  @param  -[in]  char *  pszMatrixName: [矩阵名称]
 *  @param  -[out] SVideoCamera** ppVideoCamera:  [摄像头指针]
 *  @param  -[out] int* pnVideoCamera:			  [摄像头个数]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
 *  @return  ==0 success  !=0 failure
 *
 *  @version     02/28/2011  fengjuanyong  Initial Version.
 */
VIDEO_REMOTE_API long VR_GetCamLstFromMatrix(HVideoClienT hClient, char* pszMatrixName, SVideoCamera** ppVideoCamera, int* pnVideoCamera, long lTimeOut)
{
	/************************************************************************/
    /*
	命令码：235
	请求格式：命令码(3) 命令序列号(10) 用户名长度(2) 用户名 群组名长度(2) 群组名 矩阵名称长度(2) 矩阵名称
	返回码：635
	应答格式：返回码(3) 命令序列号(10) 错误码(10) 摄像头个数(6) [分区ID(4) 摄像头逻辑ID(6) 摄像头名称长度(2) 摄像头名称]
	*/
	/************************************************************************/
	//检测客户端是否存在
	if(!GM_HELPER->IsClientRegistered(hClient))
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	PCLIENT_INFO pClient = (PCLIENT_INFO)hClient;
	if(pClient == NULL)
		return EC_ICV_CCTV_CLIENT_UNREGISTERED;
	
	long lRet = ICV_SUCCESS;
	
	//申请缓冲区
	char szBuffer[VIDEO_MESSAGE_MAX_SIZE];
	memset(szBuffer, 0x00, sizeof(szBuffer));
	long lMsgLen = 0;

	//组织消息头
	long lOutLen = 0;
	lRet = NET_WRAPPER->PackMsgHeader(pClient, szBuffer, sizeof(szBuffer), VIDEO_CMD_CAMBYMATRIX_GET, lOutLen);
	if(lRet != ICV_SUCCESS)
		return lRet;
	lMsgLen += lOutLen;

	//组织矩阵名称，都是名称，所以可以用组织区名称的方法
	lRet = NET_WRAPPER->m_pMsgPacker->PackCmdGetCustZoneCam(szBuffer+lMsgLen, sizeof(szBuffer)-lMsgLen, pszMatrixName, lOutLen);
	if(lRet != ICV_SUCCESS)
		return lRet;
	lMsgLen += lOutLen;
	
	//发送消息
	char *pRecvBuf = NULL;
	long lRecvLen = -1;
	
	lRet = SendMsgToServer(pClient, szBuffer, lMsgLen, &pRecvBuf, lRecvLen, lTimeOut);
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
	long lRemainLen = lRecvLen - lOutLen;
	char* pCharRemain = pRecvBuf + lOutLen;

	CVideoCameraList plistCam;
	// 解析返回信息
	lRet = NET_WRAPPER->m_pMsgParser->ParseBufferToSimpleCameraList(pCharRemain, lRemainLen, &plistCam);
	if(lRet != ICV_SUCCESS)
	{
		NET_WRAPPER->FreeRecvBuff(pRecvBuf);
		return lRet;
	}
	//List转化为数组
    lRet = TransCamList2CamArray(ppVideoCamera,  pnVideoCamera,  &plistCam);

	//释放Buffer
	NET_WRAPPER->FreeRecvBuff(pRecvBuf);
	return lRet;
}

//====================================释放内存===========================================================
/**
*  删除分区实体内存
* @param  -[in]  SVideoZone** ppVideoZone: 分区实体数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID  failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
long VR_FreeVideoZoneArray(SVideoZone** ppVideoZone)
{
	//参数错误
	if (NULL == ppVideoZone)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	
	delete[] *ppVideoZone;
	*ppVideoZone = NULL;
	return ICV_SUCCESS;
}

/**
*  删除设备产品型号实体内存
* @param  -[in]  SVideoProduct** ppVideoProduct: 设备产品型号实体数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
long VR_FreeVideoProductArray(SVideoProduct** ppVideoProduct)
{
	//参数错误
	if (NULL == ppVideoProduct)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	
	delete[] *ppVideoProduct;
	*ppVideoProduct = NULL;
	return ICV_SUCCESS;
}

/**
*  删除视频设备实体内存
* @param  -[in]  SVideoServer** ppVideoServer: 视频设备实体数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
long VR_FreeVideoDeviceArray(SVideoDevice** ppVideoDevice)
{
	//参数错误
	if (NULL == ppVideoDevice)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	
	delete[] *ppVideoDevice;
	*ppVideoDevice = NULL;
	return ICV_SUCCESS;
}


/**
*  删除设备类型实体实体内存
* @param  -[in]  SVideoServer** ppVideoServer: 设备类型实体数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
long VR_FreeVideoDevTypeArray(SVideoDevType** ppVideoDevType)
{
	//参数错误
	if (NULL == ppVideoDevType)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	
	delete[] *ppVideoDevType;
	*ppVideoDevType = NULL;
	return ICV_SUCCESS;
}


/**
*  删除预置位实体内存
* @param  -[in]  SVideoPSP** ppVideoPSP: 预置位实体内存数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
long VR_FreeVideoPSPArray(SVideoPSP** ppVideoPSP)
{
	//参数错误
	if (NULL == ppVideoPSP)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	
	delete[] *ppVideoPSP;
	*ppVideoPSP = NULL;
	return ICV_SUCCESS;
}

/**
* 删除摄像头实体内存
* @param  -[in]  SVideoCamera** ppVideoCamera:  摄像头实体内存数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
long VR_FreeVideoCameraArray(SVideoCamera** ppVideoCamera)
{
	//参数错误
	if (NULL == ppVideoCamera)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	
	delete[] *ppVideoCamera;
	*ppVideoCamera = NULL;
	return ICV_SUCCESS;
}

/**
* 删除监视器实体内存
* @param  -[in]  SVideoMonitor** ppVideoMonitor:  监视器实体内存数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
long VR_FreeVideoMonitorArray(SVideoMonitor** ppVideoMonitor)
{
	//参数错误
	if (NULL == ppVideoMonitor)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	
	delete[] *ppVideoMonitor;
	*ppVideoMonitor = NULL;
	return ICV_SUCCESS;
}

/**
* 删除模式实体内存
* @param  -[in]  SVideoMode** ppVideoMode: 模式实体数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
long VR_FreeVideoModeArray(SVideoMode** ppVideoMode)
{
	//参数错误
	if (NULL == ppVideoMode)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	
	delete[] *ppVideoMode;
	*ppVideoMode = NULL;
	return ICV_SUCCESS;
}

/**
* 删除抓拍图片实体实体内存
* @param  -[in]  SVideoSnapFile** ppVideoSnapFile:抓拍图片实体数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
long VR_FreeVideoSnapFileArray(SVideoSnapFile** ppVideoSnapFile)
{
	//参数错误
	if (NULL == ppVideoSnapFile)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	
	delete[] *ppVideoSnapFile;
	*ppVideoSnapFile = NULL;
	return ICV_SUCCESS;
}


/**
* 删除抓拍类型定义实体内存
* @param  -[in]  SIDMapText** ppIDMapText:抓拍类型定义实体数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
long VR_FreeIDMapTextArray(SIDMapText** ppIDMapText)
{
	//参数错误
	if (NULL == ppIDMapText)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	
	delete[] *ppIDMapText;
	*ppIDMapText = NULL;
	return ICV_SUCCESS;
}


/**
* 删除自定义分区定义实体内存
* @param  -[in]  SVideoCustomZone** ppVideoCustomZone:自定义分区定义实体数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
long VR_FreeVideoCustomZoneArray(SVideoCustomZone** ppVideoCustomZone)
{
	//参数错误
	if (NULL == ppVideoCustomZone)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	
	delete[] *ppVideoCustomZone;
	*ppVideoCustomZone = NULL;
	return ICV_SUCCESS;
}


/**
* 删除历史录像文件定义实体内存
* @param  -[in]  SHisFile** ppHisFile:历史录像文件定义实体数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
long VR_FreeHisFileArray(SHisFile** ppHisFile)
{
	//参数错误
	if (NULL == ppHisFile)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	
	delete[] *ppHisFile;
	*ppHisFile = NULL;
	return ICV_SUCCESS;
}


/**
* 删除录像片段信息实体内存
* @param  -[in]  SHisFile** ppHisFile:录像片段信息实体数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
long VR_FreeRecordArray(SRecord** ppRecord)
{
	//参数错误
	if (NULL == ppRecord)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	
	delete[] *ppRecord;
	*ppRecord = NULL;
	return ICV_SUCCESS;
} 
