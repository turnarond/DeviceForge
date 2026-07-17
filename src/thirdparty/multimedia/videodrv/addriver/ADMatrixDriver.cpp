/**************************************************************
 *  Filename:    ADMatrixDriver.cpp
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: AD矩阵实现文件.
 *
 *  @author:     liyang
 *  @version     07/24/2008  liyang  Initial Version
				 01/05/2010  CHENZHAN		增加控制通道处理
**************************************************************/
// ADMatrixDriver.cpp: implementation of the CADMatrixDriver class.
//
//////////////////////////////////////////////////////////////////////
#include "ADMatrixDriver.h"
#include "common/CVLog.h"
#include "ace/Synch_Options.h"
#include "gettext/libintl.h"

#define _(STRING) gettext(STRING)

#define CONTINUE_CMD_MAX_CTRLTIMES		30		// 连续控制命令(指云台)最多控制次数.按照当前周期设置，每100ms会执行一次，也就是说执行30次
#define MIN_RECONNECT_TIME_INTERVAL		5		// 最小重连间隔

extern CCVLog CVLog;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CADMatrixDriver::CADMatrixDriver()
{

}

CADMatrixDriver::~CADMatrixDriver()
{
	
}
/**
 *  初始化驱动.
 *
 *
 *  @version     07/10/2008  liyang  Initial Version.
 */

long CADMatrixDriver::InitDrive()
{
	CVLog.SetLogFileName("ADDriver");
	CloseAllDevice();
	return VIDEO_SUCCESS;
}
/**
 *  退出驱动.
 *
 *
 *  @version     07/10/2008  liyang  Initial Version.
 */

long CADMatrixDriver::ExitDriver()
{
	CloseAllDevice();
	return VIDEO_SUCCESS;
}

long CADMatrixDriver::Login(char* pszIP, long nIPSize, unsigned short nPort, char* pszExtent, long nExtentSize, long &nDevID)
{
	static long nLoginID = 1;
	nDevID = nLoginID++;
	// 生成一个新的连接, 放入其中
	DEVLINKINFO dev;
	dev.bIsContinueCtrl = false;
	strcpy(dev.szIP, pszIP);
	dev.uPort = nPort;
	dev.nDevID = nDevID;
	long lRet = ConnectDevice(&dev, false);
	if (VIDEO_SUCCESS != lRet)
	{
		CVLog.LogErrMessage(lRet, _("Fail to setup tcp connection when loging on device!"));//登陆设备建立TCP连接失败！
		return lRet;
	}

	m_deviceLinks.push_back(dev);
	return lRet;
}

/**
 *  摄像头镜头控制.
 *
 *  @param  -in  long  nDevID: 设备ID
 *  @param  -in  long  nCtrlChannel: 控制通道
 *  @param  -in  long  nChannel: 矩阵的通道
 *  @param  -in  long  nControlCode: 控制指令代码
 *  @param  -in  long  nSpeed: 摄像头动作速度
 *  @param  -in  long  lElapseTime: 动作时间，如果为0或负数表示一直执行，直到有客户端发送停止命令
 *
 *  @version     07/10/2008  liyang  Initial Version.
 */
long CADMatrixDriver::LensControl(long nDevID, long nCtrlChannel, long nChannel, long nControlCode, long nSpeed, long lElpaseTime)
{
	// 先根据IP和端口连接设备, 或找到设备指针
	DEVLINKINFO *pDevLink = NULL;
	long lRet = GetDeviceLink(nDevID, pDevLink);
	if(lRet != VIDEO_SUCCESS)
		return lRet;

	// 停止控制动作
	pDevLink->bIsContinueCtrl = false;
	memset(pDevLink->szCurCmdBuff, 0, sizeof(pDevLink->szCurCmdBuff));
	if(nSpeed == 0)
		return VIDEO_SUCCESS;

	// 开始控制动作
	char szCurCmd[MATRIX_COMMAND_MAX_SIZE];
	memset(szCurCmd, 0, sizeof(szCurCmd));
	
	// 设置当前控制的通道号
	sprintf(szCurCmd,"%dMa%d#a",nCtrlChannel,nChannel);
	// 发送命令给设备. 如果失败则直接返回, 不需要继续执行后续的控制命令
	lRet = WriteCmdToDevice(pDevLink, szCurCmd, strlen(szCurCmd));
	if(lRet != VIDEO_SUCCESS)
		return lRet;
	
	// 设置后续命令
	memset(szCurCmd, 0, sizeof(szCurCmd));

	switch(nControlCode)
	{
	case VIDEO_CTRL_LENS_ZOOMIN:  //拉近景物
		sprintf(szCurCmd,"%dTa",nSpeed);
		break;
	case VIDEO_CTRL_LENS_ZOOMOUT:   //拉远景物
		sprintf(szCurCmd,"%dWa",nSpeed);
		break;
	case VIDEO_CTRL_LENS_FAR:   //调近焦距
		sprintf(szCurCmd,"%dNa",nSpeed);
		break;
	case VIDEO_CTRL_LENS_NEAR:   //调远焦距
		sprintf(szCurCmd,"%dFa",nSpeed);
		break;
	case VIDEO_CTRL_LENS_BRIGHT:   //放大光圈
		sprintf(szCurCmd,"%dOa",nSpeed);
		break;
	case VIDEO_CTRL_LENS_DARK:   //缩小光圈
		sprintf(szCurCmd,"%dCa",nSpeed);
		break;
	default:
		return EC_ICV_CCTV_UNKNOWNCMD;
	}
	
	// 设置连续的控制命令
	pDevLink->bIsContinueCtrl = true;
	strcpy(pDevLink->szCurCmdBuff, szCurCmd);

	CVLog.LogMessage(LOG_LEVEL_INFO, "LensControl");
	// 发送后续命令给设备. 如果失败则直接返回
	lRet = WriteCmdToDevice(pDevLink, szCurCmd, strlen(szCurCmd));
	if(lRet != VIDEO_SUCCESS)
		return lRet;
	
	return lRet;
}
/**
 *  摄像头预置位控制.
 *
 *  @param  -in  long  nDevID: 设备ID
 *  @param  -in  long  nCtrlChannel: 控制通道
 *  @param  -in  long  nChannel: 矩阵的通道
 *  @param  -in  long  nControlCode:  控制指令代码
 *  @param  -in  long  nPresetIndex: 预置位索引
 *
 *  @version     07/10/2008  liyang  Initial Version.
 */
long CADMatrixDriver::PresetControl(long nDevID, long nCtrlChannel, long nChannel, long nControlCode, long nPresetIndex)
{	
	// 先根据IP和端口连接设备, 或找到设备指针
	DEVLINKINFO *pDevLink = NULL;
	long lRet = GetDeviceLink(nDevID, pDevLink);
	if(lRet != VIDEO_SUCCESS)
		return lRet;

	// 设置为不是连续控制类型
	pDevLink->bIsContinueCtrl = false;	
	memset(pDevLink->szCurCmdBuff, 0, sizeof(pDevLink->szCurCmdBuff));
	
	// 开始控制动作
	char szCurCmd[MATRIX_COMMAND_MAX_SIZE];
	memset(szCurCmd, 0, sizeof(szCurCmd));
	
	// 形成预置位控制命令
    switch(nControlCode)
	{	
	case VIDEO_CTRL_PSP_APPLY:  // 调用预置位
        sprintf(szCurCmd,"%dMa%d#a%d\\a",nCtrlChannel,nChannel,nPresetIndex);
		break;
	case VIDEO_CTRL_PSP_ADD:  // 记录预置位
        sprintf(szCurCmd,"%dMa%d#a%d^a",nCtrlChannel,nChannel,nPresetIndex);
		break;
	default:
		return EC_ICV_CCTV_UNKNOWNCMD;
	}

	// 发送命令给设备
	lRet = WriteCmdToDevice(pDevLink, szCurCmd, strlen(szCurCmd));
	if(lRet != VIDEO_SUCCESS)
		return lRet;

	return lRet;
}

// 附加设备控制
long CADMatrixDriver::AuxControl(long nDevID, long nCtrlChannel, long nChannel, long nControlCode, long nSpeed)
{	
	// 先根据IP和端口连接设备, 或找到设备指针
	DEVLINKINFO *pDevLink = NULL;
	long lRet = GetDeviceLink(nDevID, pDevLink);
	if(lRet != VIDEO_SUCCESS)
		return lRet;

	// 设置为不是连续控制类型
	pDevLink->bIsContinueCtrl = false;	
	memset(pDevLink->szCurCmdBuff, 0, sizeof(pDevLink->szCurCmdBuff));
	
	// 开始控制动作
	char szCurCmd[MATRIX_COMMAND_MAX_SIZE];
	memset(szCurCmd, 0, sizeof(szCurCmd));
	
	// 下面命令不太熟悉, 可能不正确
	// 形成预置位控制命令
    switch(nControlCode)
	{	
	case VIDEO_CTRL_AUX_BRUSH:  // 雨刷
		if(0 < nSpeed) // 启动附加设备
			sprintf(szCurCmd,"%dMa%d#a%dAa%dAa",nCtrlChannel,nChannel,nSpeed, nSpeed);
		else // 停止附加设备
			sprintf(szCurCmd,"%dMa%d#a%dBa%dBa",nCtrlChannel,nChannel,nSpeed, nSpeed);
		break;
	case VIDEO_CTRL_AUX_HEATER:  // 加热器
		if(0 < nSpeed) // 启动附加设备
			sprintf(szCurCmd,"%dMa%d#a%dAa%dAa",nCtrlChannel,nChannel,nSpeed, nSpeed);
		else // 停止附加设备
			sprintf(szCurCmd,"%dMa%d#a%dBa%dBa",nCtrlChannel,nChannel,nSpeed, nSpeed);
		break;
	default:
		return EC_ICV_CCTV_UNKNOWNCMD;
	}

	// 发送命令给设备
	lRet = WriteCmdToDevice(pDevLink, szCurCmd, strlen(szCurCmd));
	if(lRet != VIDEO_SUCCESS)
		return lRet;

	return lRet;
}

/**
 *  摄像头云台控制.
 *
 *  @param  -in  long  nDevID:设备ID
 *  @param  -in  long  nCtrlChannel: 控制通道
 *  @param  -in  long  nChannel: 矩阵的通道
 *  @param  -in  long  nControlCode: 控制指令代码
 *  @param  -in  long  nSpeed: 云台转动速度
 *  @param  -in  long  lElapseTime: 动作时间，如果为0或负数表示一直执行，直到有客户端发送停止命令
 *
 *  @version     07/10/2008  liyang  Initial Version.
 */
long CADMatrixDriver::PanControl(long nDevID, long nCtrlChannel, long nChannel, long nControlCode, long nSpeed, long lElpaseTime)
{
	// 先根据IP和端口连接设备, 或找到设备指针
	DEVLINKINFO *pDevLink = NULL;
	long lRet = GetDeviceLink(nDevID, pDevLink);
	if(lRet != VIDEO_SUCCESS)
		return lRet;

	// 停止控制动作
	pDevLink->bIsContinueCtrl = false;
	memset(pDevLink->szCurCmdBuff, 0, sizeof(pDevLink->szCurCmdBuff));
	if(nSpeed == 0)
		return VIDEO_SUCCESS;

	// 开始控制动作
	char szCurCmd[MATRIX_COMMAND_MAX_SIZE];
	memset(szCurCmd, 0, sizeof(szCurCmd));
	
	// 设置当前控制的通道号
	sprintf(szCurCmd,"%dMa%d#a",nCtrlChannel,nChannel);
	// 发送命令给设备. 如果失败则直接返回, 不需要继续执行后续的控制命令
	lRet = WriteCmdToDevice(pDevLink, szCurCmd, strlen(szCurCmd));
	if(lRet != VIDEO_SUCCESS)
		return lRet;
	
	// 形成后续的控制命令并继续发送
	switch(nControlCode)
	{
	case VIDEO_ORIENT_UP:  //控制云台运动方向上
		sprintf(szCurCmd,"%dUa%dUa",nSpeed,nSpeed);
		break;
	case VIDEO_ORIENT_RIGHT:   //控制云台运动方向右
		sprintf(szCurCmd,"%dRa%dRa",nSpeed,nSpeed);
		break;
	case VIDEO_ORIENT_DOWN:   //控制云台运动方向下
		sprintf(szCurCmd,"%dDa%dDa",nSpeed,nSpeed);
		break;
	case VIDEO_ORIENT_LEFT:   //控制云台运动方向左
		sprintf(szCurCmd,"%dLa%dLa",nSpeed,nSpeed);
		break;
	default:
		return EC_ICV_CCTV_UNKNOWNCMD;
	}
	
	// 设置连续的控制命令
	pDevLink->bIsContinueCtrl = true;
	strcpy(pDevLink->szCurCmdBuff, szCurCmd);

	// 发送后续的控制命令
	lRet = WriteCmdToDevice(pDevLink, szCurCmd, strlen(szCurCmd));
	if(lRet != VIDEO_SUCCESS)
		return lRet;
	
	return lRet;
}

/**
 *  切换视频到监视器.
 *
 *  @param  -in  long  nDevID: 设备ID
 *  @param  -in  long  nCamChannel: 摄像头所在矩阵的通道
 *  @param  -in  long  nMonChannel: 监视器所在矩阵的通道
 *
 *  @version     07/10/2008  liyang  Initial Version.
 */
long CADMatrixDriver::SwitchCameratoMonitor(long nDevID, long nCamChannel, long nMonChannel)
{
	// 先根据IP和端口连接设备, 或找到设备指针
	DEVLINKINFO *pDevLink = NULL;
	long lRet = GetDeviceLink(nDevID, pDevLink);
	if(lRet != VIDEO_SUCCESS)
		return lRet;

	// 设置为不是连续控制类型
	pDevLink->bIsContinueCtrl = false;	
	memset(pDevLink->szCurCmdBuff, 0, sizeof(pDevLink->szCurCmdBuff));
		
	// 开始控制动作
	char szCurCmd[MATRIX_COMMAND_MAX_SIZE];
	memset(szCurCmd, 0, sizeof(szCurCmd));
	
	// 设置当前控制的通道号
    sprintf(szCurCmd,"%dMa%d#a",nMonChannel,nCamChannel);
	
	// 发送命令给设备. 如果失败则直接返回, 不需要继续执行后续的控制命令
	lRet = WriteCmdToDevice(pDevLink, szCurCmd, strlen(szCurCmd));
	if(lRet != VIDEO_SUCCESS)
		return lRet;

	return lRet;
}

/**
 * 定时器到来时调用，以便发送需要持续发送的命令。这里的命令指云台控制
 *
 *
 *  @version     07/10/2008  liyang  Initial Version.
 */

long CADMatrixDriver::TimerFunc()
{
	long lRet = VIDEO_SUCCESS;

	for(vector<DEVLINKINFO>::iterator itDevLink = m_deviceLinks.begin(); itDevLink != m_deviceLinks.end(); itDevLink ++)
	{
		DEVLINKINFO &pDevLink = (*itDevLink);
		//if(!pDevLink)
		//	continue;

		time_t tmNow;
		time(&tmNow);
		if (5 < abs((int)(tmNow-pDevLink.lLastCtrlTime)))
		{
			pDevLink.sockToMatrix.close();
		}

		if(!pDevLink.bIsContinueCtrl)
			continue;

		// 如果是连续控制命令, 则继续进行控制动作
		lRet = WriteCmdToDevice(&pDevLink, pDevLink.szCurCmdBuff, strlen(pDevLink.szCurCmdBuff));
	}
	
	return lRet;
}

/**
 *  关闭所有设备的连接.
 *
 *
 *  @version     07/10/2008  liyang  Initial Version.
 */
void CADMatrixDriver::CloseAllDevice()
{
	for(vector<DEVLINKINFO>::iterator itDevLink = m_deviceLinks.begin(); itDevLink != m_deviceLinks.end(); itDevLink ++)
	{
		DEVLINKINFO &pDevLink = *itDevLink;
		//if(!pDevLink)
		//	continue;

		pDevLink.sockToMatrix.close ();
		//if(pDevLink)
		//{
		//	delete pDevLink;
		//	pDevLink = NULL;
		//}
	}
	
	m_deviceLinks.clear();	
}

/**
 *  获取与设备的连接信息.
 *
 *  @param  -in   long nDevID: 需要连接的视频设备ID号
 *  @param  -out  DEVLINKINFO  pDevLink: 连接信息
 *
 *  @version     07/10/2008  liyang  Initial Version.
 */
long  CADMatrixDriver::GetDeviceLink(long nDevID, DEVLINKINFO *&pCurDevLink)
{
	for(vector<DEVLINKINFO>::iterator itDevLink = m_deviceLinks.begin(); itDevLink != m_deviceLinks.end(); itDevLink++)
	{
		if(nDevID == itDevLink->nDevID)
		{			
			pCurDevLink = &(*itDevLink);
			return VIDEO_SUCCESS;
		}
	}

	return EC_ICV_CCTV_FAILITOCONNDEV;
}


/**
 *  获取与设备的连接信息.
 *
 *  @param  -out  DEVLINKINFO  pDevLink: 连接信息
 *
 *  @version     07/10/2008  liyang  Initial Version.
 */
long  CADMatrixDriver::ConnectDevice(DEVLINKINFO *pCurDevLink, bool bForceConnect)
{
	// 短期内不需要重新连接, 以避免滚轮滚动连接不上时不断重连
	time_t tmNow;
	time(&tmNow);

	long lRet = VIDEO_SUCCESS;
	// 一定间隔内不需要重连
	if(abs((int)(tmNow - pCurDevLink->lLastConnTime)) < MIN_RECONNECT_TIME_INTERVAL)
	{
		lRet = EC_ICV_CCTV_FAILITOCONNDEV;
		CVLog.LogMessage(LOG_LEVEL_INFO, _("Connect to device(%s:%d) failed, and reconnect in %d seconds, that will be stop, RetCode:%d"),
					pCurDevLink->szIP, pCurDevLink->uPort, MIN_RECONNECT_TIME_INTERVAL, lRet);
		return lRet;
	}

	// 设定本次重连时间
	pCurDevLink->lLastConnTime = tmNow;

	// 与服务器建立连接
	ACE_Time_Value timeout(1);   // 获取连接超时, 1second
	// ACE_Synch_Options synch_option(ACE_Synch_Options::USE_TIMEOUT, timeout);
	ACE_INET_Addr remote_addr_(pCurDevLink->uPort, pCurDevLink->szIP);

	ACE_SOCK_Connector connector;
	int nConn = connector.connect(pCurDevLink->sockToMatrix, remote_addr_, &timeout);
	if(nConn == -1) // 连接失败则返回失败
		lRet = EC_ICV_CCTV_FAILITOCONNDEV;

	CVLog.LogMessage(LOG_LEVEL_INFO, _("Connect to device(%s:%d), RetCode:%d"),
				pCurDevLink->szIP, pCurDevLink->uPort, lRet);
	return lRet;
}


/**
 *  发送控制命令给设
 *
 *  @param  -in  void*  arg: 无
 *
 *  @version     07/10/2008  liyang  Initial Version.
 */
long CADMatrixDriver::WriteCmdToDevice(DEVLINKINFO *pCurDevLink, char *szCmdBuf, long lCmdBufSize)
{
	if(lCmdBufSize <= 0)
		return EC_ICV_CCTV_ERRLENOFBUFF;

	long lRet = VIDEO_SUCCESS;
	int nSentBytes = pCurDevLink->sockToMatrix.send_n(szCmdBuf, lCmdBufSize);
	if(nSentBytes == -1) // 如果发送失败, 则尝试重连一次
	{
		// 再重连一把
		lRet = ConnectDevice(pCurDevLink, false);
		if(lRet != VIDEO_SUCCESS)
			return lRet;

		// 重连成功重新发送
		nSentBytes = pCurDevLink->sockToMatrix.send_n(szCmdBuf, lCmdBufSize);
		time_t tmNow;
		time(&tmNow);
		pCurDevLink->lLastCtrlTime = tmNow;
	}

	CVLog.LogMessage(LOG_LEVEL_INFO, _("WriteCmdToDevice(), send to device(%s:%d) information(%s), retrun length:%d"), 
							pCurDevLink->szIP, pCurDevLink->uPort, szCmdBuf, nSentBytes);
	
	if(nSentBytes == lCmdBufSize)
		return VIDEO_SUCCESS;
	
	return EC_ICV_CCTV_ERRLENOFBUFF;
}
