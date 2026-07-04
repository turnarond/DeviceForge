#ifndef  _VIDEORAPI_DEF_H_
#define  _VIDEORAPI_DEF_H_

#include "common/CVNDK.h"
#include "common/CVLog.h"
#include "common/cvGlobalHelper.h"
#include "errcode/ErrCode_iCV_Common.hxx"
#include "multimedia/video/VideoStructDef.h"
#include "multimedia/video/VideoDefineStruct.h"
#include <map>
#include <string>
#include <list>
//using namespace std;

//#pragma warning(disable:4786)

typedef  void* HQUEUE;

#define VIDEO_SERVICE_RECVQUEST_QUEUEID		201		//服务端接收客户端请求的QueueID
#define VIDEO_LENGTH_CLIENT_NAME			1024	//视频客户端名称最大长度
#define ICV_GROUP_MAXLEN ICV_USERNAME_MAXLEN		//群组名称最大长度
#define VIDEO_FILE_PATH_MAX_SIZE			256		//文件路径最大长度

//客户端信息
typedef struct _CLIENT_INFO 
{
	char szCliName[VIDEO_LENGTH_CLIENT_NAME];
	HQUEUE hServerQ;
	HQUEUE hBakServQ;
	bool bIsExistBak;
	char szUserName[ICV_USERNAME_MAXLEN];
	char szGroup[ICV_GROUP_MAXLEN];
	_CLIENT_INFO()
	{
		memset(szCliName, 0, sizeof(szCliName));
		hServerQ = NULL;
		hBakServQ = NULL;
		bIsExistBak = false;
		memset(szUserName, 0, sizeof(szUserName));
		memset(szGroup, 0, sizeof(szGroup));
	}
}CLIENT_INFO, *PCLIENT_INFO;

typedef struct _SERV_INFO
{
	char szServIp[ICV_IPSTRING_MAXLEN];
	HQUEUE hServerQ;
	_SERV_INFO()
	{
		memset(szServIp, 0, sizeof(szServIp));
		hServerQ = NULL;
	}
}SERV_INFO, *pSERV_INFO;


typedef std::list<CLIENT_INFO> CLIENTLIST;
typedef std::list<PCLIENT_INFO> PCLIENTLIST;
typedef std::map<string, CLIENT_INFO> CLIENTMAP;
typedef std::map<string, SERV_INFO> SERVERMAP;

extern CCVLog CVLog;
#endif
