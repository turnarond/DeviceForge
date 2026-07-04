// GlobalManager.h: interface for the CGlobalManager class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_GLOBALMANAGER_H__078F4930_F6B4_4C82_9BC0_C7C1EDD4DF8D__INCLUDED_)
#define AFX_GLOBALMANAGER_H__078F4930_F6B4_4C82_9BC0_C7C1EDD4DF8D__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

//#include <ace/Null_Mutex.h>
#include <ace/Singleton.h>
#include <ace/Task.h>
//#include <ace/RW_Process_Mutex.h>
#include <ace/Process_Mutex.h>
#include "multimedia/video/VideoRAPIDef.h"
#include "common/CVLog.h"

class CGlobalManager  
{
friend class ACE_Singleton<CGlobalManager, ACE_Thread_Mutex>;
public:
	CGlobalManager();
	virtual ~CGlobalManager();
	long Init();
	long UnInit();   
public:
	const char* GetRTDPath();
	int GetRegClientNum() { return m_pClientList.size(); }
	long IsServerExist(char *szIp, bool &bRet);
	long GetServPort() { return m_lServPort; }
	HQUEUE GetServQue(char * szIp);
	long RegisterServer(SERV_INFO &serv);
	long RegisterClient(CLIENT_INFO &client, HVideoClienT* pHClient);
	long UnRegisterClient(HVideoClienT hClient);
	
	bool IsClientRegistered(HVideoClienT hClient);
	
	long ClearAllServer();
	
private:
	//所有注册的客户端列表
	PCLIENTLIST m_pClientList;
	//服务列表
	SERVERMAP m_ServerMap;
	ACE_Process_Mutex m_CliMapMutex;
	long m_lServPort;
};

typedef ACE_Singleton<CGlobalManager, ACE_Thread_Mutex> GlobalManager;
#define GM_HELPER GlobalManager::instance()

#endif // !defined(AFX_GLOBALMANAGER_H__078F4930_F6B4_4C82_9BC0_C7C1EDD4DF8D__INCLUDED_)
