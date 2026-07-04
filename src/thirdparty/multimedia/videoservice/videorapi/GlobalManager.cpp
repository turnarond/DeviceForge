/**************************************************************
 *  Filename:    GlobalManager.cpp
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: $(客户端管理).
 *
 *  @author:     zhucongfeng
 *  @version     07/20/2010  zhucongfeng  Initial Version
**************************************************************/

// GlobalManager.cpp: implementation of the CGlobalManager class.
//
//////////////////////////////////////////////////////////////////////

#include "GlobalManager.h"
#include "NetWrapper.h"
#include "common/cvcomm.hxx"

#include "ace/ACE.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

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

extern CCVLog CVLog;

CGlobalManager::CGlobalManager()
{
	m_lServPort = 0;
}

CGlobalManager::~CGlobalManager()
{
	PCLIENTLIST::iterator it = m_pClientList.begin();
	while(it != m_pClientList.end())
	{
		PCLIENT_INFO pClient = *it;
		if(pClient)
			delete pClient;

		it++;
	}

	m_pClientList.clear();
}

/**
 *  $(初始化).
 *  $(Detail).
 *
 *  @return $(成功0；其它非0).
 *
 *  @version     07/20/2010  zhucongfeng  Initial Version.
 */

long CGlobalManager::Init()
{
	long lRet = ICV_SUCCESS;
	m_lServPort = CVComm.GetServicePort("VideoService", 50009);
	return lRet;
}

/**
 *  $(反初始化).
 *  $(Detail).
 *
 *  @return $(成功0；其它非0).
 *
 *  @version     07/20/2010  zhucongfeng  Initial Version.
 */

long CGlobalManager::UnInit()
{
	long lRet = ICV_SUCCESS;
	lRet = ClearAllServer();
	return lRet;
}

/**
 *  $(检测服务是否已经注册过).
 *  $(Detail).
 *
 *  @param  -[in]  char*  szIp: [服务端IP]
 *  @param  -,out]  bool&  bRet: [ture，注册过；false，未注册]
 *  @return $(成功0；其它非0).
 *
 *  @version     07/20/2010  zhucongfeng  Initial Version.
 */

long CGlobalManager::IsServerExist(char *szIp, bool &bRet)
{
	long lRet = ICV_SUCCESS;
	if((szIp == NULL) || (szIp[0] == '\0'))
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	SERVERMAP::iterator it = m_ServerMap.find(szIp);
	if(it != m_ServerMap.end())
	{
		bRet = true;		
	}
	else
	{
		bRet = false;
	}
	return lRet;
}

/**
 *  $(获取注册的服务端的句柄).
 *  $(Detail).
 *
 *  @param  -[in]  char *  szIp: [服务端IP地址]
 *  @return $(成功，返回句柄；否则返回空).
 *
 *  @version     07/20/2010  zhucongfeng  Initial Version.
 */

HQUEUE CGlobalManager::GetServQue(char * szIp)
{
	if((szIp == NULL) || (szIp[0] == '\0'))
	{
		return NULL;
	}
	SERVERMAP::iterator it = m_ServerMap.find(szIp);
	if(it != m_ServerMap.end())
	{
		return it->second.hServerQ;		
	}
	else
	{
		return NULL;
	}
}

/**
 *  $(注册Server).
 *  $(Detail).
 *
 *  @param  -[in]  SERV_INFO&  serv: [服务信息]
 *  @return $(成功0；其它非0).
 *
 *  @version     07/20/2010  zhucongfeng  Initial Version.
 */

long CGlobalManager::RegisterServer(SERV_INFO &serv)
{
	long lRet = ICV_SUCCESS;
	if((serv.szServIp == NULL) || (serv.szServIp[0] == '\0'))
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	m_ServerMap.insert(SERVERMAP::value_type(serv.szServIp, serv));
	return lRet;
}

/**
 *  $(注册客户端).
 *  $(Detail).
 *
 *  @param  -[in]  CLIENT_INFO&  client: [客户端信息]
 *  @param  -[out]  HVideoClienT*  pHClient: [注册的客户端句柄]
 *  @return $(成功0；其它非0).
 *
 *  @version     07/20/2010  zhucongfeng  Initial Version.
 */

long CGlobalManager::RegisterClient(CLIENT_INFO &client, HVideoClienT* pHClient)
{
	long lRet = ICV_SUCCESS;

	//注册客户端
	PCLIENT_INFO pClient = new CLIENT_INFO();
	memcpy(pClient, &client, sizeof(CLIENT_INFO));

	m_pClientList.push_back(pClient);
	*pHClient = pClient;

	return lRet;
}

/**
 *  $(注销客户端).
 *  $(Detail).
 *
 *  @param  -[in]  HVideoClienT  hClient: [要注销的客户端句柄]
 *  @return $(成功0；其它非0).
 *
 *  @version     07/20/2010  zhucongfeng  Initial Version.
 */

long CGlobalManager::UnRegisterClient(HVideoClienT hClient)
{
	long lRet = ICV_SUCCESS;

	//注销客户端
	PCLIENTLIST::iterator it = m_pClientList.begin();
	while(it != m_pClientList.end())
	{
		if((*it != NULL) &&(*it == hClient))
		{
			delete (*it);
			m_pClientList.erase(it);
			break;
		}
		it++;
	}

	return lRet;
}

/**
 *  $(检测客户端是否已经注册过).
 *  $(Detail).
 *
 *  @param  -[in]  HVideoClienT  hClient: [客户端句柄]
 *  @return $(注册过true；没注册过false).
 *
 *  @version     07/20/2010  zhucongfeng  Initial Version.
 */

bool CGlobalManager::IsClientRegistered(HVideoClienT hClient)
{
	bool bRegistered = false;

	if(hClient == NULL)
		return bRegistered;

	PCLIENTLIST::iterator it = m_pClientList.begin();
	while(it != m_pClientList.end())
	{
		PCLIENT_INFO pClient = *it;
		if(pClient == (PCLIENT_INFO)hClient)
		{
			bRegistered = true;
			break;
		}

		it++;
	}

	return bRegistered;
	
}

/**
 *  $(清除所有的server).
 *  $(Detail).
 *
 *  @return $(成功0；其它非0).
 *
 *  @version     07/20/2010  zhucongfeng  Initial Version.
 */

long CGlobalManager::ClearAllServer()
{
	long lRet = ICV_SUCCESS;
	SERVERMAP::iterator it = m_ServerMap.begin();
	for(; it != m_ServerMap.end(); it++)
	{
		NET_WRAPPER->FreeRecvHandle(it->second.hServerQ);
	}
	m_ServerMap.clear();
	return lRet;
}

/**
 *  $(获取RTD路径).
 *  $(Detail).
 *
 *  @return $(返回获取到的RTD路径，若获取失败，返回空).
 *
 *  @version     07/20/2010  zhucongfeng  Initial Version.
 */

const char* CGlobalManager::GetRTDPath()
{
	const char * szRTDPath = NULL;

	//获取rtd路径
	szRTDPath = CVComm.GetCVRunTimeDataPath();
	if((szRTDPath == NULL) || (szRTDPath[0] == '\0'))
	{
		char szPath[VIDEO_FILE_PATH_MAX_SIZE];
		memset(szPath, 0, sizeof(szPath));

#ifdef _WIN32
		GetTempPath(VIDEO_FILE_PATH_MAX_SIZE, szPath);
#else
		strcpy(szPath, "/tmp");
#endif

		szRTDPath = szPath;
	}

	return szRTDPath;
}
