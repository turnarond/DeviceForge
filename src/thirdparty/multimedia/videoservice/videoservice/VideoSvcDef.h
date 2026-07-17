#ifndef _VIDEO_SERVICE_DEF_H_DEF_
#define	_VIDEO_SERVICE_DEF_H_DEF_


// ACE header files
#include <ace/SOCK_Stream.h>
#include <ace/Event_Handler.h>
#include <ace/Null_Mutex.h>
#include <ace/Synch.h>
#include <ace/Message_Queue.h>
#include <ace/Singleton.h>
#include <ace/Task.h>
#include <ace/ACE.h>
#include <common/OS.h>
#include <ace/Get_Opt.h>
#include <ace/Process_Mutex.h>

// stl header files
#include <iostream>
#include <string>
#include <map>
#include "tinyxml/tinyxml.h"
#include "tinyxml/tinystr.h"

// CC header files
#include "common/cvGlobalHelper.h"
#include "common/cvcomm.hxx"
#include "common/CommHelper.h"
#include "common/CVNDK.h"
#include "common/CVLog.h"
#include "common/RMAPI.h"
#include "eventalarm/EAAcceptor.h"
#include "errcode/ErrCode_iCV_Common.hxx"
#include "errcode/ErrCode_iCV_CCTV.hxx"
#include "multimedia/video/VideoStructDef.h"
#include "multimedia/video/VideoDefine.h"
#include "multimedia/video/VideoProtoPacker.h"
#include "multimedia/video/VideoProtoParser.h"

// assure that NULL string converted to "", to avoid TiXMLElement::SetAttribute(x, NULL) exception sometimes
#define	VIDEO_STRING(x)	(x == NULL ? "" : x)

// 接受接口的消息, 对视频服务进行启动和重启
#define		SERVICE_EVENT_NONE							0	// 服务维护当前状态
#define		SERVICE_EVENT_START							1	// 服务启动
#define		SERVICE_EVENT_RESTART						2	// 服务重启
#define		SERVICE_EVENT_STOP							3	// 服务停止

#define	LOCK_OBJECT_TYPE_CAMERA							1	// 加锁的对象类型为摄像头
#define	LOCK_OBJECT_TYPE_MONITOR						2	// 加锁的对象类型为监视器

#define VIDEO_LOCAL_SERVER_FILE_NAME	"VideoServer.xml"
#define	VIDEO_LOG_SERVICE_FILE_NAME		"VideoService"
#define VIDEO_DB_INFO_FILE_NAME			"VideoInfo.db"

// XML中的节点名称
#define VIDEO_SERVER_NODE_LOCALIP		"LocalIP"

typedef TiXmlDocument CCXmlParser;
typedef TiXmlElement CCXmlElement;

using namespace std;

class CDevCtrlTask;
class CDriverTask : public CVideoProduct  
{
protected:
	CDevCtrlTask * m_pDevCtrlTask;							// 如果是本地服务进行控制, 对应的设备控制任务指针
	
public:
	void SetCtrlTask(CDevCtrlTask *pTask){m_pDevCtrlTask = pTask;}
	CDevCtrlTask *GetCtrlTask(){return m_pDevCtrlTask;}

public:
	CDriverTask():CVideoProduct()
	{
		m_pDevCtrlTask = NULL;
	}
	CDriverTask(CVideoProduct &Product):CVideoProduct(Product)
	{
		m_pDevCtrlTask = NULL;
	}

};
typedef list<CDriverTask *> CDriverTaskList;

class CDevCtrlTask;
class CVideoCameraEx : public CVideoCamera  
{
protected:
	bool m_bLocalCtrlSvr;								// 是否是本地控制服务对应的摄像头
	CDevCtrlTask * m_pDevCtrlTask;							// 如果是本地服务进行控制, 对应的设备控制任务指针
	CVideoServer *m_pCtrlSvr;							// 如果是远程服务进行控制, 对应的服务指针
	char m_szAuthLabel[VIDEO_NAME_MAXSIZE];			// 摄像头对应的分区对应的权限标签ID
	bool m_bCanCtrl;
public:
	void SetCanCtrl(bool bCanCtrl){m_bCanCtrl = bCanCtrl;}
	bool GetCanCtrl(){return m_bCanCtrl;}
	void SetIsLocalCtrlSvr(bool bLocalCtrlSvr){m_bLocalCtrlSvr = bLocalCtrlSvr;}
	bool GetIsLocalCtrlSvr(){return m_bLocalCtrlSvr;}
	void SetCtrlSvr(CVideoServer *pSvr){m_pCtrlSvr = pSvr;}
	CVideoServer *GetCtrlSvr(){return m_pCtrlSvr;}
	void SetDevCtrlTask(CDevCtrlTask *pTask){m_pDevCtrlTask = pTask;}
	CDevCtrlTask *GetDevCtrlTask(){return m_pDevCtrlTask;}

	void SetAuthLabel(char *szAuthLabel){CVStringHelper::Safe_StrNCpy(m_szAuthLabel, szAuthLabel, sizeof(m_szAuthLabel)) ;}
	char *GetAuthLabel(){return m_szAuthLabel;}

public:
	CVideoCameraEx():CVideoCamera()
	{
		m_bLocalCtrlSvr = true;
		m_pDevCtrlTask = NULL;
		m_pCtrlSvr = NULL;
		memset(m_szAuthLabel, 0,  sizeof(m_szAuthLabel));
	}
	
	CVideoCameraEx(CVideoCamera &camera):
					CVideoCamera(camera)
	{
		m_bLocalCtrlSvr = false;
		m_pDevCtrlTask = NULL;
		m_pCtrlSvr = NULL;
		memset(m_szAuthLabel, 0,  sizeof(m_szAuthLabel));
	}

	virtual ~CVideoCameraEx() 
	{
		m_pCtrlSvr = NULL;
		m_pDevCtrlTask = NULL;
	}
};

typedef std::map<long, CVideoCameraEx *> CVideoCameraMap;

typedef ACE_Singleton<CVideoProtoParser, ACE_Null_Mutex> CVideoProtoParserSingleton;
#define VIDEO_PARSER CVideoProtoParserSingleton::instance()

typedef ACE_Singleton<CVideoProtoPacker, ACE_Null_Mutex> CVideoProtoPackerSingleton;
#define VIDEO_PACKER CVideoProtoPackerSingleton::instance()

extern CCVLog CVLog;

extern void CVLogEvent(const char *szOperator, HQUEUE hQueue, const char *szComment, const char *szFormat, ...);

#endif //_CC_SERVICE_H_DEF_
