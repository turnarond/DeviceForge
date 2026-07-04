/**************************************************************
 *  Filename:    ADMatrixDriver.h
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: AD矩阵头文件.
 *
 *  @author:     liyang
 *  @version     07/10/2008  liyang  Initial Version
**************************************************************/
// ADMatrixDriver.h: interface for the CADMatrixDriver class.
//
//////////////////////////////////////////////////////////////////////
#ifndef _ADRVE_HEADER_H_
#define _ADRVE_HEADER_H_

#include "ace/SOCK_Connector.h"
#include "ace/INET_Addr.h"
#include "multimedia/video/VideoDefine.h"
#include "common/cvdefine.h"

#include<vector>
using namespace std;

#define MATRIX_COMMAND_MAX_SIZE		256		// 矩阵控制命令的最大尺寸

// 错误码

// Matrix 相关
#define EC_ICV_CCTV_MATRIX_BASENO				0
#define VIDEO_SUCCESS							EC_ICV_CCTV_MATRIX_BASENO

#define EC_ICV_CCTV_FAILITOCONNDEV				EC_ICV_CCTV_MATRIX_BASENO + 1  // 连接设备失败
#define EC_ICV_CCTV_ERRLENOFBUFF				EC_ICV_CCTV_MATRIX_BASENO + 2  // 缓冲区数据长度异常
#define EC_ICV_CCTV_UNKNOWNCMD					EC_ICV_CCTV_MATRIX_BASENO + 3  // 未知的镜头控制命令
#define EC_ICV_CCTV_MATRIXINITDRIVERERR			EC_ICV_CCTV_MATRIX_BASENO + 4  // 初始化驱动失败
#define EC_ICV_CCTV_MATRIXEXITDRIVERERR			EC_ICV_CCTV_MATRIX_BASENO + 5  // 退出驱动失败
#define EC_ICV_CCTV_MATRIXLENSCONTROLERR		EC_ICV_CCTV_MATRIX_BASENO + 6  // 镜头控制失败
#define EC_ICV_CCTV_MATRIXPSPCONTROLERR			EC_ICV_CCTV_MATRIX_BASENO + 7  // 预置位控制失败
#define EC_ICV_CCTV_MATRIXPANCONTROLERR			EC_ICV_CCTV_MATRIX_BASENO + 8  // 云台控制失败
#define EC_ICV_CCTV_MATRIXSWITCHCONTROLERR		EC_ICV_CCTV_MATRIX_BASENO + 9  // 监视器视频切换失败
#define EC_ICV_CCTV_MATRIXAUXICONTROLERR		EC_ICV_CCTV_MATRIX_BASENO + 10 // 附加设备控制失败
#define EC_ICV_CCTV_MATRIXTIMERRUNERR			EC_ICV_CCTV_MATRIX_BASENO + 11 // 定时器调用失败
#define EC_ICV_CCTV_MATRIXGETTIMERERR			EC_ICV_CCTV_MATRIX_BASENO + 12 // 获取定时周期失败
#define EC_ICV_CCTV_NOTIMPLEMENTED				EC_ICV_CCTV_MATRIX_BASENO + 13 // 未实现功能


// 同每一个矩阵设备的连接状态信息
typedef struct tagDEVLINKINFO
{	
	// KEY(IP,PORT)
	char	szIP[ICV_IPSTRING_MAXLEN];
	unsigned short uPort;
	long	nDevID;
	ACE_SOCK_Stream sockToMatrix;	// 连接矩阵的命令号, 每个设备一个
	long	lLastConnTime;			// 上次连接的时间(无论成功与否,用于判断短期内是否需要重连)
	long	lLastCtrlTime;			// 上次控制的时间

	bool	bIsContinueCtrl;		// 是否是连续发送命令给设备的控制类型
	long	lCmdLeftCount;			// 连续命令剩余的次数
	char	szCurCmdBuff[MATRIX_COMMAND_MAX_SIZE];
	tagDEVLINKINFO()
	{
		memset(szIP, 0, sizeof(szIP));
		memset(szCurCmdBuff, 0, sizeof(szCurCmdBuff));
		uPort = 0;
		nDevID = 0;
		lLastConnTime = 0;
		bIsContinueCtrl = false;
		lCmdLeftCount = 0;
		lLastCtrlTime = 0;
	}
}DEVLINKINFO;


class CADMatrixDriver
{
public:
	CADMatrixDriver();
	virtual ~CADMatrixDriver();
private:
	vector<DEVLINKINFO>	m_deviceLinks;	//设备连接信息列表
private:
	void	CloseAllDevice();				//关闭所有设备连接
	long	GetDeviceLink(long nDevID, DEVLINKINFO *&pDevLink);							// 初始化设备连接
	long	WriteCmdToDevice(DEVLINKINFO *pDevLink, char *szCmdBuf, long lCmdBufSize);	// 发送控制命令给设备
	long	ConnectDevice(DEVLINKINFO *pCurDevLink, bool bForceConnect = false);
public:
	long	InitDrive();			//初始化驱动
	long	ExitDriver();			//退出驱动
	long	Login(char* pszIP, long nIPSize, unsigned short nPort, char* pszExtent, long nExtentSize, long &nDevID);
	long	GetVersionNumber();		//获取版本号
	long	LensControl(long nDevID, long nCtrlChannel, long nChannel, long nControlCode, long nSpeed, long lElpaseTime);	//镜头控制
	long	PresetControl(long nDevID, long nCtrlChannel, long nChannel, long nControlCode, long nPresetIndex);			//预置位控制
	long	PanControl(long nDevID, long nCtrlChannel, long nChannel, long nControlCode, long nSpeed, long lElpaseTime);	//云台控制
	long	SwitchCameratoMonitor(long nDevID, long nCamChannel, long nMonChannel);						//监视器视频切换
	long	AuxControl(long nDevID, long nCtrlChannel, long nChannel, long nControlCode, long nSpeed);						// 附加设备控制
	long	TimerFunc();			// 定时器调用的函数
};


#endif // _ADRVE_HEADER_H_
