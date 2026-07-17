/**************************************************************
 *  Filename:    ReqFetchTask.cpp
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: 接收请求消息,将其进行分发,分为三类:
	1. 本地设备控制任务, 找到该控制对应的设备任务(每个设备的任务只有1个线程), 分发
	2. 远程服务的控制, 发给远程服务处理线程(该任务可有多个线程,暂定2个)
	3. 信息查询类请求, 本任务直接查询/返回(可有多个,这里暂定2个)
 *
 *  @author:     shijunpu
 *  @version     05/29/2008  shijunpu  Initial Version
  				 08/31/2009  chenzhan  增加录像片段管理.
				 12/08/2009  chenzhan  取消获取QIP的打印日志.
				 04/20/2010  chenzhan  增加视频设备级连切换
**************************************************************/
 
#include "NetWrapper.h"
#include "MsgDispatchTask.h"
#include "RemoteProcTask.h"
#include "DevCtrlTask.h"
#include "VideoCfg.h"
#include "TimerTask.h"
#include "gettext/libintl.h"

#define _(STRING) gettext(STRING)
extern long g_lSvcState;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CMsgDispatchTask::CMsgDispatchTask()
{

}

CMsgDispatchTask::~CMsgDispatchTask()
{

}

/**
 *  Start the Task and threads in the task are activated.
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     05/23/2008  shijunpu  Initial Version.
 */
long CMsgDispatchTask::StartTask()
{
	long lRet = ICV_SUCCESS;
	int nRet = (long)this->activate();
	if(nRet != 0)
	{
		lRet = EC_ICV_CC_FAILTOSTARTTASK;
		CVLog.LogErrMessage(lRet, _("StartTask() Failed."));
	}
	
	return lRet;	
}

/**
 *  stop the request handler task and threads in it.
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     05/23/2008  shijunpu  Initial Version.
 */
long CMsgDispatchTask::StopTask()
{
	// 投递一个退出消息令其退出
	ACE_Message_Block *pQuitMsgBlk = NULL;
	ACE_NEW_RETURN(pQuitMsgBlk, ACE_Message_Block(sizeof(long)), 1);
	pQuitMsgBlk->msg_type(ACE_Message_Block::MB_STOP);
	this->putq(pQuitMsgBlk);

	return ICV_SUCCESS;	
}

/**
 *  main procedure of the task.
 *	get a msg from CVNDK and put it to Request Handler Queue
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version     05/23/2008  shijunpu  Initial Version.
 */
int CMsgDispatchTask::svc()
{
	CVLog.LogMessage(LOG_LEVEL_INFO, _("CMsgDispatchTask::svc() begin"));

	// 启动各个设备驱动线程
	long lRet = StartAllDriverTask();
	if (lRet != ICV_SUCCESS) 
		CVLog.LogErrMessage(lRet, _("Not AllDriverTask Start."));

	// 等待其他线程发来的消息处理时间间隔
	ACE_Time_Value tvTimeout;
	tvTimeout.msec(0);
	
	// 接收消息缓冲区
	ACE_Message_Block *pMsgBlk = NULL;
	while(true)
	{
		// 从消息队列获取消息
		ACE_Time_Value tv = tvTimeout + ACE_OS::gettimeofday();
		int nQnum = getq(pMsgBlk, &tv);
		if (nQnum < 0)
		{
			lRet = RecvNetMessage();
			if (lRet != ICV_SUCCESS)
				CVLog.LogErrMessage(lRet, _("Fail to process message from the client!"));//处理从客户端接收来的消息失败！
			continue;
		}
		if(pMsgBlk->msg_type() == ACE_Message_Block::MB_STOP) 
		{
			pMsgBlk->release(); 
			CVLog.LogMessage(LOG_LEVEL_INFO,_("CMsgDispatchTask::svc() rcv a MB_STOP Msg, exit!"));
			break;
		}
		
		// 消息请求缓冲区
		char *pReqBuff = pMsgBlk->rd_ptr();
		long lReqLen = (long)pMsgBlk->length();
		lRet = DispatchCmdMessage(pReqBuff, lReqLen);
		if (lRet != ICV_SUCCESS)
			CVLog.LogErrMessage(lRet,  _("DispatchCmdMessage error, ret: <%d>."));

		pMsgBlk->release();
	}
	
	// 停止各个设备驱动线程
	lRet = StopAllDriverTask();
	if (lRet != ICV_SUCCESS) 
		CVLog.LogErrMessage(lRet,  _("Not AllDriverTask End."));

	CVLog.LogMessage(LOG_LEVEL_INFO,_("CMsgDispatchTask::svc() end"));
	return ICV_SUCCESS;
}

/**
 *  开启各个设备产品型号的驱动的任务.
 *
 *
 *  @version     09/14/2008  shijunpu  Initial Version.
 */
long CMsgDispatchTask::StartAllDriverTask()
{
	long lRet = ICV_SUCCESS;

	// 启动设备控制任务(每个设备产品型号对应一个DLL, 开启一个线程)
	CVideoProductList *pProducts = VIDEO_CONFIG->GetProducts();
	for(CVideoProductList::iterator itProduct = pProducts->begin(); itProduct != pProducts->end(); itProduct ++)
	{
		CVideoProduct &Product = *itProduct;
		// 如果是硬盘录像机、解码器、编码器
		if ((VIDEO_DEVICE_TYPE_MATRIX != itProduct->GetDeviceType()))
			continue;

		CDevCtrlTask *pDevCtrlTask = new CDevCtrlTask();
		pDevCtrlTask->SetDllName(Product.GetDllName());
		lRet = pDevCtrlTask->StartTask();
		
		// 如果启动任务失败
		if(lRet != ICV_SUCCESS)
		{
			SAFE_DELETE(pDevCtrlTask);
			continue;
		}

		CDriverTask *pDrvTask = new CDriverTask(Product);
		pDrvTask->SetCtrlTask(pDevCtrlTask);
		m_drvTaskList.push_back(pDrvTask);

		// 设定该驱动相关的摄像头的任务号
		// 为每个摄像头设定一个对应的驱动任务指针
		CVideoCameraMap *pCamMap = VIDEO_CONFIG->GetCameraMap();
		for(CVideoCameraMap::iterator itCam = pCamMap->begin(); itCam != pCamMap->end(); itCam ++)
		{
			CVideoCameraEx *pCamEx = itCam->second;
			CVideoProduct *pProduct = NULL;
			lRet = VIDEO_CONFIG->GetProductByCamID(pCamEx->GetID(), pProduct);
			if(lRet != ICV_SUCCESS)
				continue;
			// 如果驱动名称匹配, 则设定是摄像头对应的驱动任务
			if(strcmp(pProduct->GetDllName(), Product.GetDllName()) == 0)
				pCamEx->SetDevCtrlTask(pDevCtrlTask);
		}
	} // for(CVideoProductList::iterator itProduct

	return ICV_SUCCESS;
}

// 停止各个设备产品型号的驱动的任务
long CMsgDispatchTask::StopAllDriverTask()
{
	long lRet = ICV_SUCCESS;
	CDriverTaskList::iterator itDevTask = m_drvTaskList.begin();
	for(; itDevTask != m_drvTaskList.end(); itDevTask ++)
	{
		CDriverTask *pTask = *itDevTask;
		if(pTask && pTask->GetCtrlTask())
			pTask->GetCtrlTask()->StopTask(); // 发出停止消息
	}

	for(itDevTask = m_drvTaskList.begin(); itDevTask != m_drvTaskList.end(); itDevTask ++)
	{
		CDriverTask *pTask = *itDevTask;
		if(pTask && pTask->GetCtrlTask())
		{
			CDevCtrlTask *pDevCtrlTask = pTask->GetCtrlTask();
			pDevCtrlTask->wait(); // 等待驱动停止消息
			SAFE_DELETE(pDevCtrlTask); // 删除驱动内存
		}

		SAFE_DELETE(pTask); // 删除驱动内存
	}

	m_drvTaskList.clear();
	return lRet;
}

long CMsgDispatchTask::RecvNetMessage()
{
	HQUEUE hCliQue = 0;
	long lRcvLen = 0;
	char *pszResponse = NULL;
	
	long lRet = NET_WRAPPER->RecvFromClient(hCliQue, pszResponse, lRcvLen);
	if (lRet != ICV_SUCCESS)
	{   // 此处200毫秒打印一次日志是没有意义的，因为影响程序运行，所以不打印日志
		NET_WRAPPER->FreeRecvBuff(pszResponse);
		NET_WRAPPER->FreeRecvHandle(hCliQue);
		return ICV_SUCCESS;
	}
	
	// 如果是冗余机器中的非活动机器,则不处理任何控制命令; 否则处理
	long lRMStatus = VIDEO_CONFIG->GetVideoRMStatus();
	if(lRMStatus != RM_STATUS_ACTIVE)
		return ICV_SUCCESS;
	
	lRet = DispatchCmdMessage(pszResponse, lRcvLen, hCliQue);
	if (lRet != ICV_SUCCESS)
		CVLog.LogErrMessage(lRet,  _("DispatchCmdMessage error, ret: <%d>."));

	NET_WRAPPER->FreeRecvBuff(pszResponse);

	return ICV_SUCCESS;
}

// 分发各种控制命令
long CMsgDispatchTask::DispatchCmdMessage(char *szCmdMsg, long lCmdMsgLen, HQUEUE hCliQue)
{	
	// 首先取出请求的类型
	long lOffSet = 0;
	long lCmdCode = 0;
	long lStampID = 0;
	char szUserName[VIDEO_NAME_MAXSIZE];
	char szGroupName[VIDEO_NAME_MAXSIZE];
	memset(szUserName, 0x00, VIDEO_NAME_MAXSIZE);
	memset(szGroupName, 0x00, VIDEO_NAME_MAXSIZE);

	// 2.命令号.命令序列号.用户名.群组名
	VIDEO_CHECKFAIL_RETURN(VIDEO_PARSER->ParseRequestCmdHeaderInfo(szCmdMsg, lCmdMsgLen, lOffSet, lCmdCode, lStampID,
		szUserName, sizeof(szUserName), szGroupName, sizeof(szGroupName)));

	long lRet = ICV_SUCCESS;

	// 首先处理信息管理类请求.不与摄像头发生关系,不涉及到控制命令,也不涉及到其他服务的请求.处理完毕后直接返回
	switch(lCmdCode) 
	{
	case VIDEO_CMD_RESTART_SERVICE:		// 重启视频服务
		g_lSvcState = SERVICE_EVENT_RESTART;
		return ICV_SUCCESS;
	case VIDEO_CMD_SHUTDOWN_SERVICE:	// 关闭视频服务
		g_lSvcState = SERVICE_EVENT_STOP;
		return ICV_SUCCESS;
	case VIDEO_CMD_CYCLE_SWITCH_BY_BUFFER:
	case VIDEO_CMD_STOP_CYCLE_SWITCH:
		{
			// 形成转发的消息请求块
			ACE_Message_Block *pMsgBlk = new ACE_Message_Block(lCmdMsgLen + sizeof(HQUEUE));
			if(pMsgBlk)
			{
				pMsgBlk->copy((const char *)&hCliQue, sizeof(HQUEUE));
				pMsgBlk->copy(szCmdMsg, lCmdMsgLen);
				// 投递完整的消息+QUEUEID 到时间线程任务进行处理
				TIMER_TASK->putq(pMsgBlk);
			}
		}
		return ICV_SUCCESS;
	case VIDEO_CMD_GET_ALL_CONFIG_INFO:
	case VIDEO_CMD_GET_ALL_ZONE:
	case VIDEO_CMD_GET_ALL_PRODUCT:
	case VIDEO_CMD_GET_ALL_DEVICE:
	case VIDEO_CMD_GET_ALL_CAMERA:
	case VIDEO_CMD_GET_ALL_SIMPLE_CAMERA:
	case VIDEO_CMD_GET_ZONE_CAMERA:
	case VIDEO_CMD_GET_ZONE_SIMPLE_CAMERA:
	case VIDEO_CMD_GET_CAMERA_BY_NAME:
	case VIDEO_CMD_GET_ALL_MONITOR:
	case VIDEO_CMD_GET_ZONE_MONITOR:
	case VIDEO_CMD_GET_SNAP_TYPE:		// 获取抓拍类型
	case VIDEO_CMD_QUERY_SNAP_INFO:		// 按指定条件查询抓拍信息
	case VIDEO_CMD_GET_PIC_BUFFER_BYID:	// 获取指定抓拍图片的图片内容
	case VIDEO_CMD_DOWNLOAD_PIC:		// 下载抓拍图片
	case VIDEO_CMD_DELETE_SNAP_PIC:		// 删除抓拍图片
	case VIDEO_CMD_ADDTO_PIC:			// 累加抓拍图片

	// 录像管理
	case VIDEO_CMD_MDF_CAPTURE_RECORD:		// 修改录像片段信息
	case VIDEO_CMD_QUERY_CAPTURE_INFO:		// 按指定条件查询录像片段信息
	case VIDEO_CMD_GET_CAPTRUE_BUFFER:		// 下载录像片段
	case VIDEO_CMD_DEL_CAPTURE_RECORD:		// 删除录像片段

	// 模式管理(增删改查)
	case VIDEO_CMD_GET_ALL_MODE_NAME:
	case VIDEO_CMD_ADD_MODE:
	case VIDEO_CMD_DELETE_MODE:
	case VIDEO_CMD_MODIFY_MODE:
	case VIDEO_CMD_GET_MODE_LAYOUT:

	// 获取摄像头和监视器的对应关系, 只需要在本地执行查找
	case VIDEO_CMD_GET_ALL_MCLINK:

	// 其他功能
	case VIDEO_CMD_GET_SERVICE_STATUS:	// 获取冗余状态
	// 自定义分区
	case VIDEO_CMD_CUSTZONE_GET:		// 获取用户有权限看到的所有自定义分区信息
	case VIDEO_CMD_CUSTZONE_ADD:		// 添加自定义分组
	case VIDEO_CMD_CUSTZONE_DEL:		// 删除自定义分区
	case VIDEO_CMD_CUSTZONE_GETCAM:		// 获取用户有权限看到的某一自定义分区摄像头信息
	case VIDEO_CMD_CUSTZONE_CPYCAM:		// 复制摄像头到自定义分区
	case VIDEO_CMD_CUSTZONE_DELCAM:		// 删除自定义分区中的摄像头
	// 获取版本号
	case VIDEO_CMD_GET_SERVICEVER:
	// 获取矩阵关联的摄像头列表
	case VIDEO_CMD_CAMBYMATRIX_GET:
		return m_infoMgrCmd.ProcessInfoMgrCmd(VIDEO_ID_INVALIDATION, szCmdMsg+lOffSet, 
										lCmdMsgLen-lOffSet, lCmdCode, lStampID, szUserName, szGroupName, hCliQue);
	default:
		break;
	}

	//////////////////////////////////////////////////////////////////////////
	// 与摄像头有关的请求命令
	// 获取摄像头信息
	long lCamID = VIDEO_ID_INVALIDATION;
	CVideoCameraEx *pCamEx = NULL;
	switch (lCmdCode)
	{
	case VIDEO_CMD_SWITCH_BY_NAME: // 解析摄像头名称
	case VIDEO_CMD_MONITORDEV_GET: // 解析摄像头名称
	case VIDEO_CMD_MONITORDEV_FREE:
		{
			char szCamName[VIDEO_NAME_MAXSIZE];
			memset(szCamName, 0x00, sizeof(szCamName));
			lRet = VIDEO_PARSER->ParseSubBuffer(szCmdMsg, &lOffSet, szCamName, lCmdMsgLen, VIDEO_NAME_LENGTH_SIZE, VIDEO_NAME_MAXSIZE);
			if(lRet != ICV_SUCCESS)
			{
				CVLog.LogErrMessage(lRet,  _("DispatchCmdMessage(), failed to parse camera name."));
				DirectRetErrCode(lCmdCode, lStampID, lRet, hCliQue); // 直接返回错误码给客户端
				return lRet;
			}
			lRet = VIDEO_CONFIG->GetCameraIDByCamName(szCamName, lCamID);
		}
		break;
	default: // 解析摄像头ID
		lRet = VIDEO_PARSER->ParseBufferToInteger(szCmdMsg, &lOffSet, lCmdMsgLen, VIDEO_CAMERA_ID_SIZE, lCamID);
		if(lRet != ICV_SUCCESS)
		{
			CVLog.LogErrMessage(lRet,  _("DispatchCmdMessage(), failed to parse camera id."));
			DirectRetErrCode(lCmdCode, lStampID, lRet, hCliQue); // 直接返回错误码给客户端
			return lRet;
		}

		break;
	}

	lRet = VIDEO_CONFIG->GetCameraByCamID(lCamID, pCamEx);
	if(lRet != ICV_SUCCESS || !pCamEx)
	{
		CVLog.LogErrMessage(lRet,  _("DispatchCmdMessage(), find camera information failed by camera id(%d)."), lCamID);
		DirectRetErrCode(lCmdCode, lStampID, lRet, hCliQue); // 直接返回错误码给客户端
		return lRet;
	}
	
	// 如果不是本地服务, 那么转发给远程服务任务进行处理. 需要将摄像头ID/客户端Queue到转发消息中去
	if(!pCamEx->GetIsLocalCtrlSvr())
	{
		if(!pCamEx->GetCtrlSvr())
		{
			lRet = EC_ICV_CCTV_CANNOTGETCTRLSVRBYCAM;
			DirectRetErrCode(lCmdCode, lStampID, lRet, hCliQue); // 直接返回错误码给客户端
			return lRet;
		}

		// 形成转发的消息请求块
		long lMsgBlkLen = lCmdMsgLen + sizeof(HQUEUE) + sizeof(long)  + sizeof(long)  + sizeof(long)  + sizeof(long);
		ACE_Message_Block *pMsgBlk = new ACE_Message_Block(lMsgBlkLen);
		if(pMsgBlk)
		{
			long lVideoSvrID = pCamEx->GetCtrlSvr()->GetID();
			pMsgBlk->copy((const char *)&hCliQue, sizeof(HQUEUE)); // 放入客户请求QueueID
			pMsgBlk->copy((const char *)&lVideoSvrID, sizeof(lVideoSvrID));	// 放入对应的服务器ID

			pMsgBlk->copy((const char *)&lCmdCode, sizeof(lCmdCode));	// 放入命令号
			pMsgBlk->copy((const char *)&lStampID, sizeof(lStampID));	// 放入命令序号
			pMsgBlk->copy((const char *)&lCamID, sizeof(lCamID));	// 放入摄像头ID
			pMsgBlk->copy(szCmdMsg, lCmdMsgLen);	// 放入客户端发送的消息

			// 投递到远程服务任务进行处理
			REMOTE_TASK->putq(pMsgBlk);
		}
		return lRet;
	}

	// 与摄像头有关, 但摄像头本身不能控制
	if(!pCamEx->GetCanCtrl())
	{
	}
	//////////////////////////////////////////////////////////////////////////
	// 下面是本地服务, 且是与摄像头有关的请求处理
	// 本地服务处理, 与摄像头有关, 但不涉及控制命令两种
	switch(lCmdCode) 
	{
	case VIDEO_CMD_GET_PSP:
	case VIDEO_CMD_DELETE_PSP:
	case VIDEO_CMD_MODIFY_PSP:
	case VIDEO_CMD_ADD_PSP2:	
	// 抓拍图片管理(与摄像头相关, 但与设备控制无关, 需要考虑转发给其他服务实现)
	case VIDEO_CMD_ADD_SNAP_PIC:		// 增加抓拍图片
	case VIDEO_CMD_MODIFY_SNAP_INFO:	// 修改抓拍信息

	// 录像管理
	case VIDEO_CMD_ADD_CAPTURE_RECORD:		// 增加录像片段
		
	// 锁定摄像头和监视器
	case VIDEO_CMD_LOCK_CAMERA:
	case VIDEO_CMD_LOCK_MONITOR: // 本版本(5.0)暂没有实现

	// 获取摄像头关联设备
	case VIDEO_CMD_REALDEVICE_GET:
	case VIDEO_CMD_HISDEVICE_GET:
	case VIDEO_CMD_CTLDEVICE_GET:
	// 释放编码器
	case VIDEO_CMD_ENCODEDEV_FREE:
	// 获取摄像头关联监视器
	case VIDEO_CMD_MONITORDEV_GET:
	case VIDEO_CMD_MONITORDEV_FREE:
		return m_infoMgrCmd.ProcessInfoMgrCmd(lCamID, szCmdMsg+lOffSet, lCmdMsgLen-lOffSet, 
			lCmdCode, lStampID, szUserName, szGroupName, hCliQue);
	}

	//////////////////////////////////////////////////////////////////////////
	// 下面是本地服务, 且是与摄像头有关的请求处理, 且涉及控制命令
	// 首先找到摄像头的控制设备
    CVideoProduct* pProduct = NULL;
    lRet = VIDEO_CONFIG->GetProductByCamID(lCamID, pProduct);
    if ((NULL == pProduct) || (ICV_SUCCESS != lRet))
        return EC_ICV_CCTV_CANNOTFINDPRODUCT;
    
    long lDevType = pProduct->GetDeviceType();
    if((lDevType == VIDEO_DEVICE_TYPE_DVR) || (lDevType == VIDEO_DEVICE_TYPE_ENCODER) || (lDevType == VIDEO_DEVICE_TYPE_NVR) )
    {
        return m_infoMgrCmd.ProcessInfoMgrCmd(lCamID, szCmdMsg+lOffSet, lCmdMsgLen-lOffSet, 
			lCmdCode, lStampID, szUserName, szGroupName, hCliQue);
    }
    
    // 首先找到摄像头对应的设备的驱动
    CDevCtrlTask *pDrvTask = pCamEx->GetDevCtrlTask();
    if (NULL == pDrvTask)
    {
        lRet = EC_ICV_CCTV_CAMRELATEDDEVDRVNOTSTART;
        CVLog.LogErrMessage(lRet, _("The task of device that can control the camera(%s) has not started."), pCamEx->GetName());
        DirectRetErrCode(lCmdCode, lStampID, lRet, hCliQue); // 直接返回错误码给客户端
        return lRet;
    }

	switch (lCmdCode)
	{
	case VIDEO_CMD_SWITCH_BY_NAME: // 解析摄像头名称
	case VIDEO_CMD_SWITCH_BY_ID:
		return m_infoMgrCmd.ProcessInfoMgrCmd(lCamID, szCmdMsg, lCmdMsgLen, 
			lCmdCode, lStampID, szUserName, szGroupName, hCliQue);
		break;
	default:
		{
			// 形成转发的消息请求块
			ACE_Message_Block *pMsgBlk = new ACE_Message_Block(lCmdMsgLen + sizeof(HQUEUE));
			if(pMsgBlk)
			{
				pMsgBlk->copy((const char *)&hCliQue, sizeof(HQUEUE));
				pMsgBlk->copy(szCmdMsg, lCmdMsgLen);
				// 投递完整的消息+QUEUEID 到远程服务任务进行处理
				pDrvTask->putq(pMsgBlk);
			}
		}
		break;
	}

	return lRet;
}

// 返回处理失败的结果缓冲区给客户端
long CMsgDispatchTask::DirectRetErrCode(long lCmdCode, long lStampID, long lErrRetCode, HQUEUE hCliQue)
{
	long lRet = ICV_SUCCESS;
	long lRetCmdCode = lCmdCode + COMMAND_CODE_GAP_REQANDRET; // 命令对应的RetCode

	char szRetCmdBuf[VIDEO_GENERAL_RET_MAX_SIZE]; // 4K at most
	memset(szRetCmdBuf, 0, sizeof(szRetCmdBuf)); // 返回buffer的指针
	
	// 插入命令的RetCode和StampID/错误代码
	sprintf(szRetCmdBuf, "%03d%010d%010d", lRetCmdCode, lStampID, lErrRetCode);
	lRet = NET_WRAPPER->SendToClient(hCliQue, szRetCmdBuf, sizeof(szRetCmdBuf));
	CVLog.LogErrMessage(lRet,  _("DirectRetErrCode(), retrun error code(%s) to Client."), szRetCmdBuf);

	return lRet;
}
