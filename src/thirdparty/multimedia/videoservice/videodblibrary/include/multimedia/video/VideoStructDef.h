/**************************************************************
 *  Filename:    VideoCommonDefine.h
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: 视频监控系统内部共用宏定义和实体类定义.
 *
 *  @author:     xulizai
 *  @version     2008/05/16  xulizai		Initial Version
				 03/31/2010  chenzhan		将客户端控制的设备按照类别分为实时现实\历史录像和控制设备.
				 14/04/2010  chenzhan		修改监视器摄像头的连接关系定义为设备视频切换关系定义
				 01/12/2011  fengjuanyong   修改CVideoLinkDevice类，增加矩阵链接多个编码器的信息
**************************************************************/
#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef VIDEO_STRUCT_DEF_H_
#define VIDEO_STRUCT_DEF_H_

#include <list>
#include <sys/types.h>
#include "VideoDefine.h"
#include "VideoDefineMacro.h"
#include <sys/stat.h>
#include "common/cvGlobalHelper.h"
#include <string.h>
////////////////////// 客户端与服务端通讯协议相关定义结束 //////////////////

// 字符串定义
class CStrName
{
public:
	CStrName() { memset(m_szName, 0x00, sizeof(m_szName)); }
	~CStrName() {}

	char m_szName[VIDEO_NAME_MAXSIZE];
};

// 定义运行状态检测
#define VIDEO_CHECKFAIL_RETURN(LRETVALUE)	\
	{ \
		long lRet = LRETVALUE; \
		if (lRet != ICV_SUCCESS) \
			return lRet; \
	}

///////////////////////// 视频list/queue相关定义 /////////////////////////
template <class Element>
// 定义CVideoList
class CVideoList : public std::list<Element>
{
	typedef typename CVideoList<Element>::iterator POSITION;
private:
	POSITION m_pos;
public:
	// 获取list长度
	int GetSize();
	// 插入实体
	void InsertObject(Element);
	// 获取某个实体
	Element* GetAt(const int nIndex);
	// 删除指定的实体
	void RemoveAt(const int nIndex);
	// 在某一个指定的位置插入实体
	void SetAt(const int nIndex, Element M);

	CVideoList();
	virtual ~CVideoList() {}
};

template <class Element>
CVideoList<Element>::CVideoList()
{
	m_pos = this->begin();
}

template <class Element>
int CVideoList<Element>::GetSize()
{
	return this->size();
}

template <class Element>
void CVideoList<Element>::RemoveAt(const int nIndex)
{
	if (nIndex >= GetSize() || nIndex < 0)
	{
		return;
	}
	POSITION pos = this->begin();
	int i = nIndex;
	while (i--)
	{
		pos++;
	}

	this->erase(pos);
}

template <class Element>
void CVideoList<Element>::SetAt(int nIndex, Element M)
{
	if (nIndex >= GetSize() || nIndex < 0)
	{
		return;
	}
	POSITION pos = this->begin();
	while (nIndex--)
	{
		pos++;
	}

	*pos = M;
}

template <class Element>
Element* CVideoList<Element>::GetAt(const int nIndex)
{
	if (nIndex >= GetSize() || nIndex < 0)
	{
		return NULL;  // went too far
	}

	m_pos = this->begin();
	int i = nIndex;
	while (i--)
	{
		m_pos++;
	}

	return &(*(m_pos));
}

template <class Element>
void CVideoList<Element>::InsertObject(Element M)
{
	this->push_back(M);
	m_pos = this->begin();
}
///////////////////////// 视频list相关定义结束 /////////////////////////

///////////////////////// 视频服务实体定义 ////////////////////////
class CVideoServer
{
private:
	long m_nID;											// 视频服务ID
	char m_szName[VIDEO_NAME_MAXSIZE];					// 视频服务名称
	char m_szIP[VIDEO_IPADDRESS_MAXSIZE];				// 视频服务主IP
	bool m_bBak;										// 是否有备机
	char m_szBakIP[VIDEO_IPADDRESS_MAXSIZE];			// 视频服务备机IP
	int  m_nLockSeconds;								// 设备锁定时间,单位(秒)
	char m_szDescription[VIDEO_DESCRIPTION_MAXSIZE];	// 视频服务描述
public:		
	CVideoServer()
	{
		m_nID = VIDEO_ID_INVALIDATION;
		memset(m_szName, 0x00, VIDEO_NAME_MAXSIZE);
		memset(m_szIP, 0x00, VIDEO_IPADDRESS_MAXSIZE);
		CVStringHelper::Safe_StrNCpy(m_szIP, VIDEO_DEFAULT_IP, sizeof(m_szIP));
		m_bBak = false;
		memset(m_szBakIP, 0x00, VIDEO_IPADDRESS_MAXSIZE);
		CVStringHelper::Safe_StrNCpy(m_szBakIP, VIDEO_DEFAULT_IP, sizeof(m_szBakIP));
		m_nLockSeconds = VIDEO_DEFAULT_LOCK_TIME;
		memset(m_szDescription, 0x00, VIDEO_DESCRIPTION_MAXSIZE);
	}
	
	// 自定义构造函数
	CVideoServer(const long nID, const char* pszName, const char* pszIP, const bool bBak,
				 const char* pszBakIP, const int nLockSeconds, const char* pszDescription)
	{
		SetID(nID);
		SetName(pszName);
		SetIP(pszIP);
		SetIsBak(bBak);
		SetBakIP(pszBakIP);
		SetLockSeconds(nLockSeconds);
		SetDescription(pszDescription);
	}
	
	virtual ~CVideoServer() {}
	
	// 获取CVideoServer类的private属性值
	long GetID() { return m_nID; }
	char* GetName() { return m_szName; }
	char* GetIP() { return m_szIP; }
	bool GetIsBak() { return m_bBak; }
	char* GetBakIP() { return m_szBakIP; }
	int GetLockSeconds() { return m_nLockSeconds; }
	char* GetDescription() { return m_szDescription; }

	// 设置CVideoServer类的private属性值
	void SetID(const long nID) { m_nID = nID; }
	void SetName(const char* pszName) { CVStringHelper::Safe_StrNCpy(m_szName, pszName, sizeof(m_szName)); }
	void SetIP(const char* pszIP) { CVStringHelper::Safe_StrNCpy(m_szIP, pszIP, sizeof(m_szIP)); }
	void SetIsBak(const bool bBak) { m_bBak = bBak; }
	void SetBakIP(const char* pszBakIP) { CVStringHelper::Safe_StrNCpy(m_szBakIP, pszBakIP, sizeof(m_szBakIP)); }
	void SetLockSeconds(const int nLockSeconds) { m_nLockSeconds = nLockSeconds; }
	void SetDescription(const char* pszDescription) { CVStringHelper::Safe_StrNCpy(m_szDescription, pszDescription, sizeof(m_szDescription)); }
};

class  CVideoServerList : public CVideoList<CVideoServer>
{
public:
	CVideoServerList() { clear(); }
	virtual ~CVideoServerList() { clear(); }
	// 根据视频服务ID得到视频服务名称
	CVideoServer* FindServerbyID(const long nID)
	{
		CVideoServerList::iterator it = begin();
		for (; it!=end(); it++)
		{
			if (it->GetID() == nID)
				return &(*it);
		}
		return NULL;
	}
	// 根据视频服务名找到视频服务
	CVideoServer* FindServerbyName(const char* pszName, long* pnIndex=NULL)
	{
		CVideoServerList::iterator it = begin();
		int i=0;
		for (; it!=end(); it++,i++)
		{
			if (strcmp((char *)it->GetName(), (char *)pszName) == 0)
			{ 
				if (pnIndex != NULL)
				{
					*pnIndex = i;
				}
				return &(*it);
			}
		}
		return NULL;
	}
	// 根据视频服务名找到ID
	long FindServerIDbyName(const char* pszName)
	{
		CVideoServer* pServer = FindServerbyName(pszName);
		if (!pServer)
			return VIDEO_ID_INVALIDATION;
		else
			return pServer->GetID();
	}
};
///////////////////////// 视频服务实体定义 ////////////////////////

///////////////////////// 设备产品型号实体定义 ////////////////////////
class CVideoProduct
{
private:
	long m_nDeviceType;						// 设备类型
	long m_nID;								// 产品型号ID
	char m_szName[VIDEO_NAME_MAXSIZE];		// 产品型号名称
	char m_szDllName[VIDEO_NAME_MAXSIZE];	// 插件/驱动的dll名称

public:
	CVideoProduct()
	{
		m_nDeviceType = VIDEO_DEVICE_TYPE_NONE;
		m_nID = VIDEO_ID_INVALIDATION;
		memset(m_szName, 0x00, VIDEO_NAME_MAXSIZE);
		memset(m_szDllName, 0x00, VIDEO_NAME_MAXSIZE);
	}

	// 自定义构造函数
	CVideoProduct(const long nDeviceType, const long nID, const char* pszName, const char* pszDllName)
	{
		SetDeviceType(nDeviceType);
		SetID(nID);
		SetName(pszName);
		SetDllName(pszDllName);
	}

	virtual ~CVideoProduct() {}
	
	// 获取CVideoProduct类的private属性值
	long GetDeviceType() { return m_nDeviceType; }
	long GetID() { return m_nID; }
	char* GetName() { return m_szName; }
	char* GetDllName() { return m_szDllName; }

	// 设置CVideoProduct类的private属性值
	void SetDeviceType(const long nDeviceType) { m_nDeviceType = nDeviceType; }
	void SetID(const long nID) { m_nID = nID; }
	void SetName(const char* pszName) { CVStringHelper::Safe_StrNCpy(m_szName, pszName, sizeof(m_szName)); }
	void SetDllName(const char* pszDllName) { CVStringHelper::Safe_StrNCpy(m_szDllName, pszDllName, sizeof(m_szDllName)); }
};

class  CVideoProductList : public CVideoList<CVideoProduct>
{
public:
	CVideoProductList() { clear(); }
	virtual ~CVideoProductList() { clear(); }
	// 根据产品型号ID找到产品型号
	CVideoProduct* FindProductbyID(const long nID, long* pnIndex=NULL)
	{
		CVideoProductList::iterator it = begin();
		int i=0;
		for (; it!=end(); it++,i++)
		{
			if (it->GetID() == nID)
			{
				if (pnIndex != NULL)
				{
					*pnIndex = i;
				}
				return &(*it);
			}
		}
		return NULL;
	}
	// 根据产品型号名称找到产品型号
	CVideoProduct* FindProductbyName(const char* pszName)
	{
		CVideoProductList::iterator it = begin();
		for (; it!=end(); it++)
		{
			if (strcmp(it->GetName(), pszName) == 0)
				return &(*it);
		}
		return NULL;
	}

	// 根据产品型号名称找到产品型号ID
	long FindProductIDbyName(const char* pszName)
	{
		CVideoProduct* pProduct = FindProductbyName(pszName);
		if (!pProduct)
			return VIDEO_ID_INVALIDATION;
		else
			return pProduct->GetID();
	}

	// 根据产品型号ID找到产品型号名称
	char* FindProductNamebyID(const long nID)
	{
		CVideoProduct* pProduct = FindProductbyID(nID);
		if (!pProduct)
			return "";
		else
			return pProduct->GetName();
	}

	// 根据产品型号ID找到产品型号名称
	char* FindDllNamebyID(const long nID)
	{
		CVideoProduct* pProduct = FindProductbyID(nID);
		if (!pProduct)
			return "";
		else
			return pProduct->GetDllName();
	}
};
///////////////////////// 设备产品型号实体定义 ////////////////////////

///////////////////////// 分区实体定义 ////////////////////////
class CVideoZone
{
private:
	long m_nID;											// 分区ID
	char m_szName[VIDEO_NAME_MAXSIZE];					// 分区名称
	char m_szDescription[VIDEO_DESCRIPTION_MAXSIZE];	// 分区描述
	char m_szAuthLabel[VIDEO_NAME_MAXSIZE];				// 分区对应的权限标签ID
	long m_nAuthField;									// 权限范围
public:
	CVideoZone()
	{
		m_nID = VIDEO_ID_INVALIDATION;
		memset(m_szName, 0x00, VIDEO_NAME_MAXSIZE);
		memset(m_szDescription, 0x00, VIDEO_DESCRIPTION_MAXSIZE);
		memset(m_szAuthLabel, 0x00, VIDEO_NAME_MAXSIZE);
		m_nAuthField = 0;
	}

	// 自定义构造函数
	CVideoZone(const long nID, const char* pszName, const char* pszDescription, const char* pszAuthLabel, const long nAuthField)
	{
		SetID(nID);
		SetName(pszName);
		SetDescription(pszDescription);
		SetAuthLabel(pszAuthLabel);
		SetAuthField(nAuthField);
	}

	virtual ~CVideoZone() {}

	// 获取CVideoZone类的private属性值
	long GetID() { return m_nID; }
	char* GetName() { return m_szName; }
	char* GetDescription() { return m_szDescription; }
	char* GetAuthLabel() { return m_szAuthLabel; }
	long GetAuthField() { return m_nAuthField; }

	// 设置CVideoZone类的private属性值
	void SetID(const long nID) { m_nID = nID; }
	void SetName(const char* pszName) { CVStringHelper::Safe_StrNCpy(m_szName, pszName, sizeof(m_szName)); }
	void SetDescription(const char* pszDescription) { CVStringHelper::Safe_StrNCpy(m_szDescription, pszDescription, sizeof(m_szDescription)); }
	void SetAuthLabel(const char* pszAuthLabel) { CVStringHelper::Safe_StrNCpy(m_szAuthLabel, pszAuthLabel, sizeof(m_szAuthLabel)); }
	void SetAuthField(const long nAuthField) { m_nAuthField = nAuthField; }
};

class CVideoZoneList : public CVideoList<CVideoZone>
{
public:
	CVideoZoneList() { clear(); }
	virtual ~CVideoZoneList() { clear(); }
	// 根据分区ID得到分区
	CVideoZone* FindZonebyID(const long nID)
	{
		CVideoZoneList::iterator it = begin();
		for (; it!=end(); it++)
		{
			if (it->GetID() == nID)
				return &(*it);
		}
		return NULL;
	}
	// 根据分区名称得到分区
	CVideoZone* FindZonebyName(const char* pszName, long* pnIndex=NULL)
	{
		CVideoZoneList::iterator it = begin();
		int i=0;
		for (; it!=end(); it++,i++)
		{
			if (strcmp(it->GetName(), pszName) == 0)
			{
				if (pnIndex != NULL)
				{
					*pnIndex = i;
				}
				return &(*it);
			}
		}
		return NULL;
	}

	// 根据分区ID得到分区名称
	char* FindZoneNamebyID(const long nID)
	{
		CVideoZone* pZone = FindZonebyID(nID);
		if (!pZone)
			return "";
		else
			return pZone->GetName();
	}
	// 根据分区名称得到分区ID
	long FindZoneIDbyName(const char* pszName)
	{
		CVideoZone* pZone = FindZonebyName(pszName);
		if (!pZone)
			return VIDEO_ID_INVALIDATION;
		else
			return pZone->GetID();
	}
};
///////////////////////// 分区实体定义 ////////////////////////

///////////////////////// 连接设备实体定义 ////////////////////////
/*fengjuanyong 2011-1-12 增加矩阵下编码器的信息*/
class CDevUser
{
private:
	char m_szUserName[VIDEO_NAME_MAXSIZE];	// 占用编码器的用户名
	long m_lUserPRI;		// 占用编码器的用户的优先级
	time_t m_tmLinkTime;	// 编码器被占用时间

public:
	CDevUser()
	{
		m_tmLinkTime = 0;
		memset(m_szUserName, 0, VIDEO_NAME_MAXSIZE);
		m_lUserPRI = 0;
	}

	virtual ~CDevUser() {}

	// 获取CDevUser类的private属性值
	char* GetUserName() { return m_szUserName; }
	long GetUserPRI() { return m_lUserPRI; }
	time_t GetLinkTime() { return m_tmLinkTime; }
		
	// 设置CDevUser类的private属性值
	void SetUserName(const char* pszName) { CVStringHelper::Safe_StrNCpy(m_szUserName, pszName, sizeof(m_szUserName)); }
	void SetUserPRI(const long lUserPRI) { m_lUserPRI = lUserPRI; }
	void SetLinkTime(const time_t tmLinkTime) { m_tmLinkTime = tmLinkTime; }
};

class CDevUserList : public CVideoList<CDevUser>
{
public:
	CDevUserList() { clear(); }
	virtual ~CDevUserList() { clear(); }

	// 找到最高的优先级,如果列表中无数据，返回无效值
	CDevUser* FindLastUserPRI()
	{
		if (GetSize() <= 0)
			return NULL;
		
		long lTemp = -1;
		CDevUser *pTemp = NULL;
		CDevUserList::iterator it = begin();
		for (; it!=end(); it++)
		{
			if (it->GetUserPRI() > lTemp)
			{
				lTemp = it->GetUserPRI();
				pTemp = &(*it);
			}
		}
		return pTemp;
	}

	// 根据用户名找位置，便于删除
	int FindDevUserIndexbyName(const char* pszName)
	{
		CDevUserList::iterator it = begin();
		int i=0;
		for (; it!=end(); it++,i++)
		{
			if (strcmp((char *)it->GetUserName(), (char *)pszName) == 0)
				return i;
		}
		return VIDEO_ID_INVALIDATION;
	}
};

class CVideoLinkDevice
{
private:
	long m_nDeviceID;			// 连接设备ID
	char m_szChannel[VIDEO_CHANNEL_MAXSIZE];			// 连接设备的端口号/通道号
	char m_szLocalChannel[VIDEO_CHANNEL_MAXSIZE];		// 本设备的输出端口号/通道号
	bool m_bCtrlDevice;			// 是否为控制设备
	bool m_bVideoSourceDevice;	// 是否为实时视频源设备
	bool m_bHistoryDevice;		// 是否为录像管理设备
	long m_nTimeStamp;          //通道上次使用时间
	bool m_bIsUsed;             //通道当前是否在用
	long m_nLinkDevType;		// 连接设备的类型

public:	
	CVideoLinkDevice()
	{
		m_nDeviceID = VIDEO_ID_INVALIDATION;
		memset(m_szChannel, 0, sizeof(m_szChannel));
		memset(m_szLocalChannel, 0, sizeof(m_szLocalChannel));
		SetChannel(VIDEO_DEFAULT_STRING_CHANNEL);
		SetLocalChannel(VIDEO_DEFAULT_STRING_CHANNEL);
		m_bCtrlDevice = false;
		m_bVideoSourceDevice = false;
		m_bHistoryDevice = false;
		m_nTimeStamp = 0;
		m_bIsUsed = false;
		m_nLinkDevType = VIDEO_DEVICE_TYPE_NONE;
	}

	// 自定义构造函数
	CVideoLinkDevice(const long nDeviceID, const char* szChannel, bool bCtrlDevice=false,
						bool bVideoSourceDevice=false, bool bHistoryDevice=false, char* szLocalChannel=VIDEO_DEFAULT_STRING_CHANNEL)
	{
		SetDeviceID(nDeviceID);
		SetChannel(szChannel);
		SetLocalChannel(szLocalChannel);
		SetIsCtrlDevice(bCtrlDevice);
		SetIsVideoSourceDevice(bVideoSourceDevice);
		SetIsHistoryDevice(bHistoryDevice);
		SetTimeStamp( 0 );
		SetIsUsed( false );
		SetLinkDevType(VIDEO_DEVICE_TYPE_NONE);
	}

	virtual ~CVideoLinkDevice() {}

	// 获取CVideoLinkDevice类的private属性值
	long GetDeviceID() { return m_nDeviceID; }
	char* GetChannel() { return m_szChannel; }
	char* GetLocalChannel() { return m_szLocalChannel; }
	bool GetIsCtrlDevice() { return m_bCtrlDevice; }
	bool GetIsVideoSourceDevice() { return m_bVideoSourceDevice; }
	bool GetIsHistoryDevice() { return m_bHistoryDevice; }
	long GetTimeStamp() { return m_nTimeStamp; }
	bool GetIsUsed() { return m_bIsUsed; }
	long GetLinkDevType() { return m_nLinkDevType;}
		
	// 设置CVideoLinkDevice类的private属性值
	void SetDeviceID(const long nDeviceID) { m_nDeviceID = nDeviceID; }
	void SetChannel(const char* szChannel) { CVStringHelper::Safe_StrNCpy(m_szChannel, szChannel, sizeof(m_szChannel)); }
	void SetLocalChannel(const char* szLocalChannel) { CVStringHelper::Safe_StrNCpy(m_szLocalChannel, szLocalChannel, sizeof(m_szLocalChannel)); }
	void SetIsCtrlDevice(const bool bCtrlDevice) { m_bCtrlDevice = bCtrlDevice; }
	void SetIsVideoSourceDevice(const bool bVideoSourceDevice) { m_bVideoSourceDevice = bVideoSourceDevice; }
	void SetIsHistoryDevice(const bool bHistoryDevice) { m_bHistoryDevice = bHistoryDevice; }
	void SetTimeStamp(const long nTimeStamp) { m_nTimeStamp = nTimeStamp; }
	void SetIsUsed(const bool bIsUsed) { m_bIsUsed = bIsUsed; }
	void SetLinkDevType(const long nLinkDevType) { m_nLinkDevType = nLinkDevType;}
};

class CVideoLinkDeviceList : public CVideoList<CVideoLinkDevice>
{
public:
	CVideoLinkDeviceList() { clear(); }
	virtual ~CVideoLinkDeviceList() { clear(); }

	// 找到信号源的连接设备信息
	CVideoLinkDevice* FindSourceLinkDevice()
	{
		CVideoLinkDeviceList::iterator it = begin();
		for (; it!=end(); it++)
		{
			if (it->GetIsVideoSourceDevice())
				return &(*it);
		}
		return NULL;
	}

	// 找到控制设备
	CVideoLinkDevice* FindCtrlLinkDevice()
	{
		CVideoLinkDeviceList::iterator it = begin();
		for (; it!=end(); it++)
		{
			if (it->GetIsCtrlDevice())
				return &(*it);
		}
		return NULL;
	}

	// 找到历史录像设备
	CVideoLinkDevice* FindHisLinkDevice()
	{
		CVideoLinkDeviceList::iterator it = begin();
		for (; it!=end(); it++)
		{
			if (it->GetIsHistoryDevice())
				return &(*it);
		}
		return NULL;
	}
		
	// 找到信号源的连接设备
	CVideoLinkDevice* FindLinkDeviceByIDAndChannel(const long nID, const char* szChannel, long* pnIndex=NULL)
	{
		CVideoLinkDeviceList::iterator it = begin();
		int i = 0;
		for (; it!=end(); it++,i++)
		{
			if (it->GetDeviceID() == nID && strcmp(it->GetChannel(), szChannel) == 0)
			{
				if (pnIndex != NULL)
				{
					*pnIndex = i;
				}
				return &(*it);
			}
		}
		return NULL;
	}

	// 根据设备ID找到设备
	CVideoLinkDevice* FindLinkDevByDevIDAndChannel(const long lDevID, const char* szChannel)
	{
		CVideoLinkDeviceList::iterator it = begin();
		for (; it!=end(); it++)
		{
			if ((it->GetDeviceID() == lDevID) && (strcmp(it->GetChannel(), szChannel) == 0))
				return &(*it);
		}
		return NULL;
	}
};

///////////////////////// 连接设备实体定义 ////////////////////////

///////////////////////// 视频设备实体定义 ////////////////////////
class CVideoDevice
{
private:
	long m_nServerID;									// 所属视频服务ID
	long m_nProductID;									// 设备的产品型号ID
	long m_nID;											// 设备ID
	char m_szName[VIDEO_NAME_MAXSIZE];					// 设备名称
	char m_szIP[VIDEO_IPADDRESS_MAXSIZE];				// 设备IP地址
	unsigned short m_nPort;								// 设备端口
	char m_szExtent[VIDEO_DEFAULTINFO_MAXSIZE];			// 设备自定义信息
	char m_szDescription[VIDEO_DESCRIPTION_MAXSIZE];	// 设备描述
	long m_nCtrlPort;									// 矩阵摄像头控制专用通道
	CVideoLinkDeviceList m_listLinkDev;					// 本设备所连接的其他设备信息列表	
	long m_nLoginID;									// 注册ID
public:
	CVideoDevice()
	{
		m_nServerID = VIDEO_ID_INVALIDATION;
		m_nProductID = VIDEO_ID_INVALIDATION;
		m_nID = VIDEO_ID_INVALIDATION;
		memset(m_szName, 0x00, VIDEO_NAME_MAXSIZE);
		memset(m_szIP, 0x00, VIDEO_IPADDRESS_MAXSIZE);
		CVStringHelper::Safe_StrNCpy(m_szIP, VIDEO_DEFAULT_IP, sizeof(m_szIP));
		m_nPort = VIDEO_PORT_INVALIDATION;
		memset(m_szExtent, 0x00, VIDEO_DEFAULTINFO_MAXSIZE);
		memset(m_szDescription, 0x00, VIDEO_DESCRIPTION_MAXSIZE);
		m_nCtrlPort = VIDEO_MATRIX_CTRL_PORT;
		m_listLinkDev.clear();
		m_nLoginID = VIDEO_SDK_ID_INVALIDATION;
	}
	
	// 自定义构造函数
	CVideoDevice(const long nServerID, const long nProductID, const long nID, const char* pszName, const char* pszIP, 
					const unsigned short nPort, const char* pszExtent, const char* pszDescription, const long nCtrlPort, long nLoginID=VIDEO_SDK_ID_INVALIDATION)
	{
		SetServerID(nServerID);
		SetProductID(nProductID);
		SetID(nID);
		SetName(pszName);
		SetIP(pszIP);
		SetPort(nPort);
		SetExtent(pszExtent);
		SetDescription(pszDescription);
		SetCtrlPort(nCtrlPort);
		SetLoginID(nLoginID);
		m_listLinkDev.clear();
	}

	virtual ~CVideoDevice() {}

	// 获取CVideoDevice类的private属性值
	long GetServerID() { return m_nServerID; }
	long GetProductID() { return m_nProductID; }
	long GetID() { return m_nID; }
	char* GetName() { return m_szName; }
	char* GetIP() { return m_szIP; }
	unsigned short GetPort() { return m_nPort; }
	char* GetExtent() { return m_szExtent; }
	char* GetDescription() { return m_szDescription; }
	long GetCtrlPort() { return m_nCtrlPort; }
	CVideoLinkDeviceList* GetLinkDev() { return &m_listLinkDev; }	
	long GetLoginID() { return m_nLoginID; }

	// 设置CVideoDevice类的private属性值
	void SetServerID(const long nServerID) { m_nServerID = nServerID; }
	void SetProductID(const long nProductID) { m_nProductID = nProductID; }
	void SetID(const long nID) { m_nID = nID; }
	void SetName(const char* pszName) { CVStringHelper::Safe_StrNCpy(m_szName, pszName, sizeof(m_szName)); }
	void SetIP(const char* pszIP) { CVStringHelper::Safe_StrNCpy(m_szIP, pszIP, sizeof(m_szIP)); }
	void SetPort(const unsigned short nPort) { m_nPort = nPort; }	
	void SetExtent(const char* pszExtent) { CVStringHelper::Safe_StrNCpy(m_szExtent, pszExtent, sizeof(m_szExtent)); }
	void SetDescription(const char* pszDescription) { CVStringHelper::Safe_StrNCpy(m_szDescription, pszDescription, sizeof(m_szDescription)); }
	void SetCtrlPort(const long nCtrlPort) { m_nCtrlPort = nCtrlPort; }
	void SetLoginID(const long nLoginID) { m_nLoginID = nLoginID; }
};

class CVideoDeviceList : public CVideoList<CVideoDevice>
{
public:
	CVideoDeviceList() { clear(); }
	virtual ~CVideoDeviceList() { clear(); }
	// 根据设备名称找到设备
	CVideoDevice* FindDevicebyName(const char* pszName, long* pnIndex=NULL)
	{
		CVideoDeviceList::iterator it = begin();
		int i = 0;
		for (; it!=end(); it++,i++)
		{
			if (strcmp(it->GetName(), pszName) == 0)
			{
				if (pnIndex != NULL)
				{
					*pnIndex = i;
				}
				return &(*it);
			}
		}
		return NULL;
	}
	// 根据设备ID找到设备
	CVideoDevice* FindDevicebyID(const long nID)
	{
		CVideoDeviceList::iterator it = begin();
		for (; it!=end(); it++)
		{
			if (it->GetID() == nID)
				return &(*it);
		}
		return NULL;
	}
	// 根据设备ID找到设备名称
	char* FindDeviceNamebyID(const long nID)
	{
		CVideoDevice* pDev = FindDevicebyID(nID);
		if (!pDev)
			return "";
		else
			return pDev->GetName();
	}
		
	// 根据设备名称找到设备ID
	long FindDeviceIDbyName(const char* pszName)
	{
		CVideoDevice* pDev = FindDevicebyName(pszName);
		if (!pDev)
			return VIDEO_ID_INVALIDATION;
		else
			return pDev->GetID();
	}
};

///////////////////////// 视频设备实体定义 ////////////////////////

///////////////////////// 设备类型实体定义 ////////////////////////
class CVideoDevType
{
private:
	long m_nID;											// 设备类型ID
	char m_szName[VIDEO_NAME_MAXSIZE];					// 设备类型
public:	
	CVideoDevType()
	{	
		m_nID = VIDEO_ID_INVALIDATION;
		memset(m_szName, 0x00, VIDEO_NAME_MAXSIZE);
	}
	
	// 自定义构造函数
	CVideoDevType(const long nID, const char* pszName)
	{
		SetID(nID);
		SetName(pszName);
	}
	
	virtual ~CVideoDevType() {}
	
	// 获取CVideoDevType类的private属性值
	long GetID() { return m_nID; }
	char* GetName() { return m_szName; }
	
	// 设置CVideoDevType类的private属性值
	void SetID(const long nID) { m_nID = nID; }
	void SetName(const char* pszName) { CVStringHelper::Safe_StrNCpy(m_szName, pszName, sizeof(m_szName)); }
};

class CVideoDevTypeList : public CVideoList<CVideoDevType>
{
public:
	CVideoDevTypeList() { clear(); }
	virtual ~CVideoDevTypeList() { clear(); }
	// 根据设备类型ID找到设备类型
	CVideoDevType* GetByID(const long nID)
	{
		CVideoDevTypeList::iterator it = begin();
		for (; it!=end(); it++)
		{
			if (it->GetID() == nID)
				return &(*it);
		}
		return NULL;
	}
	// 根据设备类型名称找到设备类型
	CVideoDevType* GetByName(const char* pszName)
	{
		CVideoDevTypeList::iterator it = begin();
		for (; it!=end(); it++)
		{
			if (strcmp((char *)it->GetName(), (char *)pszName) == 0)
				return &(*it);
		}
		return NULL;
	}
};

///////////////////////// 设备类型实体定义 ////////////////////////

///////////////////////// 预置位实体定义 ////////////////////////
class CVideoPSP
{
private:
	char m_szName[VIDEO_NAME_MAXSIZE];					// 预置位名称
	long m_nPresetIndex;								// 预置点序号
	char m_szDescription[VIDEO_DESCRIPTION_MAXSIZE];	// 预置位描述
public:	
	CVideoPSP()
	{
		memset(m_szName, 0x00, VIDEO_NAME_MAXSIZE);
		m_nPresetIndex = VIDEO_INDEX_INVALIDATION;
		memset(m_szDescription, 0x00, VIDEO_DESCRIPTION_MAXSIZE);
	}

	// 自定义构造函数
	CVideoPSP(const char* pszName, const long nPresetIndex, const char* pszDescription)
	{
		SetName(pszName);
		SetPresetIndex(nPresetIndex);
		SetDescription(pszDescription);
	}

	virtual ~CVideoPSP() {}

	// 获取CVideoPSP类的private属性值
	char* GetName() { return m_szName; }
	long GetPresetIndex() { return m_nPresetIndex; }
	char* GetDescription() { return m_szDescription; }

	// 设置CVideoPSP类的private属性值
	void SetName(const char* pszName) { CVStringHelper::Safe_StrNCpy(m_szName, pszName, sizeof(m_szName)); }
	void SetPresetIndex(const long nPresetIndex) { m_nPresetIndex = nPresetIndex; }	
	void SetDescription(const char* pszDescription) { CVStringHelper::Safe_StrNCpy(m_szDescription, pszDescription, sizeof(m_szDescription)); }
};

class CVideoPSPList : public CVideoList<CVideoPSP>
{
public:
	CVideoPSPList() { clear(); }
	virtual ~CVideoPSPList() { clear(); }
};
///////////////////////// 预置位实体定义 ////////////////////////

///////////////////////// 摄像头实体定义 ////////////////////////
class CVideoCamera
{
private:
	long m_nServerID;									// 所属视频服务ID
	long m_nZoneID;										// 分区ID
	char m_szName[VIDEO_NAME_MAXSIZE];					// 摄像头名称
	long m_nID;											// 摄像头逻辑ID
	char m_szDescription[VIDEO_DESCRIPTION_MAXSIZE];	// 摄像头描述
	bool m_bRemoteFileCtrl;								// 远程文件操作
	bool m_bLocalFileCtrl;								// 本地文件操作
	bool m_bRemoteTimeCtrl;								// 时间回放操作
	bool m_bOrientCtrl;									// 云台控制
	bool m_bPSPCtrl;									// 预置位控制
	bool m_bLensCtrl;									// 镜头控制
	bool m_bQualityCtrl;								// 画面质量
	bool m_bSnapCtrl;									// 抓拍控制
	bool m_bHeaterCtrl;									// 加热器控制
	bool m_bRainBrushCtrl;								// 雨刷控制
	long m_nSnapWidth;									// 抓拍图片宽度
	long m_nSnapHeight;									// 抓拍图片高度
	long m_nSnapBitDeep;								// 抓拍位深度
	CVideoLinkDeviceList m_listLinkDev;					// 摄像头所连接的设备列表
	long m_nPSPMaxCount;								// 预置位的最大数量
	CVideoPSPList m_listPSP;							// 预置位信息列表
public:
	CVideoCamera()
	{
		m_nServerID = VIDEO_ID_INVALIDATION;
		m_nZoneID = VIDEO_DEFAULT_ZONE_ID;
		memset(m_szName, 0x00, VIDEO_NAME_MAXSIZE);	
		m_nID = VIDEO_ID_INVALIDATION;	
		memset(m_szDescription, 0x00, VIDEO_DESCRIPTION_MAXSIZE);
		m_bRemoteFileCtrl = true;
		m_bLocalFileCtrl = true;
		m_bRemoteTimeCtrl = true;		
		m_bOrientCtrl = true;				
		m_bPSPCtrl = true;
		m_bLensCtrl = true;
		m_bQualityCtrl = true;
		m_bSnapCtrl = true;
		m_bHeaterCtrl = false;
		m_bRainBrushCtrl = false;
		m_nSnapWidth = VIDEO_DEFAULT_SNAP_WIDTH;
		m_nSnapHeight = VIDEO_DEFAULT_SNAP_HEIGHT;
		m_nSnapBitDeep = VIDEO_DEFAULT_SNAP_BITDEEP;				
		m_listLinkDev.clear();
		m_nPSPMaxCount = VIDEO_CAMERA_PSP_MAX_COUNT;
		m_listPSP.clear();
	}
	
	// 自定义构造函数
	CVideoCamera(const long nServerID, const long nZoneID, const char* pszName, const long nID, const char* pszDescription,
					const bool bRemoteFileCtrl, const bool bLocalFileCtrl, const bool bRemoteTimeCtrl, const bool bOrientCtrl,
					const bool bPSPCtrl, const bool bLensCtrl, const bool bQualityCtrl, const bool bSnapCtrl, const bool bHeaterCtrl,
					const bool bRainBrushCtrl, const long nSnapWidth, const long nSnapHeight, const long nSnapBitDeep, const long nPSPMaxCount)
	{
		SetServerID(nServerID);
		SetZoneID(nZoneID);
		SetName(pszName);
		SetID(nID);
		SetDescription(pszDescription);
		SetIsRemoteFileCtrl(bRemoteFileCtrl);
		SetIsLocalFileCtrl(bLocalFileCtrl);
		SetIsRemoteTimeCtrl(bRemoteTimeCtrl);
		SetIsOrientCtrl(bOrientCtrl);
		SetIsPSPCtrl(bPSPCtrl);
		SetIsLensCtrl(bLensCtrl);	
		SetIsQualityCtrl(bQualityCtrl);
		SetIsSnapCtrl(bSnapCtrl);
		SetIsHeaterCtrl(bHeaterCtrl);
		SetIsRainBrushCtrl(bRainBrushCtrl);
		SetSnapWidth(nSnapWidth);
		SetSnapHeight(nSnapHeight);
		SetSnapBitDeep(nSnapBitDeep);
		SetPSPMaxCount(nPSPMaxCount);
		m_listLinkDev.clear();
		m_listPSP.clear();
	}

	virtual ~CVideoCamera() {}

	// 获取CVideoCamera类的private属性值
	long GetServerID() { return m_nServerID; }
	long GetZoneID() { return m_nZoneID; }
	char* GetName() { return m_szName; }
	long GetID() { return m_nID; }
	char* GetDescription() { return m_szDescription; }
	bool GetIsRemoteFileCtrl() { return m_bRemoteFileCtrl; }
	bool GetIsLocalFileCtrl() { return m_bLocalFileCtrl; }
	bool GetIsRemoteTimeCtrl() { return m_bRemoteTimeCtrl; }
	bool GetIsOrientCtrl() { return m_bOrientCtrl; }
	bool GetIsPSPCtrl() { return m_bPSPCtrl; }
	bool GetIsLensCtrl() { return m_bLensCtrl; }	
	bool GetIsQualityCtrl() { return m_bQualityCtrl; }
	bool GetIsSnapCtrl() { return m_bSnapCtrl; }	
	bool GetIsHeaterCtrl() { return m_bHeaterCtrl; }	
	bool GetIsRainBrushCtrl() { return m_bRainBrushCtrl; }
	long GetSnapWidth() { return m_nSnapWidth; }
	long GetSnapHeight() { return m_nSnapHeight; }
	long GetSnapBitDeep() { return m_nSnapBitDeep; }
	CVideoLinkDeviceList* GetLinkDev() { return &m_listLinkDev; }
	long GetPSPMaxCount() { return m_nPSPMaxCount; }
	CVideoPSPList* GetPSP() { return &m_listPSP; }

	// 设置CVideoCamera类的private属性值
	void SetServerID(const long nServerID) { m_nServerID = nServerID; }
	void SetZoneID(const long nZoneID) { m_nZoneID = nZoneID; }
	void SetName(const char* pszName) { CVStringHelper::Safe_StrNCpy(m_szName, pszName, sizeof(m_szName)); }
	void SetID(const long nID) { m_nID = nID; }
	void SetDescription(const char* pszDescription) { CVStringHelper::Safe_StrNCpy(m_szDescription, pszDescription, sizeof(m_szDescription)); }
	void SetIsRemoteFileCtrl(const bool bRemoteFileCtrl) { m_bRemoteFileCtrl = bRemoteFileCtrl; }
	void SetIsLocalFileCtrl(const bool bLocalFileCtrl) { m_bLocalFileCtrl = bLocalFileCtrl; }
	void SetIsRemoteTimeCtrl(const bool bRemoteTimeCtrl) { m_bRemoteTimeCtrl = bRemoteTimeCtrl; }
	void SetIsOrientCtrl(const bool bOrientCtrl) { m_bOrientCtrl = bOrientCtrl; }
	void SetIsPSPCtrl(const bool bPSPCtrl) { m_bPSPCtrl = bPSPCtrl; }
	void SetIsLensCtrl(const bool bLensCtrl) { m_bLensCtrl = bLensCtrl; }	
	void SetIsQualityCtrl(const bool bQualityCtrl) { m_bQualityCtrl = bQualityCtrl; }
	void SetIsSnapCtrl(const bool bSnapCtrl) { m_bSnapCtrl = bSnapCtrl; }	
	void SetIsHeaterCtrl(const bool bHeaterCtrl) { m_bHeaterCtrl = bHeaterCtrl; }	
	void SetIsRainBrushCtrl(const bool bRainBrushCtrl) { m_bRainBrushCtrl = bRainBrushCtrl; }
	void SetSnapWidth(const long nSnapWidth) { m_nSnapWidth = nSnapWidth; }
	void SetSnapHeight(const long nSnapHeight) { m_nSnapHeight = nSnapHeight; }
	void SetSnapBitDeep(const long nSnapBitDeep) { m_nSnapBitDeep = nSnapBitDeep; }
	void SetPSPMaxCount(const long nPSPMaxCount) { m_nPSPMaxCount = nPSPMaxCount; }
};

class CVideoCameraList : public CVideoList<CVideoCamera>
{
public:
	CVideoCameraList() { clear(); }
	virtual ~CVideoCameraList() { clear(); }
	// 根据摄像头名称找到摄像头
	CVideoCamera* FindCamerabyName(const char* pszName, long* pnIndex=NULL)
	{
		CVideoCameraList::iterator it = begin();
		int i=0;
		for (; it!=end(); it++,i++)
		{
			if (strcmp((char *)it->GetName(), (char *)pszName) == 0)
			{ 
				if (pnIndex != NULL)
				{
					*pnIndex = i;
				}
				return &(*it);
			}
		}
		return NULL;
	}
	// 根据摄像头ID找到摄像头
	CVideoCamera* FindCamerabyID(const long nID)
	{
		CVideoCameraList::iterator it = begin();
		for (; it!=end(); it++)
		{
			if (it->GetID() == nID)
				return &(*it);
		}
		return NULL;
	}
};
///////////////////////// 摄像头实体定义 ////////////////////////

///////////////////////// 监视器实体定义 ////////////////////////
class CVideoMonitor
{
private:
	long m_nServerID;									// 所属视频服务ID
	long m_nZoneID;										// 分区ID
	char m_szName[VIDEO_NAME_MAXSIZE];					// 监视器名称
	long m_nID;											// 监视器逻辑ID
	char m_szDescription[VIDEO_DESCRIPTION_MAXSIZE];	// 监视器描述
	CVideoLinkDevice m_theLinkDevice;					// 监视器所连接的设备信息
public:
	CVideoMonitor()
	{
		m_nServerID = VIDEO_ID_INVALIDATION;
		m_nZoneID = VIDEO_DEFAULT_ZONE_ID;				
		memset(m_szName, 0x00, VIDEO_NAME_MAXSIZE);					
		m_nID = VIDEO_ID_INVALIDATION;
		memset(m_szDescription, 0x00, VIDEO_DESCRIPTION_MAXSIZE);
	}

	// 自定义构造函数
	CVideoMonitor(const long nServerID, const long nZoneID, const char* pszName, const long nID, const char* pszDescription,
					long nDeviceID=VIDEO_ID_INVALIDATION, const char* szChannel = VIDEO_DEFAULT_STRING_CHANNEL)
	{
		SetServerID(nServerID);
		SetZoneID(nZoneID);
		SetName(pszName);
		SetID(nID);
		SetDescription(pszDescription);
		m_theLinkDevice.SetDeviceID(nDeviceID);
		m_theLinkDevice.SetChannel(szChannel);
	}

	virtual ~CVideoMonitor() {}

	// 获取CVideoMonitor类的private属性值
	long GetServerID() { return m_nServerID; }
	long GetZoneID() { return m_nZoneID; }
	char* GetName() { return m_szName; }
	long GetID() { return m_nID; }
	char* GetDescription() { return m_szDescription; }
	CVideoLinkDevice* GetLinkDev() { return &m_theLinkDevice; }
		
	// 设置CVideoMonitor类的private属性值
	void SetServerID(const long nServerID) { m_nServerID = nServerID; }
	void SetZoneID(const long nZoneID) { m_nZoneID = nZoneID; }
	void SetName(const char* pszName) { CVStringHelper::Safe_StrNCpy(m_szName, pszName, sizeof(m_szName)); }
	void SetID(const long nID) { m_nID = nID; }
	void SetDescription(const char* pszDescription) { CVStringHelper::Safe_StrNCpy(m_szDescription, pszDescription, sizeof(m_szDescription)); }
};

class CVideoMonitorList : public CVideoList<CVideoMonitor>
{
public:
	CVideoMonitorList() { clear(); }
	virtual ~CVideoMonitorList() { clear(); }
	// 根据监视器名称找到监视器
	CVideoMonitor* FindMonitorbyName(const char* pszName, long* pnIndex=NULL)
	{
		CVideoMonitorList::iterator it = begin();
		int i=0;
		for (; it!=end(); it++,i++)
		{
			if (strcmp((char *)it->GetName(), (char *)pszName) == 0)
			{ 
				if (pnIndex != NULL)
				{
					*pnIndex = i;
				}
				return &(*it);
			}
		}
		return NULL;
	}
	// 根据监视器ID找到监视器
	CVideoMonitor* FindMonitorbyID(const long nID)
	{
		CVideoMonitorList::iterator it = begin();
		for (; it!=end(); it++)
		{
			if (it->GetID() == nID)
				return &(*it);
		}
		return NULL;
	}
	// 根据监视器所连设备ID查找对应的通道号
	const char* FindChannelByDevID(const long lID)
	{
		CVideoMonitorList::iterator it = begin();
		for (; it!=end(); it++)
		{
			CVideoLinkDevice *pLinkDev = it->GetLinkDev();
			if (pLinkDev == NULL)
				continue;

			if (pLinkDev->GetDeviceID() == lID)
				return pLinkDev->GetLocalChannel();
		}
		return VIDEO_ID_STRING_INVALIDATION;
	}
};

class CVideoMatLinkDev
{
private:
	char m_szLocalChannel[VIDEO_CHANNEL_MAXSIZE];		// 本设备的输出端口号/通道号
	bool m_bEncodeDev;			// 是否有编码器
	bool m_bMonitorDev;			// 是否有监视器
	bool m_bUsed;				// 该通道是否被占用
	CDevUserList m_listDevUser;	// 该通道所连接的用户信息列表
	long m_lCamID;				// 占用编码器的摄像头ID
	long m_lResCount;			// 计数，如果同一个摄像头重复发指令需要有一个资源计数
	CVideoLinkDeviceList m_listLinkDevice; // 连接的编码器
	CVideoMonitorList m_listMonitor; // 连接的监视器
	
public:	
	CVideoMatLinkDev()
	{
		memset(m_szLocalChannel, 0, sizeof(m_szLocalChannel));
		SetLocalChannel(VIDEO_DEFAULT_STRING_CHANNEL);
		m_bEncodeDev = false;
		m_bMonitorDev = false;
		m_bUsed = false;
		m_listDevUser.clear();
		m_lCamID = VIDEO_ID_INVALIDATION;
		m_lResCount = 0;
		m_listLinkDevice.clear();
		m_listMonitor.clear();
	}

	virtual ~CVideoMatLinkDev() {}

	// 获取CVideoMatLinkDev类的private属性值
	char* GetLocalChannel() { return m_szLocalChannel; }
	bool GetHasEncodeDev() { return m_bEncodeDev; }
	bool GetHasMonitorDev() { return m_bMonitorDev; }
	bool GetIsUsed() { return m_bUsed; }
	CDevUserList* GetDevUserList() { return &m_listDevUser; }
	long GetCamID() { return m_lCamID; }
	long GetResCount() { return m_lResCount; }
	CVideoLinkDeviceList* GetLinkDeviceList() { return &m_listLinkDevice; }
	CVideoMonitorList* GetMonitorList() { return &m_listMonitor; }
		
	// 设置CVideoMatLinkDev类的private属性值
	void SetLocalChannel(const char* szLocalChannel) { CVStringHelper::Safe_StrNCpy(m_szLocalChannel, szLocalChannel, sizeof(m_szLocalChannel)); }
	void SetHasEncodeDev(const bool bEncodeDev) { m_bEncodeDev = bEncodeDev; }
	void SetHasMonitorDev(const bool bMonitorDev) { m_bMonitorDev = bMonitorDev; }
	void SetIsUsed(const bool bUsed) { m_bUsed = bUsed; }
	void SetCamID(const long lCamID) { m_lCamID = lCamID; }
	void SetResCount(const long lResCount) { m_lResCount = lResCount; }

	// 清空list
	void InitDevUserList() { m_listDevUser.clear(); }
	void InitLinkDeviceList() { m_listLinkDevice.clear(); }
	void InitMonitorList() { m_listMonitor.clear(); }
};

class CVideoMatLinkDevList : public CVideoList<CVideoMatLinkDev>
{
public:
	CVideoMatLinkDevList() { clear(); }
	virtual ~CVideoMatLinkDevList() { clear(); }

	// 根据通道号查找对应的CVideoMatLinkDev
	CVideoMatLinkDev* FindMatLinkDevByChannel(const char* szChannel)
	{
		CVideoMatLinkDevList::iterator it = begin();
		for (; it!=end(); it++)
		{
			if (strcmp(it->GetLocalChannel(), szChannel) == 0)
				return &(*it);
		}
		return NULL;
	}

	// 根据CamID和设备查找对应的CVideoMatLinkDev
	CVideoMatLinkDev* FindMatEncodeLinkDevByCamID(const long lCamID)
	{
		CVideoMatLinkDevList::iterator it = begin();
		for (; it!=end(); it++)
		{
			if ((it->GetCamID() == lCamID) && it->GetHasEncodeDev())
				return &(*it);
		}
		return NULL;
	}

	// 根据CamID和设备查找对应的CVideoMatLinkDev
	CVideoMatLinkDev* FindMatMonLinkDevByCamID(const long lCamID)
	{
		CVideoMatLinkDevList::iterator it = begin();
		for (; it!=end(); it++)
		{
			if ((it->GetCamID() == lCamID) && it->GetHasMonitorDev())
				return &(*it);
		}
		return NULL;
	}
};

class CVideoMatrix
{
private:
	long m_lMatrixID; // 矩阵ID
	CVideoMatLinkDevList m_listMatLinkDev; // 矩阵连接的设备列表，编码器和监视器

public:
	CVideoMatrix()
	{
		m_lMatrixID = VIDEO_ID_INVALIDATION;
		m_listMatLinkDev.clear();
	}

	virtual ~CVideoMatrix() {}

	// 获取CVideoMatrix类的private属性值
	long GetMatrixID() { return m_lMatrixID; }
	CVideoMatLinkDevList* GeMatLinkDevList() { return &m_listMatLinkDev; }
		
	// 设置CVideoMatrix类的private属性值
	void SetMatrixID(const long lMatrixID) { m_lMatrixID = lMatrixID; }
	// 清空list
	void InitMatLinkDevList() { m_listMatLinkDev.clear(); }
};

class CVideoMatrixList : public CVideoList<CVideoMatrix>
{
public:
	CVideoMatrixList() { clear(); }
	virtual ~CVideoMatrixList() { clear(); }

	// 根据ID查找对应的CVideoMatrix
	CVideoMatrix* FindMatByID(const long lID)
	{
		CVideoMatrixList::iterator it = begin();
		for (; it!=end(); it++)
		{
			if (it->GetMatrixID() == lID)
				return &(*it);
		}
		return NULL;
	}
};
///////////////////////// 监视器实体定义 ////////////////////////

///////////////////////// 模式实体定义 ////////////////////////
class CVideoMode
{
private:
	long m_nType;										// 模式类型
	char m_szName[VIDEO_NAME_MAXSIZE];					// 模式名称
	char m_szPara[VIDEO_MODE_PARA_MAXSIZE];				// 模式参数,包括模式所涉及的分区，模式类型以及模式布局内容
	char m_szDescription[VIDEO_DESCRIPTION_MAXSIZE];	// 模式描述
	CVideoList<long> m_listZone;						// 模式中摄像头所涉及的分区
public:
	CVideoMode()
	{
		m_nType = VIDEO_MODETYPE_SHOW;
		memset(m_szName, 0x00, VIDEO_NAME_MAXSIZE);
		memset(m_szPara, 0x00, VIDEO_MODE_PARA_MAXSIZE);
		memset(m_szDescription, 0x00, VIDEO_DESCRIPTION_MAXSIZE);
		m_listZone.clear();
	}

	// 自定义构造函数
	CVideoMode(const long nType, const char* pszName, const char* pszPara, const char* pszDescription)
	{
		SetType(nType);
		SetName(pszName);
		SetPara(pszPara);
		SetDescription(pszDescription);
	}

	virtual ~CVideoMode() {}

	// 获取CVideoMode类的private属性值
	long GetType() { return m_nType; }
	char* GetName() { return m_szName; }
	char* GetPara() { return m_szPara; }
	char* GetDescription() { return m_szDescription; }
	CVideoList<long>* GetListZone() { return &m_listZone; }
	
	// 设置CVideoMode类的private属性值
	void SetType(const long nType) { m_nType = nType; }
	void SetName(const char* pszName) { CVStringHelper::Safe_StrNCpy(m_szName, pszName, sizeof(m_szName)); }
	void SetPara(const char* pszPara) { CVStringHelper::Safe_StrNCpy(m_szPara, pszPara,  sizeof(m_szPara)); }
	void SetDescription(const char* pszDescription) {  CVStringHelper::Safe_StrNCpy(m_szDescription, pszDescription, sizeof(m_szDescription)); }
};

class CVideoModeList : public CVideoList<CVideoMode>
{
public:
	CVideoModeList() { clear(); }
	virtual ~CVideoModeList() { clear(); }

	// 根据模式名称找到模式
	CVideoMode* FindModebyName(const char* pszName)
	{
		CVideoModeList::iterator it = begin();
		for (; it!=end(); it++)
		{
			if (strcmp((char *)it->GetName(), (char *)pszName) == 0)
				return &(*it);
		}
		return NULL;
	}
};
///////////////////////// 模式实体定义结束 ////////////////////////

///////////////////////// 抓拍图片实体定义 ////////////////////////
class CVideoSnapFile
{
private:
	long m_nID;											// 抓拍图片ID
	long m_nCameraID;									// 摄像头ID
	char m_szInfoType[VIDEO_NAME_MAXSIZE];				// 信息类型
	time_t m_tmSnap;									// 抓拍时间
	long m_nSnapCount;									// 连续抓拍张数
	char m_szExtent[VIDEO_DEFAULTINFO_MAXSIZE];			// 扩展属性
	char m_szDescription[VIDEO_DESCRIPTION_MAXSIZE];	// 抓拍描述
public:
	CVideoSnapFile()
	{
		m_nID = VIDEO_ID_INVALIDATION;
		m_nCameraID = VIDEO_ID_INVALIDATION;
		memset(m_szInfoType, 0x00, VIDEO_NAME_MAXSIZE);
		m_nSnapCount = 0;
		memset(m_szExtent, 0x00, sizeof(m_szExtent));
		memset(m_szDescription, 0x00, VIDEO_DESCRIPTION_MAXSIZE);
	}

	// 自定义构造函数
	CVideoSnapFile(const long nID, const long nCameraID, const char* pszInfoType, const time_t tmSnap,
					const long nSnapCount, const char* pszExtent, const char* pszDescription)
	{
		SetID(nID);
		SetCameraID(nCameraID);
		SetInfoType(pszInfoType);
		SetSnapTime(tmSnap);
		SetSnapCount(nSnapCount);
		SetExtent(pszExtent);
		SetDescription(pszDescription);
	}
	
	virtual ~CVideoSnapFile() {}

	// 获取CVideoSnapFile类的private属性值
	long GetID() { return m_nID; }
	long GetCameraID() { return m_nCameraID; }
	char* GetInfoType() { return m_szInfoType; }
	time_t GetSnapTime() { return m_tmSnap; }
	long GetSnapCount() { return m_nSnapCount; }
	char* GetExtent() { return m_szExtent; }
	char* GetDescription() { return m_szDescription; }

	// 设置CVideoSnapFile类的private属性值
	void SetID(const long nID) { m_nID = nID; }
	void SetCameraID(const long nCameraID) { m_nCameraID = nCameraID; }
	void SetInfoType(const char* pszInfoType) { CVStringHelper::Safe_StrNCpy(m_szInfoType, pszInfoType, sizeof(m_szInfoType)); }
	void SetSnapTime(const time_t tmSnap) { m_tmSnap = tmSnap; }
	void SetSnapCount(const long nSnapCount) { m_nSnapCount = nSnapCount; }
	void SetExtent(const char* pszExtent) { CVStringHelper::Safe_StrNCpy(m_szExtent, pszExtent, sizeof(m_szExtent)); }
	void SetDescription(const char* pszDescription) { CVStringHelper::Safe_StrNCpy(m_szDescription, pszDescription, sizeof(m_szDescription)); }
};
class CVideoSnapFileList : public CVideoList<CVideoSnapFile>
{
public:
	CVideoSnapFileList() { clear(); }
	virtual ~CVideoSnapFileList() { clear(); }
};
///////////////////////// 抓拍图片实体定义 ////////////////////////

///////////////////////// 设备视频切换关系定义 ////////////////////////
class CReSwitchChannel
{
private:
	CVideoDevice m_dev;	// 相关设备
	char m_szInChannel[VIDEO_CHANNEL_MAXSIZE];		// 输入通道
	char m_szOutChannel[VIDEO_CHANNEL_MAXSIZE];		// 输出通道
public:
	CReSwitchChannel()
	{
		memset(m_szInChannel, 0, sizeof(m_szInChannel));
		memset(m_szOutChannel, 0, sizeof(m_szOutChannel));
		SetInChannel(VIDEO_ID_STRING_INVALIDATION);
		SetOutChannel(VIDEO_ID_STRING_INVALIDATION);
	}

	// 自定义构造函数
	CReSwitchChannel(CVideoDevice *pDev, const char* szInChannel, const char* szOutChannel)
	{
		SetDev(pDev);
		SetInChannel(szInChannel);
		SetOutChannel(szOutChannel);
	}

	virtual ~CReSwitchChannel() {}

	// 获取CReSwitchChannel类的private属性值
	CVideoDevice *GetDev() { return &m_dev; }
	char* GetInChannel() { return m_szInChannel; }
	char* GetOutChannel() { return m_szOutChannel; }
		
	// 设置CReSwitchChannel类的private属性值
	void SetDev(CVideoDevice *pDev) {
			m_dev.SetServerID(pDev->GetServerID());
			m_dev.SetProductID(pDev->GetProductID());
			m_dev.SetID(pDev->GetID());
			m_dev.SetName(pDev->GetName());
			m_dev.SetIP(pDev->GetIP());
			m_dev.SetPort(pDev->GetPort());
			m_dev.SetExtent(pDev->GetExtent());
			m_dev.SetDescription(pDev->GetDescription());
			m_dev.SetCtrlPort(pDev->GetCtrlPort());
			m_dev.SetLoginID(pDev->GetLoginID()); }
	void SetInChannel(const char* szInChannel) { CVStringHelper::Safe_StrNCpy(m_szInChannel, szInChannel, sizeof(m_szInChannel)); }
	void SetOutChannel(const char* szOutChannel) { CVStringHelper::Safe_StrNCpy(m_szOutChannel, szOutChannel, sizeof(m_szOutChannel)); }
};

class CReSwitchChannelList : public CVideoList<CReSwitchChannel>
{
public:
	CReSwitchChannelList() { clear(); }
	virtual ~CReSwitchChannelList() { clear(); }
};
///////////////////////// 监视器摄像头的连接关系定义 ////////////////////////

///////////////////////// 监视器轮询实体定义 ////////////////////////////
class CMonitorLink
{
private:
	char m_szName[VIDEO_NAME_MAXSIZE];
	unsigned long m_ulTimerIntv;
	CVideoCameraList *m_plistCam;
	int m_nIndex;
	time_t m_nTime;
public:
	CMonitorLink()
	{
		memset(m_szName, 0x00, sizeof(m_szName));
		m_ulTimerIntv = 0;
		m_plistCam = NULL;
		m_nIndex = 0;
		m_nTime = 0;
	}
	virtual ~CMonitorLink() {}

	char *GetName() { return m_szName; }
	unsigned long GetIntv() { return m_ulTimerIntv; }
	CVideoCameraList *GetCamList() { return m_plistCam; }
	int GetNextCam() { return m_nIndex; }
	time_t GetTime() { return m_nTime; }
	void SetName(char *pszName) { memcpy(m_szName, pszName, strlen(pszName)); }
	void SetIntv(unsigned long nTimerIntv) { m_ulTimerIntv = nTimerIntv; }
	void SetCamList(CVideoCameraList *plistCam) { m_plistCam = plistCam; }
	void SetNextCam(int nIndex) { m_nIndex = nIndex; }
	void SetTime(time_t nTime) { m_nTime = nTime; };
};

class CMonitorLinkList : public CVideoList<CMonitorLink>
{
public:
	CMonitorLinkList() { clear(); }
	virtual ~CMonitorLinkList() { clear(); }
	// 根据分区名称找到分区
	CMonitorLink* FindMonitorLinkbyName(const char* pszName)
	{
		CMonitorLinkList::iterator it = begin();
		for (; it!=end(); it++)
		{
			if (strcmp((char *)it->GetName(), (char *)pszName) == 0)
				return &(*it);
		}
		return NULL;
	}

	int FindMonitorLinkIndexbyName(const char* pszName)
	{
		CMonitorLinkList::iterator it = begin();
		int i=0;
		for (; it!=end(); it++,i++)
		{
			if (strcmp((char *)it->GetName(), (char *)pszName) == 0)
				return i;
		}
		return VIDEO_ID_INVALIDATION;
	}
};
///////////////////////// 监视器轮询实体定义 ////////////////////////////

///////////////////////// 抓拍类型定义 ////////////////////////////
class CIDMapText
{
private:
	long m_nID;								// ID
	char m_szText[VIDEO_NAME_MAXSIZE];		// Text
public:
	CIDMapText()
	{
		m_nID = VIDEO_ID_INVALIDATION;
		memset(m_szText, 0x00, VIDEO_NAME_MAXSIZE);
	}
	virtual ~CIDMapText() {}

	// 获取CIDMapText类的private属性值
	long GetID() { return m_nID; }
	char* GetText() { return m_szText; }
		
	// 设置CIDMapText类的private属性值
	void SetID(const long nID) { m_nID = nID; }
	void SetText(const char* pszText) { CVStringHelper::Safe_StrNCpy(m_szText, pszText, sizeof(m_szText)); }
};

class CIDMapTextList : public CVideoList<CIDMapText>
{
public:
	CIDMapTextList() { clear(); }
	virtual ~CIDMapTextList() { clear(); }
	// 根据抓拍类型名称找到抓拍类型信息
	CIDMapText* FindIDMapbyText(const char* pszName)
	{
		CIDMapTextList::iterator it = begin();
		for (; it!=end(); it++)
		{
			if (strcmp(it->GetText(), pszName) == 0)
				return &(*it);
		}
		return NULL;
	}
	// 根据抓拍类型ID
	CIDMapText* FindIDMapbyID(const long nID)
	{
		CIDMapTextList::iterator it = begin();
		for (; it!=end(); it++)
		{
			if (it->GetID() == nID)
				return &(*it);
		}
		return NULL;
	}
};
///////////////////////// 抓拍类型定义 ////////////////////////////

///////////////////////// 自定义分区定义 ////////////////////////////
class CVideoCustomZone
{
private:
	long m_nID;									// 分区ID
	char m_szName[VIDEO_NAME_MAXSIZE];			// 分区名称
	char m_szUserName[VIDEO_NAME_MAXSIZE];		// 用户名
	char m_szDesc[VIDEO_DESCRIPTION_MAXSIZE];	// 描述
public:
	CVideoCustomZone()
	{
		m_nID = VIDEO_ID_INVALIDATION;
		memset(m_szName, 0x00, sizeof(m_szName));
		memset(m_szUserName, 0x00, sizeof(m_szUserName));
		memset(m_szDesc, 0x00, sizeof(m_szDesc));
	}
	// 自定义构造函数
	CVideoCustomZone(const long nID, const char* pszName, const char* pszUserName, const char* pszDesc)
	{
		SetID(nID);
		SetName(pszName);
		SetUserName(pszUserName);
		SetDesc(pszDesc);
	}
	virtual ~CVideoCustomZone() {}

	// 获取private属性
	long GetID() { return m_nID; }
	char* GetName() { return m_szName; }
	char* GetRelateUserName() { return m_szUserName; }
	char* GetDesc() { return m_szDesc; }

	// 设置private属性
	void SetID(const long nID) { m_nID = nID; }
	void SetName(const char* pszName) { CVStringHelper::Safe_StrNCpy(m_szName, pszName, sizeof(m_szName)); }
	void SetUserName(const char* pszUserName) { CVStringHelper::Safe_StrNCpy(m_szUserName, pszUserName, sizeof(m_szUserName)); }
	void SetDesc(const char* pszDesc) { CVStringHelper::Safe_StrNCpy(m_szDesc, pszDesc, sizeof(m_szDesc)); }
};

class CVideoCustomZoneList : public CVideoList<CVideoCustomZone>
{
public:
	CVideoCustomZoneList() { clear(); }
	virtual ~CVideoCustomZoneList() { clear(); }
	// 根据分区ID得到分区
	CVideoCustomZone* FindCustZonebyID(const long nID)
	{
		CVideoCustomZoneList::iterator it = begin();
		for (; it!=end(); it++)
		{
			if (it->GetID() == nID)
				return &(*it);
		}
		return NULL;
	}
	// 根据分区名称找到分区
	CVideoCustomZone* FindCustZonebyName(const char* pszName)
	{
		CVideoCustomZoneList::iterator it = begin();
		for (; it!=end(); it++)
		{
			if (strcmp(it->GetName(), pszName) == 0)
				return &(*it);
		}
		return NULL;
	}
};
///////////////////////// 自定义分区定义 ////////////////////////////

///////////////////////// 历史录像文件定义 ////////////////////////////
class CHisFile
{
private:
	char m_szName[VIDEO_DEFAULTINFO_MAXSIZE];
	long m_nFileSize;
	long m_nStartTime;
	long m_nEndTime;
public:
	CHisFile()
	{
		memset(m_szName, 0x00, sizeof(m_szName));
		m_nFileSize = 0;
		m_nStartTime = 0;
		m_nEndTime = 0;
	}
	CHisFile(const char* pszName, const long nSize, const long nStartTime, const long nEndTime)
	{
		SetName(pszName);
		SetFileSize(nSize);
		SetStartTime(nStartTime);
		SetEndTime(nEndTime);
	}
	virtual ~CHisFile() {}

	// 设置private属性
	char* GetName() { return m_szName; }
	long GetFileSize() { return m_nFileSize; }
	long GetStartTime() { return m_nStartTime; }
	long GetEndTime() { return m_nEndTime; }

	// 设置private属性
	void SetName(const char* pszName) { memcpy(m_szName, pszName, sizeof(m_szName)); }
	void SetFileSize(const long nSize) { m_nFileSize = nSize; }
	void SetStartTime(const long nStartTime) { m_nStartTime = nStartTime; }
	void SetEndTime(const long nEndTime) { m_nEndTime = nEndTime; }
};
class CHisFileList : public CVideoList<CHisFile>
{
public:
	CHisFileList() { clear(); }
	virtual ~CHisFileList() { clear(); }

	// 根据文件名称得到文件信息
	CHisFile* GetFileInfoByName(const char* pszName)
	{
		CHisFileList::iterator it = begin();
		for (; it!=end(); it++)
		{
			if (strcmp(it->GetName(), pszName) == 0)
				return &(*it);
		}
		return NULL;
	}
};
///////////////////////// 历史录像文件定义 ////////////////////////////

///////////////////////// 录像片段信息实体定义 ////////////////////////
class CRecord
{
private:
	long m_nID;											// 抓拍图片ID
	long m_nCamID;										// 摄像头ID
	time_t m_tmStart;									// 起始时间
	time_t m_tmEnd;										// 结束时间
	char m_szExtent[VIDEO_EXTENT_MAXSIZE];				// 扩展属性
	char m_szFileName[VIDEO_FILE_NAME_MAXSIZE];			// 文件名称
	char m_szDesc[VIDEO_DESCRIPTION_MAXSIZE];			// 抓拍描述
	long m_nStartSnapID;								// 录像开始时抓拍ID
	long m_nMidSnapID;									// 录像播放中间抓拍ID，通过累加可以有多张抓拍图片
	long m_nEndSnapID;									// 录像结束时抓拍ID
	char m_szReserve1[VIDEO_DEFAULTINFO_MAXSIZE];		// 保留1
	char m_szReserve2[VIDEO_DEFAULTINFO_MAXSIZE];		// 保留2
	char m_szReserve3[VIDEO_DEFAULTINFO_MAXSIZE];		// 保留3
public:
	CRecord()
	{
		m_nID = 0;
		m_nCamID = 0;
		m_tmStart = 0;
		m_tmEnd = 0;
		memset(m_szExtent, 0x00, sizeof(m_szExtent));
		memset(m_szFileName, 0x00, sizeof(m_szFileName));
		memset(m_szDesc, 0x00, sizeof(m_szDesc));
		m_nStartSnapID = 0;
		m_nMidSnapID = 0;
		m_nEndSnapID = 0;
		memset(m_szReserve1, 0x00, sizeof(m_szReserve1));
		memset(m_szReserve2, 0x00, sizeof(m_szReserve2));
		memset(m_szReserve3, 0x00, sizeof(m_szReserve3));
	}
	
	// 自定义构造函数
	CRecord(const long nID, const long nCamID, const time_t tmStart, const time_t tmEnd, const char* pszExtent, const char* pszFileName, const char* pszDesc)
	{
		SetID(nID);
		SetCameraID(nCamID);
		SetTimeStart(tmStart);
		SetTimeEnd(tmEnd);
		SetExtent(pszExtent);
		SetFileName(pszFileName);
		SetDesc(pszDesc);
	}
	
	virtual ~CRecord() {}
	
	// 获取CVideoSnapFile类的private属性值
	long GetID() { return m_nID; }
	long GetCameraID() { return m_nCamID; }
	time_t GetStartTime() { return m_tmStart; }
	time_t GetEndTime() { return m_tmEnd; }
	char* GetExtent() { return m_szExtent; }
	char* GetFileName() { return m_szFileName; }
	char* GetDesc() { return m_szDesc; }
	long GetStartSnapID() { return m_nStartSnapID; }
	long GetMidSnapID() { return m_nMidSnapID; }
	long GetEndSnapID() { return m_nEndSnapID; }
	char* GetReserve1() { return m_szReserve1; }
	char* GetReserve2() { return m_szReserve2; }
	char* GetReserve3() { return m_szReserve3; }
	
	// 设置CVideoSnapFile类的private属性值
	void SetID(const long nID) { m_nID = nID; }
	void SetCameraID(const long nCamID) { m_nCamID = nCamID; }
	void SetTimeStart(const time_t tmStart) { m_tmStart = tmStart; }
	void SetTimeEnd(const time_t tmEnd) { m_tmEnd = tmEnd; }
	void SetExtent(const char* pszExtent) { CVStringHelper::Safe_StrNCpy(m_szExtent, pszExtent, sizeof(m_szExtent)); }
	void SetFileName(const char* pszFileName) { CVStringHelper::Safe_StrNCpy(m_szFileName, pszFileName, sizeof(m_szFileName)); }
	void SetDesc(const char* pszDesc) { CVStringHelper::Safe_StrNCpy(m_szDesc, pszDesc, sizeof(m_szDesc)); }
	void SetStartSnapID(const long SnapID) { m_nStartSnapID = SnapID; }
	void SetMidSnapID(const long SnapID) { m_nMidSnapID = SnapID; }
	void SetEndSnapID(const long SnapID) { m_nEndSnapID = SnapID; }
	void SetReserve1(const char* pszReserve) { CVStringHelper::Safe_StrNCpy(m_szReserve1, pszReserve, sizeof(m_szReserve1)); }
	void SetReserve2(const char* pszReserve) { CVStringHelper::Safe_StrNCpy(m_szReserve2, pszReserve, sizeof(m_szReserve2)); }
	void SetReserve3(const char* pszReserve) { CVStringHelper::Safe_StrNCpy(m_szReserve3, pszReserve, sizeof(m_szReserve3)); }
};
class CRecordList : public CVideoList<CRecord>
{
public:
	CRecordList() { clear(); }
	virtual ~CRecordList() { clear(); }
};

///////////////////////// 录像片段信息实体定义 ////////////////////////

#endif //#ifndef VIDEO_STRUCT_DEF_H_

