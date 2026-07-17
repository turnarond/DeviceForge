#ifndef _DEF_VIDEO_DEFINE_H_
#define _DEF_VIDEO_DEFINE_H_
/**************************************************************
 *  Filename:    VideoDefine.h
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: 视频监控系统外部宏定义.
 *
 *  @author:     xulizai
 *  @version     2008/05/16  xulizai  Initial Version
				 2009/10/26  chenzhan  VIDEO_CURRENT_VERSION  5001 此版本插件增加了快进和慢进功能
				 2010/01/31  chenzhan  VIDEO_CURRENT_VERSION  5002 此版本插件增加了编码器视频切换功能和修改获取历史文件信息接口参数
				 2010/07/20  zhucongefeng	添加HVideoClienT定义
**************************************************************/
///////////////////////// 插件/驱动/控件二次开发相关宏定义 /////////////////////////
#define VIDEO_CURRENT_VERSION			5002	// 当前版本号5002
#define VIDEO_CTRL_MAXSPEED				5		// 视频控制的最大速度
#define VIDEO_CTRL_DEFAULTSPEED			3		// 视频控制的默认速度
#define VIDEO_CTRL_MINSPEED				1		// 视频控制的最小速度
#define VIDEO_DVR_DEFAULT_CODE_FLOW		1		// 硬盘录像机默认的码流等级数	

//2013-03-10
#define VIDEO_CAMERA_DEFAULT_CODEFLOW	0		// 摄像头默认码流	
#define VIDEO_CAMERA_MAIN_CODEFLOW		0		// 摄像头主码流	
#define VIDEO_CAMERA_SECOND_CODEFLOW	1		// 摄像头辅码流	

// 控制码定义
// 云台转动方向定义
#define VIDEO_ORIENT_CENTER				0		// 方向_中心位置
#define VIDEO_ORIENT_LEFT				1		// 方向_左边
#define VIDEO_ORIENT_UP					2		// 方向_上边
#define VIDEO_ORIENT_RIGHT				3		// 方向_右边
#define VIDEO_ORIENT_DOWN				4		// 方向_下边

// 镜头控制
#define VIDEO_CTRL_LENS_ZOOMIN			1		// 镜头放大
#define VIDEO_CTRL_LENS_ZOOMOUT			2		// 镜头缩小
#define VIDEO_CTRL_LENS_BRIGHT			3		// 镜头变亮
#define VIDEO_CTRL_LENS_DARK			4		// 镜头变暗
#define VIDEO_CTRL_LENS_FAR				5		// 调近焦距(拉远)
#define VIDEO_CTRL_LENS_NEAR			6		// 调远焦距(拉近)

// 预置位控制
#define VIDEO_CTRL_PSP_ADD				1		// 设置预置位
#define VIDEO_CTRL_PSP_APPLY			2		// 调用预置位
#define VIDEO_CTRL_PSP_DELETE			3		// 删除预置位

// 附加设备控制
#define VIDEO_CTRL_AUX_BRUSH			1		// 雨刷控制
#define VIDEO_CTRL_AUX_HEATER			2		// 加热器控制

#define VIDEO_CTRL_TYPE_START			1		// 开始控制动作
#define VIDEO_CTRL_TYPE_STOP			0		// 停止控制动作

// 抓拍相关定义
#define VIDEO_SNAP_FORMAT_JPG			1		// 抓拍图片为jpg格式
#define VIDEO_SNAP_FORMAT_BMP			2		// 抓拍图片为bmp格式

#define VIDEO_SERIES_SNAP_MAX_COUNT		10		// 连续抓拍的最大张数
#define VIDEO_SNAP_PERIOD_MIN			100		// 连续抓拍的最小时间间隔,ms
#define VIDEO_SNAP_PERIOD_MAX			10000	// 连续抓拍的最大时间间隔,ms

typedef void*  HVideoClienT;

#define VIDEO_CHECK_FAIL_RETURN(LRETVALUE)	\
{ \
	/*long lRet = LRETVALUE;*/ \
	lRet = LRETVALUE;\
	if(lRet != ICV_SUCCESS) \
	return lRet; \
	}

#endif //_DEF_VIDEO_DEFINE_H_
