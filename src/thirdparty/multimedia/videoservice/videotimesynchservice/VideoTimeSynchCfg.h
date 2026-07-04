// VideoTimeSynchCfg.h: interface for the CVideoTimeSynchCfg class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_VIDEOTIMESYNCHCFG_H__935A3A6E_9390_48D9_AACA_357D5C023ADC__INCLUDED_)
#define AFX_VIDEOTIMESYNCHCFG_H__935A3A6E_9390_48D9_AACA_357D5C023ADC__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <ace/Null_Mutex.h>
#include <ace/Singleton.h>
#include <multimedia/video/VideoStructDef.h>
#include "errcode/ErrCode_iCV_CCTV.hxx"
#include "VideoTimeSynchDef.h"

class CVideoTimeSynchCfg  
{
public:
	CVideoTimeSynchCfg();
	virtual ~CVideoTimeSynchCfg();

	long LoadXMLConfig();

public:
	char m_szError[VIDEO_LOG_SIZE];						//错误日志
	char m_szMainSvrIP[VIDEO_IPADDRESS_MAXSIZE];		// 主机IP地址
	char m_szBakSvrIP[VIDEO_IPADDRESS_MAXSIZE];			// 备机IP地址
	long m_lIntervalTime;								//校时时间间隔（秒）
	bool m_bIsExistBak;									//是否存在备机

};

typedef ACE_Singleton<CVideoTimeSynchCfg, ACE_Null_Mutex> CVideoTimeSynchCfgSingleton;
#define VIDEO_TIME_SYNCH_CONFIG CVideoTimeSynchCfgSingleton::instance()

#endif // !defined(AFX_VIDEOTIMESYNCHCFG_H__935A3A6E_9390_48D9_AACA_357D5C023ADC__INCLUDED_)
