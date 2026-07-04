/**************************************************************
 *  Filename:    VideoTimeSynchCfg.cpp
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: $(读取配置文件).
 *
 *  @author:     zhucongfeng
 *  @version     08/28/2009  zhucongfeng  Initial Version
 *  @version     08/30/2009  zhucongfeng  修改读取备机IP地址的错误
 *  @version	 06/23/2010  zhucongfeng  删除CVCommon的声明，直接用后台导出的定义
**************************************************************/
// VideoTimeSynchCfg.cpp: implementation of the CVideoTimeSynchCfg class.
//
//////////////////////////////////////////////////////////////////////

#include "VideoTimeSynchCfg.h"
#include "common/cvcomm.hxx"
#include "tinyxml/tinyxml.h"
#include "common/cvGlobalHelper.h"
#include "gettext/libintl.h"

#define _(STRING) gettext(STRING)
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
#define	VIDEO_TIME_SYNCH_CFGFILE_NAME		"VideoTimeSynchService.xml"
#define	VIDEO_MAIN_SERVER_IP				"MainSvrIP"
#define	VIDEO_BAK_SERVER_IP					"BakSvrIP"
#define	VIDEO_TIME_SYNCH_INTERVAL			"InterValTime"

CVideoTimeSynchCfg::CVideoTimeSynchCfg()
{
	memset(m_szError, 0, sizeof(m_szError));
	memset(m_szMainSvrIP, 0, sizeof(m_szMainSvrIP));
	memset(m_szBakSvrIP, 0, sizeof(m_szBakSvrIP));
	m_lIntervalTime = 60;
	m_bIsExistBak = false;
}

CVideoTimeSynchCfg::~CVideoTimeSynchCfg()
{

}

/**
 *  $(读取配置文件).
 *  $(包括主机IP、备机IP、间隔时间等).
 *
 *  @return $(正确，返回0；否则非0).
 *
 *  @version     08/27/2009  zhucongfeng  Initial Version.
 */

long CVideoTimeSynchCfg::LoadXMLConfig()
{
	long lRet = ICV_SUCCESS;
	char szXmlCfgPath[ICV_LONGFILENAME_MAXLEN + 1];
	memset(szXmlCfgPath, 0, sizeof(szXmlCfgPath));
	
	// 获取配置目录
	CVStringHelper::Safe_StrNCpy(szXmlCfgPath, CVComm.GetCVProjCfgPath(), sizeof(szXmlCfgPath));
	
	// 获取配置文件名称
	if(strlen(szXmlCfgPath) + strlen(VIDEO_TIME_SYNCH_CFGFILE_NAME) >= ICV_LONGFILENAME_MAXLEN)
	{
		lRet = EC_ICV_CCTV_FILEPATHNAMETOOLONG;
		CVLog.LogMessage(LOG_LEVEL_ERROR, _("Config file <%s\\%s> length exceed maximum length<%d>, RetCode:%d."),
			szXmlCfgPath, VIDEO_TIME_SYNCH_CFGFILE_NAME, ICV_LONGFILENAME_MAXLEN, lRet);
		return lRet;
	}
	strcat(szXmlCfgPath, ACE_DIRECTORY_SEPARATOR_STR);
	strcat(szXmlCfgPath, VIDEO_TIME_SYNCH_CFGFILE_NAME);
	
	// 建立tinyxml的文档对象
	TiXmlDocument doc;
	
	// 解析xml文件名
	if(!doc.LoadFile(szXmlCfgPath))
	{
		lRet = EC_ICV_CCTV_FAILTOLOADXMLCFGFILE;
		CVLog.LogMessage(LOG_LEVEL_ERROR, _("Load config file(%s) error, RetCode:%d"), szXmlCfgPath, lRet);
		return lRet;
	}
	
	// 解析文件
	TiXmlElement *pRootElem = doc.RootElement();
	// 确认节点在xml文件里存在
	if(pRootElem == NULL)
	{
		lRet = EC_ICV_CCTV_FAILTOPARSEXMLCFGFILE;
		CVLog.LogMessage(LOG_LEVEL_ERROR, _("Parse config file(%s) information error, cann't find node 'VideoTimeSynchService', RetCode:%d"), szXmlCfgPath, lRet);
		return lRet;
	}
	
	// 获取主机IP
	const char* MainIP = pRootElem->Attribute(VIDEO_MAIN_SERVER_IP);
	if((MainIP == NULL) || strcmp(MainIP, "") == 0 || strlen(MainIP) >= ICV_IPSTRING_MAXLEN)
	{
		lRet = EC_ICV_CCTV_FAILTOPARSEXMLCFGFILE;
		CVLog.LogMessage(LOG_LEVEL_ERROR, _("Get main server ip failed, cann't find %s in config file, RetCode:%d"), 
			VIDEO_MAIN_SERVER_IP, lRet);
		return lRet;
	}

	CVStringHelper::Safe_StrNCpy(m_szMainSvrIP, MainIP, sizeof(m_szMainSvrIP));

	// 获取备机IP
	const char* BakIP = pRootElem->Attribute(VIDEO_BAK_SERVER_IP);
	if((BakIP != NULL) && strcmp(BakIP, "") != 0)
	{
		if(strlen(BakIP) >= ICV_IPSTRING_MAXLEN)
		{
			lRet = EC_ICV_CCTV_FAILTOPARSEXMLCFGFILE;
			CVLog.LogMessage(LOG_LEVEL_ERROR, _("Get bak server ip failed, cann't find %s in config file, RetCode:%d"), 
				VIDEO_BAK_SERVER_IP, lRet);
			return lRet;
		}
		else
		{
			CVStringHelper::Safe_StrNCpy(m_szBakSvrIP, BakIP, sizeof(m_szBakSvrIP));
			m_bIsExistBak = true;
		}
	}
	
	// 获取重试时间间隔，验证合法性
	const char* IntervalTime = pRootElem->Attribute(VIDEO_TIME_SYNCH_INTERVAL);
	if((IntervalTime == NULL) || strcmp(IntervalTime, "") == 0)
	{
		lRet = EC_ICV_CCTV_FAILTOPARSEXMLCFGFILE;
		CVLog.LogMessage(LOG_LEVEL_ERROR, _("Get interval time failed, cann't find %s in config file, RetCode:%d"), 
			VIDEO_TIME_SYNCH_INTERVAL, lRet);
		return lRet;
	}
	
	// 设置时间间隔
	m_lIntervalTime = atoi(IntervalTime);
	if(m_lIntervalTime <= 0)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, _("Interval time error: %d"), m_lIntervalTime);
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	
	CVLog.LogMessage(LOG_LEVEL_INFO, _("Read config files(%s) successfully."), szXmlCfgPath);
	
	return lRet;
}
