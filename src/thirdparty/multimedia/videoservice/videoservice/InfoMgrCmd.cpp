/**************************************************************
 *  Filename:    InfoMgrCmd.cpp
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: 消息处理实现文件，主要处理在本地处理数据库.
 *
 *  @author:     chenzhan
 *  @version     04/21/2009  chenzhan  Initial Version
 *  @version     04/21/2009  chenzhan  增加摄像头分组.
 *  @version     06/11/2009  zhucongfeng  修改GetSnapPic，不管是否返回错误，都申请缓冲区.
 *				 08/31/2009  chenzhan  增加录像片段管理.
 *				 12/08/2009  chenzhan  取消获取QIP的打印日志.
				 04/20/2010  chenzhan  增加视频设备级连切换,加在此处是因为设备控制线程
				 DevCtrlTask只能处理一个品牌的设备,此处是可以向多个DevCtrlTask发消息控制.
**************************************************************/
// InfoMgrCmd.cpp: implementation of the CInfoMgrCmd class.
//
//////////////////////////////////////////////////////////////////////

#include "InfoMgrCmd.h"
#include "VideoCfg.h"
#include "NetWrapper.h"
#include "multimedia/video/VideoAuth.h"
#include "DevCtrlTask.h"
#include "ace/Mem_Map.h"
#include "version_resource.h"
#include "gettext/libintl.h"

#define _(STRING) gettext(STRING)
#define VIDEO_ONLY_ONE 1

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CInfoMgrCmd::CInfoMgrCmd()
{

}

CInfoMgrCmd::~CInfoMgrCmd()
{

}


// 处理客户端发送的增删改查类信息管理请求, 速度较快, 就在请求线程中处理即可
long CInfoMgrCmd::ProcessInfoMgrCmd(long lCamID, char *szCmdMsg, long lCmdMsgLen, long lCmdCode, long lStampID, 
									char *szUserName, char *szGroupName, HQUEUE hCliQue)
{
	long lRet = ICV_SUCCESS;

	long lOutSize = 0;
	char *pRetBuf = NULL;
	long nRetBufSize = VIDEO_GENERAL_RET_MAX_SIZE*16; // 摄像头多的时候缓冲区不够用
	if ((VIDEO_CMD_GET_ALL_ZONE!=lCmdCode)//以下命令都需要实算缓冲区大小
	 && (VIDEO_CMD_GET_ALL_PRODUCT!=lCmdCode)
	 && (VIDEO_CMD_GET_ALL_DEVICE!=lCmdCode)
	 && (VIDEO_CMD_GET_ALL_CAMERA!=lCmdCode)
	 && (VIDEO_CMD_GET_ALL_SIMPLE_CAMERA!=lCmdCode)
	 && (VIDEO_CMD_GET_ZONE_CAMERA!=lCmdCode)
	 && (VIDEO_CMD_GET_ZONE_SIMPLE_CAMERA!=lCmdCode)
	 && (VIDEO_CMD_GET_ALL_MONITOR!=lCmdCode)
	 && (VIDEO_CMD_GET_ZONE_MONITOR!=lCmdCode)
	 && (VIDEO_CMD_QUERY_SNAP_INFO!=lCmdCode)
	 && (VIDEO_CMD_CAMBYMATRIX_GET!=lCmdCode)
	 && (VIDEO_CMD_GET_PSP!=lCmdCode))
	{
		pRetBuf = new char[nRetBufSize];
		memset(pRetBuf, 0x00, nRetBufSize);
	}

	// 信息管理/获取类,不与摄像头发生关系,不涉及到控制命令,也不涉及到其他服务的请求
	switch(lCmdCode) 
	{
	case VIDEO_CMD_GET_ALL_CONFIG_INFO: // 最大多大
		SAFE_DELETE_ARRAY(pRetBuf);
		lRet = GetAllConfigInfoBuffer(szUserName, szGroupName, szCmdMsg, lCmdMsgLen, 
			pRetBuf, &lOutSize);
		break;

	case VIDEO_CMD_GET_ALL_ZONE:
		lRet = GetAllZoneBuffer(szUserName, szGroupName, NULL, 
			0, &lOutSize);

		pRetBuf = new char[lOutSize + RETCMD_HEADERINFO_LENGTH];
		memset(pRetBuf, 0x00, lOutSize + RETCMD_HEADERINFO_LENGTH);

		lRet = GetAllZoneBuffer(szUserName, szGroupName, pRetBuf, 
			lOutSize + 1, &lOutSize);
		break;

	case VIDEO_CMD_GET_ALL_PRODUCT:
		lRet = GetAllProductBuffer(NULL, 0, &lOutSize);

		pRetBuf = new char[lOutSize + RETCMD_HEADERINFO_LENGTH];
		memset(pRetBuf, 0x00, lOutSize + RETCMD_HEADERINFO_LENGTH);

		lRet = GetAllProductBuffer(pRetBuf, lOutSize + 1, &lOutSize);
		break;

	case VIDEO_CMD_GET_ALL_DEVICE:
		lRet = GetAllDeviceBuffer(NULL, 0, &lOutSize);

		pRetBuf = new char[lOutSize + RETCMD_HEADERINFO_LENGTH];
		memset(pRetBuf, 0x00, lOutSize + RETCMD_HEADERINFO_LENGTH);

		lRet = GetAllDeviceBuffer(pRetBuf, lOutSize + 1, &lOutSize);
		break;

	case VIDEO_CMD_GET_ALL_CAMERA:
		lRet = GetAllCameraBuffer(szUserName, szGroupName, NULL, 
			0, &lOutSize, false, VIDEO_ID_INVALIDATION);

		pRetBuf = new char[lOutSize + RETCMD_HEADERINFO_LENGTH];
		memset(pRetBuf, 0x00, lOutSize + RETCMD_HEADERINFO_LENGTH);

		lRet = GetAllCameraBuffer(szUserName, szGroupName, pRetBuf, 
			lOutSize + 1, &lOutSize, false, VIDEO_ID_INVALIDATION);
		break;

	case VIDEO_CMD_GET_ALL_SIMPLE_CAMERA:
		lRet = GetAllCameraBuffer(szUserName, szGroupName, NULL, 
			0, &lOutSize, true, VIDEO_ID_INVALIDATION);

		pRetBuf = new char[lOutSize + RETCMD_HEADERINFO_LENGTH];
		memset(pRetBuf, 0x00, lOutSize + RETCMD_HEADERINFO_LENGTH);

		lRet = GetAllCameraBuffer(szUserName, szGroupName, pRetBuf, 
			lOutSize + 1, &lOutSize, true, VIDEO_ID_INVALIDATION);
		break;
		
	case VIDEO_CMD_GET_ZONE_CAMERA:
		lRet = GetAllCameraBufferOfZone(szUserName, szGroupName, szCmdMsg, lCmdMsgLen, NULL, 
			0, &lOutSize, false);

		pRetBuf = new char[lOutSize + RETCMD_HEADERINFO_LENGTH];
		memset(pRetBuf, 0x00, lOutSize + RETCMD_HEADERINFO_LENGTH);

		lRet = GetAllCameraBufferOfZone(szUserName, szGroupName, szCmdMsg, lCmdMsgLen, pRetBuf, 
			lOutSize + 1, &lOutSize, false);
		break;

	case VIDEO_CMD_GET_ZONE_SIMPLE_CAMERA:
		lRet = GetAllCameraBufferOfZone(szUserName, szGroupName, szCmdMsg, lCmdMsgLen, NULL, 
			0, &lOutSize, true);

		pRetBuf = new char[lOutSize + RETCMD_HEADERINFO_LENGTH];
		memset(pRetBuf, 0x00, lOutSize + RETCMD_HEADERINFO_LENGTH);

		lRet = GetAllCameraBufferOfZone(szUserName, szGroupName, szCmdMsg, lCmdMsgLen, pRetBuf, 
			lOutSize + 1, &lOutSize, true);
		break;

	case VIDEO_CMD_GET_CAMERA_BY_NAME:
		lRet = GetCameraBufferByName(szUserName, szGroupName, szCmdMsg, lCmdMsgLen, pRetBuf, 
			lOutSize + 1, &lOutSize);
		break;

	case VIDEO_CMD_GET_ALL_MONITOR:
		lRet = GetAllMonitorBuffer(szUserName, szGroupName, NULL, 
			0, &lOutSize, VIDEO_ID_INVALIDATION);

		pRetBuf = new char[lOutSize + RETCMD_HEADERINFO_LENGTH];
		memset(pRetBuf, 0x00, lOutSize + RETCMD_HEADERINFO_LENGTH);

		lRet = GetAllMonitorBuffer(szUserName, szGroupName, pRetBuf, 
			lOutSize + 1, &lOutSize, VIDEO_ID_INVALIDATION);
		break;
		
	case VIDEO_CMD_GET_ZONE_MONITOR:
		// 解析出分区ID
		{
			long nOffset = 0;
			long lZoneID = 0;
			lRet = VIDEO_PARSER->ParseBufferToInteger(szCmdMsg, &nOffset, lCmdMsgLen, VIDEO_ZONE_ID_SIZE, lZoneID);
			if(lRet != ICV_SUCCESS)
			{
				CVLog.LogErrMessage(lRet, _("Parse zone id failed when get camera information of one zone."));
				break;
			}

			lRet = GetAllMonitorBuffer(szUserName, szGroupName, NULL, 0, &lOutSize, lZoneID);

			pRetBuf = new char[lOutSize + RETCMD_HEADERINFO_LENGTH];
			memset(pRetBuf, 0x00, lOutSize + RETCMD_HEADERINFO_LENGTH);

			lRet = GetAllMonitorBuffer(szUserName, szGroupName, pRetBuf, lOutSize + 1, &lOutSize, lZoneID);
		}
		break;

	// 模式管理(增删改查)
	case VIDEO_CMD_GET_ALL_MODE_NAME:
		lRet = GetAllModeNameBuffer(szUserName, szGroupName, szCmdMsg, lCmdMsgLen, pRetBuf, 
			nRetBufSize-RETCMD_HEADERINFO_LENGTH, &lOutSize);
		break;

	// 抓拍图片管理(与摄像头相关, 但与设备控制无关, 需要考虑转发给其他服务实现)
	case VIDEO_CMD_GET_SNAP_TYPE:
		lRet = GetAllSnapType(pRetBuf, nRetBufSize-RETCMD_HEADERINFO_LENGTH, &lOutSize);
		break;

	case VIDEO_CMD_QUERY_SNAP_INFO:		// 按指定条件查询抓拍信息
		lRet = QuerySnapInfo(szUserName, szGroupName, szCmdMsg, lCmdMsgLen, NULL, 
			0, &lOutSize);

		pRetBuf = new char[lOutSize + RETCMD_HEADERINFO_LENGTH];
		memset(pRetBuf, 0x00, lOutSize + RETCMD_HEADERINFO_LENGTH);

		lRet = QuerySnapInfo(szUserName, szGroupName, szCmdMsg, lCmdMsgLen, pRetBuf, 
			lOutSize + 1, &lOutSize);

		break;

	case VIDEO_CMD_GET_PIC_BUFFER_BYID:	// 获取指定抓拍图片的图片内容
		lRet = GetSnapPic(szUserName, szGroupName, szCmdMsg, lCmdMsgLen, pRetBuf, lOutSize);
		break;
		
	case VIDEO_CMD_DELETE_SNAP_PIC:		// 删除抓拍图片
		lRet = DelSnapInfo(szUserName, szGroupName, szCmdMsg, lCmdMsgLen);
		break;

	case VIDEO_CMD_DOWNLOAD_PIC:		// 下载抓拍图片
		lRet = DownloadSnapPic(szUserName, szGroupName, szCmdMsg, lCmdMsgLen, pRetBuf, lOutSize);
		break;

	// 录像片段管理
	case VIDEO_CMD_ADD_CAPTURE_RECORD:	// 增加录像片段
		lRet = AddRecord(szUserName, szGroupName, lCamID, szCmdMsg, lCmdMsgLen, pRetBuf, lOutSize);
		break;

	case VIDEO_CMD_MDF_CAPTURE_RECORD:	// 修改录像片段信息
		lRet = ModifyRecordInfo(szUserName, szGroupName, szCmdMsg, lCmdMsgLen);
		break;

	case VIDEO_CMD_QUERY_CAPTURE_INFO:	// 按指定条件查询录像片段信息
		lRet = QueryRecordInfo(szUserName, szGroupName, szCmdMsg, lCmdMsgLen, pRetBuf, lOutSize);
		break;

	case VIDEO_CMD_GET_CAPTRUE_BUFFER:	// 下载录像片段
		lRet = DownloadRecord(szUserName, szGroupName, szCmdMsg, lCmdMsgLen, pRetBuf, lOutSize);
		break;

	case VIDEO_CMD_DEL_CAPTURE_RECORD:	// 删除录像片段
		lRet = DeleteRecord(szUserName, szGroupName, szCmdMsg, lCmdMsgLen);
		break;

	case VIDEO_CMD_ADD_MODE:
		lRet = AddMode(szUserName, szGroupName, szCmdMsg, lCmdMsgLen);
		break;

	case VIDEO_CMD_DELETE_MODE:
		lRet = DeleteMode(szUserName, szGroupName, szCmdMsg, lCmdMsgLen);
		break;

	case VIDEO_CMD_MODIFY_MODE:
		lRet = ModifyMode(szUserName, szGroupName, szCmdMsg, lCmdMsgLen);
		break;

	case VIDEO_CMD_GET_MODE_LAYOUT:
		lRet = GetModeLayoutBuffer(szUserName, szGroupName, szCmdMsg, lCmdMsgLen, pRetBuf, 
			nRetBufSize-RETCMD_HEADERINFO_LENGTH, &lOutSize);
		break;

	case VIDEO_CMD_GET_SERVICE_STATUS:	// 获取视频服务的当前状态
		lRet = GetServiceRMStauts(pRetBuf, nRetBufSize-RETCMD_HEADERINFO_LENGTH, &lOutSize);
		break;

	case VIDEO_CMD_GET_ALL_MCLINK:		// 获取摄像头和监视器的对应关系, 只需要在本地执行查找
		lRet = EC_ICV_CCTV_NOTIMPLEMENTEDCMDCODE;
		break;

	case VIDEO_CMD_ADD_PSP2:
		lRet = AddPSP(szUserName, szGroupName, lCamID, szCmdMsg, lCmdMsgLen);
        break;

	case VIDEO_CMD_GET_PSP:				// 需要输入摄像头ID
		lRet = GetPSP(lCamID, szUserName, szGroupName, szCmdMsg, lCmdMsgLen, 
			NULL, 0, &lOutSize);

		pRetBuf = new char[lOutSize + RETCMD_HEADERINFO_LENGTH];
		memset(pRetBuf, 0x00, lOutSize + RETCMD_HEADERINFO_LENGTH);

		lRet = GetPSP(lCamID, szUserName, szGroupName, szCmdMsg, lCmdMsgLen, 
			pRetBuf, lOutSize + 1, &lOutSize);
		break;

	case VIDEO_CMD_DELETE_PSP:
		lRet = DeletePSP(lCamID, szUserName, szGroupName, szCmdMsg, lCmdMsgLen);
		break;

	case VIDEO_CMD_MODIFY_PSP:
		lRet = ModifyPSP(lCamID, szUserName, szGroupName, szCmdMsg, lCmdMsgLen);
		break;
		
	case VIDEO_CMD_ADD_SNAP_PIC:		// 添加抓拍图片
		lRet = AddSnapPicture(lCamID, szUserName, szGroupName, szCmdMsg, lCmdMsgLen, pRetBuf, lOutSize);
		break;

	case VIDEO_CMD_ADDTO_PIC:			// 图片累加
		lRet = CapturePicAddTo(szUserName, szGroupName, szCmdMsg, lCmdMsgLen);
		break;

	case VIDEO_CMD_MODIFY_SNAP_INFO:	// 修改抓拍信息
		lRet = ModifySnapInfo(lCamID, szUserName, szGroupName, szCmdMsg, lCmdMsgLen);
		break;
		
	case VIDEO_CMD_LOCK_CAMERA:			// 锁定摄像头和监视器
		lRet = LockCamera(lCamID, szUserName, szGroupName, szCmdMsg, lCmdMsgLen);
		break;

	// 自定义分区
	case VIDEO_CMD_CUSTZONE_GET:		// 获取用户有权限看到的所有自定义分区信息
		lRet = GetCustZone(szUserName, pRetBuf, nRetBufSize-RETCMD_HEADERINFO_LENGTH, lOutSize);
		break;
	case VIDEO_CMD_CUSTZONE_ADD:		// 添加自定义分组
		lRet = AddCustZone(szUserName, szCmdMsg, pRetBuf, nRetBufSize-RETCMD_HEADERINFO_LENGTH);
		break;
	case VIDEO_CMD_CUSTZONE_DEL:		// 删除自定义分区
		lRet = DelCustZone(szUserName, szCmdMsg, pRetBuf, nRetBufSize-RETCMD_HEADERINFO_LENGTH);
		break;
	case VIDEO_CMD_CUSTZONE_GETCAM:		// 获取用户有权限看到的某一自定义分区摄像头信息
		lRet = GetCustZoneCam(szUserName, szCmdMsg, pRetBuf, nRetBufSize-RETCMD_HEADERINFO_LENGTH, lOutSize);
		break;
	case VIDEO_CMD_CUSTZONE_CPYCAM:		// 复制摄像头到自定义分区
		lRet = CpyCustZoneCam(szUserName, szCmdMsg, pRetBuf, nRetBufSize-RETCMD_HEADERINFO_LENGTH);
		break;
	case VIDEO_CMD_CUSTZONE_DELCAM:		// 删除自定义分区中的摄像头
		lRet = DelCustZoneCam(szUserName, szGroupName, szCmdMsg, pRetBuf, nRetBufSize-RETCMD_HEADERINFO_LENGTH);
		break;

	case VIDEO_CMD_LOCK_MONITOR:		// 本版本(5.0)暂没有实现
		break;
	case VIDEO_CMD_GET_SERVICEVER:
		lOutSize = sprintf(pRetBuf, "%02d%s", strlen(RC_FILE_VERSION_STR), RC_FILE_VERSION_STR);
		break;

	case VIDEO_CMD_REALDEVICE_GET:
		lRet = GetRealDevToCam(lCamID, szUserName, szGroupName, szCmdMsg, lCmdMsgLen, lStampID, pRetBuf, nRetBufSize-RETCMD_HEADERINFO_LENGTH, lOutSize, hCliQue, VIDEO_CMD_REALDEVICE_GET);
		break;
	case VIDEO_CMD_ENCODEDEV_FREE:
		lRet = FreeEncodeDevToCam(lCamID, szUserName, szCmdMsg, lCmdMsgLen);
		break;
	case VIDEO_CMD_HISDEVICE_GET:
		lRet = GetHisDevToCam(lCamID, szUserName, szCmdMsg, pRetBuf, nRetBufSize-RETCMD_HEADERINFO_LENGTH, lOutSize);
		break;
	case VIDEO_CMD_CTLDEVICE_GET:
		lRet = GetCtlDevToCam(lCamID, szUserName, szCmdMsg, pRetBuf, nRetBufSize-RETCMD_HEADERINFO_LENGTH, lOutSize);
		break;
	case VIDEO_CMD_MONITORDEV_GET:
		lRet = GetRealDevToCam(lCamID, szUserName, szGroupName, szCmdMsg, lCmdMsgLen, lStampID, pRetBuf, nRetBufSize-RETCMD_HEADERINFO_LENGTH, lOutSize, hCliQue, VIDEO_CMD_MONITORDEV_GET);
		break;
	case VIDEO_CMD_MONITORDEV_FREE:
		lRet = FreeMonitorDevToCam(lCamID, szUserName, szCmdMsg, lCmdMsgLen);
		break;
	// 查找矩阵连接的摄像头
	case VIDEO_CMD_CAMBYMATRIX_GET:
		lRet = GetCameraBufferByMatrix(szUserName, szGroupName, szCmdMsg, lCmdMsgLen, NULL, 0, &lOutSize);

		pRetBuf = new char[lOutSize + RETCMD_HEADERINFO_LENGTH];
		memset(pRetBuf, 0x00, lOutSize + RETCMD_HEADERINFO_LENGTH);

		lRet = GetCameraBufferByMatrix(szUserName, szGroupName, szCmdMsg, lCmdMsgLen, pRetBuf, lOutSize + 1, &lOutSize);
		break;
	default:
		lRet = EC_ICV_CCTV_NOTIMPLEMENTEDCMDCODE;
		break;
	}

	switch(lCmdCode) 
	{
	case VIDEO_CMD_SWITCH_BY_ID: // 切换摄像头（按照摄像头ID）
	case VIDEO_CMD_SWITCH_BY_NAME: // 切换摄像头（按照摄像头名称）
		lRet = CallSwitchDriverTask(szCmdMsg, lCmdMsgLen, pRetBuf, lOutSize, hCliQue);
		break;
	default:
		break;
	}

	char szHeader[RETCMD_HEADERINFO_LENGTH];
	sprintf(szHeader, "%03d%010d%010d", lCmdCode+COMMAND_CODE_GAP_REQANDRET, lStampID, lRet);

	memmove(pRetBuf+RETCMD_HEADERINFO_LENGTH, pRetBuf, lOutSize);
	memcpy(pRetBuf, szHeader, RETCMD_HEADERINFO_LENGTH);

	lRet = RetCmdBuffer(hCliQue, pRetBuf, lOutSize+RETCMD_HEADERINFO_LENGTH);
	SAFE_DELETE_ARRAY(pRetBuf);

	// 返回给客户端
	return lRet;
}

/**
 *  $(获取所有的设备配置信息buffer).
 *
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *  @param  -[out] char *& pMsgBuff: 输出buffer的指针.
 *  @param  -[out] long* plOutSize: 输出buffer的长度.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CInfoMgrCmd::GetAllConfigInfoBuffer(const char* pszUserName, const char* pszGroupName, 
										 const char* pInBuf, const long nInBufSize,
										 char *& pMsgBuff, long* plOutSize)
{
	*plOutSize = 0;

	// 取得上次获取的最新模式的时间(10位)
	long lCliLastChgTime = 0;
	long nOffset = 0;
	VIDEO_CHECKFAIL_RETURN(VIDEO_PARSER->ParseBufferToInteger(pInBuf, &nOffset, nInBufSize, VIDEO_CHANGETIME_LENGTH_SIZE, lCliLastChgTime));
	
	// 根据用户信息获取分区列表、摄像头列表，监视器列表
	CVideoZoneList listZone;
	CVideoCameraList listCam;
	CVideoMonitorList listMon;
	VIDEO_CHECKFAIL_RETURN(VIDEO_CONFIG->GetZoneCamMonListByUserInfo(&listZone, &listCam, &listMon, pszUserName, pszGroupName));

	// 计算缓冲区
	long nConfigCount = VIDEO_CONFIG->GetProductList()->GetSize()+VIDEO_CONFIG->GetDeviceList()->GetSize()+listZone.GetSize()+listCam.GetSize()+listMon.GetSize();
	long nRetBufSize = nConfigCount*512+VIDEO_CHANGETIME_LENGTH_SIZE;
	pMsgBuff = new char[nRetBufSize+RETCMD_HEADERINFO_LENGTH];

	long lOutSize = 0;
	long nOff = 0;

	// 比较, 
	time_t lSvrLastChgTime = VIDEO_CONFIG->GetLastAllCfgChgTime();
	if(lSvrLastChgTime == lCliLastChgTime)
		return EC_ICV_CCTV_INFONOTCHANGED;

	// 如果发生改变则将所有名称发送给客户端
	lOutSize = sprintf(pMsgBuff, "%010I64d", lSvrLastChgTime);
	nOff += lOutSize;

	// 将产品型号list解析成buffer
	VIDEO_CHECKFAIL_RETURN(VIDEO_PACKER->PackProductListToBuffer(VIDEO_CONFIG->GetProductList(), pMsgBuff+nOff, nRetBufSize-nOff, &lOutSize));
	nOff += lOutSize;

	// 将设备list解析成buffer
	VIDEO_CHECKFAIL_RETURN(VIDEO_PACKER->PackDeviceListToBuffer(VIDEO_CONFIG->GetDeviceList(), pMsgBuff+nOff, nRetBufSize-nOff, &lOutSize));
	nOff += lOutSize;

	// 将分区list解析成buffer
	VIDEO_CHECKFAIL_RETURN(VIDEO_PACKER->PackZoneListToBuffer(&listZone, pMsgBuff+nOff, nRetBufSize-nOff, &lOutSize));
	nOff += lOutSize;
  
	// 将摄像头list解析成buffer
	VIDEO_CHECKFAIL_RETURN(VIDEO_PACKER->PackCameraListToBuffer(&listCam, pMsgBuff+nOff, nRetBufSize-nOff, &lOutSize));
	nOff += lOutSize;

	// 将监视器list解析成buffer
	VIDEO_CHECKFAIL_RETURN(VIDEO_PACKER->PackMonitorListToBuffer(&listMon, pMsgBuff+nOff, nRetBufSize-nOff, &lOutSize));
	nOff += lOutSize;

	// 返回信息
	*plOutSize = nOff;

	return ICV_SUCCESS;
}

/**
 *  $(获取所有设备信息buffer).
 *
 *  @param  -[out] char * szRetBuf: 输出buffer的指针.
 *  @param  -[out] long* plOutSize: 输出buffer的长度.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CInfoMgrCmd::GetAllDeviceBuffer(char * szRetBuf, const long nRetBufSize, long* plOutSize)
{
	long lRet = ICV_SUCCESS;
	CVideoDeviceList* pDevList = VIDEO_CONFIG->GetDeviceList();
	// 将设备list解析成buffer
	lRet = VIDEO_PACKER->PackDeviceListToBuffer(pDevList, szRetBuf, nRetBufSize, plOutSize);
	return lRet;
}

/**
 *  $(获取所有分区信息buffer).
 *
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *  @param  -[out] char * szRetBuf: 输出buffer的指针.
 *  @param  -[out] long* plOutSize: 输出buffer的长度.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CInfoMgrCmd::GetAllZoneBuffer(const char* pszUserName, const char* pszGroupName, char * szRetBuf, const long nRetBufSize, long* plOutSize)
{
	
	// 根据用户信息获取分区列表
	CVideoZoneList listZone;
	VIDEO_CHECKFAIL_RETURN(VIDEO_CONFIG->GetZoneListByUserInfo(&listZone, pszUserName, pszGroupName));

	// 将分区list解析成buffer
	VIDEO_CHECKFAIL_RETURN(VIDEO_PACKER->PackZoneListToBuffer(&listZone, szRetBuf, nRetBufSize, plOutSize));

	return ICV_SUCCESS;
}

/**
 *  $(获取所有产品型号信息buffer).
 *
 *  @param  -[out] char * szRetBuf: 输出buffer的指针.
 *  @param  -[out] long* plOutSize: 输出buffer的长度.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CInfoMgrCmd::GetAllProductBuffer(char * szRetBuf, const long nRetBufSize, long* plOutSize)
{
	CVideoProductList *pProductList = VIDEO_CONFIG->GetProductList();

	// 将产品型号list解析成buffer
	long lRet = VIDEO_PACKER->PackProductListToBuffer(pProductList, szRetBuf, nRetBufSize, plOutSize);

	return lRet;
}

/**
 *  $(获取所有摄像头信息buffer).
 *
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *  @param  -[out] char * szRetBuf: 输出buffer的指针.
 *  @param  -[out] long* plOutSize: 输出buffer的长度.
 *  @param  -[in] bool bSimple: 是否是简易信息.
 *  @param  -[in] long lZoneID: 为负数表示所有的Zone.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CInfoMgrCmd::GetAllCameraBuffer(const char* pszUserName, const char* pszGroupName, 
									 char * szRetBuf, const long nRetBufSize, long* plOutSize, 
									 bool bSimple, long lZoneID)
{
	long lRet = ICV_SUCCESS;

	// 根据用户信息获取摄像头列表
	CVideoCameraList listCamera;
	lRet = VIDEO_CONFIG->GetCameraListByZoneAndUserInfo(&listCamera, pszUserName, pszGroupName, lZoneID);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("When get camera, GetCameraListByZoneAndUserInfo run failed."));
		return lRet;
	}

	if(bSimple)
		// 简易信息的解析
		lRet = VIDEO_PACKER->PackSimpleCameraListToBuffer(&listCamera, szRetBuf, nRetBufSize, plOutSize);
	else
		// 详细信息的解析
		lRet = VIDEO_PACKER->PackCameraListToBuffer(&listCamera, szRetBuf, nRetBufSize, plOutSize);

	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("PackCameraListToBuffer."));
		return lRet;
	}

	return lRet;
}

/**
 *  $(获取某一分区所有摄像头信息buffer).
 *
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *  @param  -[in] const char* pInBuf: 输入buffer.
 *  @param  -[in] const long nInBufSize: 输入buffer长度.
 *  @param  -[out] char * szRetBuf: 输出buffer的指针.
 *  @param  -[out] long* plOutSize: 输出buffer的长度.
 *  @param  -[in] bool bSimple: 是否是简易信息.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CInfoMgrCmd::GetAllCameraBufferOfZone(const char* pszUserName, const char* pszGroupName, const char* pInBuf,
											const long nInBufSize, char * szRetBuf, const long nRetBufSize, long* plOutSize, bool bSimple)
{
	// 解析出分区ID
	long nOff = 0;
	long lZoneID = 0;
	long lRet = VIDEO_PARSER->ParseBufferToInteger(pInBuf, &nOff, nInBufSize, VIDEO_ZONE_ID_SIZE, lZoneID);
	if(lRet != ICV_SUCCESS || lZoneID <= 0)
	{
		CVLog.LogErrMessage(lRet, _("Parse zone id failed when get camera information of one zone."));
		return lRet;
	}

	lRet = GetAllCameraBuffer(pszUserName, pszGroupName, szRetBuf, nRetBufSize, plOutSize, bSimple, lZoneID);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Get camera information by camera name, GetAllCameraBuffer run failed."));
		return lRet;
	}

	return lRet;
}

/**
 *  $(根据摄像头名称获取摄像头信息buffer).
 *
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *  @param  -[in] const char* pInBuf: 输入buffer.
 *  @param  -[in] const long nInBufSize: 输入buffer长度.
 *  @param  -[out] char * szRetBuf: 输出buffer的指针.
 *  @param  -[out] long* plOutSize: 输出buffer的长度.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CInfoMgrCmd::GetCameraBufferByName(const char* pszUserName, const char* pszGroupName, const char* pInBuf,
											const long nInBufSize, char * szRetBuf, const long nRetBufSize, long* plOutSize)
{
	long lRet = ICV_SUCCESS;
	long nOff = 0;
	// 解析摄像头名称
	char szCameraName[VIDEO_NAME_MAXSIZE];
	memset(szCameraName, 0x00, VIDEO_NAME_MAXSIZE);
	lRet = VIDEO_PARSER->ParseSubBuffer(pInBuf, &nOff, szCameraName, nInBufSize, VIDEO_NAME_LENGTH_SIZE, VIDEO_NAME_MAXSIZE);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("When get camera information by camera name, parse camera information failed."));
		return lRet;
	}
	
	// 根据名称获取摄像头信息
	CVideoCameraEx *pCamEx = NULL;
	lRet = VIDEO_CONFIG->GetCameraByCamName(szCameraName, pCamEx);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Failed to get camera information by camera name."));
		return lRet;
	}

	// 分配空间
	// 将摄像头信息解析成buffer
	lRet = VIDEO_PACKER->PackCameraToBuffer((CVideoCamera*)pCamEx, szRetBuf, nRetBufSize, plOutSize);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Failed to get camera information by camera name, PackCameraToBuffer."));
	}

	return lRet;
}

/**
 *  $(获取所有的监视器信息buffer).
 *
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *  @param  -[out] char * szRetBuf: 输出buffer的指针.
 *  @param  -[out] long* plOutSize: 输出buffer的长度.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CInfoMgrCmd::GetAllMonitorBuffer(const char* pszUserName, const char* pszGroupName, char* szRetBuf, const long nRetBufSize, long* plOutSize, long lZoneID)
{
	
	*plOutSize = 0; // plOutSize既做输出又做输入, 先初始化为输入

	// 根据用户信息获取监视器列表
	CVideoMonitorList listMonitor;
	long lRet = VIDEO_CONFIG->GetMonitorListByZoneAndUserInfo(&listMonitor, pszUserName, pszGroupName, lZoneID);
	if(lRet != ICV_SUCCESS)
	{
		return lRet;
	}

	// 将监视器list解析成buffer
	lRet = VIDEO_PACKER->PackMonitorListToBuffer(&listMonitor, szRetBuf, nRetBufSize, plOutSize);
	return lRet;
}


/**
 *  $(获取所有模式信息(模式名称和描述)buffer).
 *
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *  @param  -[out] char * szRetBuf: 输出buffer的指针.
 *  @param  -[out] long* plOutSize: 输出buffer的长度.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CInfoMgrCmd::GetAllModeNameBuffer(const char* pszUserName, const char* pszGroupName, 
									   const char* pInBuf, const long nInBufSize,
									   char * szRetBuf,    const long nRetBufSize, long* plOutSize)
{
	// 根据用户信息获取模式名称列表
	*plOutSize = 0;

	long lOffset = 0;
	CVideoModeList listMode;
	long lRet = VIDEO_CONFIG->GetModeListByUserInfo(&listMode, pszUserName, pszGroupName);
	if(lRet != ICV_SUCCESS)
		return lRet;

	// 取得上次获取的最新模式的时间(10位)
	long lCliLastChgTime = 0;
	lRet = VIDEO_PARSER->ParseBufferToInteger(pInBuf, &lOffset, nInBufSize, VIDEO_CHANGETIME_LENGTH_SIZE, lCliLastChgTime);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("GetAllModeNameBuffer, parse newest time failed."));
		return lRet;
	}
	
	// 比较, 如果发生改变则将所有名称发送给客户端
	time_t lSvrLastChgTime = VIDEO_CONFIG->GetLastModeChgTime();
	if(lSvrLastChgTime == lCliLastChgTime)
	{
		lRet = EC_ICV_CCTV_INFONOTCHANGED;
		return lRet;
	}
	
	sprintf(szRetBuf, "%010I64d", lSvrLastChgTime);
	*plOutSize += VIDEO_CHANGETIME_LENGTH_SIZE;

	lOffset = 0;
	// 将模式list解析成buffer
	lRet = VIDEO_PACKER->PackModeListToBuffer(&listMode, szRetBuf + VIDEO_CHANGETIME_LENGTH_SIZE, nRetBufSize - VIDEO_CHANGETIME_LENGTH_SIZE,
												&lOffset, false);
	*plOutSize += lOffset;
	
	return lRet;
}

/**
 *  $(添加模式).
 *
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *  @param  -[in] const char* pInBuf: 输入buffer.
 *  @param  -[in] const long nInBufSize: 输入buffer长度.
 *  @param  -[out] char * szRetBuf: 输出buffer的指针.
 *  @param  -[out] long* plOutSize: 输出buffer的长度.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CInfoMgrCmd::AddMode(const char* pszUserName, const char* pszGroupName, const char* pInBuf,
						  const long nInBufSize)
{
	// 解析buffer
	CVideoMode theMode;
	long lOffset = 0;
	long lRet = VIDEO_PARSER->ParseBufferToMode(pInBuf, &lOffset, nInBufSize, &theMode, true);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Call ParseBufferToMode interface failed."));
		return lRet;
	}
	
	// 添加模式
	lRet = VIDEO_CONFIG->AddModeByUserInfo(&theMode, pszUserName, pszGroupName);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Add mode to database failed."));
	}
	
	return lRet;
}

/**
 *  $(删除模式).
 *
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *  @param  -[in] const char* pInBuf: 输入buffer.
 *  @param  -[in] const long nInBufSize: 输入buffer长度.
 *  @param  -[out] char * szRetBuf: 输出buffer的指针.
 *  @param  -[out] long* plOutSize: 输出buffer的长度.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CInfoMgrCmd::DeleteMode(const char* pszUserName, const char* pszGroupName, const char* pInBuf, const long nInBufSize)
{
	long lRet = ICV_SUCCESS;
	long nOff = 0;
	
	// 解析buffer
	char szName[VIDEO_NAME_MAXSIZE];
	// 获取名称
	memset(szName, 0x00, VIDEO_NAME_MAXSIZE);
	lRet = VIDEO_PARSER->ParseSubBuffer(pInBuf, &nOff, szName, nInBufSize, VIDEO_NAME_LENGTH_SIZE, VIDEO_NAME_MAXSIZE);
	if(lRet == ICV_SUCCESS)
	{
		// 删除
		bool bDelete = false;
		lRet = VIDEO_CONFIG->DeleteModeByUserInfo(szName, pszUserName, pszGroupName);
		if(lRet == ICV_SUCCESS)
		{
			CVLog.LogErrMessage(lRet, _("Delete mode from database successfully, ."));
			return lRet;
		}
		else
			CVLog.LogErrMessage(lRet, _("Delete mode from database failed, ."));
	}
	else
		CVLog.LogErrMessage(lRet, _("Delete mode from database, failed to parse mode name."));

	return lRet;
}

/**
 *  $(更新模式).
 *
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *  @param  -[in] const char* pInBuf: 输入buffer.
 *  @param  -[in] const long nInBufSize: 输入buffer长度.
 *  @param  -[out] char * szRetBuf: 输出buffer的指针.
 *  @param  -[out] long* plOutSize: 输出buffer的长度.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CInfoMgrCmd::ModifyMode(const char* pszUserName, const char* pszGroupName, const char* pInBuf, const long nInBufSize)
{
	long lRet = ICV_SUCCESS;

	// 解析旧模式名称以及新模式信息
	char szOldModeName[VIDEO_NAME_MAXSIZE];
	memset(szOldModeName, 0x00, VIDEO_NAME_MAXSIZE);
	CVideoMode theNewMode;
	long nOff = 0;
	lRet = VIDEO_PARSER->ParseSubBuffer(pInBuf, &nOff, szOldModeName, nInBufSize,	VIDEO_NAME_LENGTH_SIZE, VIDEO_NAME_MAXSIZE);
	if(lRet == ICV_SUCCESS)
	{
		lRet = VIDEO_PARSER->ParseBufferToMode(pInBuf, &nOff, nInBufSize, &theNewMode, true);
		if(lRet == ICV_SUCCESS)
		{
			// 更新模式
			lRet = VIDEO_CONFIG->ModifyModeByUserInfo(szOldModeName, &theNewMode, pszUserName, pszGroupName);
			if(lRet == ICV_SUCCESS)
			{
				CVLog.LogErrMessage(lRet, _("Modify mode information in database successfully."));
				return lRet;
			}
			else
				CVLog.LogErrMessage(lRet, _("Modify mode information in database failed, ."));
		}
		else
			CVLog.LogErrMessage(lRet, _("Modify mode information in database, parse new mode name failed, ."));
	}
	else
		CVLog.LogErrMessage(lRet, _("Modify mode information in database, parse old mode name failed, ."));

	return lRet;
}

/**
 *  $(获取模式的布局buffer).
 *
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *  @param  -[in] const char* pInBuf: 输入buffer.
 *  @param  -[in] const long nInBufSize: 输入buffer长度.
 *  @param  -[out] char * szRetBuf: 输出buffer的指针.
 *  @param  -[out] long* plOutSize: 输出buffer的长度.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CInfoMgrCmd::GetModeLayoutBuffer(const char* pszUserName, const char* pszGroupName, const char* pInBuf,
									  const long nInBufSize, char * szRetBuf, const long nRetBufSize, long* plOutSize)
{
	// 解析buffer
	// 解析模式名称
	char szModeName[VIDEO_NAME_MAXSIZE];
	memset(szModeName, 0x00, VIDEO_NAME_MAXSIZE);
	CVideoMode theMode;
	long nOff = 0;
	long lRet = VIDEO_PARSER->ParseSubBuffer(pInBuf, &nOff, szModeName, nInBufSize, VIDEO_NAME_LENGTH_SIZE, VIDEO_NAME_MAXSIZE);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("When get mode layout, parse mode name failed, ."));
		return lRet;
	}
	
	lRet = VIDEO_CONFIG->GetModeInfoByNameAndUserInfo(szModeName, &theMode, pszUserName, pszGroupName);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Failed to get mode information, ."));
		return lRet;
	}
	
	sprintf(szRetBuf, "%04d%s", strlen(theMode.GetPara()), theMode.GetPara());
	*plOutSize = strlen(szRetBuf);
	
	return lRet;
}


/**
 *  $(获取预置位信息buffer).
 *
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *  @param  -[in] const char* pInBuf: 输入buffer.
 *  @param  -[in] const long nInBufSize: 输入buffer长度.
 *  @param  -[out] char * szRetBuf: 输出buffer的指针.
 *  @param  -[out] long* plOutSize: 输出buffer的长度.
 *  @param  -[in] CVideoMessage* pMsg: 消息信息.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CInfoMgrCmd::GetPSP(const long lCamID, const char* pszUserName, const char* pszGroupName, const char* pInBuf, const long nInBufSize,
						 char * szRetBuf,  const long nRetBufSize, long* plOutSize)
{
	// 本地服务操作
	CVideoPSPList listPSP;
	long lRet = VIDEO_CONFIG->GetPSPListOfCameraByUserInfo(lCamID, &listPSP, pszUserName, pszGroupName);
	
	*plOutSize = 0;
	
	if (szRetBuf == NULL)
	{
		*plOutSize += VIDEO_CAMERA_ID_SIZE + VIDEO_CHANGETIME_LENGTH_SIZE + VIDEO_PSP_COUNT_SIZE + listPSP.size() * (VIDEO_NAME_MAXSIZE + VIDEO_DESCRIPTION_MAXSIZE);
		return ICV_SUCCESS;
	}

	// 取得上次获取的最新模式的时间(10位)
	long lCliLastChgTime = 0;
	long lOffset = 0;
	lRet = VIDEO_PARSER->ParseBufferToInteger(pInBuf, &lOffset, nInBufSize, VIDEO_CHANGETIME_LENGTH_SIZE, lCliLastChgTime);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("GetPSP, parse newest time failed."));
		return lRet;
	}

	// 比较, 如果发生改变则将所有名称发送给客户端
	time_t lSvrLastChgTime = VIDEO_CONFIG->GetLastPspChgTime();
	if(lSvrLastChgTime == lCliLastChgTime)
	{
		lRet = EC_ICV_CCTV_INFONOTCHANGED;
		return lRet;
	}
	
	//sprintf(szRetBuf, "%06d%010I64d", lCamID, lSvrLastChgTime);
	sprintf(szRetBuf, "%06d%010lld", lCamID, lSvrLastChgTime); // %I64d仅MSVC平台有效，不兼容；8字节整数改用%lld
	*plOutSize = VIDEO_CAMERA_ID_SIZE + VIDEO_CHANGETIME_LENGTH_SIZE;

	// 将模式list解析成buffer
	long lActOutLen = 0;
	lRet = VIDEO_PACKER->PackPSPListToBuffer(&listPSP, szRetBuf + *plOutSize, nRetBufSize - *plOutSize, &lActOutLen);
	*plOutSize += lActOutLen;

	return lRet;
}

/**
 *  $(删除预置位).
 *
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *  @param  -[in] const char* pInBuf: 输入buffer.
 *  @param  -[in] const long nInBufSize: 输入buffer长度.
 *  @param  -[out] char * szRetBuf: 输出buffer的指针.
 *  @param  -[out] long* plOutSize: 输出buffer的长度.
 *  @param  -[in] CVideoMessage* pMsg: 消息信息.
 *	需要返回失败的个数
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CInfoMgrCmd::DeletePSP(const long lCamID, const char* pszUserName, const char* pszGroupName, 
							const char* pInBuf, const long nInBufSize)
{
	long lOffset = 0;
	// 解析buffer
	char szName[VIDEO_NAME_MAXSIZE];
	memset(szName, 0x00, VIDEO_NAME_MAXSIZE);
	
	long lRet = VIDEO_PARSER->ParseSubBuffer(pInBuf, &lOffset, szName, nInBufSize, VIDEO_NAME_LENGTH_SIZE, VIDEO_NAME_MAXSIZE);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("When delete PSP, failed to parse PSP name."));
		return lRet;
	}
		
	// 删除
	lRet = VIDEO_CONFIG->DeletePSPOfCameraByUserInfo(lCamID, szName, pszUserName, pszGroupName);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Delete PSP from database failed."));
		return lRet;
	}
	
	return lRet;
}	

/**
 *  $(修改预置位信息).
 *
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *  @param  -[in] const char* pInBuf: 输入buffer.
 *  @param  -[in] const long nInBufSize: 输入buffer长度.
 *  @param  -[out] char * szRetBuf: 输出buffer的指针.
 *  @param  -[out] long* plOutSize: 输出buffer的长度.
 *  @param  -[in] CVideoMessage* pMsg: 消息信息.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CInfoMgrCmd::ModifyPSP(const long lCamID, const char* pszUserName, const char* pszGroupName, 
							const char* pInBuf, const long nInBufSize)
{
	
	// 要删除的预置位个数
	long lOffset = 0;
		
	// 解析旧预置位名称以及新预置位信息
	char szOldPSPName[VIDEO_NAME_MAXSIZE];
	memset(szOldPSPName, 0x00, VIDEO_NAME_MAXSIZE);
	
	CVideoPSP theNewPSP;
	long lRet = VIDEO_PARSER->ParseSubBuffer(pInBuf, &lOffset, szOldPSPName, nInBufSize, 
		VIDEO_NAME_LENGTH_SIZE, VIDEO_NAME_MAXSIZE);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("When modify PSP, parse old PSP name failed,CamID:%d, "), lCamID);
		return lRet;
	}
	
	lRet = VIDEO_PARSER->ParseBufferToPSP(pInBuf+lOffset, nInBufSize-lOffset, &theNewPSP);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("When modify PSP, parse new PSP name failed,CamID:%d, old PSP name:%s, "), lCamID, szOldPSPName);
		return lRet;
	}
	
	lRet = VIDEO_CONFIG->ModifyPSPOfCameraByUserInfo(lCamID, szOldPSPName, &theNewPSP, pszUserName, pszGroupName);
	if(lRet != ICV_SUCCESS)
		CVLog.LogErrMessage(lRet, _("Modify PSP information in database failed,CamID:%d, old PSP name:%s, new PSP name:%s, "),
			lCamID, szOldPSPName, theNewPSP.GetName());

	return lRet;
}


/**
 *  $(获取所有抓拍类型信息buffer).
 *
 *  @param  -[out] char * szRetBuf: 输出buffer的指针.
 *  @param  -[out] long* plOutSize: 输出buffer的长度.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CInfoMgrCmd::GetAllSnapType(char * szRetBuf, const long nRetBufSize, long* plOutSize)
{
	*plOutSize = 0;
	long lOffset = 0;

	CIDMapTextList* pSnapTypeList = VIDEO_CONFIG->GetListSnapType();
	long nCount = pSnapTypeList->GetSize();

	// 个数
	long nLength = 0;
	sprintf(szRetBuf, "%02d", nCount);
	lOffset += VIDEO_SNAPTYPE_COUNT_SIZE;
	for(int i=0; i<nCount; i++)
	{
		nLength = strlen(pSnapTypeList->GetAt(i)->GetText());
		sprintf(szRetBuf+lOffset, "%02d%s", nLength, pSnapTypeList->GetAt(i)->GetText());
		lOffset += VIDEO_NAME_LENGTH_SIZE;
		lOffset += nLength;
	}

	*plOutSize = strlen(szRetBuf);

	return ICV_SUCCESS;
}

/**
 *  $(查询抓拍信息).
 *
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *  @param  -[in] const char* pInBuf: 输入buffer.
 *  @param  -[in] const long nInBufSize: 输入buffer长度.
 *  @param  -[out] char * szRetBuf: 输出buffer的指针.
 *  @param  -[out] long* plOutSize: 输出buffer的长度.
 *  @param  -[in] CVideoMessage* pMsg: 消息信息.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CInfoMgrCmd::QuerySnapInfo(const char* pszUserName, const char* pszGroupName, const char* pInBuf, const long nInBufSize,
									char* szRetBuf, const long nRetBufSize, long* pnOutSize)
{
	long lOffset = 0;
	char szZone[VIDEO_NAME_MAXSIZE];
	memset(szZone, 0x00, sizeof(szZone));
	long lRet = VIDEO_PARSER->ParseSubBuffer(pInBuf, &lOffset, szZone, nInBufSize, VIDEO_NAME_LENGTH_SIZE, sizeof(szZone));
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("QuerySnapInfo, parse zone name failed."));
		return lRet;
	}
	long nCamID = VIDEO_ID_INVALIDATION;
	lRet = VIDEO_PARSER->ParseBufferToInteger(pInBuf, &lOffset, nInBufSize, VIDEO_CAMERA_ID_SIZE, nCamID);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("QuerySnapInfo, parse camera id failed."));
		return lRet;
	}
	long lstar=0;
	lRet = VIDEO_PARSER->ParseBufferToInteger(pInBuf, &lOffset, nInBufSize, VIDEO_TIME_LENGTH_SIZE, lstar);
	time_t tStart = lstar;

	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("QuerySnapInfo, parse start time failed."));
		return lRet;
	}
	long lEnd =0;
	lRet = VIDEO_PARSER->ParseBufferToInteger(pInBuf, &lOffset, nInBufSize, VIDEO_TIME_LENGTH_SIZE, lEnd);
	time_t tEnd = lEnd;
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("QuerySnapInfo, parse end time failed."));
		return lRet;
	}
	char szSnapType[VIDEO_NAME_MAXSIZE];
	memset(szSnapType, 0x00, sizeof(szSnapType));
	lRet = VIDEO_PARSER->ParseSubBuffer(pInBuf, &lOffset, szSnapType, nInBufSize, VIDEO_NAME_LENGTH_SIZE, sizeof(szSnapType));
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("QuerySnapInfo, parse snaptype failed."));
		return lRet;
	}

	CVideoSnapFileList theSnapFileList;
	lRet = VIDEO_CONFIG->QuerySnapPictureByUserInfo(szZone, nCamID, tStart, tEnd, szSnapType, &theSnapFileList, pszUserName, pszGroupName);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("QuerySnapPictureByUserInfo, failed."));
		return lRet;
	}
	
	// 将图片内容list拼成成buffer
	return VIDEO_PACKER->PackSnapFileListToBuffer(&theSnapFileList, szRetBuf, nRetBufSize, pnOutSize);
}

/**
 *  $(获取指定抓拍图片的图片内容).
 *
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *  @param  -[in] const char* pInBuf: 输入buffer.
 *  @param  -[in] const long nInBufSize: 输入buffer长度.
 *  @param  -[out] char *&pRetBuf: 输出buffer的指针.
 *  @param  -[out] long* plOutSize: 输出buffer的长度.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CInfoMgrCmd::GetSnapPic(const char* pszUserName, const char* pszGroupName, const char* pInBuf, const long nInBufSize,
								char *&pRetBuf, long &nOutSize)
{
	long lOffset = 0;
	long nSnapID = VIDEO_ID_INVALIDATION;
	long lRet = VIDEO_PARSER->ParseBufferToInteger(pInBuf, &lOffset, nInBufSize, VIDEO_SNAP_ID_SIZE, nSnapID);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("GetSnapPic, parse snap id failed."));
		return lRet;
	}

	char* pPicBuf = NULL;
	lRet = VIDEO_CONFIG->GetSnapPicBufByUser(nSnapID, pPicBuf, nOutSize, pszUserName, pszGroupName);
	if(lRet != ICV_SUCCESS)
	{
		SAFE_DELETE_ARRAY(pPicBuf);
		nOutSize = 0;
		CVLog.LogErrMessage(lRet, _("GetSnapPicBufByUser, failed."));
		//return lRet;
	}
	pRetBuf = new char[nOutSize+RETCMD_HEADERINFO_LENGTH];
	memcpy(pRetBuf, pPicBuf, nOutSize);
	SAFE_DELETE_ARRAY(pPicBuf);

	return lRet;
}

/**
 *  $(下载抓拍图片).
 *
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *  @param  -[in] const char* pInBuf: 输入buffer.
 *  @param  -[in] const long nInBufSize: 输入buffer长度.
 *  @param  -[out] char *&pRetBuf: 输出buffer的指针.
 *  @param  -[out] long &nOutSize: 输出buffer的长度.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CInfoMgrCmd::DownloadSnapPic(const char* pszUserName, const char* pszGroupName, const char* pInBuf, const long nInBufSize,
								char *&pRetBuf, long &nOutSize)
{
	long lOffset = 0;
	long nCount = 0;
	long lRet = VIDEO_PARSER->ParseBufferToInteger(pInBuf, &lOffset, nInBufSize, VIDEO_SNAP_COUNT_SIZE, nCount);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("DownloadSnapPic, parse snap count failed."));
		return lRet;
	}
	for (int i=0; i<nCount; i++)
	{
		long nSnapID = VIDEO_ID_INVALIDATION;
		lRet = VIDEO_PARSER->ParseBufferToInteger(pInBuf, &lOffset, nInBufSize, VIDEO_SNAP_ID_SIZE, nSnapID);
		if(lRet != ICV_SUCCESS)
		{
			CVLog.LogErrMessage(lRet, _("DownloadSnapPic, parse snap id failed."));
			return lRet;
		}

		long nPicBufSize = 0;
		nPicBufSize = sprintf(pRetBuf+nOutSize, "%06d", nSnapID);
		nOutSize += nPicBufSize;

		char *pPicBuf = NULL;
		lRet = VIDEO_CONFIG->GetSnapPicBufByUser(nSnapID, pPicBuf, nPicBufSize, pszUserName, pszGroupName);
		if(lRet != ICV_SUCCESS)
		{
			CVLog.LogErrMessage(lRet, _("GetSnapPicBufByUser, failed."));
			return lRet;
		}
		char *pTempBuf = new char[nOutSize+nPicBufSize];
		memcpy(pTempBuf, pRetBuf, nOutSize);
		SAFE_DELETE(pRetBuf);
		memcpy(pTempBuf+nOutSize, pPicBuf, nPicBufSize);
		SAFE_DELETE(pPicBuf);
		pRetBuf = pTempBuf;
		nOutSize += nPicBufSize;
	}

	char *pTempBuf = new char[nOutSize+RETCMD_HEADERINFO_LENGTH];
	memcpy(pTempBuf, pRetBuf, nOutSize);
	SAFE_DELETE(pRetBuf);
	pRetBuf = pTempBuf;

	return lRet;
}

/**
 *  $(添加抓拍图片).
 *
 *  @param  -[in] const long lCamID: 摄像头ID.
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *  @param  -[in] const char* pInBuf: 输入buffer.
 *  @param  -[in] const long nInBufSize: 输入buffer长度.
 *  @param  -[out] char *&pRetBuf: 输出buffer的指针.
 *  @param  -[out] long &nOutSize: 输出buffer的长度.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CInfoMgrCmd::AddSnapPicture(const long lCamID, const char* pszUserName, const char* pszGroupName, 
								 const char* pInBuf, const long nInBufSize, char *&pRetBuf, long &nOutSize)
{
	long lOffset = 0;
	// 解析buffer
	CVideoSnapFile theSnap;
	theSnap.SetCameraID(lCamID);
	long lRet = VIDEO_PARSER->ParseBufferToSnapFile(pInBuf, nInBufSize, &theSnap, &lOffset);
	if(lRet != ICV_SUCCESS)
		return lRet;

	// 添加抓拍图片
	long nSnapID = VIDEO_ID_INVALIDATION;
	lRet = VIDEO_CONFIG->AddSnapPictureByUserInfo(lCamID, &theSnap, pInBuf+lOffset, nInBufSize-lOffset, pszUserName, pszGroupName, nSnapID);
	if(lRet != ICV_SUCCESS)
		CVLog.LogErrMessage(lRet, _("Add snap pictrue."));

	sprintf(pRetBuf, "%06d", nSnapID);
	nOutSize = VIDEO_SNAP_ID_SIZE;

	return lRet;
}

/**
 *  $(lei加抓拍图片).
 *
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *  @param  -[in] const char* pInBuf: 输入buffer.
 *  @param  -[in] const long nInBufSize: 输入buffer长度.
 *
 *  @version  08/25/2009  chenzhan  Initial Version.
 */
long CInfoMgrCmd::CapturePicAddTo(const char* pszUserName, const char* pszGroupName, const char* pInBuf, const long nInBufSize)
{
	// 解析buffer
	long lOffset = 0;
	long nCaptureID = 0;
	long lRet = VIDEO_PARSER->ParseBufferToInteger(pInBuf, &lOffset, nInBufSize, VIDEO_SNAP_ID_SIZE, nCaptureID);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CInfoMgrCmd::CapturePicAddTo parser nCaptureID failed!"));//CInfoMgrCmd::CapturePicAddTo解析nCaptureID失败！
		return lRet;
	}

	lRet = VIDEO_CONFIG->CapturePicAddTo(pszUserName, pszGroupName, nCaptureID, pInBuf+lOffset, nInBufSize-lOffset);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CInfoMgrCmd::CapturePicAddTo parse nCaptureID failed!"));//CInfoMgrCmd::CapturePicAddTo解析nCaptureID失败！
		return lRet;
	}
	
	return ICV_SUCCESS;
}

/**
 *  $(删除抓拍图片).
 *
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *  @param  -[in] const char* pInBuf: 输入buffer.
 *  @param  -[in] const long nInBufSize: 输入buffer长度.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CInfoMgrCmd::DelSnapInfo(const char* pszUserName, const char* pszGroupName, 
								const char* pInBuf, const long nInBufSize)
{
	long lOffset = 0;
	long nCount = 0;
	long lRet = VIDEO_PARSER->ParseBufferToInteger(pInBuf, &lOffset, nInBufSize, VIDEO_SNAP_COUNT_SIZE, nCount);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("DelSnapInfo, parse snap count failed."));
		return lRet;
	}
	for (int i=0; i<nCount; i++)
	{
		long nCamID = VIDEO_ID_INVALIDATION;
		// 多余的
		lRet = VIDEO_PARSER->ParseBufferToInteger(pInBuf, &lOffset, nInBufSize, VIDEO_CAMERA_ID_SIZE, nCamID);
		if(lRet != ICV_SUCCESS)
		{
			CVLog.LogErrMessage(lRet, _("DelSnapInfo, parse camera id failed."));
			return lRet;
		}
		long nSnapID = VIDEO_ID_INVALIDATION;
		lRet = VIDEO_PARSER->ParseBufferToInteger(pInBuf, &lOffset, nInBufSize, VIDEO_SNAP_ID_SIZE, nSnapID);
		if(lRet != ICV_SUCCESS)
		{
			CVLog.LogErrMessage(lRet, _("DelSnapInfo, parse snap id failed."));
			return lRet;
		}

		CVideoSnapFile theSnapInfo;
		lRet = VIDEO_CONFIG->GetCameraIDBySnapID(nSnapID, nCamID);
		if(lRet != ICV_SUCCESS)
		{
			CVLog.LogErrMessage(lRet, _("GetCameraIDBySnapID, failed."));
			return lRet;
		}

		lRet = VIDEO_CONFIG->DelSnapPicInfoByUser(nCamID, nSnapID, pszUserName, pszGroupName);
		if(lRet != ICV_SUCCESS)
		{
			CVLog.LogErrMessage(lRet, _("DelSnapPicBufByUser, failed."));
			return lRet;
		}
	}

	return lRet;
}

/**
 *  $(修改抓拍信息).
 *
 *  @param  -[in] const long lCamID: 摄像头ID.
 *  @param  -[in] const char* pszUserName: 用户名.
 *  @param  -[in] const char* pszGroupName: 群组名.
 *  @param  -[in] const char* pInBuf: 输入buffer.
 *  @param  -[in] const long nInBufSize: 输入buffer长度.
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 */
long CInfoMgrCmd::ModifySnapInfo(const long lCamID, const char* pszUserName, const char* pszGroupName, 
								 const char* pInBuf, const long nInBufSize)
{
	long lOffset = 0;
	long nSnapID = VIDEO_ID_INVALIDATION;
	long lRet = VIDEO_PARSER->ParseBufferToInteger(pInBuf, &lOffset, nInBufSize, VIDEO_SNAP_ID_SIZE, nSnapID);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("ModifySnapInfo, parse snap id failed."));
		return lRet;
	}
	// 解析抓拍类型
	char szSnapType[VIDEO_NAME_MAXSIZE];
	memset(szSnapType, 0x00, sizeof(szSnapType));
	lRet = VIDEO_PARSER->ParseSubBuffer(pInBuf, &lOffset, szSnapType, nInBufSize, VIDEO_NAME_LENGTH_SIZE, VIDEO_NAME_MAXSIZE);
	if(lRet != ICV_SUCCESS)
		return lRet;

	// 解析描述
	char szDesc[VIDEO_DESCRIPTION_MAXSIZE];
	memset(szDesc, 0x00, sizeof(szDesc));
	lRet = VIDEO_PARSER->ParseSubBuffer(pInBuf, &lOffset, szDesc, nInBufSize, VIDEO_DESP_LENGTH_SIZE, VIDEO_DESCRIPTION_MAXSIZE);
	if(lRet != ICV_SUCCESS)
		return lRet;

	// 创建抓拍对象
	CVideoSnapFile theSnap;
	theSnap.SetInfoType(szSnapType);
	theSnap.SetDescription(szDesc);

	CVideoSnapFile theSnapInfo;
	long nCamID = VIDEO_ID_INVALIDATION;
	lRet = VIDEO_CONFIG->GetCameraIDBySnapID(nSnapID, nCamID);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("GetCameraIDBySnapID, failed."));
		return lRet;
	}

	// 修改抓拍图片
	lRet = VIDEO_CONFIG->ModifySnapPicInfoByUser(nCamID, nSnapID, &theSnap, pszUserName, pszGroupName);
	if(lRet != ICV_SUCCESS)
		CVLog.LogErrMessage(lRet, _("ModifySnapPicInfoByUser snap pictrue,"));
	return lRet;
}

// 获取服务的冗余状态, 返回值即是冗余状态
long CInfoMgrCmd::GetServiceRMStauts(char* szRetBuf, const long nRetBufSize, long* plOutSize)
{
	long lStatus = -1;
	long lRet = GetRMStatus(&lStatus);
	if(lRet == ICV_SUCCESS)
	{
		sprintf(szRetBuf, "%010d", lStatus);
		*plOutSize = sizeof(szRetBuf);
	}

	return lRet;
}

// 返回结果缓冲区给客户端
long CInfoMgrCmd::RetCmdBuffer(HQUEUE hCliQue, char *szCliBuf, long lBufSize)
{
	long lRet = ICV_SUCCESS;
	lRet = NET_WRAPPER->SendToClient(hCliQue, szCliBuf, lBufSize);
	return lRet;
}

// 锁定摄像头
long CInfoMgrCmd::LockCamera(const long lCamID, const char* szUserName, const char* szGroupName, 
							 const char* pInBuf, const long nInBufSize)
{
	long lRet = ICV_SUCCESS;
	// 解析摄像头ID
	long lOffset = 0;
	long lLockType = 0;
	lRet = VIDEO_PARSER->ParseBufferToInteger(pInBuf, &lOffset, nInBufSize, VIDEO_TIME_LOCKTYPE_SIZE, lLockType);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("LockCamera, parse camera LockType."));
		return lRet;
	}

	// 解析时间(秒为单位)
	long lLockSeconds = 0;
	lRet = VIDEO_PARSER->ParseBufferToInteger(pInBuf, &lOffset, nInBufSize, VIDEO_TIME_LOCK_SIZE, lLockSeconds);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("LockCamera, parse camera LockSeconds."));
		return lRet;
	}

	// 获取摄像头标签
	CVideoCameraEx *pCamEx = NULL;
	lRet = VIDEO_CONFIG->GetCameraByCamID(lCamID, pCamEx);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("User(%s) of group(%s), want to get camera(%d) information failed."), 
					szUserName, szGroupName, lCamID);
		return lRet;
	}

	if(VIDEO_LOCK_STATUS_UNLOCK == lLockType)
	{
		lRet = ManualReleaseLock((char *)szUserName, (char *)szGroupName, lCamID, LOCK_OBJECT_TYPE_CAMERA, pCamEx->GetAuthLabel());
		CVLog.LogErrMessage(lRet, _("User(%s) of group(%s) want to release camera(%d) lock"), 
					szUserName, szGroupName, lCamID);
	}
	else
	{
		lRet = ManualAcquireLock((char *)szUserName, (char *)szGroupName, lCamID, LOCK_OBJECT_TYPE_CAMERA, pCamEx->GetAuthLabel(), lLockSeconds);
		CVLog.LogErrMessage(lRet, _("User(%s) of group(%s) want to lock camera(%d)"), 
					szUserName, szGroupName, lCamID);
	}

	return lRet;
}

// 获取配置状态是否改变信息. 
// 请求信息包括三个时间:总体最后更新时间/预置位最后更新时间/模式最后更新时间
// 响应信息包括:冗余状态(1位)/总体最后更新时间/预置位最后更新时间/模式最后更新时间
long CInfoMgrCmd::GetCfgChangeInfo(const char* pInBuf, const long nInBufSize, char* szRetBuf, const long nRetBufSize, long* pnOutSize)
{
	long lRet = ICV_SUCCESS;
	
	return lRet;
}

/**
 *  获取用户有权限看到的所有自定义分区信息.(需求用例ID:113179,113180,113181)
 *
 *  @param  -[in]  const char*  pszUserName:	[用户名]
 *  @param  -[out] char*		pszRetBuf:		[返回客户端的信息]
 *  @param  -[in]  long			nRetBufSize:	[返回客户端的缓冲区长度]
 *  @param  -[in]  long			&lOutSize:		[返回客户端的缓冲区长度]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     04/21/2009  chenzhan  Initial Version.
 */
long CInfoMgrCmd::GetCustZone(const char* pszUserName, char* pszRetBuf, long nRetBufSize, long &lOutSize)
{
	// 入口检测
	if ((NULL == pszUserName) || (NULL == pszRetBuf))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	// 获取自定义分区
	CVideoCustomZoneList theCustZone;
	theCustZone.clear();
	VIDEO_CHECKFAIL_RETURN(VIDEO_CONFIG->GetCustZoneByUser(pszUserName, &theCustZone));

	// 打包自定义分区信息
	VIDEO_CHECKFAIL_RETURN(VIDEO_PACKER->PackRetGetCustZone(pszRetBuf, nRetBufSize, &theCustZone, lOutSize));

	return ICV_SUCCESS;
}

/**
 *  添加自定义分组.(需求用例ID:113179,113180,113181)
 *
 *  @param  -[in]  const char*  pszUserName:	[用户名]
 *  @param  -[in]  const char*  pszCliBuf:		[来自客户端的命令]
 *  @param  -[out] char*		pszRetBuf:		[返回客户端的信息]
 *  @param  -[in]  long			nRetBufSize:	[返回客户端的缓冲区长度]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     04/21/2009  chenzhan  Initial Version.
 */
long CInfoMgrCmd::AddCustZone(const char* pszUserName, const char* pszCliBuf, char* pszRetBuf, long nRetBufSize)
{
	// 入口检测
	if ((NULL == pszUserName) || (NULL == pszCliBuf) || (NULL == pszRetBuf))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	char szCustZoneName[VIDEO_NAME_MAXSIZE];
	memset(szCustZoneName, 0x00, sizeof(szCustZoneName));
	char szCustZoneDesc[VIDEO_DESCRIPTION_MAXSIZE];
	memset(szCustZoneDesc, 0x00, sizeof(szCustZoneDesc));

	// 解析命令信息
	VIDEO_CHECKFAIL_RETURN(VIDEO_PARSER->PaseCmdAddCustZone((char*)pszCliBuf, szCustZoneName, sizeof(szCustZoneName), szCustZoneDesc, sizeof(szCustZoneDesc)));

	// 添加自定义分区
	long lRet = VIDEO_CONFIG->AddCustZoneByUser(pszUserName, szCustZoneName, szCustZoneDesc);
	
	return lRet;
}

/**
 *  删除自定义分区.(需求用例ID:113179,113180,113181)
 *
 *  @param  -[in]  const char*  pszUserName:	[用户名]
 *  @param  -[in]  const char*  pszCliBuf:		[来自客户端的命令]
 *  @param  -[out] char*		pszRetBuf:		[返回客户端的信息]
 *  @param  -[in]  long			nRetBufSize:	[返回客户端的缓冲区长度]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     04/21/2009  chenzhan  Initial Version.
 */
long CInfoMgrCmd::DelCustZone(const char* pszUserName, const char* pszCliBuf, char* pszRetBuf, long nRetBufSize)
{
	// 入口检测
	if ((NULL == pszUserName) || (NULL == pszCliBuf) || (NULL == pszRetBuf))
		return EC_ICV_CCTV_FUNCPARAMINVALID;


	// 解析自定义分区名称
	char szCustZoneName[VIDEO_NAME_MAXSIZE];
	memset(szCustZoneName, 0x00, sizeof(szCustZoneName));
	VIDEO_CHECKFAIL_RETURN(VIDEO_PARSER->PaseCmdDelCustZone((char*)pszCliBuf, szCustZoneName, sizeof(szCustZoneName)));

	// 获取自定义分区ID
	CVideoCustomZoneList theCustZone;
	theCustZone.clear();
	VIDEO_CHECKFAIL_RETURN(VIDEO_CONFIG->GetCustZoneByUser(pszUserName, &theCustZone));
	CVideoCustomZone* pCustZone = theCustZone.FindCustZonebyName(szCustZoneName);

	// 删除自定义分区
	long lRet = ICV_SUCCESS;
	if (NULL != pCustZone)
		lRet = VIDEO_CONFIG->DelCustZone(pCustZone->GetID());

	
	return lRet;
}

/**
 *  获取用户有权限看到的某一自定义分区摄像头信息.(需求用例ID:113179,113180,113181)
 *
 *  @param  -[in]  const char*  pszUserName:	[用户名]
 *  @param  -[in]  const char*  pszCliBuf:		[来自客户端的命令]
 *  @param  -[out] char*		pszRetBuf:		[返回客户端的信息]
 *  @param  -[in]  long			nRetBufSize:	[返回客户端的缓冲区长度]
 *  @param  -[in]  long			&lOutSize:		[返回客户端的缓冲区长度]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     04/21/2009  chenzhan  Initial Version.
 */
long CInfoMgrCmd::GetCustZoneCam(const char* pszUserName, const char* pszCliBuf, char* pszRetBuf, long nRetBufSize, long &lOutSize)
{
	// 入口检测
	if ((NULL == pszUserName) || (NULL == pszCliBuf) || (NULL == pszRetBuf))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	// 解析自定义分区名称
	char szCustZoneName[VIDEO_NAME_MAXSIZE];
	memset(szCustZoneName, 0x00, sizeof(szCustZoneName));
	VIDEO_CHECKFAIL_RETURN(VIDEO_PARSER->PaseCmdGetCustZoneCam((char*)pszCliBuf, szCustZoneName, sizeof(szCustZoneName)));

	// 获取自定义分区ID
	CVideoCustomZoneList theCustZone;
	theCustZone.clear();
	VIDEO_CHECKFAIL_RETURN(VIDEO_CONFIG->GetCustZoneByUser(pszUserName, &theCustZone));
	
	CVideoCustomZone *pCustZone = theCustZone.FindCustZonebyName(szCustZoneName);
	if (NULL == pCustZone)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long nCustZoneID = VIDEO_ID_INVALIDATION;
	nCustZoneID = pCustZone->GetID();

	// 获取摄像头ID列表
	CVideoCameraList theCamList;
	VIDEO_CHECKFAIL_RETURN(VIDEO_CONFIG->GetCustZoneCamByUser(pszUserName, nCustZoneID, &theCamList));

	// 打包摄像头名称
	VIDEO_CHECKFAIL_RETURN(VIDEO_PACKER->PackRetGetCustZoneCam(pszRetBuf, nRetBufSize, &theCamList, lOutSize));
	
	return ICV_SUCCESS;
}

/**
 *  复制摄像头到自定义分区.(需求用例ID:113179,113180,113181)
 *
 *  @param  -[in]  const char*  pszUserName:	[用户名]
 *  @param  -[in]  const char*  pszCliBuf:		[来自客户端的命令]
 *  @param  -[out] char*		pszRetBuf:		[返回客户端的信息]
 *  @param  -[in]  long			nRetBufSize:	[返回客户端的缓冲区长度]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     04/21/2009  chenzhan  Initial Version.
 */
long CInfoMgrCmd::CpyCustZoneCam(const char* pszUserName, const char* pszCliBuf, char* pszRetBuf, long nRetBufSize)
{
	// 入口检测
	if ((NULL == pszUserName) || (NULL == pszCliBuf) || (NULL == pszRetBuf))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	char szCustZoneName[VIDEO_NAME_MAXSIZE];
	memset(szCustZoneName, 0x00, sizeof(szCustZoneName));
	CVideoList<CStrName> theCamNameList;

	// 解析自定义分区名称及摄像头列表
	VIDEO_PARSER->PaseCmdCpyCustZoneCam((char*)pszCliBuf, szCustZoneName, VIDEO_NAME_MAXSIZE, &theCamNameList);
	
	// 获取自定义分区ID
	CVideoCustomZoneList theCustZone;
	theCustZone.clear();
	VIDEO_CHECKFAIL_RETURN(VIDEO_CONFIG->GetCustZoneByUser(pszUserName, &theCustZone));
	
	CVideoCustomZone *pCustZone = theCustZone.FindCustZonebyName(szCustZoneName);
	if (NULL == pCustZone)
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	long nCustZoneID = VIDEO_ID_INVALIDATION;
	nCustZoneID = pCustZone->GetID();

	// 获取摄像头ID列表
	CVideoCameraList theCamList;
	VIDEO_CHECKFAIL_RETURN(VIDEO_CONFIG->GetCustZoneCamByUser(pszUserName, nCustZoneID, &theCamList));

	// 获取摄像头ID
	CVideoList<long> theCamID;
	theCamID.clear();
	for (int i=0; i<theCamNameList.GetSize(); i++)
	{
		CVideoCameraEx *pCamEx = NULL;
		if (ICV_SUCCESS == VIDEO_CONFIG->GetCameraByCamName(theCamNameList.GetAt(i)->m_szName, pCamEx))
		{
			long lID = pCamEx->GetID();
			if (NULL == theCamList.FindCamerabyID(lID))
				theCamID.InsertObject(lID);
		}
	}

	// 复制摄像头到自定义分区
	long lRet = VIDEO_CONFIG->CpyCustZoneCamByUser(pszUserName, nCustZoneID, &theCamID);
	
	return lRet;
}

/**
 *  删除自定义分区中的摄像头.(需求用例ID:113179,113180,113181)
 *
 *  @param  -[in]  const char*  pszUserName:	[用户名]
 *  @param  -[in]  const char*  pszGroupName:	[用户名]
 *  @param  -[in]  const char*  pszCliBuf:		[来自客户端的命令]
 *  @param  -[out] char*		pszRetBuf:		[返回客户端的信息]
 *  @param  -[in]  long			nRetBufSize:	[返回客户端的缓冲区长度]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     04/21/2009  chenzhan  Initial Version.
 */
long CInfoMgrCmd::DelCustZoneCam(const char* pszUserName, const char* pszGroupName, const char* pszCliBuf, char* pszRetBuf, long nRetBufSize)
{
	// 入口检测
	if ((NULL == pszUserName) || (NULL == pszGroupName) || (NULL == pszCliBuf) || (NULL == pszRetBuf))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	char szCustZoneName[VIDEO_NAME_MAXSIZE];
	memset(szCustZoneName, 0x00, sizeof(szCustZoneName));
	CVideoList<CStrName> theCamNameList;

	// 解析摄像头列表
	VIDEO_CHECKFAIL_RETURN(VIDEO_PARSER->PaseCmdDelCustZoneCam((char*)pszCliBuf, szCustZoneName, VIDEO_NAME_MAXSIZE, &theCamNameList));

	// 获取自定义分区ID
	CVideoCustomZoneList theCustZone;
	theCustZone.clear();
	VIDEO_CHECKFAIL_RETURN(VIDEO_CONFIG->GetCustZoneByUser(pszUserName, &theCustZone));
	
	CVideoCustomZone *pCustZone = theCustZone.FindCustZonebyName(szCustZoneName);
	if (NULL == pCustZone)
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	long nCustZoneID = VIDEO_ID_INVALIDATION;
	nCustZoneID = pCustZone->GetID();

	// 获取摄像头ID
	CVideoList<long> theCamID;
	for (int i=0; i<theCamNameList.GetSize(); i++)
	{
		CVideoCameraEx *pCamEx = NULL;
		if (VIDEO_SUCCESS == VIDEO_CONFIG->GetCameraByCamName(theCamNameList.GetAt(i)->m_szName, pCamEx))
			theCamID.InsertObject(pCamEx->GetID());
	}

	// 删除摄像头索引
	long lRet = VIDEO_CONFIG->DelCustZoneCamByUser(pszUserName, pszGroupName, nCustZoneID, &theCamID);
	
	return lRet;
}

/**
 *  预置位相关操作.
 *  包括增/删/改/应用预置位四种处理.
 *
 *  @param  -[in]  char* pszUserName: [用户名]
 *  @param  -[in]  char* pszGroupName: [用户组]
 *  @param  -[in]  char*  pMsgBuf: [请求的其他信息]
 *  @param  -[in]  long  nBufLen: [请求信息的长度]
 *
 *  @version     09/15/2008  shijunpu  Initial Version.
 */
long CInfoMgrCmd::AddPSP(char* pszUserName, char* pszGroupName, long lCamID, char* pMsgBuf, long nBufLen)
{
	long lRet = ICV_SUCCESS;
	long lOffset = 0;

	// 得到预置位信息
	char szPsPName[VIDEO_NAME_MAXSIZE];
	memset(szPsPName, 0, sizeof(szPsPName));
	lRet = VIDEO_PARSER->ParseSubBuffer(pMsgBuf, &lOffset, szPsPName, nBufLen, VIDEO_NAME_LENGTH_SIZE, VIDEO_CAMERA_ID_SIZE);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("AddPSP  Parse Psp Name error."));
		return lRet;
	}

	// 得到预置位描述
	char szPsPDesc[VIDEO_DESCRIPTION_MAXSIZE];
	memset(szPsPDesc, 0, sizeof(szPsPDesc));
	lRet = VIDEO_PARSER->ParseSubBuffer(pMsgBuf, &lOffset, szPsPDesc, nBufLen, VIDEO_DESP_LENGTH_SIZE, sizeof(szPsPDesc));
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("AddPSP  Parse Psp description error."));
		return lRet;
	}

	// 解析预置位索引
	long nPresetIndex = 0;
	lRet = VIDEO_PARSER->ParseBufferToInteger(pMsgBuf, &lOffset, nBufLen, VIDEO_PSP_INDEX_SIZE, nPresetIndex);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("AddPSP  Parse Psp index error."));
		return lRet;
	}

    // 添加
    CVideoPSP psp(szPsPName, nPresetIndex, szPsPDesc);
    lRet = VIDEO_CONFIG->AddPSPOfCameraByUserInfo(lCamID, &psp, pszUserName, pszGroupName);
    if(lRet != ICV_SUCCESS)
        CVLog.LogErrMessage(lRet, _("Failed to add psp information to database."));

	return lRet;
}

/**
 *  添加录像片段信息.
 *
 *  @param  -[in] const char* pszUserName:		[用户名]
 *  @param  -[in] const char* pszGroupName:		[用户组]
 *  @param  -[in] const long  nCamID:			[摄像头ID]
 *  @param  -[in] const char*  pInBuf:			[请求的其他信息]
 *  @param  -[in] const long  nInBufSize:		[请求信息的长度]
 *  @param  -[in] char *&pRetBuf:				[返回的信息]
 *  @param  -[in] long &nOutSize:				[返回信息的长度]
 *
 *  @version     08/26/2009  chenzhan  Initial Version.
 */
long CInfoMgrCmd::AddRecord(const char* pszUserName, const char* pszGroupName, const long nCamID,
						const char* pInBuf, const long nInBufSize, char *&pRetBuf, long &nOutSize)
{
	CRecord rec;
	char *pRecordBuffer = NULL;
	long nRecSize = 0;
	long lRet = VIDEO_PARSER->ParseCmdAddRecord((char*)pInBuf, &rec, pRecordBuffer, nRecSize);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CInfoMgrCmd::AddRecord fail to parse and add video information!"));//CInfoMgrCmd::AddRecord解析添加录像信息失败！
		return lRet;
	}

	// 增加摄像头ID
	rec.SetCameraID(nCamID);

	// 添加
	long nRecordID = 0;
	lRet = VIDEO_CONFIG->AddRecordOfCameraByUserInfo(pszUserName, pszGroupName, nCamID, &rec, nRecordID);
	if(ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CInfoMgrCmd::AddRecord fail to add video information!"));//CInfoMgrCmd::AddRecord添加录像信息失败！
		return lRet;
	}
	
	lRet = VIDEO_CONFIG->SaveRecord(nRecordID, pRecordBuffer, nRecSize);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CInfoMgrCmd::AddRecord fail to save the length of video!"));//CInfoMgrCmd::AddRecord保存录像长度失败！
		return lRet;
	}
	
	nOutSize = sprintf(pRetBuf, "%06d", nRecordID);

	return lRet;
}

/**
 *  修改录像片段信息.
 *
 *  @param  -[in] const char* pszUserName:		[用户名]
 *  @param  -[in] const char* pszGroupName:		[用户组]
 *  @param  -[in] const char*  pInBuf:			[请求的其他信息]
 *  @param  -[in] const long  nInBufSize:		[请求信息的长度]
 *
 *  @version     08/26/2009  chenzhan  Initial Version.
 */
long CInfoMgrCmd::ModifyRecordInfo(const char* pszUserName, const char* pszGroupName, const char* pInBuf, const long nInBufSize)
{
	CRecord rec;
	long lRet = VIDEO_PARSER->ParseCmdModifyRecord((char*)pInBuf, &rec);
	if(ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CInfoMgrCmd::ModifyRecordInfo fail to parse the information of video!"));//CInfoMgrCmd::ModifyRecordInfo解析录像信息失败！
		return lRet;
	}
	lRet = VIDEO_CONFIG->ModifyRecordByUserInfo(pszUserName, pszGroupName, rec.GetID(), &rec);
	if(ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CInfoMgrCmd::ModifyRecordInfo fail to modify the information of video!"));//CInfoMgrCmd::ModifyRecordInfo修改录像信息失败！
		return lRet;
	}

	return ICV_SUCCESS;
}

/**
 *  查询录像片段信息.
 *
 *  @param  -[in] const char* pszUserName:		[用户名]
 *  @param  -[in] const char* pszGroupName:		[用户组]
 *  @param  -[in] const char*  pInBuf:			[请求的其他信息]
 *  @param  -[in] const long  nInBufSize:		[请求信息的长度]
 *  @param  -[in] char *&pRetBuf:				[返回的信息]
 *  @param  -[in] long &nOutSize:				[返回信息的长度]
 *
 *  @version     08/26/2009  chenzhan  Initial Version.
 */
long CInfoMgrCmd::QueryRecordInfo(const char* pszUserName, const char* pszGroupName, const char* pInBuf, const long nInBufSize, char *&pRetBuf, long &nOutSize)
{
	long lOffset = 0;

	long nCamID = 0;
	long nTimeStart = 0;
	long nTimeEnd = 0;
	long lRet = VIDEO_PARSER->ParseCmdQueryRecord((char*)pInBuf, nCamID, nTimeStart, nTimeEnd);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CInfoMgrCmd::QueryRecordInfo fail to parse the start time of video!"));//CInfoMgrCmd::QueryRecordInfo解析录像开始时间失败！
		return lRet;
	}

	// 查询
	CRecordList thelistRec;
	lRet = VIDEO_CONFIG->QueryRecordByUserInfo(pszUserName, pszGroupName, nCamID, nTimeStart, nTimeEnd, &thelistRec);
	if(ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CInfoMgrCmd::QueryRecordInfo fail to query the information of video!"));//CInfoMgrCmd::QueryRecordInfo查询录像信息失败！
		return lRet;
	}

	lRet = VIDEO_PACKER->PackRetQueryRecord(pRetBuf, VIDEO_MESSAGE_MAX_SIZE, &thelistRec, nOutSize);
	if(ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CInfoMgrCmd::QueryRecordInfo fail to query the information of video!"));//CInfoMgrCmd::QueryRecordInfo查询录像信息失败！
		return lRet;
	}
	
	return ICV_SUCCESS;
}

/**
 *  下载录像片段.
 *
 *  @param  -[in] const char* pszUserName:		[用户名]
 *  @param  -[in] const char* pszGroupName:		[用户组]
 *  @param  -[in] const char*  pInBuf:			[请求的其他信息]
 *  @param  -[in] const long  nInBufSize:		[请求信息的长度]
 *  @param  -[out] char *&pRetBuf:				[返回的信息]
 *  @param  -[out] long &nOutSize:				[返回信息的长度]
 *
 *  @version     08/26/2009  chenzhan  Initial Version.
 */
long CInfoMgrCmd::DownloadRecord(const char* pszUserName, const char* pszGroupName, const char* pInBuf, const long nInBufSize, char *&pRetBuf, long &nOutSize)
{
	long lRet = ICV_SUCCESS;
	long lOffset = 0;
	
	long nRecordID = 0;
	lRet = VIDEO_PARSER->ParseBufferToInteger(pInBuf, &lOffset, nInBufSize, VIDEO_RECORD_ID_SIZE, nRecordID);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CInfoMgrCmd::DownloadRecord fail to parse the ID of video!"));//CInfoMgrCmd::DownloadRecord解析录像ID失败！
		return lRet;
	}

	// 下载
	char szFileName[VIDEO_FILE_NAME_MAXSIZE];
	memset(szFileName, 0x00, sizeof(szFileName));
	lRet = VIDEO_CONFIG->DownloadRecordByUserInfo(pszUserName, pszGroupName, nRecordID, szFileName);
	if(ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CInfoMgrCmd::DownloadRecord fail to check the authority of user when downloading video information!"));//CInfoMgrCmd::DownloadRecord下载录像信息判断用户权限失败！
		return lRet;
	}

	// 读取录像内容
	char szFullName[VIDEO_DEFAULTINFO_MAXSIZE];
	memset(szFullName, 0x00, sizeof(szFullName));
	sprintf(szFullName, "%s\\VideoServer\\%s", CVComm.GetProjectDataPath(), szFileName);

	ACE_Mem_Map memMaper;
	int nRet = memMaper.map(szFullName);
	if(0 != nRet)
	{
		memMaper.close();
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}

	long nRecordSize = memMaper.size();
	nOutSize = sprintf(pRetBuf, "%02d%s%010d", strlen(szFileName), szFileName, nRecordSize);
	char *pTempBuf = new char[nOutSize+nRecordSize+RETCMD_HEADERINFO_LENGTH];
	memcpy(pTempBuf, pRetBuf, nOutSize);
	SAFE_DELETE(pRetBuf);
	memcpy(pTempBuf+nOutSize, memMaper.addr(), nRecordSize);
	memMaper.close();
	pRetBuf = pTempBuf;
	nOutSize += nRecordSize;

	return ICV_SUCCESS;
}

/**
 *  删除录像片段信息.
 *
 *  @param  -[in] const char* pszUserName:		[用户名]
 *  @param  -[in] const char* pszGroupName:		[用户组]
 *  @param  -[in] const char*  pInBuf:			[请求的其他信息]
 *  @param  -[in] const long  nInBufSize:		[请求信息的长度]
 *
 *  @version     08/26/2009  chenzhan  Initial Version.
 */
long CInfoMgrCmd::DeleteRecord(const char* pszUserName, const char* pszGroupName, const char* pInBuf, const long nInBufSize)
{
	long nRecordID = 0;

	long lOffset = 0;
	long lRet = VIDEO_PARSER->ParseBufferToInteger(pInBuf, &lOffset, nInBufSize, VIDEO_SNAP_ID_SIZE, nRecordID);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CInfoMgrCmd::DeleteRecord fail to parse the ID of video!"));//CInfoMgrCmd::DeleteRecord解析录像ID失败！
		return lRet;
	}

	// 删除
	lRet = VIDEO_CONFIG->DeleteRecordByUserInfo(pszUserName, pszGroupName, nRecordID);
	if(ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CInfoMgrCmd::DeleteRecord fail to delete the information of video!"));//CInfoMgrCmd::DeleteRecord删除录像信息失败！
		return lRet;
	}

	return ICV_SUCCESS;
}

/**
 *  调用设备驱动线程执行视频级连切换.
 *
 *  @param  -[in] const char*  pInBuf:			[请求的其他信息]
 *  @param  -[in] const long  nInBufSize:		[请求信息的长度]
 *  @param  -[out] char *&pRetBuf:				[返回的信息]
 *  @param  -[out] long &nOutSize:				[返回信息的长度]
 *  @param  -[in] HQUEUE hCliQue:				[返回客户端句柄]
 *
 *  @version     04/19/2010  chenzhan  Initial Version.
 */
long CInfoMgrCmd::CallSwitchDriverTask(const char* pInBuf, const long nInBufSize, char *&pRetBuf, long &nOutSize, HQUEUE hCliQue)
{
	if ((nInBufSize <= 0) || (NULL == pInBuf))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lOffSet = 0;
	long lCmdCode = 0;
	long lStampID = 0;
	char szUserName[VIDEO_NAME_MAXSIZE];
	char szGroupName[VIDEO_NAME_MAXSIZE];
	memset(szUserName, 0x00, VIDEO_NAME_MAXSIZE);
	memset(szGroupName, 0x00, VIDEO_NAME_MAXSIZE);

	// 1.命令号.命令序列号.用户名.群组名
	VIDEO_CHECKFAIL_RETURN(VIDEO_PARSER->ParseRequestCmdHeaderInfo(pInBuf, nInBufSize, lOffSet, lCmdCode, lStampID,
					szUserName, sizeof(szUserName), szGroupName, sizeof(szGroupName)));

	long lCamID = VIDEO_ID_INVALIDATION;
	long lMonID = VIDEO_ID_INVALIDATION;
	long lRet = ICV_SUCCESS;
	switch(lCmdCode) 
	{
	case VIDEO_CMD_SWITCH_BY_ID: // 切换摄像头（按照摄像头ID）
		{
			lRet = VIDEO_PARSER->ParseBufferToInteger(pInBuf, &lOffSet, nInBufSize, VIDEO_CAMERA_ID_SIZE, lCamID);
			if(lRet != ICV_SUCCESS)
			{
				CVLog.LogErrMessage(lRet, _("CallSwitchDriverTask get command, illegal parsed camera id."));
				return lRet;
			}

			// 解析监视器ID
			lRet = VIDEO_PARSER->ParseBufferToInteger(pInBuf, &lOffSet, nInBufSize, VIDEO_MONITOR_ID_SIZE, lMonID);
			if(lRet != ICV_SUCCESS)
			{
				CVLog.LogErrMessage(lRet, _("VideoSwitch, error when parse monitor id"));
				return lRet;
			}
		}
		break;
	case VIDEO_CMD_SWITCH_BY_NAME: // 切换摄像头（按照摄像头名称）
		{
			// 解析摄像头名称
			char szCamName[VIDEO_NAME_MAXSIZE];
			memset(szCamName, 0x00, sizeof(szCamName));
			lRet = VIDEO_PARSER->ParseSubBuffer(pInBuf, &lOffSet, szCamName, nInBufSize, VIDEO_NAME_LENGTH_SIZE, sizeof(szCamName));
			if(ICV_SUCCESS != lRet)
			{
				CVLog.LogErrMessage(lRet, _("CallSwitchDriverTask get command, when video switch by name mode, illegal parsed camera name."));
				return lRet;
			}
			lRet = VIDEO_CONFIG->GetCameraIDByCamName(szCamName, lCamID);
			if(lRet != ICV_SUCCESS)
			{
				CVLog.LogErrMessage(lRet, _("CallSwitchDriverTask get command, illegal when get camera id by camera name."));
				return lRet;
			}
			char szMonName[VIDEO_NAME_MAXSIZE];
			memset(szMonName, 0x00, sizeof(szMonName));
			lRet = VIDEO_PARSER->ParseSubBuffer(pInBuf, &lOffSet, szMonName, nInBufSize, VIDEO_NAME_LENGTH_SIZE, VIDEO_NAME_MAXSIZE);
			if(lRet != ICV_SUCCESS)
			{
				CVLog.LogErrMessage(lRet, _("CallSwitchDriverTask, error when parse monitor name"));
				return lRet;
			}
			lRet = VIDEO_CONFIG->GetMonitorIDByMonName(szMonName, lMonID);
			if(lRet != ICV_SUCCESS)
			{
				CVLog.LogErrMessage(lRet, _("CallSwitchDriverTask get command, illegal when get monitor id by monitor name."));
				return lRet;
			}
		}
		break;
	default:
		return EC_ICV_CCTV_FUNCINVALID;
	}

	// 查找摄像头对应的控制设备信息
	CVideoDevice* pDev = NULL;
	char szChannelCam[VIDEO_CHANNEL_MAXSIZE];
	memset(szChannelCam, 0x00, sizeof(szChannelCam));

	lRet = VIDEO_CONFIG->GetCtrlDeviceByCamID(lCamID, pDev, szChannelCam);
	if(ICV_SUCCESS != lRet)
		return lRet;

	// 得到监视器对应的设备(矩阵)的IP和端口
	CVideoDevice* pDevLinkMon = NULL;

	char szChannelLinkMon[VIDEO_CHANNEL_MAXSIZE];
	memset(szChannelLinkMon, 0x00, sizeof(szChannelLinkMon));

	lRet = VIDEO_CONFIG->GetLinkDeviceByMonitorID(lMonID, pDevLinkMon, szChannelLinkMon);
	if(ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CallSwitchDriverTask::GetLinkDeviceByMonitorID() error."));
		return lRet;
	}

	CReSwitchChannelList theSwitchList;
	lRet = CalcSwitchDev(pDev, szChannelCam, pDevLinkMon, szChannelLinkMon, theSwitchList);
	if (ICV_SUCCESS != lRet)
		return lRet;

	CReSwitchChannelList theECDCSwitchList;
	CReSwitchChannelList::iterator it = theSwitchList.begin();
	for (; it!=theSwitchList.end(); it++)
	{
		CVideoDevice *pDevCtrl = it->GetDev();
		long nDevType = VIDEO_CONFIG->GetProducts()->FindProductbyID(pDevCtrl->GetProductID())->GetDeviceType();
		if (VIDEO_DEVICE_TYPE_MATRIX == nDevType)
		{
			// 首先找到摄像头对应的设备的驱动
			CVideoCameraEx *pCamEx = NULL;
			lRet = VIDEO_CONFIG->GetCameraByCamID(lCamID, pCamEx);
			CDevCtrlTask *pDrvTask = pCamEx->GetDevCtrlTask();
			if (NULL == pDrvTask)
			{
				lRet = EC_ICV_CCTV_CAMRELATEDDEVDRVNOTSTART;
				CVLog.LogErrMessage(lRet, _("The task of device that can control the camera(%s) has not started."), pCamEx->GetName());
				return lRet;
			}
			// 发送切换消息给设备驱动线程
			// 形成转发的消息请求块
			char szCmdMsg[VIDEO_GENERAL_RET_MAX_SIZE];
			memset(szCmdMsg, 0x00, sizeof(szCmdMsg));
			long nCmd = VIDEO_CMD_SWITCH_BY_ID;
			long lCmdMsgLen = sprintf(szCmdMsg, "%03d%010d%02d%s%02d%s", nCmd, lStampID, strlen(szUserName), szUserName, strlen(szGroupName), szGroupName);
			//2013/4/27 lixiaohao  修复获取channel信息时，将char* 作为 整数拷贝到buffer中的问题
			lCmdMsgLen += sprintf(szCmdMsg+lCmdMsgLen, "%04d%04d%s%04d%s", pDevCtrl->GetID(), strlen(it->GetInChannel()),it->GetInChannel(),strlen(it->GetOutChannel()), it->GetOutChannel());
			ACE_Message_Block *pMsgBlk = new ACE_Message_Block(lCmdMsgLen + sizeof(HQUEUE));
			if(pMsgBlk)
			{
				pMsgBlk->copy((const char *)&hCliQue, sizeof(HQUEUE));
				pMsgBlk->copy(szCmdMsg, lCmdMsgLen);
				// 投递完整的消息+QUEUEID 到远程服务任务进行处理
				pDrvTask->putq(pMsgBlk);
			}
		}
		else
		{
			theECDCSwitchList.InsertObject(*it);
		}
	}

	lRet = VIDEO_PACKER->PackRetVideoSwitch(pRetBuf, VIDEO_MESSAGE_MAX_SIZE, &theECDCSwitchList, nOutSize);
	if(ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CInfoMgrCmd::CallSwitchDriverTask  fail to return packing back switching codec list!"));//CInfoMgrCmd::CallSwitchDriverTask 打包返回切换编解码器列表失败!
		return lRet;
	}

	return ICV_SUCCESS;
}

/**
 *  计算模拟视频通路设备.
 *
 *  @param  -[in] CVideoDevice *pDevLinkCam:		[连接摄像机的设备]
 *  @param  -[in] long nCamChannel:					[连接摄像机的设备的通道]
 *  @param  -[in] CVideoDevice *pDevFromMon:		[连接监视器的设备]
 *  @param  -[in] long nFromMonChannel:				[连接监视器的设备的通道]
 *  @param  -[out] CReSwitchChannelList &theSwitchList:		[要切换的列表]
 *
 *  @version     04/19/2010  chenzhan  Initial Version.
 */
long CInfoMgrCmd::CalcSwitchDev(CVideoDevice *pDevLinkCam, char* szCamChannel, 
								 CVideoDevice *pDevFromMon, char* szFromMonChannel, CReSwitchChannelList &theSwitchList)
{
	if (!pDevLinkCam)
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	if (!pDevFromMon)
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	// 获取设备类型
	long nDevType = VIDEO_CONFIG->GetProducts()->FindProductbyID(pDevFromMon->GetProductID())->GetDeviceType();
	switch (nDevType)
	{
	case VIDEO_DEVICE_TYPE_MATRIX:	// 如果是矩阵就搜索下一级设备
	case VIDEO_DEVICE_TYPE_ENCODER: // 如果是编码器就搜索下一级设备
		{
			// 此处找到摄像机连接的最后一个设备
			if (pDevLinkCam->GetID() == pDevFromMon->GetID())
			{
				// 记录
				CReSwitchChannel theSwitchInfo;
				// 设置切换关系所用到的设备
				theSwitchInfo.SetDev(pDevFromMon);
				theSwitchInfo.SetInChannel(szCamChannel);
				theSwitchInfo.SetOutChannel(szFromMonChannel);
				theSwitchList.InsertObject(theSwitchInfo);
				return ICV_SUCCESS;
			}
			
			// 获取本设备输入通道连接的设备和通道
			CReSwitchChannelList theNextLinkList;
			long lRet = VIDEO_CONFIG->GetInputDevLinkList(pDevFromMon->GetID(), theNextLinkList);
			if (ICV_SUCCESS != lRet)
				return lRet;
			
			CReSwitchChannelList::iterator it = theNextLinkList.begin();
			for (; it!=theNextLinkList.end(); it++)
			{
				CVideoDevice *pDevLinkNext = VIDEO_CONFIG->GetDeviceList()->FindDevicebyID(it->GetDev()->GetID());
				if (!pDevLinkNext)
					continue;

				// 如果两个设备之间有多条通道相连
				// 就在这几条连接的通道之间按照通道号大小顺序选择当前应该使用的通道
				// 下面是获取两个连接的设备之间后继设备正在使用的连接设备通道
				char szDevLinkChannel[VIDEO_CHANNEL_MAXSIZE];
				memset(szDevLinkChannel, 0x00, sizeof(szDevLinkChannel));

				char szDevmonInChannel[VIDEO_CHANNEL_MAXSIZE];
				memset(szDevmonInChannel, 0x00, sizeof(szDevmonInChannel));

				lRet = VIDEO_CONFIG->GetCurrSwitchChannel(pDevLinkNext->GetID(), pDevFromMon->GetID(), szDevLinkChannel, szDevmonInChannel);
				if (ICV_SUCCESS != lRet)
					continue;

				lRet = CalcSwitchDev(pDevLinkCam, szCamChannel, pDevLinkNext, szDevLinkChannel, theSwitchList);
				if (ICV_SUCCESS != lRet)
					continue;
				else
				{
					// 记录
					CReSwitchChannel theSwitchInfo;
					// 设置切换关系所用到的设备
					theSwitchInfo.SetDev(pDevFromMon);
					theSwitchInfo.SetInChannel(szDevmonInChannel);
					theSwitchInfo.SetOutChannel(szFromMonChannel);
					theSwitchList.InsertObject(theSwitchInfo);
					return ICV_SUCCESS;
				}
			}
		}
		break;
	case VIDEO_DEVICE_TYPE_DECODER:	// 如果是解码器就要把所有编码器都搜索一遍
		{
			// 检索所有编码器
			CVideoDeviceList *pDevList = VIDEO_CONFIG->GetDeviceList();
			CVideoDeviceList::iterator itDev = pDevList->begin();
			for (; itDev!=pDevList->end(); itDev++)
			{
				nDevType = VIDEO_CONFIG->GetProducts()->FindProductbyID(itDev->GetProductID())->GetDeviceType();
				if (VIDEO_DEVICE_TYPE_ENCODER == nDevType)
				{
					CVideoDevice *pDevLinkNext = &(*itDev);
					long lRet = CalcSwitchDev(pDevLinkCam, szCamChannel, pDevFromMon, szFromMonChannel, theSwitchList);
					if (ICV_SUCCESS != lRet)
						continue;
					else
					{
						// 记录解码器
						CReSwitchChannel theDCSwitchInfo;
						// 设置切换关系所用到的设备
						theDCSwitchInfo.SetDev(pDevFromMon);
						theDCSwitchInfo.SetOutChannel(szFromMonChannel);
						theSwitchList.InsertObject(theDCSwitchInfo);
						return ICV_SUCCESS;
					}
				}
			}
		}
		break;
	default:	// 配置错误,从上往下不能有除矩阵和编解码器以外的其他设备
		long lRet = EC_ICV_CCTV_DEVNOTEXIST;
		CVLog.LogErrMessage(lRet, "Configuration error, output channel of analog line isn't the matrix or codec, please check the configuration,Device ID(%d)!", pDevFromMon->GetID());//配置出错,模拟线路的输出通道不是矩阵或者编解码器,请检查配置,设备ID(%d)!
		break;
	}
	
	return EC_ICV_CCTV_DEVNOTEXIST;
}

/**
 *  获取摄像头连接的实时图像的设备信息
 *
 *  @param  -[in]  const long	lCamID:			[摄像头ID]
 *  @param  -[in]  const char*  pszUserName:	[用户名]
 *  @param  -[in]  const char*  pszGroupName:	[群组名]
 *  @param  -[in]  const char*  pszCliBuf:		[来自客户端的命令]
 *  @param  -[out] char*		pszRetBuf:		[返回客户端的信息]
 *  @param  -[in]  long			nRetBufSize:	[返回客户端的缓冲区长度]
 *  @param  -[in]  long			&lOutSize:		[返回客户端的缓冲区长度]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     01/17/2011  fengjuanyong  Initial Version.
 */
long CInfoMgrCmd::GetRealDevToCam(const long lCamID, const char* pszUserName, const char* pszGroupName, 
									const char* pszCliBuf, const long nInBufSize, const long lStampID, 
									char* pszRetBuf, long nRetBufSize, long &lOutSize, HQUEUE hCliQue,
									long lCmdID)
{
	// 入口检测
	if ((NULL == pszUserName) || (NULL == pszRetBuf))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	// 解析权限模式
	long lAuthMode = 0;
	if (lCmdID == VIDEO_CMD_MONITORDEV_GET)
	{
		long lOffset = 0;
		long lRet = VIDEO_PARSER->ParseBufferToInteger(pszCliBuf, &lOffset, nInBufSize, VIDEO_NAME_LENGTH_SIZE, lAuthMode);
		if (VIDEO_SUCCESS != lRet)
		{
			CVLog.LogErrMessage(lRet, _("CInfoMgrCmd::GetRealDevToCam fail to parse authority mode."));//CInfoMgrCmd::GetRealDevToCam解析权限模式失败！
			return lRet;
		}
	}

	// 获取摄像头关联的设备
	CVideoCameraEx *pCamEx = NULL;
	VIDEO_CONFIG->GetCameraByCamID(lCamID, pCamEx);

	// 如果是VIDEO_CMD_REALDEVICE_GET,则需要判断是否是直连实时图像设备
	if (lCmdID == VIDEO_CMD_REALDEVICE_GET)
	{
		CVideoLinkDevice *pLinkDev = pCamEx->GetLinkDev()->FindSourceLinkDevice();
		// 如果实时图像有关联设备则直接返回该设备
		if (pLinkDev != NULL)
		{
			long lProductID = VIDEO_CONFIG->GetDeviceList()->FindDevicebyID(pLinkDev->GetDeviceID())->GetProductID();
			long lLinkDevType = VIDEO_CONFIG->GetProducts()->FindProductbyID(lProductID)->GetDeviceType();
			// 打包摄像头名称
			VIDEO_CHECKFAIL_RETURN(VIDEO_PACKER->PackRetGetDevToCam(pszRetBuf, nRetBufSize, lLinkDevType, pLinkDev, lOutSize));
			return ICV_SUCCESS;
		}
	}

	// 先查找摄像头关联的矩阵
	CVideoLinkDeviceList *pDevList = pCamEx->GetLinkDev();
	// 如果设备是矩阵则查找矩阵关联的设备
	CVideoDevice *pDev = NULL;
	// 矩阵关联的通道号
	char szMatrixChannel[VIDEO_CHANNEL_MAXSIZE];
	memset(szMatrixChannel, 0x00, sizeof(szMatrixChannel));

	bool bFindMatrix = false;
	for(CVideoLinkDeviceList::iterator itLinkDev = pDevList->begin(); itLinkDev != pDevList->end(); itLinkDev ++)
	{
		CVideoLinkDevice &linkDev = *itLinkDev;
		pDev = VIDEO_CONFIG->GetDeviceList()->FindDevicebyID(linkDev.GetDeviceID());
		long lProductID = pDev->GetProductID();
		long lLinkDevType = VIDEO_CONFIG->GetProducts()->FindProductbyID(lProductID)->GetDeviceType();
		if (lLinkDevType == VIDEO_DEVICE_TYPE_MATRIX)
		{
			Safe_CopyString(szMatrixChannel, linkDev.GetChannel(), sizeof(szMatrixChannel));
			bFindMatrix = true;
			break;
		}
	}

	// 未找到矩阵
	if (!bFindMatrix)
		return EC_ICV_CCTV_MATRIX_NOEXIST;

	// 找到矩阵，则在VIDEO_CONFIG->GetMatrixList()中查找
	CVideoMatrix *pMatrix = VIDEO_CONFIG->GetMatrixList()->FindMatByID(pDev->GetID());
	if (pMatrix == NULL)
		return EC_ICV_CCTV_MATRIX_NOEXIST;
	
	time_t tmTemp = 0;
	time(&tmTemp);
	bool bFindChannel = false;
	long lLevel = 0;
	long lRet = GetUserPRI((char *)pszUserName, (char *)pszGroupName, lLevel);
	if (lRet != ICV_SUCCESS)
		lLevel = 0;
	
	// 查找一下是否该摄像头已经占用了通道
	CVideoMatLinkDevList *pMatLinkDevList = pMatrix->GeMatLinkDevList();
	CVideoMatLinkDev *pMatLinkDev = NULL;

	CVideoLinkDevice *pLinkDevTemp = NULL;
	CVideoMonitor *pMonTemp = NULL;
	// 如果是VIDEO_CMD_REALDEVICE_GET
	if (lCmdID == VIDEO_CMD_REALDEVICE_GET)
		pMatLinkDev = pMatLinkDevList->FindMatEncodeLinkDevByCamID(lCamID);
	else
		pMatLinkDev = pMatLinkDevList->FindMatMonLinkDevByCamID(lCamID);
	// 已经占用
	if (pMatLinkDev != NULL)
	{
		// 需要增加资源计数
		long lResCount = pMatLinkDev->GetResCount();
		lResCount++;
		pMatLinkDev->SetResCount(lResCount);
		// 列表中增加用户信息
		CDevUser devUser;
		devUser.SetUserName(pszUserName);
		devUser.SetUserPRI(lLevel);
		devUser.SetLinkTime(tmTemp);
		pMatLinkDev->GetDevUserList()->InsertObject(devUser);

		if (lCmdID == VIDEO_CMD_REALDEVICE_GET)
		{
			// 打包摄像头名称
			pLinkDevTemp = pMatLinkDev->GetLinkDeviceList()->GetAt(0);
			VIDEO_CHECKFAIL_RETURN(VIDEO_PACKER->PackRetGetDevToCam(pszRetBuf, nRetBufSize, VIDEO_DEVICE_TYPE_ENCODER, pLinkDevTemp, lOutSize));
		}
		else
		{
			// 打包监视器名称
			pMonTemp = pMatLinkDev->GetMonitorList()->GetAt(0);
			VIDEO_CHECKFAIL_RETURN(VIDEO_PACKER->PackRetGetMonToCam(pszRetBuf, nRetBufSize, pMonTemp, lOutSize));
		}
		return ICV_SUCCESS;
	}

	// 没有占用，则继续查找
	CVideoMatLinkDevList::iterator itMatLinkDev = pMatLinkDevList->begin();
	for (; itMatLinkDev != pMatLinkDevList->end(); itMatLinkDev ++)
	{
		CVideoMatLinkDev &matLinkDev = *itMatLinkDev;
		// 如果没使用则占用
		if (lCmdID == VIDEO_CMD_REALDEVICE_GET)
		{
			if (!matLinkDev.GetIsUsed() && matLinkDev.GetHasEncodeDev())
			{
				pMatLinkDev = &(*itMatLinkDev);
				bFindChannel = true;
				break;
			}
			else if (matLinkDev.GetIsUsed() && matLinkDev.GetHasEncodeDev())
			{
				// 记录列表中优先级最高用户
				CDevUser *pTemp = matLinkDev.GetDevUserList()->FindLastUserPRI();
				if (pTemp != NULL)
				{
					bool bAuthMode = false;
					if (lAuthMode == 0)
						bAuthMode = pTemp->GetUserPRI() <= lLevel;
					else
						bAuthMode = pTemp->GetUserPRI() < lLevel;
					if (bAuthMode && (pTemp->GetLinkTime() < tmTemp))
					{
						pMatLinkDev = &(*itMatLinkDev);
						bFindChannel =true;
						tmTemp = pTemp->GetLinkTime();
					}
				}
			}
		}
		else
		{
			if (!matLinkDev.GetIsUsed() && matLinkDev.GetHasMonitorDev())
			{
				pMatLinkDev = &(*itMatLinkDev);
				bFindChannel = true;
				break;
			}
			else if (matLinkDev.GetIsUsed() && matLinkDev.GetHasMonitorDev())
			{
				// 记录列表中优先级最高用户
				CDevUser *pTemp = matLinkDev.GetDevUserList()->FindLastUserPRI();
				if (pTemp != NULL)
				{
					bool bAuthMode = false;
					if (lAuthMode == 0)
						bAuthMode = pTemp->GetUserPRI() <= lLevel;
					else
						bAuthMode = pTemp->GetUserPRI() < lLevel;
					if (bAuthMode && (pTemp->GetLinkTime() < tmTemp))
					{
						pMatLinkDev = &(*itMatLinkDev);
						bFindChannel =true;
						tmTemp = pTemp->GetLinkTime();
					}
				}
			}
		}
	}

	// 全部被占用或者找不到
	if (!bFindChannel)
		return EC_ICV_CCTV_DEV_NOEXIST;

	// 调用矩阵切换
	// 首先找到摄像头对应的设备的驱动
	CDevCtrlTask *pDrvTask = pCamEx->GetDevCtrlTask();
	if (NULL == pDrvTask)
	{
		lRet = EC_ICV_CCTV_CAMRELATEDDEVDRVNOTSTART;
		CVLog.LogErrMessage(lRet, _("The task of device that can control the camera(%s) has not started."), pCamEx->GetName());
		return lRet;
	}
	// 发送切换消息给设备驱动线程
	// 形成转发的消息请求块
	char szCmdMsg[VIDEO_GENERAL_RET_MAX_SIZE];
	memset(szCmdMsg, 0x00, sizeof(szCmdMsg));
	long nCmd = VIDEO_CMD_SWITCH_BY_ID;

	//将szMatrixChannel转化为整型放入消息中
	long lMatrixChannel = std::atoi(szMatrixChannel);

	long lCmdMsgLen = sprintf(szCmdMsg, "%03d%010d%02d%s%02d%s", nCmd, lStampID, strlen(pszUserName), pszUserName, strlen(pszGroupName), pszGroupName);
	lCmdMsgLen += sprintf(szCmdMsg+lCmdMsgLen, "%04d%04d%04d", pDev->GetID(), lMatrixChannel, pMatLinkDev->GetLocalChannel());
	ACE_Message_Block *pMsgBlk = new ACE_Message_Block(lCmdMsgLen + sizeof(HQUEUE));
	if(pMsgBlk)
	{
		pMsgBlk->copy((const char *)&hCliQue, sizeof(HQUEUE));
		pMsgBlk->copy(szCmdMsg, lCmdMsgLen);
		// 投递完整的消息+QUEUEID 到远程服务任务进行处理
		pDrvTask->putq(pMsgBlk);
	}

	// 记录使用信息
	pMatLinkDev->SetIsUsed(true);

	// 列表中增加用户信息
	if (pMatLinkDev->GetCamID() != lCamID)
	{
		pMatLinkDev->InitDevUserList();
		pMatLinkDev->SetResCount(0);
	}
	CDevUser devUser;
	devUser.SetUserName(pszUserName);
	devUser.SetUserPRI(lLevel);
	time_t tmNow;
	time(&tmNow);
	devUser.SetLinkTime(tmNow);
	pMatLinkDev->GetDevUserList()->InsertObject(devUser);
	pMatLinkDev->SetCamID(lCamID);
	long lResCount = pMatLinkDev->GetResCount();
	lResCount++;
	pMatLinkDev->SetResCount(lResCount);
	
	if (lCmdID == VIDEO_CMD_REALDEVICE_GET)
	{
		// 打包摄像头名称
		pLinkDevTemp = pMatLinkDev->GetLinkDeviceList()->GetAt(0);
		VIDEO_CHECKFAIL_RETURN(VIDEO_PACKER->PackRetGetDevToCam(pszRetBuf, nRetBufSize, VIDEO_DEVICE_TYPE_ENCODER, pLinkDevTemp, lOutSize));
	}
	else
	{
		// 打包监视器名称
		pMonTemp = pMatLinkDev->GetMonitorList()->GetAt(0);
		VIDEO_CHECKFAIL_RETURN(VIDEO_PACKER->PackRetGetMonToCam(pszRetBuf, nRetBufSize, pMonTemp, lOutSize));
	}

	return ICV_SUCCESS;
}

/**
 *  释放摄像头连接的编码器信息
 *
 *  @param  -[in]  const char*  pszUserName:	[用户名]
 *  @param  -[in]  const char*  pszCliBuf:		[来自客户端的命令]
 *  @param  -[out] char*		pszRetBuf:		[返回客户端的信息]
 *  @param  -[in]  long			nRetBufSize:	[返回客户端的缓冲区长度]
 *  @param  -[in]  long			&lOutSize:		[返回客户端的缓冲区长度]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     01/17/2011  fengjuanyong  Initial Version.
 */
long CInfoMgrCmd::FreeEncodeDevToCam(const long lCamID, const char* pszUserName, const char* pInBuf, const long nInBufSize)
{
	// 入口检测
	if ((NULL == pszUserName) || (NULL == pInBuf))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	// 解析编码器ID
	long lEncodeID = 0;
	long lOffset = 0;
	long lRet = VIDEO_PARSER->ParseBufferToInteger(pInBuf, &lOffset, nInBufSize, VIDEO_DEVICE_ID_SIZE, lEncodeID);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CInfoMgrCmd::FreeEncodeDevToCam fail to parse codec ID."));//CInfoMgrCmd::FreeEncodeDevToCam解析编码器ID失败！
		return lRet;
	}

	//解析通道号
	char szChannel[VIDEO_CHANNEL_MAXSIZE];
	memset(szChannel, 0x00, sizeof(szChannel));

	lRet = VIDEO_PARSER->ParseBufferToChannel(pInBuf, &lOffset, nInBufSize, szChannel);
	if (ICV_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("CInfoMgrCmd::FreeEncodeDevToCam fail to parse channel ID."));//CInfoMgrCmd::FreeEncodeDevToCam解析通道号ID失败！
		return lRet;
	}

	// 获取摄像头关联的设备
	CVideoCameraEx *pCamEx = NULL;
	VIDEO_CONFIG->GetCameraByCamID(lCamID, pCamEx);

	// 否则查找摄像头关联的矩阵下的编码器
	// 先查找摄像头关联的矩阵
	CVideoLinkDeviceList *pDevList = pCamEx->GetLinkDev();
	// 如果设备是矩阵则查找矩阵关联的设备
	CVideoDevice *pDev = NULL;
	bool bFindMatrix = false;
	for(CVideoLinkDeviceList::iterator itLinkDev = pDevList->begin(); itLinkDev != pDevList->end(); itLinkDev ++)
	{
		CVideoLinkDevice &linkDev = *itLinkDev;
		pDev = VIDEO_CONFIG->GetDeviceList()->FindDevicebyID(linkDev.GetDeviceID());
		long lProductID = pDev->GetProductID();
		long lLinkDevType = VIDEO_CONFIG->GetProducts()->FindProductbyID(lProductID)->GetDeviceType();
		if (lLinkDevType == VIDEO_DEVICE_TYPE_MATRIX)
		{
			bFindMatrix = true;
			break;
		}
	}

	// 未找到矩阵
	if (!bFindMatrix)
		return EC_ICV_CCTV_MATRIX_NOEXIST;

	// 找到矩阵，则在VIDEO_CONFIG->GetMatrixList()中查找
	CVideoMatrix *pMatrix = VIDEO_CONFIG->GetMatrixList()->FindMatByID(pDev->GetID());
	if (pMatrix == NULL)
		return EC_ICV_CCTV_MATRIX_NOEXIST;
	
	CVideoMatLinkDevList *pMatLinkDevList = pMatrix->GeMatLinkDevList();
	CVideoMatLinkDev *pMatLinkDev = pMatLinkDevList->FindMatEncodeLinkDevByCamID(lCamID);
	if (pMatLinkDev == NULL)
		return EC_ICV_CCTV_DEV_NOEXIST;

	int nIndex = pMatLinkDev->GetDevUserList()->FindDevUserIndexbyName(pszUserName);
	// 未找到该用户
	if (nIndex == VIDEO_ID_INVALIDATION)
		return EC_ICV_CCTV_DEVUSER_NOEXIST;

	CVideoLinkDevice *pLinkDevice = pMatLinkDev->GetLinkDeviceList()->FindLinkDevByDevIDAndChannel(lEncodeID, szChannel);
	if (pLinkDevice == NULL)
		return EC_ICV_CCTV_DEV_NOEXIST;

	// 如果资源数>1，则减1，否则清空
	long lResCount = pMatLinkDev->GetResCount();
	if (lResCount > 0)		
		lResCount--;
	
	pMatLinkDev->SetResCount(lResCount);
	if (lResCount == 0)
	{
		// 清空占用记录
		pMatLinkDev->SetIsUsed(false);
		pMatLinkDev->InitDevUserList();
		pMatLinkDev->SetCamID(VIDEO_ID_INVALIDATION);
	}
	else
		pMatLinkDev->GetDevUserList()->RemoveAt(nIndex);
	
	return ICV_SUCCESS;
}

/**
 *  释放摄像头连接的编码器信息
 *
 *  @param  -[in]  const char*  pszUserName:	[用户名]
 *  @param  -[in]  const char*  pszCliBuf:		[来自客户端的命令]
 *  @param  -[out] char*		pszRetBuf:		[返回客户端的信息]
 *  @param  -[in]  long			nRetBufSize:	[返回客户端的缓冲区长度]
 *  @param  -[in]  long			&lOutSize:		[返回客户端的缓冲区长度]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     03/02/2011  fengjuanyong  Initial Version.
 */
long CInfoMgrCmd::FreeMonitorDevToCam(const long lCamID, const char* pszUserName, const char* pInBuf, const long nInBufSize)
{
	// 入口检测
	if ((NULL == pszUserName) || (NULL == pInBuf))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	// 获取摄像头关联的设备
	CVideoCameraEx *pCamEx = NULL;
	VIDEO_CONFIG->GetCameraByCamID(lCamID, pCamEx);

	// 否则查找摄像头关联的矩阵下的编码器
	// 先查找摄像头关联的矩阵
	CVideoLinkDeviceList *pDevList = pCamEx->GetLinkDev();
	// 如果设备是矩阵则查找矩阵关联的设备
	CVideoDevice *pDev = NULL;
	bool bFindMatrix = false;
	for(CVideoLinkDeviceList::iterator itLinkDev = pDevList->begin(); itLinkDev != pDevList->end(); itLinkDev ++)
	{
		CVideoLinkDevice &linkDev = *itLinkDev;
		pDev = VIDEO_CONFIG->GetDeviceList()->FindDevicebyID(linkDev.GetDeviceID());
		long lProductID = pDev->GetProductID();
		long lLinkDevType = VIDEO_CONFIG->GetProducts()->FindProductbyID(lProductID)->GetDeviceType();
		if (lLinkDevType == VIDEO_DEVICE_TYPE_MATRIX)
		{
			bFindMatrix = true;
			break;
		}
	}

	// 未找到矩阵
	if (!bFindMatrix)
		return EC_ICV_CCTV_MATRIX_NOEXIST;

	// 找到矩阵，则在VIDEO_CONFIG->GetMatrixList()中查找
	CVideoMatrix *pMatrix = VIDEO_CONFIG->GetMatrixList()->FindMatByID(pDev->GetID());
	if (pMatrix == NULL)
		return EC_ICV_CCTV_MATRIX_NOEXIST;
	
	CVideoMatLinkDevList *pMatLinkDevList = pMatrix->GeMatLinkDevList();
	CVideoMatLinkDev *pMatLinkDev = pMatLinkDevList->FindMatMonLinkDevByCamID(lCamID);
	if (pMatLinkDev == NULL)
		return EC_ICV_CCTV_DEV_NOEXIST;

	int nIndex = pMatLinkDev->GetDevUserList()->FindDevUserIndexbyName(pszUserName);
	// 未找到该用户
	if (nIndex == VIDEO_ID_INVALIDATION)
		return EC_ICV_CCTV_DEVUSER_NOEXIST;

	// 如果资源数>1，则减1，否则清空
	long lResCount = pMatLinkDev->GetResCount();
	if (lResCount > 0)		
		lResCount--;
	
	pMatLinkDev->SetResCount(lResCount);
	if (lResCount == 0)
	{
		// 清空占用记录
		pMatLinkDev->SetIsUsed(false);
		pMatLinkDev->InitDevUserList();
		pMatLinkDev->SetCamID(VIDEO_ID_INVALIDATION);
	}
	else
		pMatLinkDev->GetDevUserList()->RemoveAt(nIndex);
	
	return ICV_SUCCESS;
}

/**
 *  获取摄像头连接的历史录像的设备信息
 *
 *  @param  -[in]  const long	lCamID:			[摄像头ID]
 *  @param  -[in]  const char*  pszUserName:	[用户名]
 *  @param  -[in]  const char*  pszCliBuf:		[来自客户端的命令]
 *  @param  -[out] char*		pszRetBuf:		[返回客户端的信息]
 *  @param  -[in]  long			nRetBufSize:	[返回客户端的缓冲区长度]
 *  @param  -[in]  long			&lOutSize:		[返回客户端的缓冲区长度]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     01/17/2011  fengjuanyong  Initial Version.
 */
long CInfoMgrCmd::GetHisDevToCam(const long lCamID, const char* pszUserName, const char* pszCliBuf, char* pszRetBuf, long nRetBufSize, long &lOutSize)
{
	// 入口检测
	if ((NULL == pszUserName) || (NULL == pszCliBuf) || (NULL == pszRetBuf))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	// 获取摄像头关联的设备
	CVideoCameraEx *pCamEx = NULL;
	VIDEO_CONFIG->GetCameraByCamID(lCamID, pCamEx);
	CVideoLinkDevice *pLinkDev = pCamEx->GetLinkDev()->FindHisLinkDevice();

	// 如果实时图像有关联设备则直接返回该设备
	if (pLinkDev != NULL)
	{
		long lProductID = VIDEO_CONFIG->GetDeviceList()->FindDevicebyID(pLinkDev->GetDeviceID())->GetProductID();
		long lLinkDevType = VIDEO_CONFIG->GetProducts()->FindProductbyID(lProductID)->GetDeviceType();
		// 打包摄像头名称
		VIDEO_CHECKFAIL_RETURN(VIDEO_PACKER->PackRetGetDevToCam(pszRetBuf, nRetBufSize, lLinkDevType, pLinkDev, lOutSize));
		return ICV_SUCCESS;
	}
	else
		return EC_ICV_CCTV_HISDEV_NOEXIST;
	
	return ICV_SUCCESS;
}

/**
 *  获取摄像头连接的控制设备信息
 *
 *  @param  -[in]  const long	lCamID:			[摄像头ID]
 *  @param  -[in]  const char*  pszUserName:	[用户名]
 *  @param  -[in]  const char*  pszCliBuf:		[来自客户端的命令]
 *  @param  -[out] char*		pszRetBuf:		[返回客户端的信息]
 *  @param  -[in]  long			nRetBufSize:	[返回客户端的缓冲区长度]
 *  @param  -[in]  long			&lOutSize:		[返回客户端的缓冲区长度]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     01/17/2011  fengjuanyong  Initial Version.
 */
long CInfoMgrCmd::GetCtlDevToCam(const long lCamID, const char* pszUserName, const char* pszCliBuf, char* pszRetBuf, long nRetBufSize, long &lOutSize)
{
	// 入口检测
	if ((NULL == pszUserName) || (NULL == pszCliBuf) || (NULL == pszRetBuf))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	// 获取摄像头关联的设备
	CVideoCameraEx *pCamEx = NULL;
	VIDEO_CONFIG->GetCameraByCamID(lCamID, pCamEx);
	CVideoLinkDevice *pLinkDev = pCamEx->GetLinkDev()->FindCtrlLinkDevice();

	// 如果实时图像有关联设备则直接返回该设备
	if (pLinkDev != NULL)
	{
		long lProductID = VIDEO_CONFIG->GetDeviceList()->FindDevicebyID(pLinkDev->GetDeviceID())->GetProductID();
		long lLinkDevType = VIDEO_CONFIG->GetProducts()->FindProductbyID(lProductID)->GetDeviceType();
		// 打包摄像头名称
		VIDEO_CHECKFAIL_RETURN(VIDEO_PACKER->PackRetGetDevToCam(pszRetBuf, nRetBufSize, lLinkDevType, pLinkDev, lOutSize));
		return ICV_SUCCESS;
	}
	else
		return EC_ICV_CCTV_DEV_NOEXIST;
	
	return ICV_SUCCESS;
}

/**
 *  获取矩阵关联的摄像头
 *
 *  @param  -[in] const char* pszUserName:		[用户名]
 *  @param  -[in] const char* pszGroupName:		[群组名]
 *  @param  -[in]  const char*  pszCliBuf:		[来自客户端的命令]
 *  @param  -[out] char * szRetBuf:				[输出buffer的指针]
 *  @param  -[out] long* plOutSize:				[输出buffer的长度]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     02/28/2011  fengjuanyong  Initial Version.
 */
long CInfoMgrCmd::GetCameraBufferByMatrix(const char* pszUserName, const char* pszGroupName, const char* pszCliBuf, const long nInBufSize, char* szRetBuf, const long nRetBufSize, long* pnOutSize)
{
	long lRet = ICV_SUCCESS;
	long nOff = 0;
	// 解析矩阵名称
	char szMatrixName[VIDEO_NAME_MAXSIZE];
	memset(szMatrixName, 0x00, VIDEO_NAME_MAXSIZE);
	lRet = VIDEO_PARSER->ParseSubBuffer(pszCliBuf, &nOff, szMatrixName, nInBufSize, VIDEO_NAME_LENGTH_SIZE, VIDEO_NAME_MAXSIZE);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("When get matrix information by matrix name, parse matrix information failed."));
		return lRet;
	}
	
	// 根据矩阵名称获取矩阵ID
	long lMatrixID = VIDEO_ID_INVALIDATION;
	lRet = VIDEO_CONFIG->GetMatrixIDByMatrixName(szMatrixName, lMatrixID);
	if (lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("Failed to get matrix information by matrix name."));
		return lRet;
	}

	// 获取矩阵下的摄像头列表
	CVideoCameraList listCamera;
	lRet = VIDEO_CONFIG->GetCameraListByMatrixAndUserInfo(&listCamera, pszUserName, pszGroupName, lMatrixID);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("When get camera, GetCameraListByMatrixAndUserInfo run failed."));
		return lRet;
	}

	// 解析
	lRet = VIDEO_PACKER->PackSimpleCameraListToBuffer(&listCamera, szRetBuf, nRetBufSize, pnOutSize);
	if(lRet != ICV_SUCCESS)
	{
		CVLog.LogErrMessage(lRet, _("PackCameraListToBuffer."));
		return lRet;
	}

	return lRet;

}


