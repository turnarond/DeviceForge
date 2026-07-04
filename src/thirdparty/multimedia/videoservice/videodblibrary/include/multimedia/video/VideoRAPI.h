/**************************************************************
 *  Filename:    VideoRAPI.h
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: 视频远程API接口.
 *
 *  @author:     zhucongfeng
 *  @version     04/20/2009  zhucongfeng  Initial Version
 *			     04/27/2009  zhucongfeng  添加根据摄像头名称获取摄像头信息，根据设备ID获取设备信息的接口
 *				 04/28/2009	 zhucongfeng  提供VRN接口
 *				 05/05/2009	 zhucongfeng  根据分区获取摄像头信息改为提供分区编号；修改VRN的参数；修改同行评审缺陷
 *				 06/01/2009	 zhucongfeng  添加获取图片内容、删除图片接口
 *				 06/04/2009	 zhucongfeng  获取硬盘录像机信息接口中返回添加：设备自定义信息;设备名称;设备描述
 *				 06/09/2009	 zhucongfeng  修改VR_GetRecordInfoByCamName，返回通道号
 *				 06/09/2009	 zhucongfeng  获取抓拍图片内容及删除抓拍图片，删除接口中的摄像头参数
 *				 06/23/2009	 zhucongfeng  增加接口VRN_GetCameras2，VRN_GetRecordInfoByCamName2，VR_GetSnapPicBuffer2，所需
                                          缓冲区长度由外部分配，(.net不方便调用**的参数)。长度不够时直接返回错误。
 *				 07/16/2009	 chenzhazhan  增加接口VR_DeletePsp，VR_ModifyPsp，预置位修改和删除
 *				 02/08/2010	 chenzhan	  批量删除图片和录像查询删除下载接口
 *				 07/20/2010	 zhucongfeng  修改远程接口，添加句柄参数
 *				 07/24/2010  lixiaohao    配置信息远程接口，预置位管理远程接口
 *				 07/24/2010  lixiaohao    模式管理远程接口
 *				 08/14/2010  zhucongfeng  增加自定义分区远程接口，客户端管理
 *				 08/17/2010  lixiaohao    增加录像，抓拍，控制远程接口
 *               08/30/2010  lixiaohao    将作为输出参数的VideoList，修改为char*形式的buffer，缓冲区由调用方分配
 *               01/27/2011  louzhengqing 将RAPI的函数接口从字符串变为数组
 *               04/11/2012  zhucongfeng  添加VR_ADDPSP2方法，此方法直接在配置中添加预置位信息，客户端通过插件控制设备时调用此接口
**************************************************************/


#ifndef _VIDEORAPI_H
#define _VIDEORAPI_H

#ifdef WIN32
	#ifdef videorapi_EXPORTS 
		#define VIDEO_REMOTE_API extern "C" __declspec(dllexport)
	#else
		#define VIDEO_REMOTE_API extern "C" __declspec(dllimport)
	#endif
#else
	#define VIDEO_REMOTE_API extern "C"
#endif

#include "VideoRAPIDef.h"

#define DEFAULT_MSG_WAIT_TIME	3000
#define VIDEO_NAME_SEPARATOR	";"

/***************************************************************
*注册客户端
***************************************************************/

/**
 *  $(注册客户端信息).
 *  $(Detail).
 *
 *  @param  -[out]  HVideoClienT*  pHClient: [客户端句柄]
 *  @param  -[in]  const char*  szCliName: [客户端名称]
 *  @param  -[in]  char*  szServIp: [主机IP]
 *  @param  -[in]  int  nIsExistBak: [是否存在备机]
 *  @param  -[in]  char*  szBakIp: [备机IP]
 *  @param  -[in]  char* szUserName =  ICV_AUTH_DEFAULT_USER: [用户名称]
 *  @param  -[in]  char* szGroup =  "": [群组]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     04/21/2009  zhucongfeng  Initial Version.
 *  @version     07/20/2010  zhucongfeng  Initial Version.
 */

VIDEO_REMOTE_API long VR_RegisterClient(HVideoClienT* pHClient, const char *szCliName, char *szServIp, int nIsExistBak, char *szBakIp, char* szUserName = ICV_AUTH_DEFAULT_USER, char* szGroup = "");

/**
 *  $(为.net提供的注册客户端接口).
 *  $(Detail).
 *
 *  @param  -[out]  HVideoClienT*  pHClient: [客户端句柄]
 *  @param  -[in]  const char*  szCliName: [客户端名称]
 *  @param  -[in]  char*  szMainServIP: [主服务器IP地址]
 *  @param  -[in]  bool  bIsExistBak: [是否存在备机]
 *  @param  -[in]  char*  szBakServIP: [备机IP地址]
 *  @param  -[in]  char* szUserName =  ICV_AUTH_DEFAULT_USER: [用户名]
 *  @param  -[in]  char* szGroup =  "": [群组]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     04/28/2009  zhucongfeng  Initial Version.
 *  @version     07/20/2010  zhucongfeng  Initial Version.
 */

VIDEO_REMOTE_API long VRN_RegisterClient(HVideoClienT* pHClient, const char *szCliName, char* szMainServIP, bool bIsExistBak, char* szBakServIP, char* szUserName = ICV_AUTH_DEFAULT_USER, char* szGroup = "");

/**
*  注销客户端
*
*  @param  -[in]  HVideoClienT  hClient: [客户端句柄]
*  @return long.
*		- ==0 成功
*		- !=0 出现异常
*
*  @version     07/20/2010  zhucongfeng  Initial Version.
*/

VIDEO_REMOTE_API long  VR_Release(HVideoClienT hClient);

/***************************************************************
*摄像头同步接口
***************************************************************/
/**
 *  获取分区列表。对于一般用户只能获取自己有权限的分区，无权限的分区不显示
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[out]  SVideoZone** ppVideoZone: [返列表回分区]
 *  @param  -[out]  int* pnVideoZone:         [分区个数]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     04/20/2009  zhucongfeng  Initial Version.
 */

VIDEO_REMOTE_API long VR_GetAllZone(HVideoClienT hClient, SVideoZone** ppVideoZone, int* pnVideoZone, long lTimeOut = DEFAULT_MSG_WAIT_TIME);


/**
 *  获取自定义分区列表。对于一般用户只能获取自己有权限的分区，无权限的分区不显示
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[out]  SVideoCustomZone** ppVideoCustomZone: [返回的自定义分区指针]
 *  @param  -[out]  int* pnVideoCustomZone:               [返回的自定义分区个数]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     04/20/2009  zhucongfeng  Initial Version.
 */

VIDEO_REMOTE_API long VR_GetAllCustZone(HVideoClienT hClient, SVideoCustomZone** ppVideoCustomZone, int* pnVideoCustomZone,
					                    long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
 *  根据分区名称获取摄像头列表。对于一般用户只能获取自己有权限看到的摄像头，无权限的摄像头不显示
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[out]  SVideoCamera** ppVideoCamera:  [返回的摄像头指针]
 *  @param  -[out]  int* pnVideoCamera:            [返回的摄像头个数]
 *  @param  -[in]  long lZoneID: [分区编号]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     04/20/2009  zhucongfeng  Initial Version.
 */

VIDEO_REMOTE_API long VR_GetAllCamInfoOfZone(HVideoClienT hClient, SVideoCamera** ppVideoCamera, int* pnVideoCamera,
							                 long lZoneID, long lTimeOut = DEFAULT_MSG_WAIT_TIME);


/**
 *  根据用户自定义分区名称获取摄像头列表。对于一般用户只能获取自己有权限看到的摄像头，无权限的摄像头不显示
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[out]  SVideoCamera** ppVideoCamera: [返回的摄像头指针]
 *  @param  -[out]  int* pnVideoCamera:           [返回的摄像头个数]
 *  @param  -[in]  char *  pszCustZoneName: [分区名称]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     04/20/2009  zhucongfeng  Initial Version.
 */
VIDEO_REMOTE_API long VR_GetAllCamInfoOfCustZone(HVideoClienT hClient, SVideoCamera** ppVideoCamera, int* pnVideoCamera,
								                 char * pszCustZoneName, long lTimeOut = DEFAULT_MSG_WAIT_TIME);
/**
 *  根据提供的摄像头名称进行模糊查询，获取摄像头信息.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[out]  char*  plistCam: [返回的摄像头列表]
 *  @param  -[in]  char *  pszCamName: [摄像头模糊名称]
 *  @param  -[in]  long  lTimeOut: [超时时间]
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     04/22/2009  zhucongfeng  Initial Version.
 */

VIDEO_REMOTE_API long VR_GetCameras(HVideoClienT hClient, char* plistCam, char * pszCamName, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
 *  为.net提供的根据摄像头名称模糊查询摄像头名称接口.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[out]  char*  &pszCam: [返回的摄像头信息，中间以“;”分割]
 *  @param  -[in]  char *  pszCamName: [摄像头名称]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     04/28/2009  zhucongfeng  Initial Version.
 */

VIDEO_REMOTE_API long VRN_GetCameras (HVideoClienT hClient, char* &pszCam, char * pszCamName, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
 *  为.net提供的根据摄像头名称模糊查询摄像头名称接口.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[out]  char*  pszCam: [返回的摄像头信息，中间以“;”分割]
 *  @param  -[in]  long lBufLen: [分配的接收摄像头信息的Buffer长度]
 *  @param  -[in]  char *  pszCamName: [摄像头名称]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     06/23/2009  zhucongfeng  Initial Version.
 */
VIDEO_REMOTE_API long VRN_GetCameras2 (HVideoClienT hClient, char* pszCam, long lBufLen, char * pszCamName, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
 *  根据摄像头名称获取关联的硬盘录像机信息（IP地址、通道号、端口号）.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[out]  CVideoDevice*  pVideoDev: [硬盘录像机信息]
 *  @param  -[out]  char* szChannel: [通道号]
 *  @param  -[in]  char *  pszCamName: [摄像头名称]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     04/22/2009  zhucongfeng  Initial Version.
 */

VIDEO_REMOTE_API long VR_GetRecordInfoByCamName (HVideoClienT hClient, CVideoDevice* pVideoDev, char* szChannel, char * pszCamName, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
 *  为.net提供的根据摄像头名称获取关联的硬盘录像机信息的接口.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[out]  char*  pszDev: [硬盘录像机信息，格式为“IP地址;通道号;端口号;设备自定义信息;设备名称;设备描述”]
 *  @param  -[in]  char *  pszCamName: [摄像头名称]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     04/28/2009  zhucongfeng  Initial Version.
 */

VIDEO_REMOTE_API long VRN_GetRecordInfoByCamName (HVideoClienT hClient, char* &pszDev, char * pszCamName, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
 *  为.net提供的根据摄像头名称获取关联的硬盘录像机信息的接口.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[out]  char*  pszDev: [硬盘录像机信息，格式为“IP地址;通道号;端口号;设备自定义信息;设备名称;设备描述”]
 *  @param  -[in]  long lBufLen: [分配的接收硬盘录像机信息的Buffer长度]
 *  @param  -[in]  char *  pszCamName: [摄像头名称]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     06/23/2009  zhucongfeng  Initial Version.
 */
VIDEO_REMOTE_API long VRN_GetRecordInfoByCamName2 (HVideoClienT hClient, char* pszDev, long lBufLen, char * pszCamName, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
 *  根据摄像头名称获取摄像头信息.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[out]  CVideoCamera*  pCamera: [comment]
 *  @param  -[in]  char *  pszCamName: [摄像头名称]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     04/27/2009  zhucongfeng  Initial Version.
 */

VIDEO_REMOTE_API long VR_GetCamInfoByCamName (HVideoClienT hClient, CVideoCamera* pCamera, char * pszCamName, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
 *  获取所有设备信息.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[out]  SVideoDevice** ppVideoDevice:  [获取到的设备指针]
 *  @param  -[out]  int* pnVideoDevice:            [获取到的设备个数]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     04/27/2009  zhucongfeng  Initial Version.
 */

VIDEO_REMOTE_API long VR_GetAllDeviceInfo (HVideoClienT hClient, SVideoDevice** ppVideoDevice, int* pnVideoDevice,
						                   long lTimeOut = DEFAULT_MSG_WAIT_TIME);
//////////////////////-----抓拍-----////////////////////////////////

/**
 *  根据抓拍ID获取抓拍图片的内容.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[in]  long  nSnapID: [抓拍ID]
 *  @param  -[out]  long&  nCount: [抓拍个数]
 *  @param  -[out]  char*  &pBuffer: [图片buffer内容，格式为：图片内容图片长度图片内容......]
 *  @param  -[out]  long&  nPicLen: [buffer长度]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     05/31/2009  zhucongfeng  Initial Version.
 */

VIDEO_REMOTE_API long VR_GetSnapPicBuffer(HVideoClienT hClient, long nSnapID, long &nCount, char *&pBuffer, long &nPicLen, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
 *  根据抓拍ID获取抓拍图片的内容.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[in]  long  nSnapID: [抓拍ID]
 *  @param  -[out]  long&  nCount: [抓拍个数]
 *  @param  -[out]  char*  &pBuffer: [图片buffer内容，格式为：图片内容图片长度图片内容......]
 *  @param  -[in]  long lBufLen: [外面分配的用于接收图片内容的缓冲区]
 *  @param  -[out]  long&  nPicLen: [buffer长度]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     06/23/2009  zhucongfeng  Initial Version.
 */

VIDEO_REMOTE_API long VR_GetSnapPicBuffer2(HVideoClienT hClient, long nSnapID, long &nCount, char *pBuffer, long lBufLen, long &nPicLen, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
 *  根据抓拍ID删除抓拍的图片.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[in]  long  nSnapID: [抓拍ID]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     05/31/2009  zhucongfeng  Initial Version.
 */

VIDEO_REMOTE_API long VR_DelSnapPic(HVideoClienT hClient, long nSnapID, long lCamID, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
 *  根据摄像头ID和预置位名称修改预置位.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[in]  long nCamID [摄像头ID]
 *  @param  -[in]  char* pOldPspName [老预置位名称]
 *  @param  -[in]  char* pNewPspName [新预置位名称]
 *  @param  -[in]  char* pNewPspDesc [新预置位描述]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     07/16/2009  chenzhan  Initial Version.
 */

VIDEO_REMOTE_API long VR_ModifyPsp(HVideoClienT hClient, long nCamID, char *pOldPspName, char *pNewPspName, char *pNewPspDesc, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
 *  根据摄像头ID和预置位名称删除预置位.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[in]  long nCamID [摄像头ID]
 *  @param  -[in]  char* pPspName [预置位名称]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     07/16/2009  chenzhan  Initial Version.
 */

VIDEO_REMOTE_API long VR_DeletePsp(HVideoClienT hClient, long nCamID, char *pPspName, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
 *  删除某个时间点前的抓拍图片.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[in]  char* pszTime 时间点(格式"2010,01,01,08,00,00")
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     02/04/2010  chenzhan  Initial Version.
 */
VIDEO_REMOTE_API long VRN_DelPrePicture(HVideoClienT hClient, char* pszTime, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
 *  获取某个时间段的所有抓拍录像ID.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[in]  char* pszTimeStart 时间点(格式"2010,01,01,08,00,00")
 *  @param  -[in]  char* pszTimeEnd 时间点(格式"2010,01,01,10,00,00")
 *  @param  -[out] char* pszBuff ID列表(格式"1,2,3,4,"),此指针指向的内存由使用者申请
 *  @param  -[in]  long nBuffSize 内存长度
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     02/04/2010  chenzhan  Initial Version.
 */
VIDEO_REMOTE_API long VRN_GetRecordID(HVideoClienT hClient, char* pszTimeStart, char* pszTimeEnd, char* pszBuff, long nBuffSize, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
 *  根据抓拍ID删除抓拍录像.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[in]  long nRecordID	[录像ID]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     02/04/2010  chenzhan  Initial Version.
 */
VIDEO_REMOTE_API long VRN_DelRecord(HVideoClienT hClient, long nRecordID, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
 *  下载抓拍录像片段.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[in]  char* pszFileName [路径]
 *  @param  -[in]  long nRecordID	[录像ID]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     02/04/2010  chenzhan  Initial Version.
 */
VIDEO_REMOTE_API long VRN_DownloadRecord(HVideoClienT hClient, char* pszFileName, long nRecordID, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
 * 获取所有的设备配置信息
 *  @param  -[in]  HVideoClienT hClient: 客户端句柄
 *  @param  -[in,out]  long& lLastTime:     当前最新所有配置信息的时间 
 *  @param  -[out] SVideoProduct** ppVideoProduct:	设备产品型号列表
 *  @param  -[out] int* pnVideoProduct:	            设备产品型号个数
 *  @param  -[out] SVideoDevice** ppVideoDevice:	设备列表
 *  @param  -[out] int* pnVideoDevice:	            设备个数
 *  @param  -[out] SVideoZone** ppVideoZone:		固定分区列表
 *  @param  -[out] int* pnVideoZone:		        固定分区个数
 *  @param  -[out] SVideoCamera** ppVideoCamera:	摄像头列表
 *  @param  -[out] int* pnVideoCamera:	            摄像头个数
 *  @param  -[out] SVideoMonitor** ppVideoMonitor:	监视器列表
 *  @param  -[out] int* pnVideoMonitor:	            监视器个数
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
 *  @return  ==0 success  !=0 failure
 *  @version     07/23/2010  lixiaohao  Initial Version.
 */
 VIDEO_REMOTE_API long VR_GetAllConfigInfo(HVideoClienT hClient,long& lLastTime, SVideoProduct** ppVideoProduct, 
	                      int* pnVideoProduct, SVideoDevice** ppVideoDevice, int* pnVideoDevice,
                          SVideoZone** ppVideoZone, int* pnVideoZone, SVideoCamera** ppVideoCamera,
						  int* pnVideoCamera, SVideoMonitor** ppVideoMonitor, int* pnVideoMonitor,
		                  long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
*获取所有的设备产品型号信息
* @param  -[in]  HVideoClienT hClient: 客户端句柄
* @param  -[out] SVideoProduct** ppVideoProduct:	设备产品型号指针
* @param  -[out] int* pnVideoProduct:				设备产品型号个数
* @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
* @return  ==0 success  !=0 failure
*
*  @version     07/24/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_GetAllProduct(HVideoClienT hClient, SVideoProduct** ppVideoProduct, int* pnVideoProduct,
					                   long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
* 获取所有的摄像头信息
* @param  -[in]  HVideoClienT hClient: 客户端句柄
* @param  -[out] SVideoCamera** ppVideoCamera:		[摄像头指针]
* @param  -[out] int* pnVideoCamera:				[摄像头个数]
* @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
* @return  ==0 success  !=0 failure
*
 *  @version     07/24/2010  lixiaohao  Initial Version.
 */
VIDEO_REMOTE_API long VR_GetAllCam(HVideoClienT hClient, SVideoCamera** ppVideoCamera, int* pnVideoCamera, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
 * 获取所有的监视器信息
 * @param  -[in]  HVideoClienT hClient: 客户端句柄
 * @param  -[out] SVideoMonitor** ppVideoMonitor:	[监视器指针]
 * @param  -[out] int* pnVideoMonitor:				[监视器个数]
 * @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
 * @return  ==0 success  !=0 failure
 *
 *  @version     07/24/2010  lixiaohao  Initial Version.
 */
VIDEO_REMOTE_API long VR_GetAllMon(HVideoClienT hClient, SVideoMonitor** ppVideoMonitor, int* pnVideoMonitor, long lTimeOut = DEFAULT_MSG_WAIT_TIME);


/**
 * 获取所有的摄像头简易信息
 *  @param  -[in]  HVideoClienT hClient: 客户端句柄
 *  @param  -[out] SVideoCamera** ppVideoCamera:  [摄像头指针]
 *  @param  -[out] int* pnVideoCamera:			  [摄像头个数]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
 *  @return  ==0 success  !=0 failure
 *
 *  @version     07/24/2010  lixiaohao  Initial Version.
 */

VIDEO_REMOTE_API long VR_GetAllSimpleCam(HVideoClienT hClient, SVideoCamera** ppVideoCamera, int* pnVideoCamera, long lTimeOut = DEFAULT_MSG_WAIT_TIME);


/**
 *  获取某一分区的摄像头简易信息
 *  @param  -[in]  HVideoClienT hClient: 客户端句柄
 *  @param  -[out] SVideoCamera** ppVideoCamera:	[摄像头指针]
 *  @param  -[out] int* pnVideoCamera:			    [摄像头个数]
 *  @param  -[in] long lZoneID						分区ID
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
 *  @return  ==0 success  !=0 failure
 *
 *  @version     07/24/2010  lixiaohao  Initial Version.
 */
VIDEO_REMOTE_API long VR_GetAllSimpleCamInfoOfZone(HVideoClienT hClient, SVideoCamera** ppVideoCamera, int* pnVideoCamera,
								  long lZoneID, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
 *  获取某一分区的监视器信息
 *  @param  -[in]  HVideoClienT hClient: 客户端句柄
 *  @param  -[out] SVideoMonitor** ppVideoMonitor:	监视器指针
 *  @param  -[out] int* pnVideoMonitor:	            监视器个数
 *  @param  -[in] long lZoneID						分区ID
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
 *  @return  ==0 success  !=0 failure
 *
 *  @version     07/24/2010  lixiaohao  Initial Version.
 */
VIDEO_REMOTE_API long VR_GetAllMonInfoOfZone(HVideoClienT hClient, SVideoMonitor** ppVideoMonitor, int* pnVideoMonitor,
							                 long lZoneID, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
*  获取摄像头的预置位信息
* @param  -[in]  HVideoClienT hClient: 客户端句柄
* @param  -[in,out] long& lCamID:		摄像头ID
* @param  -[in,out] long& lLastTime:     当前最新所有配置信息的时间 
* @param  -[out] SVideoPSP** ppVideoPSP:  预置位指针
* @param  -[out] int* pnVideoPSP:		  预置位个数
* @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     07/24/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_GetCamPSPInfo(HVideoClienT hClient, long lCamID, long& lLastTime, SVideoPSP** ppVideoPSP,
					                   int* pnVideoPSP, long lTimeOut = DEFAULT_MSG_WAIT_TIME);
 
/**
*  增加预置位
* @param  -[in]  HVideoClienT hClient: 客户端句柄
* @param  -[in] long nCamID:							摄像头ID
* @param  -[in] SVideoPSP *pthePSP:							预置位信息
* @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     07/24/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_AddPSP(HVideoClienT hClient, long lCamID, SVideoPSP *pthePSP, long lTimeOut = DEFAULT_MSG_WAIT_TIME);
//此方法直接在配置中添加预置位信息，客户端通过插件控制设备时调用此接口
VIDEO_REMOTE_API long VR_AddPSP2(HVideoClienT hClient, long lCamID, SVideoPSP *pthePSP, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

 
/**
*  应用预置位
* @param  -[in]  HVideoClienT hClient: 客户端句柄
* @param  -[in] long nCamID:							摄像头ID
* @param  -[in] SVideoPSP *pthePSP:							预置位信息
* @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     07/24/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_ApplyPSP(HVideoClienT hClient, long lCamID, SVideoPSP *pthePSP, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
*  增加模式
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[in] SVideoMode* ptheMode: 模式信息
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     07/28/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_AddMode(HVideoClienT hClient, SVideoMode* ptheMode, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
 *  删除模式
 * @param -[in] HVideoClienT hClient: 客户端句柄
 * @param -[in] SVideoMode* ptheMode: 模式信息
 * @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
 * @return ==0 success !=0 failure
 *
 *  @version     07/28/2010  lixiaohao  Initial Version.
 */
VIDEO_REMOTE_API long VR_DelMode(HVideoClienT hClient, SVideoMode* ptheMode, long lTimeOut = DEFAULT_MSG_WAIT_TIME);


/**
 *  修改模式
 * @param -[in] HVideoClienT hClient: 客户端句柄
 * @param -[in] char* szModeName: 旧模式名称
 * @param -[in] SVideoMode* ptheMode: 新模式信息
 * @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
 * @return ==0 success !=0 failure
 *  @version     07/28/2010  lixiaohao  Initial Version.
 */
VIDEO_REMOTE_API long VR_ModifyMode(HVideoClienT hClient, char* szModeName, SVideoMode* ptheMode, long lTimeOut = DEFAULT_MSG_WAIT_TIME);


/**
*  获取模式的布局信息
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[in] char* szModeName: 模式名称
* @param -[out] char* sztheModePara: 模式布局信息
* @param -[out] &lSize:				 模式布局信息的长度
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     07/28/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_GetModeLayout(HVideoClienT hClient, char* szModeName, char* sztheModePara,long& lSize, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

//---------------自定义分区相关接口------------------------------------

/**
 *  $(添加自定义分区).
 *  $(Detail).
 *
 * @param -[in] HVideoClienT hClient: 客户端句柄
 * @param -[in] const char *szCustZoneName： 自定义分区名称
 * @param -[in] const char *szCustZoneDesc： 自定义分区描述
 * @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
 * @return ==0 success !=0 failure
 *
 *  @version     08/01/2010  zhucongfeng  Initial Version.
 */

VIDEO_REMOTE_API long VR_AddCustomZone(HVideoClienT hClient, const char *szCustZoneName, const char *szCustZoneDesc, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
 *  $(删除自定义分区).
 *  $(Detail).
 *
 * @param -[in] HVideoClienT hClient: 客户端句柄
 * @param -[in] const char *szCustZoneName： 自定义分区名称
 * @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
 * @return ==0 success !=0 failure
 *
 *  @version     08/01/2010  zhucongfeng  Initial Version.
 */

VIDEO_REMOTE_API long VR_DeleteCustomZone(HVideoClienT hClient, const char *szCustZoneName, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
 *  $(将摄像头拷贝到其他自定义分区).
 *  $(Detail).
 *
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[in] const char *szCustZoneName： 自定义分区名称
* @param -[in] CStrName** ppCStrName： 摄像头名指针
* @param -[in] int* pnCStrName： 摄像头名个数
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
 *
 *  @version     08/01/2010  zhucongfeng  Initial Version.
 */

VIDEO_REMOTE_API long VR_CopyCamToCustomZone(HVideoClienT hClient, const char *szCustZoneName, CStrName** ppCStrName,
							                 int* pnCStrName, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
 *  $(删除自定义分区的摄像头).
 *  $(Detail).
 *
 * @param -[in] HVideoClienT hClient: 客户端句柄
 * @param -[in] const char *szCustZoneName： 自定义分区名称
 * @param -[in] CStrName** ppCStrName： 摄像头名指针
 * @param -[in] int* pnCStrName：       摄像头名个数
 * @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
 * @return ==0 success !=0 failure
 *
 *  @version     08/01/2010  zhucongfeng  Initial Version.
 */

VIDEO_REMOTE_API long VR_DeleteCustomZoneCam(HVideoClienT hClient, const char *szCustZoneName, CStrName** ppCStrName,
							                 int* pnCStrName, long lTimeOut = DEFAULT_MSG_WAIT_TIME);
//---------------抓拍相关接口------------------------------------
/**
*  场景 获取所有抓拍类型信息
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[out] ppIDMapText** ppVideoSnapFile: 抓拍类型指针
* @param -[out] int* pnIDMapText: 抓拍类型个数
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_GetAllSnapType(HVideoClienT hClient, SIDMapText** ppIDMapText, int *pnIDMapText, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
* 
* 修改抓拍图片信息
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[in] long nCamID: 摄像头ID
* @param -[in] long nSnapID: 抓拍ID
* @param -[in] char* szSnapType 抓拍类型
* @param -[in] char* szDesc 抓拍描述
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*  @version     08/16/2010  lixiaohao  Initial Version.*/

VIDEO_REMOTE_API long VR_ModifyPicInfo(HVideoClienT hClient, long nCamID, long nSnapID, char* szSnapType, char* szDesc, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

//------------------控制远程接口-----------------------------------//

/**
* 云台控制
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[in] long nCamID: 摄像头ID
* @param -[in] long nCtrlCode: 控制码
* @param -[in] long nCtrlType: 控制类型
* @param -[in] long nCtrlSpeed: 控制速度
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_PanControl(HVideoClienT hClient, long nCameraID, long nCtrlCode, long nCtrlType, long nCtrlSpeed, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
*  镜头控制
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[in] long nCamID: 摄像头ID
* @param -[in] long nCtrlCode: 控制码
* @param -[in] long nCtrlType： 控制类型
* @param -[in] long nCtrlSpeed: 控制速度
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_LensControl(HVideoClienT hClient, long nCameraID, long nCtrlCode, long nCtrlType,long nCtrlSpeed, long lTimeOut = DEFAULT_MSG_WAIT_TIME);



/**
*  附加设备控制
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[in] long nCamID: 摄像头ID
* @param -[in] long nCtrlCode: 控制码
* @param -[in] long nCtrlType： 控制类型
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_AuxDevControl(HVideoClienT hClient, long nCameraID, long nCtrlCode, long nCtrlType, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
*  视频切换
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[in] char* szCameraName： 摄像头名称
* @param -[in] char* szMonitorName： 监视器名称
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_SwitchVideoToMonitor(HVideoClienT hClient, char* szCameraName, char* szMonitorName,  long lTimeOut = DEFAULT_MSG_WAIT_TIME);



/**
*  根据ID进行视频切换
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[in] long lCamID： 摄像头ID
* @param -[in] long lMonID： 监视器ID
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_SwitchVideoToMonitorByID(HVideoClienT hClient, long lCamID, long lMonID,  long lTimeOut = DEFAULT_MSG_WAIT_TIME);



/**
* 轮询视频切换
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[in] char* szModeBuff： 轮询模式信息
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_CircularSwitchCamToMon(HVideoClienT hClient, const char* szModeBuff, long lTimeOut = DEFAULT_MSG_WAIT_TIME);


/**
* 停止轮询视频切换
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[in] char* szMonitorName： 监视器名称
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_StopCircularSwitchCamToMon(HVideoClienT hClient, const char* szMonitorName, long lTimeOut = DEFAULT_MSG_WAIT_TIME);


//-----------录像相关接口-------------------------//


/**
* * @param  -[in]  HVideoClienT hClient: 客户端句柄
* @param  -[in]  long nCamID: 		摄像头ID
* @param  -[in]  char* szStartTime   	开始时间(格式"2010,01,01,08,00,00")
* @param  -[in]  char* szEndTime    		结束时间(格式"2010,01,01,08,00,00")
* @param  -[in]  char* szExtent:		录像扩展信息
* @param  -[in]  char* szFileName:		录像文件名称
* @param  -[in]  char* szDesc:		录像描述
* @param  -[in]  char* pszBuffer:		录像片段Buffer
* @param  -[in]  long lBufferSize:      录像片度长度
* @param  -[out] long & lRecordID:      录像片段ID
* @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
* @return  ==0 Success !0 failure 
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_AddRecordInfo(HVideoClienT hClient, long nCamID, char* szStartTime, char* szEndTime, char* szExtent, char* szFileName, char* szDesc, char* pszBuffer,long lBufferSize,long & lRecordID, long lTimeOut  = DEFAULT_MSG_WAIT_TIME);

/**
*  查询录像片段信息
* @param  -[in]  HVideoClienT hClient: 客户端句柄
* @param  -[in]  long nCamID: 			摄像头ID
* @param  -[in]  char* szStartTime   	开始时间(格式"2010,01,01,08,00,00")
* @param  -[in]  char* szEndTime    	结束时间(格式"2010,01,01,08,00,00")
* @param  -[out] SRecord** ppRecord   	录像片段信息指针
* @param  -[out] int* pnRecord      	录像片段信息个数
* @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_QueryRecord(HVideoClienT hClient, long nCamID, char* szStartTime, char* szEndTime,
					                 SRecord** ppRecord, int* pnRecord, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
*修改录像片段信息
* @param  -[in]  HVideoClienT hClient: 客户端句柄
* @param  -[in] long nRecordID:   		录像片段ID
* @param  -[in] CRecord* pRec:  	 	录像片段信息
* @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_ModifyRecord(HVideoClienT hClient, long nRecordID, CRecord* pRec, long lTimeOut = DEFAULT_MSG_WAIT_TIME);


/**
*  下载录像片段
* @param  -[in]  HVideoClienT hClient: 客户端句柄
* @param  -[in] long nRecordID:   		录像片段ID
* @param  -[out] char* szFileName:      录像名称
* @param  -[out] char* pszBuffer:  		录像片段Buffer
* @param  -[out] long& lBufferSize;     录像片段长度
* @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_DownloadRecord(HVideoClienT hClient, long nRecordID, char* szFileName, char* pszBuffer,long& lBufferSize, long lTimeOut = DEFAULT_MSG_WAIT_TIME);


/**
*  删除录像片段
* @param  -[in]  HVideoClienT hClient: 客户端句柄
* @param  -[in] long nRecordID:   		录像片段ID
* @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_DeleteRecord(HVideoClienT hClient, long nRecordID, long lTimeOut = DEFAULT_MSG_WAIT_TIME);


/**
* 添加抓拍图片到服务端
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param -[in] long nCamID: 摄像头ID
* @param -[in] char* szSnapType 抓拍类型
* @param -[in] char* szDesc 抓拍描述
* @param -[in] char* szExt  扩展信息
* @param -[in] char* szTmSnap 抓拍时间
* @param -[in] BOOL bSnapMore 连续抓拍
* @param -[in] char * szPicBuffer 图片buffer
* @param -[in] long lBufferSize Buffer大小
* @param -[out] long &lSnapID  抓拍ID
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 Success !0 failure !=-1 
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_AddSnapPicture(HVideoClienT hClient, long nCamID, char* szSnapType, char* szDesc, char* szExt,int nSnapCount, char* szTmSnap, char* szPicBuffer,long lBufferSize,long& lSnapID, long lTimeOut=DEFAULT_MSG_WAIT_TIME);

/**
*  获取所有的模式名称
* @param -[in] HVideoClienT hClient: 客户端句柄
* @param  -[in,out]  long lLastTime:     当前最新所有配置信息的时间 
* @param -[out] SVideoMode** ppVideoMode: 模式指针
* @param -[out] int* pnVideoMode:         模式个数
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     07/24/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_GetAllModeName(HVideoClienT hClient,long& lLastTime, SVideoMode** ppVideoMode, int* pnVideoMode,
					                    long lTimeOut = DEFAULT_MSG_WAIT_TIME);


/**
* 获取抓拍图片信息
* @param -[in] HVideoClienT: hClient: 客户端句柄
* @param -[in] long nCamID: char* szZone:分区
* @param -[in] long nCamID: 摄像头ID
* @param -[in] char *szStart: 开始时间
* @param -[in] char *szEnd: 结束时间 时间格式为"2010,07,10,10,00,00"
* @param -[in] char *pszSnapType: 抓拍类型
* @param -[out] SVideoSnapFile** ppVideoSnapFile: 抓拍信息指针
* @param -[out] int* pnVideoSnapFile: 抓拍信息个数
* @param -[in] long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     08/16/2010  lixiaohao  Initial Version.
*/
VIDEO_REMOTE_API long VR_GetSnapPicInfo(HVideoClienT hClient, char* szZone,long nCamID,char *szStart,char *szEnd,
					                    char *pszSnapType, SVideoSnapFile** ppVideoSnapFile, int* pnVideoSnapFile, 
					                    long lTimeOut = DEFAULT_MSG_WAIT_TIME);
/**
*  删除分区实体内存
* @param  -[in]  SVideoZone** ppVideoZone: 分区实体数组
* @return ==ICV_SUCCESS success ==EC_ICV_CCTV_INVALID_PARAMETER failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
VIDEO_REMOTE_API long VR_FreeVideoZoneArray(SVideoZone** ppVideoZone);


/**
*  删除设备产品型号实体内存
* @param  -[in]  SVideoProduct** ppVideoProduct: 设备产品型号实体数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
VIDEO_REMOTE_API long VR_FreeVideoProductArray(SVideoProduct** ppVideoProduct);


/**
*  删除视频设备实体内存
* @param  -[in]  SVideoServer** ppVideoServer: 视频设备实体数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
VIDEO_REMOTE_API long VR_FreeVideoDeviceArray(SVideoDevice** ppVideoDevice);


/**
*  删除设备类型实体实体内存
* @param  -[in]  SVideoServer** ppVideoServer: 设备类型实体数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
VIDEO_REMOTE_API long VR_FreeVideoDevTypeArray(SVideoDevType** ppVideoDevType);


/**
*  删除预置位实体内存
* @param  -[in]  SVideoPSP** ppVideoPSP: 预置位实体内存数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
VIDEO_REMOTE_API long VR_FreeVideoPSPArray(SVideoPSP** ppVideoPSP);

/**
* 删除摄像头实体内存
* @param  -[in]  SVideoCamera** ppVideoCamera:  摄像头实体内存数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
VIDEO_REMOTE_API long VR_FreeVideoCameraArray(SVideoCamera** ppVideoCamera);


/**
* 删除监视器实体内存
* @param  -[in]  SVideoMonitor** ppVideoMonitor:  监视器实体内存数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
VIDEO_REMOTE_API long VR_FreeVideoMonitorArray(SVideoMonitor** ppVideoMonitor);


/**
* 删除模式实体内存
* @param  -[in]  SVideoMode** ppVideoMode: 模式实体数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
VIDEO_REMOTE_API long VR_FreeVideoModeArray(SVideoMode** ppVideoMode);


/**
* 删除抓拍图片实体实体内存
* @param  -[in]  SVideoSnapFile** ppVideoSnapFile:抓拍图片实体数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
VIDEO_REMOTE_API long VR_FreeVideoSnapFileArray(SVideoSnapFile** ppVideoSnapFile);


/**
* 删除抓拍类型定义实体内存
* @param  -[in]  SIDMapText** ppIDMapText:抓拍类型定义实体数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
VIDEO_REMOTE_API long VR_FreeIDMapTextArray(SIDMapText** ppIDMapText);


/**
* 删除自定义分区定义实体内存
* @param  -[in]  SVideoCustomZone** ppVideoCustomZone:自定义分区定义实体数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
VIDEO_REMOTE_API long VR_FreeVideoCustomZoneArray(SVideoCustomZone** ppVideoCustomZone);


/**
* 删除历史录像文件定义实体内存
* @param  -[in]  SHisFile** ppHisFile:历史录像文件定义实体数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
VIDEO_REMOTE_API long VR_FreeHisFileArray(SHisFile** ppHisFile);


/**
* 删除历史录像文件定义实体内存
* @param  -[in]  SHisFile** ppHisFile:历史录像文件定义实体数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
VIDEO_REMOTE_API long VR_FreeHisFileArray(SHisFile** ppHisFile);

/**
* 删除录像片段信息实体内存
* @param  -[in]  SHisFile** ppHisFile:录像片段信息实体数组
* @return ==ICV_SUCCESS success == EC_ICV_CCTV_FUNCPARAMINVALID failure
*
*  @version     01/14/2011  louzhengqing  Initial Version.
*/
VIDEO_REMOTE_API long VR_FreeRecordArray(SRecord** ppRecord);

/**
*  获取摄像头对应的实时图像的设备
* @param    -[in]     HVideoClienT hClient: 客户端句柄
* @param    -[in]     long lCamID: [摄像头设备ID]
* @param    -[out]    long& lDeviceType: [实时图像的设备类型]
* @param    -[out]    long& lProductID: [实时图像的设备ID]
* @param    -[out]    char* szChannel: [实时图像的设备通道号]
* @param    -[in]     long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     02/10/2010  louzhengqing  Initial Version.
*/
VIDEO_REMOTE_API long VR_GetCurPicDeviceFromCam(HVideoClienT hClient, long lCamID, long& lDeviceType, long& lProductID,
												char* szChannel, long lTimeOut);

/**
*  获取取摄像头对应的历史图像的设备
* @param    -[in]     HVideoClienT hClient: 客户端句柄
* @param    -[in]     long lCamID: [摄像头设备ID]
* @param    -[out]    long& lDeviceType: [历史图像的设备类型]
* @param    -[out]    long& lProductID: [历史图像的设备ID]
* @param    -[out]    char* szChannel: [历史图像的设备通道号]
* @param    -[in]     long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     02/10/2010  louzhengqing  Initial Version.
*/
VIDEO_REMOTE_API long VR_GetHisPicDeviceFromCam(HVideoClienT hClient, long lCamID, long& lDeviceType, long& lProductID,
												char* szChannel, long lTimeOut);
/**
*  获取取摄像头对应的控制设备
* @param    -[in]     HVideoClienT hClient: 客户端句柄
* @param    -[in]     long lCamID: [摄像头设备ID]
* @param    -[out]    long& lDeviceType: [控制设备的设备类型]
* @param    -[out]    long& lProductID: [控制设备的设备ID]
* @param    -[out]    char* szChannel: [控制设备的设备通道号]
* @param    -[in]     long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     03/30/2011  fengjuanyong  Initial Version.
*/
VIDEO_REMOTE_API long VR_GetCtrlDeviceFromCam(HVideoClienT hClient, long lCamID, long& lDeviceType, long& lProductID,
												char* szChannel, long lTimeOut);

/**
*  释放摄像头对应的编码器
* @param    -[in]     HVideoClienT hClient: 客户端句柄
* @param    -[in]     long lCamID: [摄像头设备ID]
* @param    -[in]     char* szChannel: [通道号]
* @param    -[in]     long lTimeOut = DEFAULT_MSG_WAIT_TIME: 超时时间
* @return ==0 success !=0 failure
*
*  @version     02/10/2010  louzhengqing  Initial Version.
*/
VIDEO_REMOTE_API long VR_ReleaseCodeDeviceofCam(HVideoClienT hClient, long lCamID, long lCodeID, char* szChannel, long lTimeOut);

/**
 *  根据摄像头名称获取分配的监视器信息.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[in]  char *  pszCamName: [摄像头名称]
 *  @param  -[in]  long lAuthMode: [权限模式]
 *  @param  -[out]  char* pszMonitorName: [监视器名称]
 *  @param  -[out]  long& lSize: 监视器名称的长度
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     02/28/2011  fengjuanyong  Initial Version.
 */
VIDEO_REMOTE_API long VR_GetMonitorDevFromCam(HVideoClienT hClient, char* pszCamName, long lAuthMode, char* pszMonitorName, long& lSize, long lTimeOut = DEFAULT_MSG_WAIT_TIME);
/**
 *  释放分配的监视器信息.
 *
 *  @param  -[in]  HVideoClienT hClient: [客户端句柄]
 *  @param  -[in]  char *  pszCamName: [摄像头名称]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: [超时时间]
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     02/28/2011  fengjuanyong  Initial Version.
 */
VIDEO_REMOTE_API long VR_ReleaseMonitorDevForCam(HVideoClienT hClient, char* pszCamName, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

/**
 * 根据矩阵获取连接该矩阵的摄像头简易信息
 *  @param  -[in]  HVideoClienT hClient: 客户端句柄
 *  @param  -[in]  char *  pszMatrixName: [矩阵名称]
 *  @param  -[out] SVideoCamera** ppVideoCamera:  [摄像头指针]
 *  @param  -[out] int* pnVideoCamera:			  [摄像头个数]
 *  @param  -[in]  long lTimeOut =  DEFAULT_MSG_WAIT_TIME: 超时时间
 *  @return  ==0 success  !=0 failure
 *
 *  @version     02/28/2011  fengjuanyong  Initial Version.
 */
VIDEO_REMOTE_API long VR_GetCamLstFromMatrix(HVideoClienT hClient, char* pszMatrixName, SVideoCamera** ppVideoCamera, int* pnVideoCamera, long lTimeOut = DEFAULT_MSG_WAIT_TIME);

#endif 
