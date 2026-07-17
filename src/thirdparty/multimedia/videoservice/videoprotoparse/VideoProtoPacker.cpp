/**************************************************************
 *  Filename:    VideoProtocolPacker.cpp
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: 视频监控系统协议解析库实现文件.
 *
 *  @author:     xulizai
 *  @version     2008/05/16  xulizai  Initial Version
 *  @version     2009/04/17  chenzhan  添加摄像头自定义分区和排序的打包函数
 *  @version     2009/05/04	 zhucongfeng	添加根据分区编号获取自定义分区摄像头
 *  @version     2009/05/31	 zhucongfeng	添加组织抓拍编号
 *  @version     2009/06/01	 zhucongfeng	修改压缩抓拍图片编号的接口
 *  @version     2009/06/09	 zhucongfeng	添加删除抓拍图片的消息组织
 *  @version     2009/06/11	 zhucongfeng	获取抓拍图片内容时删除参数摄像头编号
 *  @version     2009/06/17	 chenzhan	服务端协议打包函数组织
 			     09/08/2009  chenzhan     添加录像抓拍、图片累加功能
				 14/04/2010  chenzhan  增加设备级连控制服务端返回给客户端的编解码器列表
**************************************************************/
#include "multimedia/video/VideoProtoPacker.h"
#include "errcode/ErrCode_iCV_CCTV.hxx"
#include "stdio.h"

#ifndef _WIN32
#define memcpy_s(__dest__,__num__,__src__,__count__) memcpy(__dest__,__src__,__count__)
#define sprintf_s snprintf
#endif

using namespace std;

#define CMD_HEADER_MAX_SIZE		81
#define RET_HEADER_MAX_SIZE		RETCMD_HEADERINFO_LENGTH
#define CMD_MESSAGE_MAX_SIZE	(VIDEO_MESSAGE_MAX_SIZE-CMD_HEADER_MAX_SIZE)
#define RET_MESSAGE_MAX_SIZE	(VIDEO_MESSAGE_MAX_SIZE-RET_HEADER_MAX_SIZE)
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
CVideoProtoPacker::CVideoProtoPacker()
{
}

CVideoProtoPacker::~CVideoProtoPacker()
{

}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdHeader(char* pszBuff, long nInLen, long nCmd, long nStampID, char* pszUserName, char* pszGroup, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == pszUserName) || (NULL == pszGroup) || (nInLen < CMD_HEADER_MAX_SIZE))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    nOutLen = sprintf_s(pszBuff,nInLen,"%03d%010d%02d%s%02d%s", nCmd, nStampID, strlen(pszUserName), pszUserName, strlen(pszGroup), pszGroup);
    
    return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackRetHeader(char* pszBuff, long nInLen, long nCmd, long nStampID, long nRetCode, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (0 >= nCmd) || (0 >= nStampID) || (nInLen < RET_HEADER_MAX_SIZE))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    nOutLen = sprintf_s(pszBuff,nInLen, "%03d%010d%010d", nCmd, nStampID, nRetCode);
    
    return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackRetGetAllConfigInfo(char* pszBuff, long nInLen, CVideoProductList *plistProduct, CVideoDeviceList *plistDev, 
												CVideoZoneList *plistZone, CVideoCameraList *plistCam, CVideoMonitorList *plistMon, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == plistProduct) || (NULL == plistDev) || (NULL == plistZone) || (NULL == plistCam) || (NULL == plistMon))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    long nTempLen = 0;
    
    lRet = PackRetGetProductInfo(pszBuff+nOutLen, nInLen, plistProduct, nTempLen);
    if (ICV_SUCCESS != lRet)
        return lRet;

    nOutLen += nTempLen;
    nTempLen = 0;

    lRet = PackRetGetDevInfo(pszBuff+nOutLen, nInLen, plistDev, nTempLen);
    if (ICV_SUCCESS != lRet)
        return lRet;
    
    nOutLen += nTempLen;
    nTempLen = 0;

    lRet = PackRetGetZoneInfo(pszBuff+nOutLen, nInLen, plistZone, nTempLen);
    if (ICV_SUCCESS != lRet)
        return lRet;
    
    nOutLen += nTempLen;
    nTempLen = 0;

    lRet = PackRetGetCamInfo(pszBuff+nOutLen, nInLen, plistCam, nTempLen);
    if (ICV_SUCCESS != lRet)
        return lRet;
    
    nOutLen += nTempLen;
    nTempLen = 0;

    lRet = PackRetGetMonInfo(pszBuff+nOutLen, nInLen, plistMon, nTempLen);
    if (ICV_SUCCESS != lRet)
        return lRet;
    
    nOutLen += nTempLen;
    nTempLen = 0;
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackRetGetZoneInfo(char* pszBuff, long nInLen, CVideoZoneList* plistZone, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == plistZone))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    lRet = PackZoneListToBuffer(plistZone, pszBuff, nInLen, &nOutLen);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackRetGetProductInfo(char* pszBuff, long nInLen, CVideoProductList* plistProduct, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == plistProduct))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    lRet = PackProductListToBuffer(plistProduct, pszBuff, nInLen, &nOutLen);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackRetGetDevInfo(char* pszBuff, long nInLen, CVideoDeviceList* plistDev, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == plistDev))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    lRet = PackDeviceListToBuffer(plistDev, pszBuff, nInLen, &nOutLen);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackRetGetCamInfo(char* pszBuff, long nInLen, CVideoCameraList* plistCam, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == plistCam))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;

    lRet = PackCameraListToBuffer(plistCam, pszBuff, nInLen, &nOutLen);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackRetGetSimpleCamInfo(char* pszBuff, long nInLen, CVideoCameraList* plistCam, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == plistCam))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = EC_ICV_CCTV_FUNCPARAMINVALID;
    
    lRet = PackSimpleCameraListToBuffer(plistCam, pszBuff, nInLen, &nOutLen);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdGetZoneCam(char* pszBuff, long nInLen, long nZoneID, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    nOutLen = sprintf_s(pszBuff,nInLen,"%04d", nZoneID);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackRetGetZoneCam(char* pszBuff, long nInLen, CVideoCameraList* plistCam, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == plistCam))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    lRet = PackCameraListToBuffer(plistCam, pszBuff, nInLen, &nOutLen);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdGetZoneSimpleCam(char* pszBuff, long nInLen, long nZoneID, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = EC_ICV_CCTV_FUNCPARAMINVALID;
    
    nOutLen = sprintf_s(pszBuff,nInLen,"%04d", nZoneID);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackRetGetZoneSimpleCam(char* pszBuff, long nInLen, CVideoCameraList* plistCam, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == plistCam))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = EC_ICV_CCTV_FUNCPARAMINVALID;
    
    lRet = PackSimpleCameraListToBuffer(plistCam, pszBuff, nInLen, &nOutLen);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdGetCamByName(char* pszBuff, long nInLen, char* pszCamName, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == pszCamName))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = EC_ICV_CCTV_FUNCPARAMINVALID;
    
    nOutLen = sprintf_s(pszBuff,nInLen, "%02d%s", strlen(pszCamName), pszCamName);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackRetGetCamByName(char* pszBuff, long nInLen, CVideoCameraList* plistCam, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == plistCam))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = EC_ICV_CCTV_FUNCPARAMINVALID;
    
    lRet = PackCameraListToBuffer(plistCam, pszBuff, nInLen, &nOutLen);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackRetGetMonInfo(char* pszBuff, long nInLen, CVideoMonitorList* plistMon, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == plistMon))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    lRet = PackMonitorListToBuffer(plistMon, pszBuff, nInLen, &nOutLen);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdGetZoneMon(char* pszBuff, long nInLen, long nZoneID, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    nOutLen = sprintf_s(pszBuff,nInLen, "%04d", nZoneID);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackRetGetZoneMon(char* pszBuff, long nInLen, CVideoMonitorList* plistMon, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == plistMon))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    lRet = PackMonitorListToBuffer(plistMon, pszBuff, nInLen, &nOutLen);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdGetAllModeName(char* pszBuff, long nInLen, long nTime, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    nOutLen = sprintf_s(pszBuff,nInLen, "%010d", nTime);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackRetGetAllModeName(char* pszBuff, long nInLen, long nTime, CVideoModeList* plistMode, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == plistMode))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    nOutLen = sprintf_s(pszBuff,nInLen, "%010d", nTime);

    long nTempSize = 0;
    lRet = PackModeListToBuffer(plistMode, pszBuff+nOutLen, nInLen, &nTempSize, false);

    nOutLen += nTempSize;
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdAddMode(char* pszBuff, long nInLen, CVideoMode* ptheMode, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == ptheMode))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    lRet = PackModeToBuffer(ptheMode, pszBuff, nInLen, &nOutLen, true);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdDelMode(char* pszBuff, long nInLen, CVideoModeList* plistMode, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == plistMode))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    long nlistLen = plistMode->GetSize();

    nOutLen =sprintf_s(pszBuff,nInLen, "%04d", nlistLen);

    for (int i=0; i<nlistLen; i++)
    {
        CVideoMode *pMode = plistMode->GetAt(i);
        if (NULL == pMode)
            return EC_ICV_CCTV_FAILTOPARSEMODEBUFF;

        nOutLen += sprintf_s(pszBuff+nOutLen,nInLen-nOutLen, "%02d%s", strlen(pMode->GetName()), pMode->GetName());
    }
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackRetDelMode(char* pszBuff, long nInLen, CVideoModeList* plistMode, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == plistMode))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    long nlistLen = plistMode->GetSize();
    
    nOutLen =sprintf_s(pszBuff,nInLen, "%04d", nlistLen);
    
    for (int i=0; i<nlistLen; i++)
    {
        CVideoMode *pMode = plistMode->GetAt(i);
        if (NULL == pMode)
            return EC_ICV_CCTV_FAILTOPARSEMODEBUFF;

        nOutLen += sprintf_s(pszBuff+nOutLen,nInLen-nOutLen, "%02d%s", strlen(pMode->GetName()), pMode->GetName());
    }
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdModifyMode(char* pszBuff, long nInLen, char *pszModeName, CVideoMode* ptheMode, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == pszModeName) || (NULL == ptheMode))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    nOutLen =sprintf_s(pszBuff,nInLen, "%02d%s", strlen(pszModeName), pszModeName);

    long nTempSize = 0;
    lRet = PackModeToBuffer(ptheMode, pszBuff+nOutLen, nInLen, &nTempSize, true);

    nOutLen += nTempSize;
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdGetModeLayout(char* pszBuff, long nInLen, char *pszModeName, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == pszModeName))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    nOutLen =sprintf_s(pszBuff,nInLen, "%02d%s", strlen(pszModeName), pszModeName);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackRetGetModeLayout(char* pszBuff, long nInLen, CVideoMode* ptheMode, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == ptheMode))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    nOutLen =sprintf_s(pszBuff,nInLen, "%04d%s", strlen(ptheMode->GetPara()), ptheMode->GetPara());
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdGetCamPSPInfo(char* pszBuff, long nInLen, long nCamID, long nTime, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    nOutLen =sprintf_s(pszBuff,nInLen, "%06d%010d", nCamID, nTime);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackRetGetCamPSPInfo(char* pszBuff, long nInLen, long nCamID, long nTime, CVideoPSPList* plistPSP, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == plistPSP))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    nOutLen =sprintf_s(pszBuff,nInLen, "%06d%010d", nCamID, nTime);

    long nTempSize = 0;
    lRet = PackPSPListToBuffer(plistPSP, pszBuff+nOutLen, nInLen, &nTempSize);

    nOutLen += nTempSize;
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdAddPSP(char* pszBuff, long nInLen, long nCamID, long nTime, CVideoPSP *pthePSP, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == pthePSP))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    nOutLen =sprintf_s(pszBuff,nInLen, "%06d%010d", nCamID, nTime);
    
    long nTempSize = 0;
    lRet = PackPSPToBuffer(pthePSP, pszBuff+nOutLen, nInLen, &nTempSize);
    
    nOutLen += nTempSize;
    
	return lRet;
}

// 说明参见.h文件
//2012.5.6 zhucongfeng 无使用，而且是错误的，打包与解包不一致。目前打包和解包直接写在VideoRAPI和VideoService中了
/*long CVideoProtoPacker::PackCmdDelPSP(char* pszBuff, long nInLen, long nCamID, CVideoPSPList *plistPSP, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == plistPSP))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    long nlistLen = plistPSP->GetSize();
    
    nOutLen =sprintf_s(pszBuff,nInLen, "%06d%02d", nCamID, nlistLen);
    
    for (int i=0; i<nlistLen; i++)
    {
        CVideoPSP *pPSP = plistPSP->GetAt(i);
        if (NULL == pPSP)
            return EC_ICV_CCTV_FAILTOPARSEMODEBUFF;

        nOutLen += sprintf_s(pszBuff+nOutLen,nInLen-nOutLen, "%02d%s", strlen(pPSP->GetName()), pPSP->GetName());
    }
    
	return lRet;
}*/

// 说明参见.h文件
//2012.5.6 zhucongfeng 无使用，而且是错误的，打包与解包不一致。目前打包和解包直接写在VideoRAPI和VideoService中了
/*long CVideoProtoPacker::PackRetDelPSP(char* pszBuff, long nInLen, CVideoPSPList *plistPSP, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == plistPSP))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    long nlistLen = plistPSP->GetSize();
    
    nOutLen =sprintf_s(pszBuff,nInLen, "%02d", nlistLen);
    
    for (int i=0; i<nlistLen; i++)
    {
        CVideoPSP *pPSP = plistPSP->GetAt(i);
        if (NULL == pPSP)
            return EC_ICV_CCTV_FAILTOPARSEMODEBUFF;

        nOutLen += sprintf_s(pszBuff+nOutLen,nInLen-nOutLen, "%02d%s", strlen(pPSP->GetName()), pPSP->GetName());
    }
    
	return lRet;
}*/

// 说明参见.h文件
long CVideoProtoPacker::PackCmdModifyPSP(char* pszBuff, long nInLen, long nCamID, char *pszPSPName, CVideoPSP *pthePSP, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == pszPSPName) || (NULL == pthePSP))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
 
	//2012.5.6，按照目前的功能，向数据库中更新时，只更新名称和描述，但是由于解析是与添加预置位用的同一个方法，这里也把预置位需要添加进去，以备后用
    nOutLen =sprintf_s(pszBuff,nInLen, "%06d%02d%s%02d%s%02d%s%03d", nCamID, strlen(pszPSPName), pszPSPName, strlen(pthePSP->GetName()), pthePSP->GetName(),
                        strlen(pthePSP->GetDescription()), pthePSP->GetDescription(), pthePSP->GetPresetIndex());
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdAppPSP(char* pszBuff, long nInLen, long nCamID, char *pszPSPName, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == pszPSPName))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    nOutLen =sprintf_s(pszBuff,nInLen, "%06d%02d%s", nCamID, strlen(pszPSPName), pszPSPName);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackRetGetSnapType(char* pszBuff, long nInLen, CIDMapTextList *plistSnapType, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == plistSnapType))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    long nlistLen = plistSnapType->GetSize();
    
    nOutLen =sprintf_s(pszBuff,nInLen, "%02d", nlistLen);
    
    for (int i=0; i<nlistLen; i++)
    {
        CIDMapText *pSnapType = plistSnapType->GetAt(i);
        if (NULL == pSnapType)
            return EC_ICV_CCTV_FAILTOPARSEMODEBUFF;

        nOutLen += sprintf_s(pszBuff+nOutLen,nInLen-nOutLen, "%02d%s", strlen(pSnapType->GetText()), pSnapType->GetText());
    }
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdQuerySnapInfo(char* pszBuff, long nInLen, long nCamID, long nStart, long nEnd, char *pszSnapType, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == pszSnapType))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    nOutLen =sprintf_s(pszBuff,nInLen, "%06d%010d%010d%02d%s", nCamID, nStart, nEnd, strlen(pszSnapType), pszSnapType);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackRetQuerySnapInfo(char* pszBuff, long nInLen, CVideoSnapFileList *plistSnapInfo, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == plistSnapInfo))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    lRet = PackSnapFileListToBuffer(plistSnapInfo, pszBuff, nInLen, &nOutLen);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdGetPicBuffer(char* pszBuff, long nInLen, long nSnapID, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    nOutLen =sprintf_s(pszBuff,nInLen, "%06d", nSnapID);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdAddPicInfo(char* pszBuff, long nInLen, CVideoSnapFile *ptheSnapInfo, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == ptheSnapInfo))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    lRet = PackSnapFileToBuffer(ptheSnapInfo, pszBuff, nInLen, &nOutLen);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackRetAddPicInfo(char* pszBuff, long nInLen, long nSnapID, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    nOutLen =sprintf_s(pszBuff,nInLen, "%06d", nSnapID);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdDelSnapInfo(char* pszBuff, long nInLen, CVideoSnapFileList *plistSnapInfo, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == plistSnapInfo))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    long nlistLen = plistSnapInfo->GetSize();
    
    nOutLen =sprintf_s(pszBuff,nInLen, "%06d", nlistLen);
    
    for (int i=0; i<nlistLen; i++)
    {
        CVideoSnapFile *pSnapInfo = plistSnapInfo->GetAt(i);
        if (NULL == pSnapInfo)
            return EC_ICV_CCTV_FAILTOPARSEMODEBUFF;

        nOutLen += sprintf_s(pszBuff+nOutLen,nInLen-nOutLen, "%06d", pSnapInfo->GetID());
    }
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdModifySnapInfo(char* pszBuff, long nInLen, long nSnapID, CVideoSnapFile *ptheSnapInfo, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == ptheSnapInfo))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    nOutLen =sprintf_s(pszBuff,nInLen, "%06d%02d%s%03d%s%02d%s", nSnapID, strlen(ptheSnapInfo->GetInfoType()), ptheSnapInfo->GetInfoType(), strlen(ptheSnapInfo->GetExtent()), 
                    ptheSnapInfo->GetExtent(), strlen(ptheSnapInfo->GetDescription()), ptheSnapInfo->GetDescription());
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdSwitchByID(char* pszBuff, long nInLen, long nCamID, long nMonID, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    nOutLen =sprintf_s(pszBuff,nInLen, "%06d%04d", nCamID, nMonID);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdSwitchByName(char* pszBuff, long nInLen, char *pszCamName, char *pszMonName, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (NULL == pszCamName) || (NULL == pszMonName))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    nOutLen =sprintf_s(pszBuff,nInLen, "%02d%s%02d%s", strlen(pszCamName), pszCamName, strlen(pszMonName), pszMonName);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdLensControl(char* pszBuff, long nInLen, long nCamID, long nCtrCode, long nSpeed, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    nOutLen =sprintf_s(pszBuff,nInLen, "%06d%01d%01d", nCamID, nCtrCode, nSpeed);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdAuxControl(char* pszBuff, long nInLen, long nCamID, long nCtrCode, long nSpeed, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    nOutLen =sprintf_s(pszBuff,nInLen, "%06d%01d%01d", nCamID, nCtrCode, nSpeed);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdPanControl(char* pszBuff, long nInLen, long nCamID, long nCtrCode, long nSpeed, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    nOutLen =sprintf_s(pszBuff,nInLen, "%06d%01d%01d", nCamID, nCtrCode, nSpeed);
    
	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackRetGetCustZone(char* pszBuff, long nInLen, CVideoCustomZoneList* plistCustZone, long &nOutLen)
{
	// 入口检测
	if ((NULL == pszBuff) || (NULL == plistCustZone) || (nInLen < RET_MESSAGE_MAX_SIZE))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = ICV_SUCCESS;
	
	long nLen =sprintf_s(pszBuff,nInLen, "%04d", plistCustZone->GetSize());
	for (int i=0; i<plistCustZone->GetSize(); i++)
	{
		nLen += sprintf_s(pszBuff+nLen,nInLen-nLen, "%04d%02d%s%02d%s%02d%s", plistCustZone->GetAt(i)->GetID(),
			strlen(plistCustZone->GetAt(i)->GetName()), plistCustZone->GetAt(i)->GetName(), 
			strlen(plistCustZone->GetAt(i)->GetRelateUserName()), plistCustZone->GetAt(i)->GetRelateUserName(),
			strlen(plistCustZone->GetAt(i)->GetDesc()), plistCustZone->GetAt(i)->GetDesc());
	}

	nOutLen = nLen;

	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdAddCustZone(char* pszBuff, long nInLen, char* pszCustZoneName, char* pszCustZoneDesc, long &nOutLen)
{
	// 入口检测
	if ((NULL == pszBuff) || (NULL == pszCustZoneName) || (nInLen < CMD_MESSAGE_MAX_SIZE))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = ICV_SUCCESS;

	nOutLen =sprintf_s(pszBuff,nInLen, "%02d%s%02d%s", strlen(pszCustZoneName), pszCustZoneName, strlen(pszCustZoneDesc), pszCustZoneDesc);

	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdDelCustZone(char* pszBuff, long nInLen, char* pszCustZoneName, long &nOutLen)
{
	// 入口检测
	if ((NULL == pszBuff) || (NULL == pszCustZoneName) || (nInLen < CMD_MESSAGE_MAX_SIZE))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = ICV_SUCCESS;

	nOutLen =sprintf_s(pszBuff,nInLen, "%02d%s", strlen(pszCustZoneName), pszCustZoneName);

	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdGetCustZoneCam(char* pszBuff, long nInLen, char* pszCustZoneName, long &nOutLen)
{
	// 入口检测
	if ((NULL == pszBuff) || (NULL == pszCustZoneName) || (nInLen < CMD_MESSAGE_MAX_SIZE))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = ICV_SUCCESS;

	nOutLen =sprintf_s(pszBuff,nInLen, "%02d%s", strlen(pszCustZoneName), pszCustZoneName);

	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdGetCustZoneCam(char* pszBuff, long nInLen, long lCustZoneID, long &nOutLen)
{
	// 入口检测
	if ((NULL == pszBuff) || (lCustZoneID<=0))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = ICV_SUCCESS;

	nOutLen =sprintf_s(pszBuff,nInLen, "%04d", lCustZoneID);

	return lRet;
}

long CVideoProtoPacker::PackSnapPicID(char* pszBuff, long nInLen, long lSnapID, long &nOutLen)
{
	// 入口检测
	if ((NULL == pszBuff) || (lSnapID<=0))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = ICV_SUCCESS;

	nOutLen =sprintf_s(pszBuff,nInLen, "%06d", lSnapID);

	return lRet;
}

long CVideoProtoPacker::PackDelSnapPic(char* pszBuff, long nInLen, long lCamID, long lSnapID, long &nOutLen)
{
	// 入口检测
	if ((NULL == pszBuff) || (lSnapID<=0) || (lCamID<=0) )
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = ICV_SUCCESS;

	nOutLen =sprintf_s(pszBuff,nInLen, "%06d%06d%06d", 1, lCamID, lSnapID);

	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackRetGetCustZoneCam(char* pszBuff, long nInLen, CVideoCameraList *plistCustZoneCam, long &nOutLen)
{
	// 入口检测
	if ((NULL == pszBuff) || (NULL == plistCustZoneCam) || (nInLen < RET_MESSAGE_MAX_SIZE))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = ICV_SUCCESS;

	VIDEO_CHECKFAIL_RETURN(PackCameraListToBuffer(plistCustZoneCam, pszBuff, nInLen, &nOutLen));

	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdCpyCustZoneCam(char* pszBuff, long nInLen, char* pszCustZoneName, CVideoList<CStrName> *plistCustZoneCamName, long &nOutLen)
{
	// 入口检测
	if ((NULL == pszBuff) || (NULL == pszCustZoneName) || (NULL == plistCustZoneCamName) || (nInLen < CMD_MESSAGE_MAX_SIZE))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lRet = ICV_SUCCESS;

	long nLen =sprintf_s(pszBuff,nInLen, "%02d%s%06d", strlen(pszCustZoneName), pszCustZoneName, plistCustZoneCamName->GetSize());
	for (int i=0; i<plistCustZoneCamName->GetSize(); i++)
	{
		nLen += sprintf_s(pszBuff+nLen,nInLen-nLen, "%02d%s", 
			strlen(plistCustZoneCamName->GetAt(i)->m_szName), plistCustZoneCamName->GetAt(i)->m_szName);
	}

	nOutLen = nLen;

	return lRet;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdDelCustZoneCam(char* pszBuff, long nInLen, char* pszCustZoneName, CVideoList<CStrName> *plistCustZoneCamName, long &nOutLen)
{
	// 入口检测
	if ((NULL == pszBuff) || (NULL == pszCustZoneName) || (NULL == plistCustZoneCamName) || (nInLen < CMD_MESSAGE_MAX_SIZE))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long nLen =sprintf_s(pszBuff,nInLen, "%02d%s%06d", strlen(pszCustZoneName), pszCustZoneName, plistCustZoneCamName->GetSize());
	for (int i=0; i<plistCustZoneCamName->GetSize(); i++)
	{
		nLen += sprintf_s(pszBuff+nLen,nInLen-nLen, "%02d%s", 
			strlen(plistCustZoneCamName->GetAt(i)->m_szName), plistCustZoneCamName->GetAt(i)->m_szName);
	}

	nOutLen = nLen;

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdCapturePic(char* pszBuff, long nInLen, long nCaptureID, char* pPicBuf, long nPicSize, long &nOutLen)
{
	// 入口检测
	if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE+nPicSize) || (0 >= nCaptureID) || (NULL == pPicBuf) || (0 >= nPicSize))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	nOutLen =sprintf_s(pszBuff,nInLen, "%06d", nCaptureID);
	memcpy(pszBuff+nOutLen, pPicBuf, nPicSize);
	nOutLen += nPicSize;
	
	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdAddRecord(char* pszBuff, long nInLen, CRecord *pRec, char* pRecordBuf, long nRecSize, long &nOutLen)
{
	// 入口检测
	if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE+nRecSize) || (NULL == pRec) || (NULL == pRecordBuf))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	nOutLen =sprintf_s(pszBuff,nInLen, "%06d%010d%010d%03d%s%02d%s%02d%s%010d", pRec->GetCameraID(), pRec->GetStartTime(), pRec->GetEndTime()
		, strlen(pRec->GetExtent()), pRec->GetExtent(), strlen(pRec->GetFileName()), pRec->GetFileName(), strlen(pRec->GetDesc()), pRec->GetDesc(), nRecSize);
	memcpy(pszBuff+nOutLen, pRecordBuf, nRecSize);
	nOutLen += nRecSize;
	
	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdQueryRecord( char* pszBuff, long nInLen, long nCamID, time_t tStartTime, time_t tEndTime, long &nOutLen )
{
	// 入口检测
	if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (0 > nCamID) || (0 >= tStartTime) || (tStartTime >= tEndTime))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	nOutLen =sprintf_s(pszBuff,nInLen, "%06d%010d%010d", nCamID, tStartTime, tEndTime);
	
	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoPacker::PackRetQueryRecord(char* pszBuff, long nInLen, CRecordList* pListRecord, long &nOutLen)
{
	// 入口检测
	if ((NULL == pszBuff) || (NULL == pListRecord) || (nInLen < CMD_MESSAGE_MAX_SIZE))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long nCount = pListRecord->GetSize();
	long nLen =sprintf_s(pszBuff,nInLen, "%06d", nCount);
	for (int i=0; i<nCount; i++)
	{
		nLen += sprintf_s(pszBuff+nLen,nInLen-nLen, "%06d%06d%010d%010d%03d%s%02d%s%02d%s%06d%06d%06d", pListRecord->GetAt(i)->GetID(), pListRecord->GetAt(i)->GetCameraID(), 
			pListRecord->GetAt(i)->GetStartTime(), pListRecord->GetAt(i)->GetEndTime(), 
			strlen(pListRecord->GetAt(i)->GetExtent()), pListRecord->GetAt(i)->GetExtent(),
			strlen(pListRecord->GetAt(i)->GetFileName()), pListRecord->GetAt(i)->GetFileName(),
			strlen(pListRecord->GetAt(i)->GetDesc()), pListRecord->GetAt(i)->GetDesc(), 
			pListRecord->GetAt(i)->GetStartSnapID(), pListRecord->GetAt(i)->GetMidSnapID(), pListRecord->GetAt(i)->GetEndSnapID());
	}
	
	nOutLen = nLen;

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdModifyRecord(char* pszBuff, long nInLen, long nRecordID, CRecord *pRec, long &nOutLen)
{
	// 入口检测
	if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (0 >= nRecordID) || (NULL == pRec))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	nOutLen =sprintf_s(pszBuff,nInLen, "%06d%03d%s%02d%s%06d%06d%06d", nRecordID, strlen(pRec->GetExtent()), pRec->GetExtent(),
		strlen(pRec->GetDesc()), pRec->GetDesc(), pRec->GetStartSnapID(), pRec->GetMidSnapID(), pRec->GetEndSnapID());
	
	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdDownloadRecord(char* pszBuff, long nInLen, long nRecordID, long &nOutLen)
{
	// 入口检测
	if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (0 >= nRecordID))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	nOutLen =sprintf_s(pszBuff,nInLen, "%06d", nRecordID);
	
	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdDelRecord(char* pszBuff, long nInLen, long nRecordID, long &nOutLen)
{
	// 入口检测
	if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE) || (0 >= nRecordID))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	nOutLen =sprintf_s(pszBuff,nInLen, "%06d", nRecordID);

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoPacker::PackCmdVideoSwitch(char* pszBuff, long nInLen, long nCamID, long nMonID, long &nOutLen)
{
	// 入口检测
	if ((NULL == pszBuff) || (0 >= nCamID) || (0 >= nMonID))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	nOutLen =sprintf_s(pszBuff,nInLen, "%06d%04d", nCamID, nMonID);

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoPacker::PackRetVideoSwitch(char* pszBuff, long nInLen, CReSwitchChannelList* pListSwitch, long &nOutLen)
{
	// 入口检测
	if ((NULL == pszBuff) || (NULL == pListSwitch))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long nCount = pListSwitch->GetSize();
	nOutLen =sprintf_s(pszBuff,nInLen, "%04d", nCount);

	CReSwitchChannelList::iterator it = pListSwitch->begin();
	for(; it!=pListSwitch->end(); it++)
	{
		CVideoDevice *pDev = it->GetDev();
		long nLinkCount = 0;
		// 此处的数字要特别注意
		nOutLen += sprintf_s(pszBuff+nOutLen,nInLen-nOutLen, "%04d%04d%02d%s%02d%s%06d%03d%s%02d%s%04d%04d%s%04d%s", pDev->GetProductID(), pDev->GetID(), 
						strlen(pDev->GetName()), pDev->GetName(), 
						strlen(pDev->GetIP()), pDev->GetIP(),
						pDev->GetPort(), strlen(pDev->GetExtent()),
						pDev->GetExtent(), strlen(pDev->GetDescription()),
						pDev->GetDescription(), nLinkCount, 
                        strlen(it->GetInChannel()), it->GetInChannel(), 
                        strlen(it->GetOutChannel()), it->GetOutChannel());
		if(nOutLen >= nInLen)
			return EC_ICV_CCTV_PARSERBUFFERTOOSHORT;
	}

	return ICV_SUCCESS;
}




/**
 *  将输入的摄像头列表打包为缓冲区
 *
 *  @param  -[in] const bool bZone: 是否仅仅打包某个分区
 *  @param  -[in] long nZoneID: 分区ID(如果查所有分区则为-1)
 *  @param  -[in] CVideoCamera* pCamera: 摄像头
 *  @param  -[out] char* szOutBuf: 输出缓冲区
 *  @param  -[in] long lOutBufSize: 输出缓冲区的总长度
 *  @param  -[out] long* plActOutBufLen: 实际输出的缓冲区长度,从0开始计数
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 *  @version  10/07/2008  shijunpu  Initial Version.
 */
long CVideoProtoPacker::PackGeneralCameraToBuffer(CVideoCamera* pCamera, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen, 
													const bool bZone, const long nZoneID)
{
	long lLinkCount = pCamera->GetLinkDev()->GetSize();
	*plActOutBufLen = 0;
	
	char szCamInfo[VIDEO_CAMERA_INFO_MAXSIZE];
	memset(szCamInfo, 0, sizeof(szCamInfo));
	
	// 此处的数字要特别注意
	if(bZone)
	{
		// 某一分区下的摄像头
		sprintf_s(szCamInfo, VIDEO_CAMERA_INFO_MAXSIZE,"%06d%02d%s%02d%s%1d%1d%1d%1d%1d%1d%1d%1d%1d%1d%04d%04d%02d%04d", 
					pCamera->GetID(), strlen(pCamera->GetName()),
					pCamera->GetName(), strlen(pCamera->GetDescription()),
					pCamera->GetDescription(), pCamera->GetIsRemoteFileCtrl(),
					pCamera->GetIsLocalFileCtrl(), pCamera->GetIsRemoteTimeCtrl(),
					pCamera->GetIsOrientCtrl(), pCamera->GetIsPSPCtrl(),
					pCamera->GetIsLensCtrl(), pCamera->GetIsQualityCtrl(),
					pCamera->GetIsSnapCtrl(), pCamera->GetIsHeaterCtrl(),
					pCamera->GetIsRainBrushCtrl(), pCamera->GetSnapWidth(),
					pCamera->GetSnapHeight(), pCamera->GetSnapBitDeep(), lLinkCount);
	}
	else
	{
		// 系统下的摄像头
		sprintf_s(szCamInfo,VIDEO_CAMERA_INFO_MAXSIZE, "%04d%06d%02d%s%02d%s%1d%1d%1d%1d%1d%1d%1d%1d%1d%1d%04d%04d%02d%04d", 
					pCamera->GetZoneID(), pCamera->GetID(), 
					strlen(pCamera->GetName()), pCamera->GetName(), 
					strlen(pCamera->GetDescription()), pCamera->GetDescription(), 
					pCamera->GetIsRemoteFileCtrl(), pCamera->GetIsLocalFileCtrl(),
					pCamera->GetIsRemoteTimeCtrl(), pCamera->GetIsOrientCtrl(),
					pCamera->GetIsPSPCtrl(), pCamera->GetIsLensCtrl(),
					pCamera->GetIsQualityCtrl(), pCamera->GetIsSnapCtrl(),
					pCamera->GetIsHeaterCtrl(), pCamera->GetIsRainBrushCtrl(),
					pCamera->GetSnapWidth(), pCamera->GetSnapHeight(),
					pCamera->GetSnapBitDeep(), lLinkCount);
	}
	
	//if(*plActOutBufLen + strlen(szCamInfo) >= (unsigned long)lOutBufSize)
	//	return EC_ICV_CCTV_PARSERBUFFERTOOSHORT;

	if (szOutBuf == NULL)
	{
		*plActOutBufLen = strlen(szCamInfo);
		for(long j=0; j<lLinkCount; j++)
		{
			memset(szCamInfo, 0, sizeof(szCamInfo));
			CVideoLinkDevice* pLinkDev = pCamera->GetLinkDev()->GetAt(j);
			sprintf_s(szCamInfo,VIDEO_CAMERA_INFO_MAXSIZE, "%04d%04d%s%1d%1d%1d", pLinkDev->GetDeviceID(), strlen(pLinkDev->GetChannel()), 
				pLinkDev->GetChannel(), pLinkDev->GetIsCtrlDevice(), pLinkDev->GetIsVideoSourceDevice(), 
				pLinkDev->GetIsHistoryDevice());

			// 摄像头基本信息和已经添加的连接设备长度
			*plActOutBufLen += strlen(szCamInfo);
		}
		return ICV_SUCCESS;
	}

	// 摄像头基本信息的长度
	memcpy(szOutBuf, szCamInfo, strlen(szCamInfo));
	*plActOutBufLen = strlen(szCamInfo);
	
	// 连接设备
	for(long j=0; j<lLinkCount; j++)
	{
		memset(szCamInfo, 0, sizeof(szCamInfo));
		CVideoLinkDevice* pLinkDev = pCamera->GetLinkDev()->GetAt(j);
		sprintf_s(szCamInfo,VIDEO_CAMERA_INFO_MAXSIZE, "%04d%04d%s%1d%1d%1d", pLinkDev->GetDeviceID(), strlen(pLinkDev->GetChannel()), 
                                                    pLinkDev->GetChannel(), pLinkDev->GetIsCtrlDevice(), pLinkDev->GetIsVideoSourceDevice(), 
                                                    pLinkDev->GetIsHistoryDevice());

		if(*plActOutBufLen + strlen(szCamInfo) >= (unsigned long)lOutBufSize)
			return EC_ICV_CCTV_PARSERBUFFERTOOSHORT;

		memcpy(szOutBuf + *plActOutBufLen, szCamInfo, strlen(szCamInfo));
		// 摄像头基本信息和已经添加的连接设备长度
		*plActOutBufLen += strlen(szCamInfo);
	}
	
	// 最终确定整个摄像头信息的长度
	// *plActOutBufLen = strlen(szOutBuf);

	return ICV_SUCCESS;
}

/**
 *  将输入的摄像头列表打包为缓冲区
 *
 *  @param  -[in] const bool bZone: 是否仅仅打包某个分区
 *  @param  -[in] long nZoneID: 分区ID(如果查所有分区则为-1)
 *  @param  -[in] CVideoCameraList* plistCamera: 摄像头列表
 *  @param  -[out] char* szOutBuf: 输出缓冲区
 *  @param  -[in] long lOutBufSize: 输出缓冲区的总长度
 *  @param  -[out] long* plActOutBufLen: 实际输出的缓冲区长度,从0开始计数
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 *  @version  10/07/2008  shijunpu  Initial Version.
 */
long CVideoProtoPacker::PackGeneralCameraListToBuffer(CVideoCameraList* plistCamera, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen, 
														const bool bZone, const long nZoneID)
{
	// 入口检测
	if((plistCamera == NULL) || (szOutBuf == NULL) || (plActOutBufLen == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	*plActOutBufLen = 0;
	// 统计所有摄像头连接个数
	long nCount = plistCamera->GetSize();
	long nLinkCount = 0;
	for(long k=0; k<nCount; k++)
	{
		nLinkCount += plistCamera->GetAt(k)->GetLinkDev()->GetSize();
	}

	// 若申请的buffer空间不足，则返回，空间至少为：
	// 摄像头个数*VIDEO_CAMERA_INFO_MAXSIZE + 所有连接个数*VIDEO_LINKDEVICE_INFO_MAXSIZE + VIDEO_CAMERA_ID_SIZE
	//if(lOutBufSize < nCount*VIDEO_CAMERA_INFO_MAXSIZE + nLinkCount*VIDEO_LINKDEVICE_INFO_MAXSIZE + VIDEO_CAMERA_ID_SIZE)
	//{
	//	return EC_ICV_CCTV_PARSERBUFFERTOOSHORT;
	//}

	memset(szOutBuf, 0x00, lOutBufSize);

	// 若解析某一分区的摄像头列表
	if(bZone)
	{
		sprintf_s(szOutBuf,lOutBufSize, "%04d", nZoneID);
		*plActOutBufLen  += VIDEO_ZONE_ID_SIZE;
	}

	sprintf_s(szOutBuf + *plActOutBufLen , lOutBufSize-*plActOutBufLen,"%06d", nCount);
	*plActOutBufLen  += VIDEO_CAMERA_ID_SIZE;

	CVideoCamera* pCam = NULL;
	for(long i=0; i<nCount; i++)
	{
		pCam = plistCamera->GetAt(i);
		long lThisCamOutLen = 0;
		long lRet = PackGeneralCameraToBuffer(pCam, szOutBuf + *plActOutBufLen, lOutBufSize - *plActOutBufLen, &lThisCamOutLen, bZone, nZoneID);
		if(lRet != ICV_SUCCESS)
			return lRet;

		*plActOutBufLen += lThisCamOutLen;
	}
	
	return ICV_SUCCESS;
}

/**
 *  将输入的摄像头列表打包为缓冲区(简要的摄像头信息)
 *
 *  @param  -[in] const bool bZone: 是否仅仅打包某个分区
 *  @param  -[in] long nZoneID: 分区ID(如果查所有分区则为-1)
 *  @param  -[in] CVideoCameraList* plistCamera: 摄像头列表
 *  @param  -[out] char* szOutBuf: 输出缓冲区
 *  @param  -[in] long lOutBufSize: 输出缓冲区的总长度
 *  @param  -[out] long* plActOutBufLen: 实际输出的缓冲区长度,从0开始计数
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 *  @version  10/07/2008  shijunpu  Initial Version.
 */
long CVideoProtoPacker::PackGeneralSimpleCameraListToBuffer(CVideoCameraList* plistCamera, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen, 
															  bool bZone, long nZoneID)
{
	// 入口检测
	if((plistCamera == NULL) || (plActOutBufLen == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	// 检测分配空间是否足够
	long nCount = plistCamera->GetSize();
// 	if(lOutBufSize < nCount*VIDEO_SIMPLE_CAMERA_INFO_MAXSIZE + VIDEO_CAMERA_ID_SIZE)
// 		return EC_ICV_CCTV_PARSERBUFFERTOOSHORT;
	char szInfo[VIDEO_SIMPLE_CAMERA_INFO_MAXSIZE];
	memset(szInfo, 0, sizeof(szInfo));

	*plActOutBufLen = 0;

	if (szOutBuf == NULL) // calculate buffer size
	{
		*plActOutBufLen += VIDEO_CAMERA_ID_SIZE;
		if (bZone)
			*plActOutBufLen += VIDEO_ZONE_ID_SIZE;
		CVideoCamera* pCam = NULL;
		for(long i=0; i<nCount; i++)
		{
			memset(szInfo, 0x00, VIDEO_SIMPLE_CAMERA_INFO_MAXSIZE);
			pCam = plistCamera->GetAt(i);
			*plActOutBufLen += strlen(pCam->GetName());
			// 此处的数字要特别注意
			if(bZone)
				*plActOutBufLen += 8;
			else
				*plActOutBufLen += 12;
		}
		return ICV_SUCCESS;
	}

	memset(szOutBuf, 0x00, lOutBufSize);


	// 若解析某一分区下的摄像头
	if(bZone)
	{
		sprintf_s(szOutBuf, lOutBufSize,"%04d", nZoneID);
		*plActOutBufLen += VIDEO_ZONE_ID_SIZE;
	}

	sprintf_s(szOutBuf + *plActOutBufLen,lOutBufSize-*plActOutBufLen, "%06d", nCount);
	*plActOutBufLen += VIDEO_CAMERA_ID_SIZE;

	CVideoCamera* pCam = NULL;
	for(long i=0; i<nCount; i++)
	{
		memset(szInfo, 0x00, VIDEO_SIMPLE_CAMERA_INFO_MAXSIZE);
		pCam = plistCamera->GetAt(i);

		// 此处的数字要特别注意
		if(bZone)
		{
			// 某一分区下的摄像头
			sprintf_s(szInfo, VIDEO_SIMPLE_CAMERA_INFO_MAXSIZE,"%06d%02d%s", pCam->GetID(), strlen(pCam->GetName()),
										pCam->GetName());
		}
		else
		{
			sprintf_s(szInfo, VIDEO_SIMPLE_CAMERA_INFO_MAXSIZE,"%04d%06d%02d%s", pCam->GetZoneID(), pCam->GetID(), 
						strlen(pCam->GetName()), pCam->GetName());
		}

		long nLength = strlen(szInfo);
		memcpy(szOutBuf + *plActOutBufLen, szInfo, nLength);
		*plActOutBufLen += nLength;
	}

	return ICV_SUCCESS;
}


/**
 *  将输入的监视器列表打包为缓冲区
 *
 *  @param  -[in] const bool bZone: 是否仅仅打包某个分区
 *  @param  -[in] long nZoneID: 分区ID(如果查所有分区则为-1)
 *  @param  -[in] CVideoMonitorList* plistMonitor: 监视器列表
 *  @param  -[out] char* szOutBuf: 输出缓冲区
 *  @param  -[in] long lOutBufSize: 输出缓冲区的总长度
 *  @param  -[out] long* plActOutBufLen: 实际输出的缓冲区长度,从0开始计数
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 *  @version  10/07/2008  shijunpu  Initial Version.
 *  @version  05/17/2012  huangming 增加监视器连接设备的设备类型字节 Initial Version.
 */
long CVideoProtoPacker::PackGeneralMonitorListToBuffer(CVideoMonitorList* plistMonitor, char* szOutBuf, const long lOutBufSize, 
														 long* plActOutBufLen, bool bZone, long nZoneID)
{
	// 入口检测
	if((plistMonitor == NULL) || (plActOutBufLen == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	long nCount = plistMonitor->GetSize();
	
	// 检测分配空间是否足够
	//if((lOutBufSize < nCount*VIDEO_MONITOR_INFO_MAXSIZE + VIDEO_MONITOR_ID_SIZE))
	//	return EC_ICV_CCTV_PARSERBUFFERTOOSHORT;

	*plActOutBufLen = 0;
	char szInfo[VIDEO_MONITOR_INFO_MAXSIZE];
	if (szOutBuf == NULL)
	{
		if (bZone)
			*plActOutBufLen += VIDEO_ZONE_ID_SIZE;
		*plActOutBufLen += VIDEO_MONITOR_ID_SIZE;
		CVideoMonitor* pMon = NULL;
		for(long i=0; i<nCount; i++)
		{
			memset(szInfo, 0x00, VIDEO_MONITOR_INFO_MAXSIZE);
			pMon = plistMonitor->GetAt(i);
			// 此处的数字要特别注意
			if(bZone)
			{
				// 某一分区下的监视器
				// 最后字节增加监视器连接设备的设备类型字节 huangming 2012-05-17
				sprintf_s(szInfo,VIDEO_MONITOR_INFO_MAXSIZE, "%04d%02d%s%02d%s%04d%04d%s%01d", pMon->GetID(), 
					strlen(pMon->GetName()), pMon->GetName(), 
					strlen(pMon->GetDescription()), pMon->GetDescription(),
					pMon->GetLinkDev()->GetDeviceID(), 
					strlen(pMon->GetLinkDev()->GetChannel()), pMon->GetLinkDev()->GetChannel(), 
					pMon->GetLinkDev()->GetLinkDevType());
			}
			else
			{
				// 最后字节增加监视器连接设备的设备类型字节 huangming 2012-05-17
				sprintf_s(szInfo,VIDEO_MONITOR_INFO_MAXSIZE, "%04d%04d%02d%s%02d%s%04d%04d%s%01d", pMon->GetZoneID(), pMon->GetID(), 
					strlen(pMon->GetName()), pMon->GetName(), 
					strlen(pMon->GetDescription()), pMon->GetDescription(),
					pMon->GetLinkDev()->GetDeviceID(), 
					strlen(pMon->GetLinkDev()->GetChannel()), pMon->GetLinkDev()->GetChannel(), 
					pMon->GetLinkDev()->GetLinkDevType());
			}

			long nLength = strlen(szInfo);
			*plActOutBufLen += nLength;
		}
		return ICV_SUCCESS;
	}

	memset(szOutBuf, 0x00, lOutBufSize);
	// 某一分区下的监视器
	if(bZone)
	{
		sprintf_s(szOutBuf,lOutBufSize, "%04d", nZoneID);
		*plActOutBufLen += VIDEO_ZONE_ID_SIZE;
	}
	
	sprintf_s(szOutBuf + *plActOutBufLen,lOutBufSize-*plActOutBufLen, "%04d", nCount);
	*plActOutBufLen += VIDEO_MONITOR_ID_SIZE;

	CVideoMonitor* pMon = NULL;
	for(long i=0; i<nCount; i++)
	{
		memset(szInfo, 0x00, VIDEO_MONITOR_INFO_MAXSIZE);
		pMon = plistMonitor->GetAt(i);
		// 此处的数字要特别注意
		if(bZone)
		{
			// 某一分区下的监视器
            // 最后字节增加监视器连接设备的设备类型字节 huangming 2012-05-17
			sprintf_s(szInfo,VIDEO_MONITOR_INFO_MAXSIZE, "%04d%02d%s%02d%s%04d%04d%s%01d", pMon->GetID(), 
						strlen(pMon->GetName()), pMon->GetName(), 
						strlen(pMon->GetDescription()), pMon->GetDescription(),
						pMon->GetLinkDev()->GetDeviceID(), 
                        strlen(pMon->GetLinkDev()->GetChannel()), pMon->GetLinkDev()->GetChannel(), 
                        pMon->GetLinkDev()->GetLinkDevType());
		}
		else
		{
            // 最后字节增加监视器连接设备的设备类型字节 huangming 2012-05-17
			sprintf_s(szInfo,VIDEO_MONITOR_INFO_MAXSIZE, "%04d%04d%02d%s%02d%s%04d%04d%s%01d", pMon->GetZoneID(), pMon->GetID(), 
						strlen(pMon->GetName()), pMon->GetName(), 
						strlen(pMon->GetDescription()), pMon->GetDescription(),
						pMon->GetLinkDev()->GetDeviceID(), 
                        strlen(pMon->GetLinkDev()->GetChannel()), pMon->GetLinkDev()->GetChannel(), 
                        pMon->GetLinkDev()->GetLinkDevType());
		}

		long nLength = strlen(szInfo);
		if(*plActOutBufLen + nLength >= lOutBufSize)
			return EC_ICV_CCTV_PARSERBUFFERTOOSHORT;

		memcpy(szOutBuf + *plActOutBufLen, szInfo, nLength);
		*plActOutBufLen += nLength;
	}

	return ICV_SUCCESS;
}

/**
 *  将输入的分区列表打包为缓冲区
 *
 *  @param  -[in] CVideoZoneList* plistZone: 分区列表
 *  @param  -[out] char* szOutBuf: 输出缓冲区
 *  @param  -[in] long lOutBufSize: 输出缓冲区的总长度
 *  @param  -[out] long* plActOutBufLen: 实际输出的缓冲区长度,从0开始计数
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 *  @version  10/07/2008  shijunpu  Initial Version.
 */
long CVideoProtoPacker::PackZoneListToBuffer(CVideoZoneList* plistZone, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen)
{
	// 入口检测
	if((plistZone == NULL) || (plActOutBufLen == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	long nCount = plistZone->GetSize();
	//if(lOutBufSize < nCount*VIDEO_ZONE_INFO_MAXSIZE + VIDEO_ZONE_ID_SIZE)
	//	return EC_ICV_CCTV_PARSERBUFFERTOOSHORT;

	*plActOutBufLen = 0;
	
	if (szOutBuf == NULL)
	{
		*plActOutBufLen += VIDEO_ZONE_ID_SIZE;
		CVideoZone* pZone = NULL;
		for(long i=0; i<nCount; i++)
		{
			pZone = plistZone->GetAt(i);
			*plActOutBufLen += 4 + 2 + strlen(pZone->GetName()) + 2 + strlen(pZone->GetDescription()) + 1;
		}
		return ICV_SUCCESS;
	}
	memset(szOutBuf, 0x00, lOutBufSize);

	sprintf_s(szOutBuf,lOutBufSize, "%04d", nCount);
	*plActOutBufLen += VIDEO_ZONE_ID_SIZE;

	char szInfo[VIDEO_ZONE_INFO_MAXSIZE];
	CVideoZone* pZone = NULL;
	for(long i=0; i<nCount; i++)
	{
		memset(szInfo, 0x00, VIDEO_ZONE_INFO_MAXSIZE);
		pZone = plistZone->GetAt(i);

		// 此处的数字要特别注意
		sprintf_s(szInfo,VIDEO_ZONE_INFO_MAXSIZE, "%04d%02d%s%02d%s%1d", pZone->GetID(), 
						strlen(pZone->GetName()), pZone->GetName(), 
						strlen(pZone->GetDescription()), pZone->GetDescription(),
						pZone->GetAuthField());
		
		long nLength = strlen(szInfo);
		memcpy(szOutBuf + *plActOutBufLen, szInfo, nLength);
		*plActOutBufLen += nLength;
	}

	return ICV_SUCCESS;
}

/**
 *  将输入的设备产品型号信息打包为缓冲区
 *
 *  @param  -[in] CVideoProductList* plist
 : 设备产品型号列表
 *  @param  -[out] char* szOutBuf: 输出缓冲区
 *  @param  -[in] long lOutBufSize: 输出缓冲区的总长度
 *  @param  -[out] long* plActOutBufLen: 实际输出的缓冲区长度,从0开始计数
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 *  @version  10/07/2008  shijunpu  Initial Version.
 */
long CVideoProtoPacker::PackProductListToBuffer(CVideoProductList* plistProduct, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen)
{
	// 入口检测
	if((plistProduct == NULL) || (plActOutBufLen == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long nCount = plistProduct->GetSize();
	//if(lOutBufSize < nCount*VIDEO_PRODUCT_INFO_MAXSIZE + VIDEO_PRODUCT_ID_SIZE)
	//	return EC_ICV_CCTV_PARSERBUFFERTOOSHORT;

	memset(szOutBuf, 0x00, lOutBufSize);
	*plActOutBufLen = 0;
	if (szOutBuf == NULL)
	{
		*plActOutBufLen = VIDEO_PRODUCT_ID_SIZE;
		CVideoProduct* pProduct = NULL;
		for(long i=0; i<nCount; i++)
		{
			pProduct = plistProduct->GetAt(i);
			*plActOutBufLen += 1 + 4 + 2 + strlen(pProduct->GetName()) + 2 + strlen(pProduct->GetDllName());
		}
		return ICV_SUCCESS;
	}

	char szInfo[VIDEO_PRODUCT_INFO_MAXSIZE];
	sprintf_s(szOutBuf,lOutBufSize, "%04d", nCount);
	*plActOutBufLen = VIDEO_PRODUCT_ID_SIZE;

	CVideoProduct* pProduct = NULL;
	for(long i=0; i<nCount; i++)
	{
		memset(szInfo, 0x00, sizeof(szInfo));
		pProduct = plistProduct->GetAt(i);
		// 此处的数字要特别注意
		sprintf_s(szInfo,VIDEO_PRODUCT_INFO_MAXSIZE, "%01d%04d%02d%s%02d%s", pProduct->GetDeviceType(), pProduct->GetID(), 
						strlen(pProduct->GetName()), pProduct->GetName(), 
						strlen(pProduct->GetDllName()), pProduct->GetDllName());

		memcpy(szOutBuf+ *plActOutBufLen, szInfo, strlen(szInfo));
		*plActOutBufLen += strlen(szInfo);
	}

	return ICV_SUCCESS;
}

/**
 *  将输入的设备列表打包为缓冲区
 *
 *  @param  -[in] CVideoDeviceList* plistDevice: 设备列表
 *  @param  -[out] char* szOutBuf: 输出缓冲区
 *  @param  -[in] long lOutBufSize: 输出缓冲区的总长度
 *  @param  -[out] long* plActOutBufLen: 实际输出的缓冲区长度,从0开始计数
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 *  @version  10/07/2008  shijunpu  Initial Version.
 */
long CVideoProtoPacker::PackDeviceListToBuffer(CVideoDeviceList* plistDevice, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen)
{
	// 入口检测
	if((plistDevice == NULL) || (plActOutBufLen == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	long nCount = plistDevice->GetSize();
	long nLinkCount = 0;
	for(long k=0; k<nCount; k++)
	{
		nLinkCount += plistDevice->GetAt(k)->GetLinkDev()->GetSize();
	}

	*plActOutBufLen = 0;
	if (szOutBuf == NULL)
	{
		*plActOutBufLen += VIDEO_DEVICE_ID_SIZE;
		CVideoDevice* pDev = NULL;
		for(long i=0; i<nCount; i++)
		{
			pDev = plistDevice->GetAt(i);
			nLinkCount = pDev->GetLinkDev()->GetSize();
			*plActOutBufLen += 4 + 4 + 2 + strlen(pDev->GetName()) + 2 + strlen(pDev->GetIP()) + 6 + 3 + strlen(pDev->GetExtent()) + 2 + strlen(pDev->GetDescription()) + 4;

			// 连接设备
			for(long j=0; j<nLinkCount; j++)
			{
				CVideoLinkDevice* pLinkDev = pDev->GetLinkDev()->GetAt(j);
				*plActOutBufLen += 4 + strlen(pLinkDev->GetLocalChannel()) + 4 + 4 + strlen(pLinkDev->GetChannel());
			}
		}
		return ICV_SUCCESS;
	}

	memset(szOutBuf, 0x00, lOutBufSize);
	
	char szInfo[VIDEO_DEVICE_INFO_MAXSIZE];
	sprintf_s(szOutBuf,lOutBufSize, "%04d", nCount);
	*plActOutBufLen += VIDEO_DEVICE_ID_SIZE;

	CVideoDevice* pDev = NULL;
	for(long i=0; i<nCount; i++)
	{
		memset(szInfo, 0x00, sizeof(szInfo));
		pDev = plistDevice->GetAt(i);
		nLinkCount = pDev->GetLinkDev()->GetSize();
		// 此处的数字要特别注意
		sprintf_s(szInfo, VIDEO_DEVICE_INFO_MAXSIZE,"%04d%04d%02d%s%02d%s%06d%03d%s%02d%s%04d", pDev->GetProductID(), pDev->GetID(), 
						strlen(pDev->GetName()), pDev->GetName(), 
						strlen(pDev->GetIP()), pDev->GetIP(),
						pDev->GetPort(), strlen(pDev->GetExtent()),
						pDev->GetExtent(),strlen(pDev->GetDescription()),
						pDev->GetDescription(), nLinkCount);
		long nLength = strlen(szInfo);
		if(*plActOutBufLen + nLength >= lOutBufSize)
			return EC_ICV_CCTV_PARSERBUFFERTOOSHORT;

		memcpy(szOutBuf + *plActOutBufLen, szInfo, nLength);
		*plActOutBufLen += nLength;
		
		// 连接设备
		for(long j=0; j<nLinkCount; j++)
		{
			CVideoLinkDevice* pLinkDev = pDev->GetLinkDev()->GetAt(j);
			memset(szInfo, 0x00, sizeof(szInfo));
			sprintf_s(szInfo,VIDEO_DEVICE_INFO_MAXSIZE, "%04d%s%04d%04d%s", strlen(pLinkDev->GetLocalChannel()), pLinkDev->GetLocalChannel(), 
                                                               pLinkDev->GetDeviceID(), strlen(pLinkDev->GetChannel()), pLinkDev->GetChannel());
			long nLength = strlen(szInfo);

			if(*plActOutBufLen + nLength >= lOutBufSize)
				return EC_ICV_CCTV_PARSERBUFFERTOOSHORT;

			memcpy_s(szOutBuf + *plActOutBufLen,lOutBufSize, szInfo, nLength);
			*plActOutBufLen += nLength;
		}
	}

	return ICV_SUCCESS;
}

/**
 *  将输入的摄像头列表打包为缓冲区
 *
 *  @param  -[in] CVideoCamera* pCamera: 摄像头
 *  @param  -[out] char* szOutBuf: 输出缓冲区
 *  @param  -[in] long lOutBufSize: 输出缓冲区的总长度
 *  @param  -[out] long* plActOutBufLen: 实际输出的缓冲区长度,从0开始计数
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 *  @version  10/07/2008  shijunpu  Initial Version.
 */
long CVideoProtoPacker::PackCameraToBuffer(CVideoCamera* pCamera, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen)
{
	// 入口检测
	if((szOutBuf == NULL) || (pCamera == NULL) || (plActOutBufLen == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	// 空间检测
	long nSpaceSize = VIDEO_CAMERA_INFO_MAXSIZE + pCamera->GetLinkDev()->GetSize()*VIDEO_LINKDEVICE_INFO_MAXSIZE;
	if(lOutBufSize < nSpaceSize)
		return EC_ICV_CCTV_PARSERBUFFERTOOSHORT;
		
	memset(szOutBuf, 0x00, lOutBufSize);
	long nOff = 0;
	long lRet = PackGeneralCameraToBuffer(pCamera, szOutBuf, lOutBufSize, &nOff, true, 0);
	if(lRet != ICV_SUCCESS)
		return lRet;

	*plActOutBufLen = nOff;
	return lRet;
}

/**
 *  将输入的摄像头列表打包为缓冲区(所有分区）
 *
 *  @param  -[in] CVideoCameraList* plistCamera: 摄像头列表
 *  @param  -[out] char* szOutBuf: 输出缓冲区
 *  @param  -[in] long lOutBufSize: 输出缓冲区的总长度
 *  @param  -[out] long* plActOutBufLen: 实际输出的缓冲区长度,从0开始计数
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 *  @version  10/07/2008  shijunpu  Initial Version.
 */
long CVideoProtoPacker::PackCameraListToBuffer(CVideoCameraList* plistCamera, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen)
{
	return PackGeneralCameraListToBuffer(plistCamera, szOutBuf, lOutBufSize, plActOutBufLen, false, 0);
}

/**
 *  将输入的摄像头列表打包为缓冲区(简要信息, 所有分区)
 *
 *  @param  -[in] CVideoCameraList* plistCamera: 摄像头列表
 *  @param  -[out] char* szOutBuf: 输出缓冲区
 *  @param  -[in] long lOutBufSize: 输出缓冲区的总长度
 *  @param  -[out] long* plActOutBufLen: 实际输出的缓冲区长度,从0开始计数
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 *  @version  10/07/2008  shijunpu  Initial Version.
 */
long CVideoProtoPacker::PackSimpleCameraListToBuffer(CVideoCameraList* plistCamera, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen)
{
	return PackGeneralSimpleCameraListToBuffer(plistCamera, szOutBuf, lOutBufSize, plActOutBufLen, false, 0);
}


/**
 *  将输入的摄像头列表打包为缓冲区（某个分区）
 *
 *  @param  -[in] long nZoneID: 分区ID(如果查所有分区则为-1)
 *  @param  -[in] CVideoCameraList* plistCamera: 摄像头列表
 *  @param  -[out] char* szOutBuf: 输出缓冲区
 *  @param  -[in] long lOutBufSize: 输出缓冲区的总长度
 *  @param  -[out] long* plActOutBufLen: 实际输出的缓冲区长度,从0开始计数
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 *  @version  10/07/2008  shijunpu  Initial Version.
 */
long CVideoProtoPacker::PackCameraListOfZoneToBuffer(long nZoneID, CVideoCameraList* plistCamera, 
													   char* szOutBuf, const long lOutBufSize, long* plActOutBufLen)
{
	return PackGeneralCameraListToBuffer(plistCamera, szOutBuf, lOutBufSize, plActOutBufLen, true, nZoneID);
}

/**
 *  将输入的摄像头列表打包为缓冲区(简要信息, 某个分区)
 *
 *  @param  -[in] CVideoCameraList* plistCamera: 摄像头列表
 *  @param  -[out] char* szOutBuf: 输出缓冲区
 *  @param  -[in] long lOutBufSize: 输出缓冲区的总长度
 *  @param  -[out] long* plActOutBufLen: 实际输出的缓冲区长度,从0开始计数
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 *  @version  10/07/2008  shijunpu  Initial Version.
 */
long CVideoProtoPacker::PackSimpleCameraListOfZoneToBuffer(long nZoneID, CVideoCameraList* plistCamera, 
															 char* szOutBuf, const long lOutBufSize, long* plActOutBufLen)
{
	return PackGeneralSimpleCameraListToBuffer(plistCamera, szOutBuf, lOutBufSize, plActOutBufLen, true, nZoneID);
}

/**
 *  将输入的监视器列表打包为缓冲区(所有分区)
 *
 *  @param  -[in] CVideoMonitorList* plistMonitor: 监视器列表
 *  @param  -[out] char* szOutBuf: 输出缓冲区
 *  @param  -[in] long lOutBufSize: 输出缓冲区的总长度
 *  @param  -[out] long* plActOutBufLen: 实际输出的缓冲区长度,从0开始计数
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 *  @version  10/07/2008  shijunpu  Initial Version.
 */
long CVideoProtoPacker::PackMonitorListToBuffer(CVideoMonitorList* plistMonitor, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen)
{
	return PackGeneralMonitorListToBuffer(plistMonitor, szOutBuf, lOutBufSize, plActOutBufLen, false, 0);
}

/**
 *  将输入的监视器列表打包为缓冲区(某个分区)
 *
 *  @param  -[in] CVideoMonitorList* plistMonitor: 监视器列表
 *  @param  -[out] char* szOutBuf: 输出缓冲区
 *  @param  -[in] long lOutBufSize: 输出缓冲区的总长度
 *  @param  -[out] long* plActOutBufLen: 实际输出的缓冲区长度,从0开始计数
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 *  @version  10/07/2008  shijunpu  Initial Version.
 */
long CVideoProtoPacker::PackMonitorListOfZoneToBuffer(long nZoneID, CVideoMonitorList* plistMonitor, 
														 char* szOutBuf, const long lOutBufSize, long* plActOutBufLen)
{
	return PackGeneralMonitorListToBuffer(plistMonitor, szOutBuf, lOutBufSize, plActOutBufLen, true, nZoneID);
}

/**
 *  将输入某个模式打包为缓冲区
 *
 *  @param  -[in] CVideoMode* pMode: 模式
 *  @param  -[in] const bool bPara: 是否打包参数
 *  @param  -[out] char* szOutBuf: 输出缓冲区
 *  @param  -[in] long lOutBufSize: 输出缓冲区的总长度
 *  @param  -[out] long* plActOutBufLen: 实际输出的缓冲区长度,从0开始计数
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 *  @version  10/07/2008  shijunpu  Initial Version.
 */

long CVideoProtoPacker::PackModeToBuffer(CVideoMode* pMode, char* pszBuffer, const long lBufferSize, 
									 long* plOutBufferLen, const bool bIncPara)
{
	// 入口检测
	if((pMode == NULL) || (pszBuffer == NULL) || (plOutBufferLen == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	memset(pszBuffer, 0x00, lBufferSize);
	
	// 此处的数字要特别注意
	sprintf_s(pszBuffer, lBufferSize,"%1d%02d%s%02d%s", pMode->GetType(), strlen(pMode->GetName()), pMode->GetName(),
									strlen(pMode->GetDescription()), pMode->GetDescription());
	if(bIncPara)
	{
		char *szModeParam = pMode->GetPara();
		// 模式参数
		sprintf_s(pszBuffer,lBufferSize, "%s%04d%s", pszBuffer,strlen(szModeParam), szModeParam);
	}

	*plOutBufferLen = strlen(pszBuffer);
	return ICV_SUCCESS;
}

/**
 *  将输入模式列表打包为缓冲区
 *
 *  @param  -[in] CVideoMode* pMode: 模式列表
 *  @param  -[in] const bool bPara: 是否打包参数
 *  @param  -[out] char* szOutBuf: 输出缓冲区
 *  @param  -[in] long lOutBufSize: 输出缓冲区的总长度
 *  @param  -[out] long* plActOutBufLen: 实际输出的缓冲区长度,从0开始计数
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 *  @version  10/07/2008  shijunpu  Initial Version.
 */
long CVideoProtoPacker::PackModeListToBuffer(CVideoModeList* plistMode, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen,
											   const bool bPara)
{
	// 入口检测
	if((plistMode == NULL) || (szOutBuf == NULL) || (plActOutBufLen == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	// 检测的空间大小
	long nModeCount = plistMode->GetSize();
	long nSpaceSize = VIDEO_MODE_INFO_MAXSIZE;
	if(bPara)
		nSpaceSize += VIDEO_MODE_PARA_MAXSIZE;

	if(lOutBufSize < nModeCount*nSpaceSize + VIDEO_MODE_COUNT_SIZE)
		return EC_ICV_CCTV_PARSERBUFFERTOOSHORT;

	memset(szOutBuf, 0x00, lOutBufSize);
	
	*plActOutBufLen = 0;
	// 模式个数
	sprintf_s(szOutBuf,lOutBufSize, "%04d", nModeCount);
	*plActOutBufLen += VIDEO_MODE_COUNT_SIZE;

	for(int i=0; i<nModeCount; i++)
	{
		long lActOutLen = 0;
		long lRet = PackModeToBuffer(plistMode->GetAt(i), szOutBuf + *plActOutBufLen, lOutBufSize - *plActOutBufLen, &lActOutLen, bPara);
		if(lRet != ICV_SUCCESS)
			return lRet;

		*plActOutBufLen += lActOutLen;
	}

	return ICV_SUCCESS;
}

/**
 *  将输入的摄像头预置位打包为缓冲区
 *
 *  @param  -[in] CVideoPSP* pPSP: 某个摄像头的某个预置位
 *  @param  -[out] char* szOutBuf: 输出缓冲区
 *  @param  -[in] long lOutBufSize: 输出缓冲区的总长度
 *  @param  -[out] long* plActOutBufLen: 实际输出的缓冲区长度,从0开始计数
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 *  @version  10/07/2008  shijunpu  Initial Version.
 */
long CVideoProtoPacker::PackPSPToBuffer(CVideoPSP* pPSP, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen)
{
	// 入口检测
	if((pPSP == NULL) || (szOutBuf == NULL) || (plActOutBufLen == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	// 检测的空间大小
	if(lOutBufSize < VIDEO_PSP_INFO_MAXSIZE)
		return EC_ICV_CCTV_PARSERBUFFERTOOSHORT;

	memset(szOutBuf, 0x00, lOutBufSize);

	// 此处的数字要特别注意
	sprintf_s(szOutBuf,lOutBufSize, "%02d%s%02d%s%03d", strlen(pPSP->GetName()), pPSP->GetName(),
									strlen(pPSP->GetDescription()), pPSP->GetDescription(), pPSP->GetPresetIndex());
	*plActOutBufLen = strlen(szOutBuf);
	return ICV_SUCCESS;
}

/**
 *  将输入的摄像头预置位列表打包为缓冲区
 *
 *  @param  -[in] CVideoPSP* pPSP: 某个摄像头的预置位列表
 *  @param  -[out] char* szOutBuf: 输出缓冲区
 *  @param  -[in] long lOutBufSize: 输出缓冲区的总长度
 *  @param  -[out] long* plActOutBufLen: 实际输出的缓冲区长度,从0开始计数
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 *  @version  10/07/2008  shijunpu  Initial Version.
 */
long CVideoProtoPacker::PackPSPListToBuffer(CVideoPSPList* plistPSP, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen)
{
	// 入口检测
	if((plistPSP == NULL) || (szOutBuf == NULL) || (plActOutBufLen == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	// 检测的空间大小
	long nCount = plistPSP->GetSize();
	//if(lOutBufSize < nCount*VIDEO_PSP_INFO_MAXSIZE + VIDEO_PSP_COUNT_SIZE)
	//	return EC_ICV_CCTV_PARSERBUFFERTOOSHORT;

	*plActOutBufLen = 0;
	memset(szOutBuf, 0x00, lOutBufSize);

	char szInfo[VIDEO_PSP_INFO_MAXSIZE];
	// 预置位个数
	sprintf_s(szOutBuf,lOutBufSize, "%03d", nCount);
	*plActOutBufLen += VIDEO_PSP_COUNT_SIZE;

	for(int i=0; i<nCount; i++)
	{
		memset(szInfo, 0x00, VIDEO_PSP_INFO_MAXSIZE);
		long lActOutLen = 0;
		long lRet = PackPSPToBuffer(plistPSP->GetAt(i), szInfo, sizeof(szInfo), &lActOutLen);
		if(lRet != ICV_SUCCESS)
			return lRet;
		
		memcpy(szOutBuf + *plActOutBufLen, szInfo, lActOutLen);
		*plActOutBufLen += lActOutLen;
	}

	return ICV_SUCCESS;
}

/**
 *  将输入的抓拍文件信息列表打包为缓冲区
 *
 *  @param  -[in] CVideoSnapFile* pSnapFile: 抓拍文件信息
 *  @param  -[out] char* pszBuffer: 输出缓冲区
 *  @param  -[in] long lBufferSize: 输出缓冲区的总长度
 *  @param  -[out] long* plOutBufferSize: 实际输出的缓冲区长度,从0开始计数
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 *  @version  10/07/2008  shijunpu  Initial Version.
 */
long CVideoProtoPacker::PackSnapFileToBuffer(CVideoSnapFile* pSnapFile, char* pszBuffer, const long lBufferSize, long* plOutBufferSize)
{
	// 入口检测
	if((pSnapFile == NULL) || (pszBuffer == NULL) || (plOutBufferSize == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	// 检测的空间大小
	if(lBufferSize < VIDEO_SNAP_INFO_MAXSIZE)
		return EC_ICV_CCTV_PARSERBUFFERTOOSHORT;

	memset(pszBuffer, 0x00, lBufferSize);

	// 此处的数字要特别注意
	sprintf_s(pszBuffer,lBufferSize, "%06d%02d%s%010I64d%3d%s%2d%s%02d", pSnapFile->GetCameraID(), strlen(pSnapFile->GetInfoType()),
											pSnapFile->GetInfoType(), pSnapFile->GetSnapTime(),
											strlen(pSnapFile->GetExtent()), pSnapFile->GetExtent(),
											strlen(pSnapFile->GetDescription()), pSnapFile->GetDescription(),
											pSnapFile->GetSnapCount());
	*plOutBufferSize = strlen(pszBuffer);
	return ICV_SUCCESS;
}

/**
 *  将输入的抓拍文件信息列表打包为缓冲区
 *
 *  @param  -[in] CVideoSnapFileList* plistSnapFile: 抓拍文件信息列表
 *  @param  -[out] char* szOutBuf: 输出缓冲区
 *  @param  -[in] long lOutBufSize: 输出缓冲区的总长度
 *  @param  -[out] long* plActOutBufLen: 实际输出的缓冲区长度,从0开始计数
 *
 *  @return 
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version  06/16/2008  xulizai  Initial Version.
 *  @version  10/07/2008  shijunpu  Initial Version.
 */
long CVideoProtoPacker::PackSnapFileListToBuffer(CVideoSnapFileList* plistSnapFile, char* szOutBuf, const long lOutBufSize, long* plActOutBufLen)
{
	// 入口检测
	if((plistSnapFile == NULL) || (plActOutBufLen == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	// 检测的空间大小
	long nCount = plistSnapFile->GetSize();
	//if(lOutBufSize < nCount*VIDEO_SNAP_INFO_MAXSIZE + VIDEO_SNAP_ID_SIZE)
	//	return EC_ICV_CCTV_PARSERBUFFERTOOSHORT;
	char szInfo[VIDEO_SNAP_INFO_MAXSIZE];

	if (szOutBuf == NULL)
	{
		*plActOutBufLen = VIDEO_SNAP_ID_SIZE;
		for(int i=0; i<nCount; i++)
		{
			// 图片ID
			//sprintf_s(szOutBuf + *plActOutBufLen,lOutBufSize-*plActOutBufLen, "%06d", plistSnapFile->GetAt(i)->GetID());
			*plActOutBufLen += VIDEO_SNAP_ID_SIZE;

			memset(szInfo, 0x00, VIDEO_SNAP_INFO_MAXSIZE);
			long lActOutLen = 0;

			// 将图片其他信息解析为buffer
			long lRet = PackSnapFileToBuffer(plistSnapFile->GetAt(i), szInfo, sizeof(szInfo), &lActOutLen);
			if(lRet != ICV_SUCCESS)
				return lRet;

			//memcpy_s(szOutBuf + *plActOutBufLen,lOutBufSize-*plActOutBufLen, szInfo, lActOutLen);
			// 更新偏移量
			*plActOutBufLen += lActOutLen;
		}
		return ICV_SUCCESS;
	}

	memset(szOutBuf, 0x00, lOutBufSize);
	// 抓拍图片数
	sprintf_s(szOutBuf,lOutBufSize, "%06d", nCount);
	*plActOutBufLen = VIDEO_SNAP_ID_SIZE;
	
	// 将所有的图片信息加入到buffer
	for(int i=0; i<nCount; i++)
	{
		// 图片ID
		sprintf_s(szOutBuf + *plActOutBufLen,lOutBufSize-*plActOutBufLen, "%06d", plistSnapFile->GetAt(i)->GetID());
		*plActOutBufLen += VIDEO_SNAP_ID_SIZE;
		
		memset(szInfo, 0x00, VIDEO_SNAP_INFO_MAXSIZE);
		long lActOutLen = 0;

		// 将图片其他信息解析为buffer
		long lRet = PackSnapFileToBuffer(plistSnapFile->GetAt(i), szInfo, sizeof(szInfo), &lActOutLen);
		if(lRet != ICV_SUCCESS)
			return lRet;
		
		memcpy_s(szOutBuf + *plActOutBufLen,lOutBufSize-*plActOutBufLen, szInfo, lActOutLen);
		// 更新偏移量
		*plActOutBufLen += lActOutLen;
	}
 
	return ICV_SUCCESS;
}

/**
*  打包发送摄像头对应的实时图像连接的设备.
*
*  @param  -[in, out]  char*  pszBuff: [缓冲区]
*  @param  -[in]  long  nInLen: [缓冲区长度]
*  @param  -[in]  CVideoLinkDevice *pLinkDev: [设备信息]
*  @param  -[in]  long&  nOutLen: [返回的长度]
*
*  @version     01/17/2011  fengjuanyong  Initial Version.
*/
long CVideoProtoPacker::PackRetGetDevToCam(char* pszBuff, long nInLen, long lDevType, CVideoLinkDevice *pLinkDev, long &nOutLen)
{
	if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
    nOutLen =sprintf_s(pszBuff,nInLen, "%01d%04d%04d%s", lDevType, pLinkDev->GetDeviceID(), strlen(pLinkDev->GetChannel()), pLinkDev->GetChannel());
    
	return lRet;
}

/**
*  打包发送摄像头对应的监视器设备.
*
*  @param  -[in, out]  char*  pszBuff: [缓冲区]
*  @param  -[in]  long  nInLen: [缓冲区长度]
*  @param  -[in]  CVideoMonitor *pMon: [监视器信息]
*  @param  -[in]  long&  nOutLen: [返回的长度]
*
*  @version     03/01/2011  fengjuanyong  Initial Version.
*/
long CVideoProtoPacker::PackRetGetMonToCam(char* pszBuff, long nInLen, CVideoMonitor *pMon, long &nOutLen)
{
	if ((NULL == pszBuff) || (nInLen < CMD_MESSAGE_MAX_SIZE))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long lRet = ICV_SUCCESS;
    
	nOutLen =sprintf_s(pszBuff,nInLen, "%02d%s", strlen(pMon->GetName()), pMon->GetName());
    
	return lRet;
}
