/**************************************************************
 *  Filename:    VideoProtocolParser.cpp
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: 视频监控系统协议解析库实现文件.
 *
 *  @author:     xulizai
 *  @version     2008/05/16  xulizai  Initial Version
 *  @version     2009/04/17  chenzhan  添加摄像头自定义分区和排序的解析函数
 *  @version     2009/06/01  zhucongfeng 添加获取图片内容的解析函数
 *  @version     2009/06/17	 chenzhan	服务端协议打包函数组织
 			     09/08/2009  chenzhan     添加录像抓拍、图片累加功能
				 14/04/2010  chenzhan  增加设备级连控制服务端返回给客户端的编解码器列表
**************************************************************/
#include "multimedia/video/VideoProtoParser.h"
#include "errcode/ErrCode_iCV_CCTV.hxx"
#include "stdio.h"

using namespace std;

#define VIDEO_PARSE_TEXT_SIZE		32		// 解析临时字符的长度
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
CVideoProtoParser::CVideoProtoParser()
{
}

CVideoProtoParser::~CVideoProtoParser()
{

}

// 说明参见.h文件
long CVideoProtoParser::PaseCmdHeader(char* pszBuff, long &nCmd, long &nStampID, char* pszUserName, long nNameBufLen, char* pszGroup, long nGroupBufLen, long &nOutLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == pszUserName) || (NULL == pszGroup) || (nNameBufLen < VIDEO_NAME_MAXSIZE) || (nGroupBufLen < VIDEO_NAME_MAXSIZE))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CMD_SIZE, nCmd));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CMD_STAMP_SIZE, nStampID));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToName(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, pszUserName));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToName(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, pszGroup));
    nOutLen = nOff;
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::PaseRetHeader(char* pszBuff, long &nCmd, long &nStampID, long &nRetCode, long &nOutLen)
{
    // 入口检测
    if (NULL == pszBuff)
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    
    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CMD_SIZE, nCmd));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CMD_STAMP_SIZE, nStampID));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_RETCODE_LENGTH_SIZE, nRetCode));
    nOutLen = nOff;
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseRetGetAllConfigInfo(char* pszBuff, CVideoProductList *plistProduct, CVideoDeviceList *plistDev, 
												 CVideoZoneList *plistZone, CVideoCameraList *plistCam, CVideoMonitorList *plistMon)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == plistProduct) || (NULL == plistDev) || (NULL == plistZone) || (NULL == plistCam) || (NULL == plistMon))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

    VIDEO_CHECKFAIL_RETURN(ParseRetGetProductInfo(pszBuff, plistProduct));
    VIDEO_CHECKFAIL_RETURN(ParseRetGetDevInfo(pszBuff, plistDev));
    VIDEO_CHECKFAIL_RETURN(ParseRetGetZoneInfo(pszBuff, plistZone));
    VIDEO_CHECKFAIL_RETURN(ParseRetGetCamInfo(pszBuff, plistCam));
    VIDEO_CHECKFAIL_RETURN(ParseRetGetMonInfo(pszBuff, plistMon));

    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseRetGetZoneInfo(char* pszBuff, CVideoZoneList* plistZone)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == plistZone))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToZoneList(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, plistZone));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseRetGetProductInfo(char* pszBuff, CVideoProductList* plistProduct)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == plistProduct))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToProductList(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, plistProduct));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseRetGetDevInfo(char* pszBuff, CVideoDeviceList* plistDev)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == plistDev))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToDeviceList(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, plistDev));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseRetGetCamInfo(char* pszBuff, CVideoCameraList* plistCam)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == plistCam))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToCameraList(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, plistCam));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseRetGetSimpleCamInfo(char* pszBuff, CVideoCameraList* plistCam)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == plistCam))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    VIDEO_CHECKFAIL_RETURN(ParseBufferToSimpleCameraList(pszBuff, VIDEO_MESSAGE_MAX_SIZE, plistCam));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdGetZoneCam(char* pszBuff, long &nZoneID)
{
    // 入口检测
    if (NULL == pszBuff)
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_SNAP_ID_SIZE, nZoneID));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseRetGetZoneCam(char* pszBuff, CVideoCameraList* plistCam)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == plistCam))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    VIDEO_CHECKFAIL_RETURN(ParseBufferToCameraListOfZone(pszBuff, VIDEO_MESSAGE_MAX_SIZE, plistCam));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdGetZoneSimpleCam(char* pszBuff, long &nZoneID)
{
    // 入口检测
    if (NULL == pszBuff)
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_ZONE_ID_SIZE, nZoneID));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseRetGetZoneSimpleCam(char* pszBuff, CVideoCameraList* plistCam)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == plistCam))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    VIDEO_CHECKFAIL_RETURN(ParseBufferToSimpleCameraListOfZone(pszBuff, VIDEO_MESSAGE_MAX_SIZE, plistCam));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdGetCamByName(char* pszBuff, char* pszCamName, long nCamNameLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == pszCamName) || (VIDEO_NAME_MAXSIZE > nCamNameLen))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToString(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_NAME_LENGTH_SIZE, pszCamName, nCamNameLen));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseRetGetCamByName(char* pszBuff, CVideoCamera* ptheCam)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == ptheCam))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToCamera(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, ptheCam));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseRetGetMonInfo(char* pszBuff, CVideoMonitorList* plistMon)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == plistMon))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToMonitorList(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, plistMon));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdGetZoneMon(char* pszBuff, long &nZoneID)
{
    // 入口检测
    if (NULL == pszBuff)
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_ZONE_ID_SIZE, nZoneID));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseRetGetZoneMon(char* pszBuff, CVideoMonitorList* plistMon)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == plistMon))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    VIDEO_CHECKFAIL_RETURN(ParseBufferToMonitorListOfZone(pszBuff, VIDEO_MESSAGE_MAX_SIZE, plistMon));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdGetAllModeName(char* pszBuff, long &nTime)
{
    // 入口检测
    if (NULL == pszBuff)
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_TIME_LENGTH_SIZE, nTime));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseRetGetAllModeName(char* pszBuff, long &nTime, CVideoModeList* plistMode)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == plistMode))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_TIME_LENGTH_SIZE, nTime));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToModeList(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, plistMode, false));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdAddMode(char* pszBuff, CVideoMode* ptheMode)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == ptheMode))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToMode(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, ptheMode, true));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdDelMode(char* pszBuff, CVideoModeList* plistMode)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == plistMode))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToModeList(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, plistMode, false));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseRetDelMode(char* pszBuff, CVideoModeList* plistMode)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == plistMode))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToModeList(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, plistMode, false));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdModifyMode(char* pszBuff, char *pszModeName, long nModeNameLen, CVideoMode* ptheMode)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == pszModeName) || (NULL == ptheMode) || (VIDEO_NAME_MAXSIZE > nModeNameLen))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToString(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_NAME_LENGTH_SIZE, pszModeName, nModeNameLen));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToMode(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, ptheMode, true));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdGetModeLayout(char* pszBuff, char *pszModeName, long nModeNameLen)
{
    // 入口检测
//    if ((NULL == pszBuff)|| (NULL == pszModeName) ) || (VIDEO_NAME_MAXSIZE > nModeNameLen))
	if (NULL == pszBuff)
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToString(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_MODE_PARA_LENGTH_SIZE, pszModeName, nModeNameLen));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseRetGetModeLayout(char* pszBuff, CVideoMode* ptheMode)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == ptheMode))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToString(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_MODE_PARA_LENGTH_SIZE, ptheMode->GetPara(), VIDEO_MODE_PARA_MAXSIZE));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdGetCamPSPInfo(char* pszBuff, long &nCamID, long &nTime)
{
    // 入口检测
    if (NULL == pszBuff)
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CAMERA_ID_SIZE, nCamID));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_TIME_LENGTH_SIZE, nTime));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseRetGetCamPSPInfo(char* pszBuff, long &nCamID, long &nTime, CVideoPSPList* plistPSP)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == plistPSP))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CAMERA_ID_SIZE, nCamID));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_TIME_LENGTH_SIZE, nTime));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToPSPList(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, plistPSP));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdAddPSP(char* pszBuff, long &nCamID, long &nTime, CVideoPSP *pthePSP)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == pthePSP))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CAMERA_ID_SIZE, nCamID));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_TIME_LENGTH_SIZE, nTime));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToPSP(pszBuff, VIDEO_MESSAGE_MAX_SIZE, pthePSP));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
//2012.5.6 zhucongfeng 无使用，而且是错误的，打包与解包不一致。目前打包和解包直接写在VideoRAPI和VideoService中了
/*long CVideoProtoParser::ParseCmdDelPSP(char* pszBuff, long &nCamID, CVideoPSPList *plistPSP)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == plistPSP))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CAMERA_ID_SIZE, nCamID));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToPSPList(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, plistPSP));
    
    return ICV_SUCCESS;
}*/

// 说明参见.h文件
//2012.5.6 zhucongfeng 无使用，而且是错误的，打包与解包不一致。目前打包和解包直接写在VideoRAPI和VideoService中了
/*long CVideoProtoParser::ParseRetDelPSP(char* pszBuff, CVideoList<CStrName> *plistCStrName)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == plistCStrName))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    VIDEO_CHECKFAIL_RETURN(ParseBufferToCStrNameList(pszBuff, VIDEO_MESSAGE_MAX_SIZE, plistCStrName));
    
    return ICV_SUCCESS;
}*/

// 说明参见.h文件
long CVideoProtoParser::ParseCmdModifyPSP(char* pszBuff, long &nCamID, char *pszPSPName, long nPSPNameLen, CVideoPSP *pthePSP)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == pszPSPName)|| (NULL == pthePSP) || (VIDEO_NAME_MAXSIZE > nPSPNameLen))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CAMERA_ID_SIZE, nCamID));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToString(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_NAME_LENGTH_SIZE, pszPSPName, nPSPNameLen));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToPSP(pszBuff, VIDEO_MESSAGE_MAX_SIZE, pthePSP));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdAppPSP(char* pszBuff, long &nCamID, char *pszPSPName, long nPSPNameLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == pszPSPName) || (VIDEO_NAME_MAXSIZE > nPSPNameLen))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CAMERA_ID_SIZE, nCamID));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToString(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_NAME_LENGTH_SIZE, pszPSPName, nPSPNameLen));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseRetGetSnapType(char* pszBuff, CVideoList<CStrName> *plistCStrName)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == plistCStrName))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    VIDEO_CHECKFAIL_RETURN(ParseBufferToCStrNameList(pszBuff, VIDEO_MESSAGE_MAX_SIZE, plistCStrName));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdQuerySnapInfo(char* pszBuff, long &nCamID, long &nStart, long &nEnd, char *pszSnapType, long nSnapTypeLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == pszSnapType) || (VIDEO_NAME_MAXSIZE > nSnapTypeLen))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CAMERA_ID_SIZE, nCamID));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_TIME_LENGTH_SIZE, nStart));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_TIME_LENGTH_SIZE, nEnd));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToString(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_NAME_LENGTH_SIZE, pszSnapType, nSnapTypeLen));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseRetQuerySnapInfo(char* pszBuff, CVideoSnapFileList *plistSnapInfo)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == plistSnapInfo))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    VIDEO_CHECKFAIL_RETURN(ParseBufferToSnapFileList(pszBuff, VIDEO_MESSAGE_MAX_SIZE, plistSnapInfo));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdGetPicBuffer(char* pszBuff, long &nSnapID)
{
    // 入口检测
    if (NULL == pszBuff)
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_SNAP_ID_SIZE, nSnapID));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdAddPicInfo(char* pszBuff, CVideoSnapFile *ptheSnapInfo)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == ptheSnapInfo))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToSnapFile(pszBuff, VIDEO_MESSAGE_MAX_SIZE, ptheSnapInfo, &nOff));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseRetAddPicInfo(char* pszBuff, long &nSnapID)
{
    // 入口检测
    if (NULL == pszBuff)
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_SNAP_ID_SIZE, nSnapID));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdDelSnapInfo(char* pszBuff, CVideoSnapFileList *plistSnapInfo)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == plistSnapInfo))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    VIDEO_CHECKFAIL_RETURN(ParseBufferToSnapFileList(pszBuff, VIDEO_MESSAGE_MAX_SIZE, plistSnapInfo));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdModifySnapInfo(char* pszBuff, long &nSnapID, CVideoSnapFile *ptheSnapInfo)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == ptheSnapInfo))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_SNAP_ID_SIZE, nSnapID));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToSnapFile(pszBuff, VIDEO_MESSAGE_MAX_SIZE, ptheSnapInfo, &nOff));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdSwitchByID(char* pszBuff, long &nCamID, long &nMonID)
{
    // 入口检测
    if (NULL == pszBuff)
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CAMERA_ID_SIZE, nCamID));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_MONITOR_ID_SIZE, nMonID));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdSwitchByName(char* pszBuff, char *pszCamName, long nCamNameLen, char *pszMonName, long nMonNameLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == pszCamName) || (NULL == pszMonName)|| (nCamNameLen < VIDEO_NAME_MAXSIZE) || (nMonNameLen < VIDEO_NAME_MAXSIZE))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToString(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_NAME_LENGTH_SIZE, pszCamName, nCamNameLen));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToString(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_NAME_LENGTH_SIZE, pszMonName, nMonNameLen));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdLensControl(char* pszBuff, long &nCamID, long &nCtrCode, long &nSpeed)
{
    // 入口检测
    if (NULL == pszBuff)
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CAMERA_ID_SIZE, nCamID));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CMD_LENS_SIZE, nCtrCode));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CMD_LENS_SIZE, nSpeed));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdAuxControl(char* pszBuff, long &nCamID, long &nCtrCode, long &nSpeed)
{
    // 入口检测
    if (NULL == pszBuff)
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CAMERA_ID_SIZE, nCamID));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CMD_LENS_SIZE, nCtrCode));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CMD_LENS_SIZE, nSpeed));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdPanControl(char* pszBuff, long &nCamID, long &nCtrCode, long &nSpeed)
{
    // 入口检测
    if (NULL == pszBuff)
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CAMERA_ID_SIZE, nCamID));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CMD_LENS_SIZE, nCtrCode));
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CMD_LENS_SIZE, nSpeed));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::PaseRetGetCustZone(char* pszBuff, CVideoCustomZoneList* plistCustZone)
{
	// 入口检测
	if ((NULL == pszBuff) || (NULL == plistCustZone))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	long nZoneID = VIDEO_ID_INVALIDATION;		// 分区ID
	char szName[VIDEO_NAME_MAXSIZE];			// 分区名称
	char szUserName[VIDEO_NAME_MAXSIZE];		// 分区名称
	char szDesp[VIDEO_DESCRIPTION_MAXSIZE];		// 分区描述
	
	long nOff = 0;

	// 分区个数
	long nCount = 0;
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_ZONE_ID_SIZE, nCount));
	
	plistCustZone->clear();
	for(int i=0; i<nCount; i++)
	{
		CVideoCustomZone theZone;
		// 分区id
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_ZONE_ID_SIZE, nZoneID));
		theZone.SetID(nZoneID);
		// 分区名称
		VIDEO_CHECKFAIL_RETURN(ParseBufferToName(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, szName));
		theZone.SetName(szName);
		// 用户名称
		VIDEO_CHECKFAIL_RETURN(ParseBufferToName(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, szUserName));
		theZone.SetUserName(szUserName);
		// 分区描述
		VIDEO_CHECKFAIL_RETURN(ParseBufferToDesc(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, szDesp));
		theZone.SetDesc(szDesp);
		// 插入分区
		plistCustZone->InsertObject(theZone);
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::PaseCmdAddCustZone(char* pszBuff, char* pszCustZoneName, long nNameBufLen, char* pszCustZoneDesc, long nDescBufLen)
{
	// 入口检测
	if ((NULL == pszBuff) || (NULL == pszCustZoneName) || (nNameBufLen < VIDEO_NAME_MAXSIZE) || (nDescBufLen < VIDEO_DESCRIPTION_MAXSIZE))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long nOff = 0;

	// 分区名称
	VIDEO_CHECKFAIL_RETURN(ParseBufferToName(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, pszCustZoneName));
	// 分区描述
	VIDEO_CHECKFAIL_RETURN(ParseBufferToDesc(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, pszCustZoneDesc));

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::PaseCmdDelCustZone(char* pszBuff, char* pszCustZoneName, long nNameBufLen)
{
	// 入口检测
	if ((NULL == pszBuff) || (NULL == pszCustZoneName) || (nNameBufLen < VIDEO_NAME_MAXSIZE))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long nOff = 0;
	long nBufferSize = 1024;
	
	// 分区名称
	VIDEO_CHECKFAIL_RETURN(ParseBufferToName(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, pszCustZoneName));

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::PaseCmdGetCustZoneCam(char* pszBuff, char* pszCustZoneName, long nNameBufLen)
{
	// 入口检测
	if ((NULL == pszBuff) || (NULL == pszCustZoneName) || (nNameBufLen < VIDEO_NAME_MAXSIZE))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	long nOff = 0;
	
	// 分区名称
	VIDEO_CHECKFAIL_RETURN(ParseBufferToName(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, pszCustZoneName));
	
	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::PaseRetGetCustZoneCam(char* pszBuff, CVideoCameraList *plistCustZoneCam)
{
	// 入口检测
	if ((NULL == pszBuff) || (NULL == plistCustZoneCam))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	long nOff = 0;

	plistCustZoneCam->clear();
	VIDEO_CHECKFAIL_RETURN(ParseBufferToCameraList(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, plistCustZoneCam));

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::PaseCmdCpyCustZoneCam(char* pszBuff, char* pszCustZoneName, long nNameBufLen, CVideoList<CStrName> *plistCustZoneCamName)
{
	// 入口检测
	if ((NULL == pszBuff) || (NULL == pszCustZoneName) || (NULL == plistCustZoneCamName) || (nNameBufLen < VIDEO_NAME_MAXSIZE))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long nOff = 0;

	// 分区名称
	VIDEO_CHECKFAIL_RETURN(ParseBufferToName(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, pszCustZoneName));
	
	// 摄像头个数
	long nCount = 0;
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CAMERA_SET_SIZE, nCount));
	
	plistCustZoneCamName->clear();
	for(int i=0; i<nCount; i++)
	{
		// 摄像头名称
		CStrName strName;
		// 摄像头名称
		VIDEO_CHECKFAIL_RETURN(ParseBufferToName(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, strName.m_szName));
		// 插入摄像头
		plistCustZoneCamName->InsertObject(strName);
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::PaseCmdDelCustZoneCam(char* pszBuff, char* pszCustZoneName, long nNameBufLen, CVideoList<CStrName> *plistCustZoneCamName)
{
	// 入口检测
	if ((NULL == pszBuff) || (NULL == pszCustZoneName) || (NULL == plistCustZoneCamName) || (nNameBufLen < VIDEO_NAME_MAXSIZE))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	long nOff = 0;

	// 分区名称
	VIDEO_CHECKFAIL_RETURN(ParseBufferToName(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, pszCustZoneName));
	
	// 摄像头个数
	long nCount = 0;
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CAMERA_SET_SIZE, nCount));
	
	plistCustZoneCamName->clear();
	for(int i=0; i<nCount; i++)
	{
		// 摄像头名称
		CStrName strName;
		// 摄像头名称
		VIDEO_CHECKFAIL_RETURN(ParseBufferToName(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, strName.m_szName));
		// 插入摄像头
		plistCustZoneCamName->InsertObject(strName);
	}
	
	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdAddRecord(char* pszBuff, CRecord *pRec, char*& pRecordBuffer, long &nRecSize)
{
	// 入口检测
    if ((NULL == pszBuff) || (NULL == pRec))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

	long nOff = 0;
	long nStartTime = 0;
	long nEndTime = 0;
	char szExtent[VIDEO_EXTENT_MAXSIZE];
	char szFileName[VIDEO_FILE_NAME_MAXSIZE];
	char szDesc[VIDEO_DESCRIPTION_MAXSIZE];
	memset(szExtent, 0x00, sizeof(szExtent));
	memset(szFileName, 0x00, sizeof(szFileName));
	memset(szDesc, 0x00, sizeof(szDesc));
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_TIME_LENGTH_SIZE, nStartTime));
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_TIME_LENGTH_SIZE, nEndTime));
	VIDEO_CHECKFAIL_RETURN(ParseSubBuffer(pszBuff, &nOff, szExtent, VIDEO_MESSAGE_MAX_SIZE, VIDEO_EXTENT_LENGTH_SIZE, sizeof(szExtent)));
	VIDEO_CHECKFAIL_RETURN(ParseSubBuffer(pszBuff, &nOff, szFileName, VIDEO_MESSAGE_MAX_SIZE, VIDEO_EXTENT_LENGTH_SIZE, sizeof(szFileName)));
	VIDEO_CHECKFAIL_RETURN(ParseSubBuffer(pszBuff, &nOff, szDesc, VIDEO_MESSAGE_MAX_SIZE, VIDEO_DESP_LENGTH_SIZE, sizeof(szDesc)));
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_SNAP_BUFFER_LENGTH_SIZE, nRecSize));
	pRecordBuffer = pszBuff + nOff;

	pRec->SetTimeStart(nStartTime);
	pRec->SetTimeEnd(nEndTime);
	pRec->SetExtent(szExtent);
	pRec->SetFileName(szFileName);
	pRec->SetDesc(szDesc);

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseRetAddRecord(char* pszBuff, long &nRecordID)
{
	// 入口检测
    if (NULL == pszBuff)
        return EC_ICV_CCTV_FUNCPARAMINVALID;
	
    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_SNAP_ID_SIZE, nRecordID));
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdQueryRecord(char* pszBuff, long &nCamID, long &nStartTime, long &nEndTime)
{
	// 入口检测
    if (NULL == pszBuff)
        return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	long nOff = 0;
	
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CAMERA_ID_SIZE, nCamID));
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_TIME_LENGTH_SIZE, nStartTime));
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_TIME_LENGTH_SIZE, nEndTime));

	return ICV_SUCCESS;
}


// 说明参见.h文件
long CVideoProtoParser::ParseRetQueryRecord(char* pszBuff, CRecordList* pListRecord)
{
	// 入口检测
    if ((NULL == pszBuff) || (NULL == pListRecord))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
	
    long nOff = 0;
	
	// 录像个数
	long nCount = 0;
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CAMERA_SET_SIZE, nCount));
	
	pListRecord->clear();
	for(int i=0; i<nCount; i++)
	{
		// 录像
		long nRecordID = 0;
		long nCamID = 0;
		long nStartTime = 0;
		long nEndTime = 0;
		char szExtent[VIDEO_EXTENT_MAXSIZE];
		char szFileName[VIDEO_FILE_NAME_MAXSIZE];
		char szDesc[VIDEO_DESCRIPTION_MAXSIZE];
		memset(szExtent, 0x00, sizeof(szExtent));
		memset(szFileName, 0x00, sizeof(szFileName));
		memset(szDesc, 0x00, sizeof(szDesc));
		long nStartSnapID = 0;
		long nMidSnapID = 0;
		long nEndSnapID = 0;
		// 录像
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_SNAP_ID_SIZE, nRecordID));
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CAMERA_ID_SIZE, nCamID));
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_TIME_LENGTH_SIZE, nStartTime));
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_TIME_LENGTH_SIZE, nEndTime));
		VIDEO_CHECKFAIL_RETURN(ParseSubBuffer(pszBuff, &nOff, szExtent, VIDEO_MESSAGE_MAX_SIZE, VIDEO_EXTENT_LENGTH_SIZE, sizeof(szExtent)));
		VIDEO_CHECKFAIL_RETURN(ParseSubBuffer(pszBuff, &nOff, szFileName, VIDEO_MESSAGE_MAX_SIZE, VIDEO_EXTENT_LENGTH_SIZE, sizeof(szFileName)));
		VIDEO_CHECKFAIL_RETURN(ParseSubBuffer(pszBuff, &nOff, szDesc, VIDEO_MESSAGE_MAX_SIZE, VIDEO_DESP_LENGTH_SIZE, sizeof(szDesc)));
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_SNAP_ID_SIZE, nStartSnapID));
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_SNAP_ID_SIZE, nMidSnapID));
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_SNAP_ID_SIZE, nEndSnapID));
		// 插入录像
		CRecord theRecord;
		theRecord.SetID(nRecordID);
		theRecord.SetCameraID(nCamID);
		theRecord.SetTimeStart(nStartTime);
		theRecord.SetTimeEnd(nEndTime);
		theRecord.SetExtent(szExtent);
		theRecord.SetFileName(szFileName);
		theRecord.SetDesc(szDesc);
		theRecord.SetStartSnapID(nStartSnapID);
		theRecord.SetMidSnapID(nMidSnapID);
		theRecord.SetEndSnapID(nEndSnapID);
		pListRecord->InsertObject(theRecord);
	}
    
    return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdModifyRecord(char* pszBuff, CRecord* pRecord)
{
	// 入口检测
    if ((NULL == pszBuff) || (NULL == pRecord))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lOffset = 0;
	
	long nRecordID = 0;
	char szExtent[VIDEO_DEFAULTINFO_MAXSIZE];
	char szFileName[VIDEO_NAME_MAXSIZE];
	char szDesc[VIDEO_DESCRIPTION_MAXSIZE];
	memset(szExtent, 0x00, sizeof(szExtent));
	memset(szFileName, 0x00, sizeof(szFileName));
	memset(szDesc, 0x00, sizeof(szDesc));
	long nStartSnapID = 0;
	long nMidSnapID = 0;
	long nEndSnapID = 0;
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &lOffset, VIDEO_MESSAGE_MAX_SIZE, VIDEO_SNAP_ID_SIZE, nRecordID));
	VIDEO_CHECKFAIL_RETURN(ParseSubBuffer(pszBuff, &lOffset, szExtent, VIDEO_MESSAGE_MAX_SIZE, VIDEO_EXTENT_LENGTH_SIZE, VIDEO_DEFAULTINFO_MAXSIZE));
	VIDEO_CHECKFAIL_RETURN(ParseSubBuffer(pszBuff, &lOffset, szDesc, VIDEO_MESSAGE_MAX_SIZE, VIDEO_DESP_LENGTH_SIZE, VIDEO_DESCRIPTION_MAXSIZE));
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &lOffset, VIDEO_MESSAGE_MAX_SIZE, VIDEO_SNAP_ID_SIZE, nStartSnapID));
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &lOffset, VIDEO_MESSAGE_MAX_SIZE, VIDEO_SNAP_ID_SIZE, nMidSnapID));
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &lOffset, VIDEO_MESSAGE_MAX_SIZE, VIDEO_SNAP_ID_SIZE, nEndSnapID));
	pRecord->SetID(nRecordID);
	pRecord->SetExtent(szExtent);
	pRecord->SetFileName(szFileName);
	pRecord->SetDesc(szDesc);
	pRecord->SetStartSnapID(nStartSnapID);
	pRecord->SetMidSnapID(nMidSnapID);
	pRecord->SetEndSnapID(nEndSnapID);

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseRetDownloadRecord(char* pszBuff, char* pszFileName, long &nFileSize)
{
	// 入口检测
    if ((NULL == pszBuff) || (NULL == pszFileName))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	long lOffset = 0;
	VIDEO_CHECKFAIL_RETURN(ParseSubBuffer(pszBuff, &lOffset, pszFileName, VIDEO_MESSAGE_MAX_SIZE, VIDEO_EXTENT_LENGTH_SIZE, VIDEO_FILE_NAME_MAXSIZE));
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &lOffset, VIDEO_MESSAGE_MAX_SIZE, VIDEO_SNAP_BUFFER_LENGTH_SIZE, nFileSize));

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdVideoSwitch(char* pszBuff, long &nCamID, long &nMonID)
{
	// 入口检测
    if (NULL == pszBuff)
        return EC_ICV_CCTV_FUNCPARAMINVALID;

	long lOffset = 0;
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &lOffset, VIDEO_MESSAGE_MAX_SIZE, VIDEO_CAMERA_ID_SIZE, nCamID));
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &lOffset, VIDEO_MESSAGE_MAX_SIZE, VIDEO_MONITOR_ID_SIZE, nMonID));

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseRetVideoSwitch(char* pszBuff, CReSwitchChannelList* pListSwitch)
{
	// 入口检测
    if ((NULL == pszBuff) || (NULL == pListSwitch))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

	long nProductID = 0;						// 产品型号ID
	long nDevID = 0;							// 设备ID
	char szName[VIDEO_NAME_MAXSIZE];			// 名称
	char szIP[VIDEO_IPADDRESS_MAXSIZE];			// IP地址
	long nPort = 0;								// 设备端口
	char szExtent[VIDEO_DEFAULTINFO_MAXSIZE];	// 扩展属性
	char szDesp[VIDEO_DESCRIPTION_MAXSIZE];		// 描述
	long nLinkDevCount = 0;						// 连接设备个数

	char szLocalChannel[VIDEO_CHANNEL_MAXSIZE];	// 本设备物理通道
	long nLinkDevID = 0;						// 连接设备ID
	char szLinkDevChannel[VIDEO_CHANNEL_MAXSIZE];// 连接设备物理通道	
    char szInChannel[VIDEO_CHANNEL_MAXSIZE];	// 输入通道
    char szOutChannel[VIDEO_CHANNEL_MAXSIZE];	// 输出通道

	// 设备个数
	long nCount = 0;
	long lOff = 0;
	long lBufferSize = strlen(pszBuff);
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &lOff, lBufferSize, VIDEO_DEVICE_ID_SIZE, nCount));

	pListSwitch->clear();
	for(long i=0; i<nCount; i++)
	{
		CVideoDevice theDev;
		// 产品型号ID
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &lOff, lBufferSize, VIDEO_PRODUCT_ID_SIZE, nProductID));
		theDev.SetProductID(nProductID);
		// 设备ID
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &lOff, lBufferSize, VIDEO_DEVICE_ID_SIZE, nDevID));
		theDev.SetID(nDevID);
		// 设备名称
		VIDEO_CHECKFAIL_RETURN(ParseBufferToName(pszBuff, &lOff, lBufferSize, szName));
		theDev.SetName(szName);
		// IP地址
		VIDEO_CHECKFAIL_RETURN(ParseBufferToIP(pszBuff, &lOff, lBufferSize, szIP));
		theDev.SetIP(szIP);
		// 设备端口
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &lOff, lBufferSize, VIDEO_DEVICE_PORT_SIZE, nPort));
		theDev.SetPort((unsigned short)nPort);
		// 设备扩展属性
		VIDEO_CHECKFAIL_RETURN(ParseBufferToString(pszBuff, &lOff, lBufferSize, VIDEO_EXTENT_LENGTH_SIZE, szExtent, VIDEO_DEFAULTINFO_MAXSIZE));
		theDev.SetExtent(szExtent);
		// 设备描述
		VIDEO_CHECKFAIL_RETURN(ParseBufferToDesc(pszBuff, &lOff, lBufferSize, szDesp));
		theDev.SetDescription(szDesp);
		// 设备连接信息
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &lOff, lBufferSize, VIDEO_LINK_DEVICE_COUNT_SIZE, nLinkDevCount));
		for(long j=0; j<nLinkDevCount; j++)
		{
			CVideoLinkDevice theLinkDev;
			// 本设备物理通道
			VIDEO_CHECKFAIL_RETURN(ParseBufferToChannel(pszBuff, &lOff, lBufferSize, szLocalChannel));
			theLinkDev.SetLocalChannel(szLocalChannel);
			// 连接设备ID
			VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuff, &lOff, lBufferSize, VIDEO_DEVICE_ID_SIZE, nLinkDevID));
			theLinkDev.SetDeviceID(nLinkDevID);
			// 连接设备物理通道
			VIDEO_CHECKFAIL_RETURN(ParseBufferToChannel(pszBuff, &lOff, lBufferSize, szLinkDevChannel));
			theLinkDev.SetChannel(szLinkDevChannel);
			// 插入连接设备
			theDev.GetLinkDev()->InsertObject(theLinkDev);
		}
		// 插入设备
		CReSwitchChannel theSwitch;
		theSwitch.SetDev(&theDev);
		// 输入通道
		VIDEO_CHECKFAIL_RETURN(ParseBufferToChannel(pszBuff, &lOff, lBufferSize, szInChannel));
		theSwitch.SetInChannel(szInChannel);
		// 输出通道
		VIDEO_CHECKFAIL_RETURN(ParseBufferToChannel(pszBuff, &lOff, lBufferSize, szOutChannel));
		theSwitch.SetOutChannel(szOutChannel);
		pListSwitch->InsertObject(theSwitch);
	}
	
	return ICV_SUCCESS;
}




long CVideoProtoParser::ParseName(const char* pszBuffer, long* pnOff, char* pszName, const long nBufferSize)
{
	return ParseBufferToString(pszBuffer, pnOff, nBufferSize, VIDEO_NAME_LENGTH_SIZE, pszName, VIDEO_NAME_MAXSIZE);
}

long CVideoProtoParser::ParseDescription(const char* pszBuffer, long* pnOff, char* pszDescription, const long nBufferSize)
{
	return ParseBufferToString(pszBuffer, pnOff, nBufferSize, VIDEO_DESP_LENGTH_SIZE, pszDescription, VIDEO_DESCRIPTION_MAXSIZE);
}
	
long CVideoProtoParser::ParseIPAddress(const char* pszBuffer, long* pnOff, char* pszIP, const long nBufferSize)
{
	return ParseBufferToString(pszBuffer, pnOff, nBufferSize, VIDEO_IP_LENGTH_SIZE, pszIP, VIDEO_IPADDRESS_MAXSIZE);
}

long CVideoProtoParser::ParseBufferToCamera(const char* pszBuffer, long* plOffset, const long nBufferSize, CVideoCamera* pCamera)
{
	long nCamID = 0;							// 摄像头ID
	char szName[VIDEO_NAME_MAXSIZE];			// 名称
	char szDesp[VIDEO_DESCRIPTION_MAXSIZE];		// 描述
	bool bCtrlStatus = false;					// 控制状态
	long nSnapInfo = 0;							// 抓拍信息
	long nLinkDevCount = 0;						// 连接设备个数
	long nLinkDevID = 0;						// 连接设备ID

    char szLinkDevChannel[VIDEO_CHANNEL_MAXSIZE];// 连接设备物理通道

	// 摄像头ID
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, nBufferSize, VIDEO_CAMERA_ID_SIZE, nCamID));
	pCamera->SetID(nCamID);
	// 摄像头名称
	VIDEO_CHECKFAIL_RETURN(ParseBufferToName(pszBuffer, plOffset, nBufferSize, szName));
	pCamera->SetName(szName);
	// 摄像头描述
	VIDEO_CHECKFAIL_RETURN(ParseBufferToDesc(pszBuffer, plOffset, nBufferSize, szDesp));
	pCamera->SetDescription(szDesp);
	// 控制操作状态
	long lTmpValue = 0;
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, nBufferSize, VIDEO_STATUS_SIZE, lTmpValue));
	pCamera->SetIsRemoteFileCtrl(lTmpValue == VIDEO_CTRL_STATUS_ENABLE);

	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, nBufferSize, VIDEO_STATUS_SIZE, lTmpValue));
	pCamera->SetIsLocalFileCtrl(lTmpValue == VIDEO_CTRL_STATUS_ENABLE);

	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, nBufferSize, VIDEO_STATUS_SIZE, lTmpValue));
	pCamera->SetIsRemoteTimeCtrl(lTmpValue == VIDEO_CTRL_STATUS_ENABLE);

	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, nBufferSize, VIDEO_STATUS_SIZE, lTmpValue));
	pCamera->SetIsOrientCtrl(lTmpValue == VIDEO_CTRL_STATUS_ENABLE);

	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, nBufferSize, VIDEO_STATUS_SIZE, lTmpValue));
	pCamera->SetIsPSPCtrl(lTmpValue == VIDEO_CTRL_STATUS_ENABLE);

	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, nBufferSize, VIDEO_STATUS_SIZE, lTmpValue));
	pCamera->SetIsLensCtrl(lTmpValue == VIDEO_CTRL_STATUS_ENABLE);

	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, nBufferSize, VIDEO_STATUS_SIZE, lTmpValue));
	pCamera->SetIsQualityCtrl(lTmpValue == VIDEO_CTRL_STATUS_ENABLE);

	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, nBufferSize, VIDEO_STATUS_SIZE, lTmpValue));
	pCamera->SetIsSnapCtrl(lTmpValue == VIDEO_CTRL_STATUS_ENABLE);

	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, nBufferSize, VIDEO_STATUS_SIZE, lTmpValue));
	pCamera->SetIsHeaterCtrl(lTmpValue == VIDEO_CTRL_STATUS_ENABLE);
	
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, nBufferSize, VIDEO_STATUS_SIZE, lTmpValue));
	pCamera->SetIsRainBrushCtrl(lTmpValue == VIDEO_CTRL_STATUS_ENABLE);
	
	// 抓拍信息
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, nBufferSize, VIDEO_SNAP_WIDTH_SIZE, nSnapInfo));
	pCamera->SetSnapWidth(nSnapInfo);
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, nBufferSize, VIDEO_SNAP_HEIGHT_SIZE, nSnapInfo));
	pCamera->SetSnapHeight(nSnapInfo);
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, nBufferSize, VIDEO_SNAP_BITDEEP_SIZE, nSnapInfo));
	pCamera->SetSnapBitDeep(nSnapInfo);
	
	// 设备连接信息
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, nBufferSize, VIDEO_LINK_DEVICE_COUNT_SIZE, nLinkDevCount));

	CVideoLinkDeviceList *plist = pCamera->GetLinkDev();
	if (NULL != plist)
		plist->clear();
	CVideoLinkDevice theLinkDev;
	for(long j=0; j<nLinkDevCount; j++)
	{
		// 连接设备ID
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, nBufferSize, VIDEO_DEVICE_ID_SIZE, nLinkDevID));
		theLinkDev.SetDeviceID(nLinkDevID);
		// 连接设备物理通道
		VIDEO_CHECKFAIL_RETURN(ParseBufferToChannel(pszBuffer, plOffset, nBufferSize, szLinkDevChannel));
		theLinkDev.SetChannel(szLinkDevChannel);
		// 控制状态
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, nBufferSize, VIDEO_STATUS_SIZE, lTmpValue));
		theLinkDev.SetIsCtrlDevice(lTmpValue == VIDEO_CTRL_STATUS_ENABLE);
		// 实时播放
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, nBufferSize, VIDEO_STATUS_SIZE, lTmpValue));
		theLinkDev.SetIsVideoSourceDevice(lTmpValue == VIDEO_CTRL_STATUS_ENABLE);
		// 历史录像管理
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, nBufferSize, VIDEO_STATUS_SIZE, lTmpValue));
		theLinkDev.SetIsHistoryDevice(lTmpValue == VIDEO_CTRL_STATUS_ENABLE);
		// 插入连接设备
		pCamera->GetLinkDev()->InsertObject(theLinkDev);
	}

	CVideoLinkDeviceList* pVLDList = pCamera->GetLinkDev();
	for(int ij = 0; ij < pVLDList->GetSize(); ij++)
	{
		CVideoLinkDevice *pVLD = pVLDList->GetAt(ij);
	}

	return ICV_SUCCESS;
}

long CVideoProtoParser::ParseBufferToMonitor(const char* pszBuffer, long* plOffset, const long nBufferSize, CVideoMonitor* pMonitor)
{
	long nMonID = 0;							    // 监视器ID
	char szName[VIDEO_NAME_MAXSIZE];			    // 名称
	char szDesp[VIDEO_DESCRIPTION_MAXSIZE];		    // 描述
	long nLinkDevID = 0;						    // 连接设备ID
	char szLinkDevChannel[VIDEO_CHANNEL_MAXSIZE];	// 连接设备物理通道
    long nLinkDevType = VIDEO_DEVICE_TYPE_NONE;     // 连接设备类型
	// 监视器ID
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, nBufferSize, VIDEO_MONITOR_ID_SIZE, nMonID));
	pMonitor->SetID(nMonID);

	// 监视器名称
	VIDEO_CHECKFAIL_RETURN(ParseBufferToName(pszBuffer, plOffset, nBufferSize, szName));
	pMonitor->SetName(szName);

	// 监视器描述
	VIDEO_CHECKFAIL_RETURN(ParseBufferToDesc(pszBuffer, plOffset, nBufferSize, szDesp));
	pMonitor->SetDescription(szDesp);
	
	// 连接设备
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, nBufferSize, VIDEO_DEVICE_ID_SIZE, nLinkDevID));
	pMonitor->GetLinkDev()->SetDeviceID(nLinkDevID);
	VIDEO_CHECKFAIL_RETURN(ParseBufferToChannel(pszBuffer, plOffset, nBufferSize, szLinkDevChannel));
	pMonitor->GetLinkDev()->SetChannel(szLinkDevChannel);
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, nBufferSize, VIDEO_DEVICE_TYPE_SIZE, nLinkDevType));
    pMonitor->GetLinkDev()->SetLinkDevType(nLinkDevType);

	return ICV_SUCCESS;
}

long CVideoProtoParser::ParseSubBuffer(const char* pszBuffer, long* pnOff, char* pszOutSubBuffer, const long nBufferSize, 
										const long nOutSubBufferLengthSize, const long nMaxOutSubBufferSize)
{
	return ParseBufferToString(pszBuffer, pnOff, nBufferSize,  nOutSubBufferLengthSize, pszOutSubBuffer,nMaxOutSubBufferSize);
}

long CVideoProtoParser::ParseBufferToInteger(const char* pszBuffer, long* plOffset, const long lBufferSize, 
											   const long lIntBufferSize, long &lValue)
{
	// 入口检测
	if(*plOffset + lIntBufferSize > lBufferSize)
	{
		// 将偏移量更新,防止后面的解析出问题
		*plOffset += lIntBufferSize;
		return EC_ICV_CCTV_PARSERBUFFERTOOSHORT;
	}

	// 获取整数buffer
	char szInteger[VIDEO_PARSE_TEXT_SIZE];
	memset(szInteger, 0x00, VIDEO_PARSE_TEXT_SIZE);
	memcpy(szInteger, pszBuffer + *plOffset, lIntBufferSize);
	
	// 更新偏移量
	*plOffset += lIntBufferSize;

	// 返回整数值
	lValue = atoi(szInteger);
	return ICV_SUCCESS;
}

/* 参数说明：
[in]const char* pszBuffer			原始buffer
[in/out]long* plOffset				开始偏移量[in],解析后的偏移量[out]
[out]char* pszOutString				解析后的buffer输出字符串
[in]const long lOutStringBufSize	pszOutString的缓冲区长度
*/
long CVideoProtoParser::ParseBufferToString(const char* pszBuffer, long* plOffset, const long lBufferSize, const long lOutStringLenSize,
											  char* pszOutString, const long lOutStringBufSize)
{
	// 入口检测
	if(*plOffset + lOutStringLenSize > lBufferSize)
	{
		// 将偏移量更新,防止后面的解析出问题
		*plOffset += lOutStringLenSize;
		return EC_ICV_CCTV_PARSERBUFFERTOOSHORT;
	}

	char szLength[VIDEO_PARSE_TEXT_SIZE];	// 字符串长度字符数
	long nLength = 0;						// 字符串长度
	memset(szLength, 0x00, VIDEO_PARSE_TEXT_SIZE);
	memcpy(szLength, pszBuffer + *plOffset, lOutStringLenSize);
	// 更新偏移量
	*plOffset += lOutStringLenSize;
	nLength = atoi(szLength);
	if (nLength<=0)
	{
//		return EC_ICV_CCTV_RETMSGTOOSHORT;
	}
	memset(pszOutString, 0x00, lOutStringBufSize);
		
	// 继续异常检测
	if(*plOffset + nLength > lBufferSize)
	{
		// 设置异常标志位
		// 将偏移量更新,防止后面的解析出问题
		*plOffset += nLength;
		return EC_ICV_CCTV_PARSERBUFFERTOOSHORT;
	}

	// 解析出string
	memcpy(pszOutString, pszBuffer + *plOffset, nLength);
	*plOffset += nLength;

	return ICV_SUCCESS;
}

// Client Parser use
long CVideoProtoParser::ParseBufferToName(const char* pszBuffer, long* plOffset, const long lBufferSize, char* pszName)
{
	return ParseBufferToString(pszBuffer, plOffset, lBufferSize, VIDEO_NAME_LENGTH_SIZE, pszName, VIDEO_NAME_MAXSIZE);
}

long CVideoProtoParser::ParseBufferToChannel(const char* pszBuffer, long* plOffset, const long lBufferSize, char* pszChannel)
{
    return ParseBufferToString(pszBuffer, plOffset, lBufferSize, VIDEO_DEVICE_CHANNEL_ID_SIZE, pszChannel, VIDEO_CHANNEL_MAXSIZE);
}

long CVideoProtoParser::ParseBufferToDesc(const char* pszBuffer, long* plOffset, const long lBufferSize, char* pszDescription)
{
	return ParseBufferToString(pszBuffer, plOffset, lBufferSize, VIDEO_DESP_LENGTH_SIZE, pszDescription, VIDEO_DESCRIPTION_MAXSIZE);
}


long CVideoProtoParser::ParseToSubBuffer(const char* pszBuffer, long* plOffset, const long lBufferSize, const long nOutSubBufferLengthSize, 
										   char* pszOutSubBuffer, const long nMaxOutSubBufferSize)
{
	return ParseBufferToString(pszBuffer, plOffset, lBufferSize, nOutSubBufferLengthSize, pszOutSubBuffer, nMaxOutSubBufferSize);
}


long CVideoProtoParser::ParseBufferToIP(const char* pszBuffer, long* plOffset, const long nBufferSize, char* pszIP)
{
	return ParseBufferToString(pszBuffer, plOffset, nBufferSize, VIDEO_IP_LENGTH_SIZE, pszIP, VIDEO_IPADDRESS_MAXSIZE);
}
//////////////////////////////////////////////////////////////////////////


long CVideoProtoParser::ParseBufferToZoneList(const char* pszBuffer, long* plOffset, const long lBufferSize,
												CVideoZoneList* plistZone)
{
	// 入口检测
	if((pszBuffer == NULL) || (plistZone == NULL))
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}

 	long nZoneID = VIDEO_ID_INVALIDATION;		// 分区ID
	char szName[VIDEO_NAME_MAXSIZE];			// 分区名称
	char szDesp[VIDEO_DESCRIPTION_MAXSIZE];		// 分区描述
	long nAuthField = 0;	// 权限范围

	// 分区个数
	long nCount = 0;
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, lBufferSize, VIDEO_ZONE_ID_SIZE, nCount));

	CVideoZone theZone;
	plistZone->clear();
	
	for(long i=0; i<nCount; i++)
	{
		// 分区id
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, lBufferSize, VIDEO_ZONE_ID_SIZE, nZoneID));
		theZone.SetID(nZoneID);
		// 分区名称
		VIDEO_CHECKFAIL_RETURN(ParseBufferToName(pszBuffer, plOffset, lBufferSize, szName));
		theZone.SetName(szName);
		// 分区描述
		VIDEO_CHECKFAIL_RETURN(ParseBufferToDesc(pszBuffer, plOffset, lBufferSize, szDesp));
		theZone.SetDescription(szDesp);
		// 权限范围
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, lBufferSize, VIDEO_AUTH_LEVEL_FIELD_SIZE, nAuthField));
		theZone.SetAuthField(nAuthField);
		// 插入分区
		plistZone->InsertObject(theZone);
	}

	return ICV_SUCCESS;
}

long CVideoProtoParser::ParseBufferToProductList(const char* pszBuffer, long* plOffset, const long lBufferSize,
												 CVideoProductList* plistProduct)
{
	// 入口检测
	if((pszBuffer == NULL) || (plistProduct == NULL))
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}

	long nProductID = 0;						// 产品型号ID
	long nDevType = VIDEO_DEVICE_TYPE_NONE;		// 设备类型
	char szName[VIDEO_NAME_MAXSIZE];			// 名称

	// 产品型号个数
	long nCount = 0;
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, lBufferSize, VIDEO_PRODUCT_ID_SIZE, nCount));

	plistProduct->clear();

	for(long i=0; i<nCount; i++)
	{
		CVideoProduct theProduct;
		// 设备类型
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, lBufferSize, VIDEO_DEVICE_TYPE_SIZE, nDevType));
		theProduct.SetDeviceType(nDevType);
		// 产品型号ID
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, lBufferSize, VIDEO_PRODUCT_ID_SIZE, nProductID));
		theProduct.SetID(nProductID);
		// 产品型号名称
		VIDEO_CHECKFAIL_RETURN(ParseBufferToName(pszBuffer, plOffset, lBufferSize, szName));
		theProduct.SetName(szName);
		// 插件名称
		VIDEO_CHECKFAIL_RETURN(ParseBufferToName(pszBuffer, plOffset, lBufferSize, szName));
		theProduct.SetDllName(szName);
		// 插入插件
		plistProduct->InsertObject(theProduct);
	}
	
	return ICV_SUCCESS;
}

long CVideoProtoParser::ParseBufferToDeviceList(const char* pszBuffer, long* plOffset, const long lBufferSize,
												  CVideoDeviceList* plistDevice)
{
	// 入口检测
	if((pszBuffer == NULL) || (plistDevice == NULL))
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}

 	long nProductID = 0;						// 产品型号ID
	long nDevID = 0;							// 设备ID
	char szName[VIDEO_NAME_MAXSIZE];			// 名称
	char szIP[VIDEO_IPADDRESS_MAXSIZE];			// IP地址
	long nPort = 0;								// 设备端口
	char szExtent[VIDEO_DEFAULTINFO_MAXSIZE];	// 扩展属性
	char szDesp[VIDEO_DESCRIPTION_MAXSIZE];		// 描述
	long nLinkDevCount = 0;						// 连接设备个数
	char szLocalChannel[VIDEO_CHANNEL_MAXSIZE];	// 本设备物理通道
	long nLinkDevID = 0;						// 连接设备ID
	char szLinkDevChannel[VIDEO_CHANNEL_MAXSIZE];// 连接设备物理通道
	
	// 设备个数
	long nCount = 0;
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, lBufferSize, VIDEO_DEVICE_ID_SIZE, nCount));

	CVideoDevice theDev;
	CVideoLinkDevice theLinkDev;
	plistDevice->clear();
	
	for(long i=0; i<nCount; i++)
	{
		// 产品型号ID
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, lBufferSize, VIDEO_PRODUCT_ID_SIZE, nProductID));
		theDev.SetProductID(nProductID);
		// 设备ID
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, lBufferSize, VIDEO_DEVICE_ID_SIZE, nDevID));
		theDev.SetID(nDevID);
		// 设备名称
		VIDEO_CHECKFAIL_RETURN(ParseBufferToName(pszBuffer, plOffset, lBufferSize, szName));
		theDev.SetName(szName);
		// IP地址
		VIDEO_CHECKFAIL_RETURN(ParseBufferToIP(pszBuffer, plOffset, lBufferSize, szIP));
		theDev.SetIP(szIP);
		// 设备端口
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, lBufferSize, VIDEO_DEVICE_PORT_SIZE, nPort));
		theDev.SetPort((unsigned short)nPort);
		// 设备扩展属性
		VIDEO_CHECKFAIL_RETURN(ParseBufferToString(pszBuffer, plOffset, lBufferSize, VIDEO_EXTENT_LENGTH_SIZE, szExtent, VIDEO_DEFAULTINFO_MAXSIZE));
		theDev.SetExtent(szExtent);
		// 设备描述
		VIDEO_CHECKFAIL_RETURN(ParseBufferToDesc(pszBuffer, plOffset, lBufferSize, szDesp));
		theDev.SetDescription(szDesp);
		// 设备连接信息
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, lBufferSize, VIDEO_LINK_DEVICE_COUNT_SIZE, nLinkDevCount));
		for(long j=0; j<nLinkDevCount; j++)
		{
			// 本设备物理通道
			VIDEO_CHECKFAIL_RETURN(ParseBufferToChannel(pszBuffer, plOffset, lBufferSize, szLocalChannel));
			theLinkDev.SetLocalChannel(szLocalChannel);
			// 连接设备ID
			VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, lBufferSize, VIDEO_DEVICE_ID_SIZE, nLinkDevID));
			theLinkDev.SetDeviceID(nLinkDevID);
			// 连接设备物理通道
			VIDEO_CHECKFAIL_RETURN(ParseBufferToChannel(pszBuffer, plOffset, lBufferSize, szLinkDevChannel));
			theLinkDev.SetChannel(szLinkDevChannel);
			// 插入连接设备
			(theDev.GetLinkDev())->InsertObject(theLinkDev);
		}
		// 插入设备
		plistDevice->InsertObject(theDev);
	}
	
	return ICV_SUCCESS;
}


long CVideoProtoParser::ParseBufferToCameraList(const char* pszBuffer, long* plOffset, const long lBufferSize, CVideoCameraList* plistCamera)
{
	// 入口检测
	if((pszBuffer == NULL) || (plistCamera == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

 	long nZoneID = 0;	// 分区ID
	
	// 摄像头个数
	long nCount = 0;
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, lBufferSize, VIDEO_CAMERA_ID_SIZE, nCount));
	CVideoCamera theCam;
	plistCamera->clear();
	
	for(long i=0; i<nCount; i++)
	{
		// 分区ID
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, lBufferSize, VIDEO_ZONE_ID_SIZE, nZoneID));
		theCam.SetZoneID(nZoneID);
		// 解释摄像头
		VIDEO_CHECKFAIL_RETURN(ParseBufferToCamera(pszBuffer, plOffset, lBufferSize, &theCam));
		// 插入设备
		plistCamera->InsertObject(theCam);
	}
	
	return ICV_SUCCESS;
}


long CVideoProtoParser::ParseBufferToSimpleCameraList(const char* pszBuffer, const long nBufferSize, CVideoCameraList* plistCamera)
{
	// 入口检测
	if((pszBuffer == NULL) || (plistCamera == NULL))
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}

	long nOff = 0;						// 偏移量
	long nZoneID = 0;					// 分区ID
	long nCamID = 0;					// 摄像头ID
	char szName[VIDEO_NAME_MAXSIZE];	// 摄像头名称
	
	// 摄像头个数
	long nCount = 0;
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, &nOff, nBufferSize, VIDEO_CAMERA_ID_SIZE, nCount));
	CVideoCamera theCam;
	plistCamera->clear();
	
	for(long i=0; i<nCount; i++)
	{
		// 分区ID
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, &nOff, nBufferSize, VIDEO_ZONE_ID_SIZE, nZoneID));
		theCam.SetZoneID(nZoneID);
		// 摄像头ID
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, &nOff, nBufferSize, VIDEO_CAMERA_ID_SIZE, nCamID));
		theCam.SetID(nCamID);
		// 摄像头名称
		VIDEO_CHECKFAIL_RETURN(ParseName(pszBuffer, &nOff, szName, nBufferSize));
		theCam.SetName(szName);
		// 插入设备
		plistCamera->InsertObject(theCam);
	}

	return ICV_SUCCESS;
}


long CVideoProtoParser::ParseBufferToCameraListOfZone(const char* pszBuffer, const long nBufferSize, CVideoCameraList* plistCamera)
{
	// 入口检测
	if((pszBuffer == NULL) || (plistCamera == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long nOff = 0;		// 偏移量
	long nZoneID = 0;	// 分区ID
	
	CVideoCamera theCam;
	// 分区ID
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, &nOff, nBufferSize, VIDEO_ZONE_ID_SIZE, nZoneID));
	theCam.SetZoneID(nZoneID);
	
	// 摄像头个数
	long nCount = 0;
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, &nOff, nBufferSize, VIDEO_CAMERA_ID_SIZE, nCount));
	plistCamera->clear();
	
	for(long i=0; i<nCount; i++)
	{
		// 解释摄像头
		VIDEO_CHECKFAIL_RETURN(ParseBufferToCamera(pszBuffer, &nOff, nBufferSize, &theCam));
		// 插入设备
		plistCamera->InsertObject(theCam);
	}

	return ICV_SUCCESS;
}


long CVideoProtoParser::ParseBufferToSimpleCameraListOfZone(const char* pszBuffer, const long nBufferSize, CVideoCameraList* plistCamera)
{
	// 入口检测
	if((pszBuffer == NULL) || (plistCamera == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long nOff = 0;						// 偏移量
	long nZoneID = 0;					// 分区ID
	long nCamID = 0;					// 摄像头ID
	char szName[VIDEO_NAME_MAXSIZE];	// 摄像头名称
	
	CVideoCamera theCam;
	// 分区ID
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, &nOff, nBufferSize, VIDEO_ZONE_ID_SIZE, nZoneID));
	theCam.SetZoneID(nZoneID);
	
	// 摄像头个数
	long nCount = 0;
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, &nOff, nBufferSize, VIDEO_CAMERA_ID_SIZE, nCount));
	plistCamera->clear();
	
	for(long i=0; i<nCount; i++)
	{
		// 摄像头ID
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, &nOff, nBufferSize, VIDEO_ZONE_ID_SIZE, nCamID));
		theCam.SetID(nCamID);
		// 摄像头名称
		VIDEO_CHECKFAIL_RETURN(ParseName(pszBuffer, &nOff, szName, nBufferSize));
		theCam.SetName(szName);
		// 插入设备
		plistCamera->InsertObject(theCam);
	}

	return ICV_SUCCESS;
}

long CVideoProtoParser::ParseBufferToMonitorList(const char* pszBuffer, long* plOffset, const long lBufferSize,
												   CVideoMonitorList* plistMonitor)
{
	// 入口检测
	if((pszBuffer == NULL) || (plistMonitor == NULL))
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}

 	long nZoneID = 0;							// 分区ID
	
	// 监视器个数
	long nCount = 0;
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, lBufferSize, VIDEO_MONITOR_ID_SIZE, nCount));
	CVideoMonitor theMon;
	plistMonitor->clear();
	
	for(long i=0; i<nCount; i++)
	{
		// 分区ID
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, lBufferSize, VIDEO_ZONE_ID_SIZE, nZoneID));
		theMon.SetZoneID(nZoneID);
		// 监视器
		VIDEO_CHECKFAIL_RETURN(ParseBufferToMonitor(pszBuffer, plOffset, lBufferSize, &theMon));
		// 插入设备
		plistMonitor->InsertObject(theMon);
	}

	return ICV_SUCCESS;
}



long CVideoProtoParser::ParseBufferToMonitorListOfZone(const char* pszBuffer, const long nBufferSize, CVideoMonitorList* plistMonitor)
{
	// 入口检测
	if((pszBuffer == NULL) || (plistMonitor == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long nOff = 0;								// 偏移量
	long nZoneID = 0;							// 分区ID
	
	CVideoMonitor theMon;
	// 分区ID
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, &nOff, nBufferSize, VIDEO_ZONE_ID_SIZE, nZoneID));
	theMon.SetZoneID(nZoneID);

	// 监视器个数
	long nCount = 0;
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, &nOff, nBufferSize, VIDEO_MONITOR_ID_SIZE, nCount));
	plistMonitor->clear();
	
	for(long i=0; i<nCount; i++)
	{
		// 监视器
		VIDEO_CHECKFAIL_RETURN(ParseBufferToMonitor(pszBuffer, &nOff, nBufferSize, &theMon));
		// 插入设备
		plistMonitor->InsertObject(theMon);
	}

	return ICV_SUCCESS;
}

long CVideoProtoParser::ParseBufferToMode(IN const char* pszBuffer, IN OUT long *plOffset, IN const long lBufferSize, 
											OUT CVideoMode* pMode, IN const bool bPara)
{
	// 入口检测
	if((pszBuffer == NULL) || (pMode == NULL) || !plOffset)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long nModeType = VIDEO_MODETYPE_SHOW;		// 模式类型
	char szName[VIDEO_NAME_MAXSIZE];			// 模式名称
	char szDesp[VIDEO_DESCRIPTION_MAXSIZE];		// 模式描述
	
	// 模式类型
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, lBufferSize, VIDEO_MODE_TYPE_SIZE, nModeType));
	pMode->SetType(nModeType);
	// 模式名称
	VIDEO_CHECKFAIL_RETURN(ParseBufferToName(pszBuffer, plOffset, lBufferSize, szName));
	pMode->SetName(szName);
	// 模式描述
	VIDEO_CHECKFAIL_RETURN(ParseBufferToDesc(pszBuffer, plOffset, lBufferSize, szDesp));
	pMode->SetDescription(szDesp);
	if(bPara)
	{
		// 模式参数
		char szPara[VIDEO_MODE_PARA_MAXSIZE];
		VIDEO_CHECKFAIL_RETURN(ParseBufferToString(pszBuffer, plOffset, lBufferSize, VIDEO_MODE_PARA_LENGTH_SIZE, szPara, VIDEO_MODE_PARA_MAXSIZE));
		long nParaLength = strlen(szPara);
		if(nParaLength > 0)
		{
			// 偏移量重置为0
			long lParaOff = 0;
			// 模式涉及的分区个数
			long nZoneCount = 0;
			VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(szPara, &lParaOff, nParaLength, VIDEO_ZONE_ID_SIZE, nZoneCount));
			long nZoneID = VIDEO_ID_INVALIDATION;
			for(long j=0; j<nZoneCount; j++)
			{
				// 分区ID
				VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(szPara, &lParaOff, nParaLength, VIDEO_ZONE_ID_SIZE, nZoneID));
				pMode->GetListZone()->InsertObject(nZoneID);
			}
		}
		// 模式参数,包括涉及的分区
		pMode->SetPara(szPara);
	}

	return ICV_SUCCESS;
}

long CVideoProtoParser::ParseBufferToModeList(const char* pszBuffer, long *plOffset, const long lBufferSize, CVideoModeList* plistMode, bool bIncPara)
{
	long lRet = ICV_SUCCESS;

	// 入口检测
	if((pszBuffer == NULL) || (plistMode == NULL))
		return EC_ICV_CCTV_FAILTOPARSEMODEBUFF;

	// 模式个数
	long nCount = 0;
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, lBufferSize, VIDEO_MODE_COUNT_SIZE, nCount));

	plistMode->clear();
	for(long i=0; i<nCount; i++)
	{
		CVideoMode theMode;
		VIDEO_CHECKFAIL_RETURN(ParseBufferToMode(pszBuffer, plOffset, lBufferSize, &theMode, bIncPara));
		
		// 插入模式
		plistMode->InsertObject(theMode);
	}
	
	return ICV_SUCCESS;
}


long CVideoProtoParser::ParseBufferToPSP(const char* pszBuffer, const long nBufferSize, CVideoPSP* pPSP)
{
	// 入口检测
	if((pszBuffer == NULL) || (pPSP == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long nOff = 0;								// 偏移量
	char szName[VIDEO_NAME_MAXSIZE];			// 模式名称
	long nPSPIndex = 0;							// 预置位序号
	char szDesp[VIDEO_DESCRIPTION_MAXSIZE];		// 模式描述

	// 预置位名称
	VIDEO_CHECKFAIL_RETURN(ParseName(pszBuffer, &nOff, szName, nBufferSize));
	pPSP->SetName(szName);
	// 预置位描述
	VIDEO_CHECKFAIL_RETURN(ParseDescription(pszBuffer, &nOff, szDesp, nBufferSize));
	pPSP->SetDescription(szDesp);
	// 预置位序号
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, &nOff, nBufferSize, VIDEO_PSP_INDEX_SIZE, nPSPIndex));
	pPSP->SetPresetIndex(nPSPIndex);

	return ICV_SUCCESS;
}


long CVideoProtoParser::ParseBufferToPSPList(const char* pszBuffer, long* plOffset, const long lBufferSize, CVideoPSPList* plistPSP)
{
	// 入口检测
	if((pszBuffer == NULL) || (plistPSP == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	char szName[VIDEO_NAME_MAXSIZE];			// 模式名称
	long nPSPIndex = 0;							// 预置位序号
	char szDesp[VIDEO_DESCRIPTION_MAXSIZE];	// 模式描述
	
	// 模式个数
	long nCount = 0;
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, lBufferSize, VIDEO_PSP_COUNT_SIZE, nCount));
	CVideoPSP thePSP;
	plistPSP->clear();
	
	for(long i=0; i<nCount; i++)
	{
		// 预置位名称
		VIDEO_CHECKFAIL_RETURN(ParseBufferToName(pszBuffer, plOffset, lBufferSize, szName));
		thePSP.SetName(szName);
		// 预置位描述
		VIDEO_CHECKFAIL_RETURN(ParseBufferToDesc(pszBuffer, plOffset, lBufferSize, szDesp));
		thePSP.SetDescription(szDesp);
		// 预置位序号
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, plOffset, lBufferSize, VIDEO_PSP_INDEX_SIZE, nPSPIndex));
		thePSP.SetPresetIndex(nPSPIndex);
		// 插入预置位
		plistPSP->InsertObject(thePSP);
	}

	return ICV_SUCCESS;
}

long CVideoProtoParser::ParseBufferToSnapFile(const char* pszBuffer, const long nBufferSize, CVideoSnapFile* pSnapFile, long* pnOff)
{
	// 入口检测
	if((pszBuffer == NULL) || (pSnapFile == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long nOff = 0;								// 偏移量
	long nCamID = 0;							// 摄像头ID
	char szInfoType[VIDEO_NAME_MAXSIZE];		// 抓拍信息类型
	time_t tmSnap = 0;							// 抓拍时间
	long nSnapCount = 0;						// 连续抓拍张数
	char szExt[VIDEO_EXTENT_MAXSIZE];			// 扩展信息
	char szDesp[VIDEO_DESCRIPTION_MAXSIZE];		// 抓拍描述

	// 摄像头ID
// 	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, &nOff, nBufferSize, VIDEO_CAMERA_ID_SIZE, nCamID));
// 	pSnapFile->SetCameraID(nCamID);
	// 信息类型
	VIDEO_CHECKFAIL_RETURN(ParseBufferToString(pszBuffer, &nOff, nBufferSize, VIDEO_NAME_LENGTH_SIZE, szInfoType, sizeof(szInfoType)));
	pSnapFile->SetInfoType(szInfoType);
	// 抓拍时间
	long lsnap =0;
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, &nOff, nBufferSize, VIDEO_TIME_LENGTH_SIZE, lsnap));
	tmSnap = lsnap;
	pSnapFile->SetSnapTime(tmSnap);
	// 扩展信息
	VIDEO_CHECKFAIL_RETURN(ParseBufferToString(pszBuffer, &nOff, nBufferSize, VIDEO_EXTENT_LENGTH_SIZE, szExt, sizeof(szExt)));
	pSnapFile->SetExtent(szExt);
	// 抓拍描述
	VIDEO_CHECKFAIL_RETURN(ParseDescription(pszBuffer, &nOff, szDesp, nBufferSize));
	pSnapFile->SetDescription(szDesp);
	// 连续抓拍张数
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, &nOff, nBufferSize, VIDEO_SERIAL_SNAP_COUNT_SIZE, nSnapCount));
	pSnapFile->SetSnapCount(nSnapCount);
	// 设置偏移量
	*pnOff += nOff;

	return ICV_SUCCESS;
}

long CVideoProtoParser::ParseBufferToSnapFileList(const char* pszBuffer, const long nBufferSize, CVideoSnapFileList* plistSnapFile)
{
	// 入口检测
	if((pszBuffer == NULL) || (plistSnapFile == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long nOff = 0;								// 偏移量
	long nSnapID = 0;							// 抓拍图片ID
	long nCamID = 0;							// 摄像头ID
	char szInfoType[VIDEO_NAME_MAXSIZE];		// 抓拍信息类型
	time_t tmSnap = 0;							// 抓拍时间
	long nSnapCount = 0;						// 连续抓拍张数
	char szExtent[VIDEO_DEFAULTINFO_MAXSIZE];	// 扩展属性
	char szDesp[VIDEO_DESCRIPTION_MAXSIZE];		// 抓拍描述

	// 图片数量
	long nCount = 0;
	VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, &nOff, nBufferSize, VIDEO_SNAP_ID_SIZE, nCount));

	CVideoSnapFile theSnap;
	plistSnapFile->clear();
	
	for(long i=0; i<nCount; i++)
	{
		// 图片ID
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, &nOff, nBufferSize, VIDEO_SNAP_ID_SIZE, nSnapID));
		theSnap.SetID(nSnapID);
		// 摄像头ID
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, &nOff, nBufferSize, VIDEO_CAMERA_ID_SIZE, nCamID));
		theSnap.SetCameraID(nCamID);
		// 信息类型
		VIDEO_CHECKFAIL_RETURN(ParseBufferToString(pszBuffer, &nOff, nBufferSize, VIDEO_NAME_LENGTH_SIZE, szInfoType, VIDEO_NAME_MAXSIZE));
		theSnap.SetInfoType(szInfoType);
		// 抓拍时间
		long lsnap =0;
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, &nOff, nBufferSize, VIDEO_TIME_LENGTH_SIZE, lsnap));
		tmSnap = lsnap;
		theSnap.SetSnapTime(tmSnap);
		// 抓拍扩展属性
		VIDEO_CHECKFAIL_RETURN(ParseBufferToString(pszBuffer, &nOff, nBufferSize, VIDEO_EXTENT_LENGTH_SIZE, szExtent, VIDEO_DEFAULTINFO_MAXSIZE));
		theSnap.SetDescription(szExtent);
		// 抓拍描述
		VIDEO_CHECKFAIL_RETURN(ParseDescription(pszBuffer, &nOff, szDesp, nBufferSize));
		theSnap.SetDescription(szDesp);
		// 连续抓拍张数
		VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, &nOff, nBufferSize, VIDEO_SERIAL_SNAP_COUNT_SIZE, nSnapCount));
		theSnap.SetSnapCount(nSnapCount);
		// 插入
		plistSnapFile->InsertObject(theSnap);
	}

	return ICV_SUCCESS;
}

// 解析出请求命令的头部信息, 我们需要的头部信息包括: 命令号/StampID/用户名/群组名
long CVideoProtoParser::ParseRequestCmdHeaderInfo(IN const char* pszBuffer, IN const long nBufferSize, long &lOffset,
							OUT long &lCmdCode, OUT long &lStampID, 
							OUT char *szUserName, IN long lUserNameLen, OUT char *szGrpName, IN long lGrpNameLen)
{
	// 入口检测
	if((pszBuffer == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	// 1.命令号
	long lRet = ParseBufferToInteger(pszBuffer, &lOffset, nBufferSize, VIDEO_CMD_SIZE, lCmdCode);
	if(lRet != ICV_SUCCESS)
		return lRet;
	
	// 2.命令序列号
	lRet = ParseBufferToInteger(pszBuffer, &lOffset, nBufferSize, VIDEO_CMD_STAMP_SIZE, lStampID);
	if(lRet != ICV_SUCCESS)
		return lRet;
	
	// 3. 用户名长度(2) 用户名 
	lRet = ParseSubBuffer(pszBuffer, &lOffset, szUserName, nBufferSize, VIDEO_NAME_LENGTH_SIZE, sizeof(szUserName));
	if(lRet != ICV_SUCCESS)
		return lRet;

	// 群组名长度(2) 群组名
	lRet = ParseSubBuffer(pszBuffer, &lOffset, szGrpName, nBufferSize, VIDEO_NAME_LENGTH_SIZE, sizeof(szGrpName));
	if(lRet != ICV_SUCCESS)
		return lRet;	

	return lRet;
}

//解析获取图片内容
long CVideoProtoParser::ParseGetSnapPicBuffer(const char* pszBuffer, const long nBufferSize, long &nCount, char *&pBuffer, long &nPicLen)
{
	long lOffSet = 0;

	//连续抓拍张数(2) [图片内容长度(8) 图片内容]
	int nCountSize = 2;
	long lRet = ParseBufferToInteger(pszBuffer, &lOffSet, nBufferSize, nCountSize, nCount);
	if(lRet != ICV_SUCCESS)
		return lRet;

	//图片内容
	nPicLen = nBufferSize-nCountSize;
	pBuffer = new char[nPicLen+1];
	memset(pBuffer, 0, sizeof(pBuffer));
	memcpy(pBuffer, pszBuffer+nCountSize, nPicLen);

	return ICV_SUCCESS;
}

long CVideoProtoParser::ParseBufferToCStrNameList(const char* pszBuffer, IN const long nBufferSize, OUT CVideoList<CStrName>* plistCStrName)
{
    // 入口检测
    if((pszBuffer == NULL) || (plistCStrName == NULL))
        return EC_ICV_CCTV_FUNCPARAMINVALID;
    
    long nOff = 0;
    
    // 数量
    long nCount = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToInteger(pszBuffer, &nOff, nBufferSize, VIDEO_SNAPTYPE_COUNT_SIZE, nCount));
    plistCStrName->clear();
    
    for(long i=0; i<nCount; i++)
    {
        CStrName strName;
        VIDEO_CHECKFAIL_RETURN(ParseName(pszBuffer, &nOff, strName.m_szName, nBufferSize));
        plistCStrName->InsertObject(strName);
    }
    
	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoProtoParser::ParseCmdGetMonitorName(char* pszBuff, char *pszMonitorName, long nMonitorNameLen)
{
    // 入口检测
    if ((NULL == pszBuff) || (NULL == pszMonitorName) || (VIDEO_NAME_MAXSIZE > nMonitorNameLen))
        return EC_ICV_CCTV_FUNCPARAMINVALID;

    long nOff = 0;
    VIDEO_CHECKFAIL_RETURN(ParseBufferToString(pszBuff, &nOff, VIDEO_MESSAGE_MAX_SIZE, VIDEO_NAME_LENGTH_SIZE, pszMonitorName, nMonitorNameLen));
    
    return ICV_SUCCESS;
} 
