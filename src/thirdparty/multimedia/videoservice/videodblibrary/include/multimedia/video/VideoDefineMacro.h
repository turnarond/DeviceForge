/**************************************************************
 *  Filename:    VideoCommonDefine.h
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: 视频监控系统内部共用宏定义.
 *
 *  @author:     louzhengqing
 *  @version     2010/01/14  louzhengqing		Initial Version
**************************************************************/
#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef VIDEO_DEFINE_MACRO_H_
#define VIDEO_DEFINE_MACRO_H_

///////////////////////// 视频参数标记定义 /////////////////////////
#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif

#ifndef OPT
#define OPT
#endif
///////////////////////// 视频参数标记定义结束 /////////////////////////

///////////////////////// 视频监控系统公用宏定义 /////////////////////////
//#define VIDEO_SYSTEM_TIPS				"视频监控系统"
//#define VIDEO_DEFAULT_ZONE_NAME			"默认分区"
//#define VIDEO_ALL_ZONE_DEFAULT_NAME		"所有分区"
//#define VIDEO_SYSTEM_GLOBAL_ZONE		"固定分区"
//#define VIDEO_SYSTEM_USER_ZONE			"我的分区"
//#define STR_ALLSNAPTYPE					"所有类型"
//#define	STR_ALLCAMERA					"所有摄像头"
#define VIDEO_DEFAULT_ZONE_ID			1		// 默认分区ID
#define VIDEO_NAME_MAXSIZE				32		// 视频名称最大长度
#define VIDEO_DESCRIPTION_MAXSIZE		64		// 视频描述最大长度
#define VIDEO_DEFAULTINFO_MAXSIZE		256		// 其他属性最大长度
#define VIDEO_EXTENT_MAXSIZE			256		// 扩展属性最大长度
#define VIDEO_CHANNEL_MAXSIZE			256		// 通道最大长度，这里的通道是泛指，有些视频服务就是一个编码
#define VIDEO_FILE_NAME_MAXSIZE			199		// 录像片段文件名称最大程度
#define VIDEO_IPADDRESS_MAXSIZE			16		// IP地址最大长度
#define VIDEO_IPADDRESS_FIELDNUM		4		// IP地址段数
#define VIDEO_DEFAULT_IP				"127.0.0.1"
#define VIDEO_DEFAULT_IP_SIZE			10		// 默认IP地址(127.0.0.1)长度

// 系统级极限值
#define VIDEO_LIMIT_CAMERA_MAX_COUNT	6000	// 系统摄像头限制数
#define VIDEO_LIMIT_MONITOR_MAX_COUNT	2000	// 系统监视器限制数
#define VIDEO_LIMIT_DEVICE_MAX_COUNT	1000	// 系统设备限制数
#define VIDEO_LIMIT_MODE_MAX_COUNT		1000	// 系统模式数量的限制数

// 无效值定义
#define VIDEO_ID_INVALIDATION			    -1		// ID的无效值
#define VIDEO_ID_STRING_INVALIDATION	    "-1"	    // ID的无效值，字符串类型
#define VIDEO_ID_DEFAULT				    0		// 默认ID
#define VIDEO_CHANNEL_INVALIDATION		    VIDEO_ID_INVALIDATION		    // 通道号的无效值
#define VIDEO_STRING_CHANNEL_INVALIDATION	VIDEO_ID_STRING_INVALIDATION	// 通道号的无效值，字符串类型
#define VIDEO_SDK_ID_INVALIDATION		    VIDEO_ID_INVALIDATION		// SDK返回的无效值
#define VIDEO_INDEX_INVALIDATION		    VIDEO_ID_INVALIDATION		// 序号的无效值
#define VIDEO_PORT_INVALIDATION			    VIDEO_ID_INVALIDATION		// 无效的端口号
#define VIDEO_DEFAULT_CHANNEL			    0		// 默认通道号
#define VIDEO_DEFAULT_STRING_CHANNEL	    "0"		// 默认通道号，字符串类型

// 抓拍默认参数定义
#define VIDEO_DEFAULT_SNAP_WIDTH		384		// 视频抓拍默认宽度
#define VIDEO_DEFAULT_SNAP_HEIGHT		288		// 视频抓拍默认高度
#define VIDEO_DEFAULT_SNAP_BITDEEP		24		// 视频抓拍默认深度

// 设备类型定义
#define VIDEO_DEVICE_TYPE_NONE			0		// 视频未知设备		
#define VIDEO_DEVICE_TYPE_MATRIX		1		// 矩阵		
#define VIDEO_DEVICE_TYPE_DVR			2		// 硬盘录像机
#define VIDEO_DEVICE_TYPE_ENCODER		3		// 视频编码器
#define VIDEO_DEVICE_TYPE_DECODER		4		// 视频解码器
#define VIDEO_DEVICE_TYPE_CAMERA		5		// 摄像头
#define VIDEO_DEVICE_TYPE_MONITOR		6		// 监视器
#define VIDEO_DEVICE_TYPE_NVR		    7		// NVR网络视频服务

// 其他定义
#define VIDEO_CAMERA_PSP_MAX_COUNT		256		// 默认摄像头的最大预置位数量
#define VIDEO_MATRIX_CTRL_PORT			1		// 矩阵摄像头控制专用通道

// dll名称定义
#define VIDEO_AUTH_DLLNAME				"VideoAuth.dll"

// 数据库文件、服务名定义
#define VIDEO_DB_CONFIG_FILE_NAME		"VideoCfg.db"
#define VIDEO_SERVICE_NAME				"VideoService"

// 设备功能类型
#define SRC_DEV_FUNC 1
#define CTL_DEV_FUNC 2
#define HIS_DEV_FUNC 3

// 时间相关定义
#define VIDEO_MILLISECONDS_PER_SECOND	1000	// 1秒钟=1000毫秒
#define VIDEO_SECONDS_PER_MINUTE		60		// 1分钟=60秒
#define VIDEO_PREDOWN_DELAY_TIME		5		// 启动失败退出程序延迟时间, 单位秒
#define VIDEO_HAND_PERIOD				1		// 客户端定时检测的时间周期, 单位秒
#define VIDEO_INFO_SYNC_PERIOD			5		// 信息同步周期, 单位秒
#define VIDEO_NOHAND_MAX_PERIOD			10		// 该时间周期内没有检测消息认为断开连接, 单位秒
#define VIDEO_DEFAULT_LOCK_TIME			5		// 自动锁定的默认时间, 单位秒
#define VIDEO_DEFAULT_TIMEOUT			2		// 客户端和服务端之间的通讯默认超时, 单位秒
#define VIDEO_DEFAULT_LONG_TIMEOUT		60		// 客户端和服务端之间的长时间通讯默认超时(常用于查询), 单位秒
#define VIDEO_THREAD_BLOCK_TIME			1000	// 线程读取消息的阻塞时间(us)
#define VIDEO_RECEIVE_MESSAGE_TIMEOUT	100		// 接收外部消息的超时时间(ms)
#define VIDEO_WAIT_TIME					200000	// 每次等待时间(us)

// 等待次数(每次200ms)
#define VIDEO_WAIT_COUNT_LEAST			3		// 连续等待次数__最少
#define VIDEO_WAIT_COUNT_LESS			10		// 连续等待次数__少
#define VIDEO_WAIT_COUNT				30		// 连续等待次数
#define VIDEO_WAIT_COUNT_MORE			100		// 连续等待次数__多
#define VIDEO_TIMER_COUNT_MAX			36000	// 定时器计数最大值

// 模式定义
#define VIDEO_MODETYPE_SHOW					1		// 模式类型1表示显示模式	
#define VIDEO_MODETYPE_CYCLE				2		// 模式类型2表示轮循模式
#define VIDEO_MODETYPE_MONITORCYCLE			3		// 模式类型3表示监视器轮循模式
#define VIDEO_MODE_PARA_MAXSIZE				4096	// 模式参数最大长度
#define VIDEO_GENERAL_RET_MAX_SIZE			4*1024	// 一般控制指令的返回长度
#define VIDEO_MESSAGE_MAX_SIZE				4096	// 通讯消息的长度
#define VIDEO_STRUCT_SIZE					1024	// 结构体最大长度

// 日志最大长度
#define VIDEO_LOG_SIZE						1024	// 日志长度	

// 字符长度定义
#define VIDEO_CAMERA_SET_SIZE				6		// 摄像头个数的长度
#define VIDEO_CMD_SIZE						3		// 控制码长度
#define VIDEO_CMD_STAMP_SIZE				10		// 控制命令序列长度
#define VIDEO_NAME_LENGTH_SIZE				2		// 名称长度的长度
#define VIDEO_DESP_LENGTH_SIZE				2		// 描述长度的长度
#define VIDEO_EXTENT_LENGTH_SIZE			3		// 扩展属性长度的长度
#define	VIDEO_GENERAL_CMD_LENGTH_SIZE		4		// 一般控制指令长度的长度
#define VIDEO_PSP_COUNT_SIZE				3		// 预置位个数的长度，指的是每个摄像头可以添加的预置位，目前按照最大256设计
#define VIDEO_PSP_INDEX_SIZE				3		// 预置位索引的长度
#define VIDEO_IP_LENGTH_SIZE				2		// IP地址长度的长度
#define VIDEO_STATUS_SIZE					1		// 状态信息长度
#define VIDEO_LINK_DEVICE_COUNT_SIZE		4		// 连接设备数的长度
#define VIDEO_DEVICE_TYPE_SIZE				1		// 设备类型长度
#define VIDEO_CMD_LENS_SIZE					1		// 镜头控制命令码长度
#define VIDEO_SNAP_WIDTH_SIZE				4		// 抓拍宽度长度
#define VIDEO_SNAP_HEIGHT_SIZE				4		// 抓拍高度长度
#define VIDEO_SNAP_BITDEEP_SIZE				2		// 抓拍位深度长度
#define VIDEO_TIME_LENGTH_SIZE				10		// 时间的长度
#define VIDEO_TIME_LOCKTYPE_SIZE			1		// 锁定类型的长度
#define VIDEO_TIME_LOCK_SIZE				6		// 锁定时间的长度(时间单位是秒)
#define VIDEO_TIME_LOCK_LENGTH_SIZE			2		// 锁定信息长度的长度
#define VIDEO_SERIAL_SNAP_COUNT_SIZE		2		// 连续抓拍张数的长度
#define VIDEO_AUTH_LEVEL_FIELD_SIZE			1		// 权限等级/范围的长度
#define VIDEO_MODE_TYPE_SIZE				1		// 模式类型长度
#define VIDEO_ZONE_ID_SIZE					4		// 分区ID长度
#define VIDEO_SWITCH_COUNT_SIZE				4		// 视频切换数长度
#define VIDEO_CAMERA_ID_SIZE				6		// 摄像头ID长度
#define VIDEO_MONITOR_ID_SIZE				4		// 监视器ID长度
#define VIDEO_PRODUCT_ID_SIZE				4		// 产品型号ID长度
#define VIDEO_DEVICE_ID_SIZE				4		// 设备ID长度
#define VIDEO_DEVICE_PORT_SIZE				6		// 设备端口长度
#define VIDEO_DEVICE_CHANNEL_ID_SIZE		4		// 设备通道长度
#define VIDEO_MODE_COUNT_SIZE				4		// 模式个数长度
#define VIDEO_SNAPTYPE_COUNT_SIZE			2		// 抓拍类型个数长度
#define VIDEO_SNAP_ID_SIZE					6		// 抓拍图片ID长度
#define VIDEO_RECORD_ID_SIZE				6		// 抓拍录像ID长度
#define VIDEO_SNAP_COUNT_SIZE				6		// 抓拍buffer个数长度
#define VIDEO_MODE_PARA_LENGTH_SIZE			4		// 模式参数长度的长度
#define VIDEO_INFO_CHANGE_SIZE				4		// 服务信息发生改变的状态长度
#define VIDEO_SNAP_BUFFER_LENGTH_SIZE		10		// 抓拍图片buffer长度的长度
#define VIDEO_CHANGETIME_LENGTH_SIZE		10		// 模式和预置位最新更新时间的位数
#define VIDEO_ELAPSECTRLTIME_LENGTH_SIZE	10		// 连续控制的持续时间
#define VIDEO_RETCODE_LENGTH_SIZE			10		// 返回码的长度
#define	COMMAND_CODE_GAP_REQANDRET			400		// 请求和返回命令号的差值
// *  @version     06/12/2010  lixiaohao  增加抓拍图片Buffer中
#define VIDEO_SNAP_TIME_SIZE				17		// 时间格式长度定义 YYYYmmDDHHMMSS000


// 状态定义
#define VIDEO_CTRL_STATUS_ENABLE			1		// 能够控制
#define VIDEO_SERVER_HAS_BAK				1		// 有备机
#define VIDEO_LOCK_STATUS_LOCK				1		// 设备锁定
#define VIDEO_LOCK_STATUS_UNLOCK			0		// 设备解锁

// 2. 控制码定义
// 2.1 基本信息定义
#define VIDEO_CMD_GET_ALL_CONFIG_INFO		100		// 获取所有的设备配置信息
#define VIDEO_CMD_GET_ALL_ZONE				101		// 获取所有的分区信息
#define VIDEO_CMD_GET_ALL_PRODUCT			102		// 获取所有设备的产品型号信息
#define VIDEO_CMD_GET_ALL_DEVICE			103		// 获取除了摄像头和监视器的设备信息
#define VIDEO_CMD_GET_ALL_CAMERA			110		// 获取所有的摄像头信息
#define VIDEO_CMD_GET_ALL_SIMPLE_CAMERA		111		// 获取所有的摄像头简易信息
#define VIDEO_CMD_GET_ZONE_CAMERA			112		// 获取某一分区的摄像头信息
#define VIDEO_CMD_GET_ZONE_SIMPLE_CAMERA	113		// 获取某一分区的摄像头简易信息
#define VIDEO_CMD_GET_CAMERA_BY_NAME		114		// 根据摄像头名称获取摄像头的信息
#define VIDEO_CMD_GET_ALL_MONITOR			120		// 获取所有的监视器信息
#define VIDEO_CMD_GET_ZONE_MONITOR			121		// 获取某一分区的监视器信息

// 2.2 模式管理定义
#define VIDEO_CMD_GET_ALL_MODE_NAME			130		// 获取所有模式名称
#define VIDEO_CMD_ADD_MODE					131		// 增加若干个模式
#define VIDEO_CMD_DELETE_MODE				132		// 删除若干个模式
#define VIDEO_CMD_MODIFY_MODE				133		// 修改若干个模式
#define VIDEO_CMD_GET_MODE_LAYOUT			134		// 获取模式的布局信息

// 2.3 预置位管理定义
#define VIDEO_CMD_GET_PSP					140		// 获取摄像头的预置位信息
#define VIDEO_CMD_ADD_PSP					141		// 增加若干个预置位
#define VIDEO_CMD_DELETE_PSP				142		// 删除若干个预置位
#define VIDEO_CMD_MODIFY_PSP				143		// 修改若干个预置位
#define VIDEO_CMD_APPLY_PSP					144		// 应用若干个摄像头的预置位
#define VIDEO_CMD_ADD_PSP2                  145     // 直接在配置中增加预置位信息，以处理客户端通过插件控制摄像头

// 2.4 抓拍图片管理定义
#define VIDEO_CMD_GET_SNAP_TYPE				150		// 获取抓拍图片的信息类型
#define VIDEO_CMD_QUERY_SNAP_INFO			151		// 按指定条件查询抓拍信息
#define VIDEO_CMD_GET_PIC_BUFFER_BYID		152		// 获取指定抓拍图片的图片内容
#define VIDEO_CMD_ADD_SNAP_PIC				153		// 增加抓拍图片
#define VIDEO_CMD_DELETE_SNAP_PIC			154		// 删除抓拍图片
#define VIDEO_CMD_MODIFY_SNAP_INFO			155		// 修改抓拍信息
#define VIDEO_CMD_DOWNLOAD_PIC				156		// 下载抓拍图片
#define VIDEO_CMD_ADDTO_PIC					157		// 累加到之前的抓拍图片

// 2.4 抓拍录像片段管理定义
#define VIDEO_CMD_ADD_CAPTURE_RECORD		160		// 增加录像片段信息
#define VIDEO_CMD_QUERY_CAPTURE_INFO		161		// 按指定条件查询录像片段信息
#define VIDEO_CMD_MDF_CAPTURE_RECORD		162		// 修改录像片段信息
#define VIDEO_CMD_GET_CAPTRUE_BUFFER		163		// 下载录像片段
#define VIDEO_CMD_DEL_CAPTURE_RECORD		164		// 删除录像片段

// 2.5 视频控制
#define VIDEO_CMD_GET_ALL_MCLINK			180		// 获取所有的监视器和摄像头的对应关系
#define VIDEO_CMD_SWITCH_BY_ID				181		// 视频切换--逻辑ID
#define VIDEO_CMD_SWITCH_BY_NAME			182		// 视频切换--名称
#define VIDEO_CMD_LENS_CONTROL				183		// 镜头控制
#define VIDEO_CMD_AUX_DEVICE_CONTROL		184		// 附加设备控制
#define VIDEO_CMD_ORIENT_CONTROL			185		// 云台控制
#define VIDEO_CMD_CYCLE_SWITCH_BY_NAME		186		// 监视器循环切换--模式名称
#define VIDEO_CMD_CYCLE_SWITCH_BY_BUFFER	187		// 监视器循环切换--buffer
#define VIDEO_CMD_STOP_CYCLE_SWITCH			188		// 停止监视器循环切换

// 2.6 其他功能
#define VIDEO_CMD_LOCK_CAMERA				190		// 锁定(解锁)摄像头
#define VIDEO_CMD_LOCK_MONITOR				191		// 锁定(解锁)监视器
#define VIDEO_CMD_GET_SERVICE_STATUS		195		// 获取视频服务的活动状态
#define VIDEO_CMD_RESTART_SERVICE			196		// 重启视频服务
#define VIDEO_CMD_SHUTDOWN_SERVICE			197		// 关闭视频服务
#define VIDEO_CMD_GENERAL_CONTROL			200		// 通用操作(预留)

// 2.7 自定义分区管理
#define VIDEO_CMD_CUSTZONE_GET				210		// 获取用户有权限看到的所有自定义分区信息
#define VIDEO_CMD_CUSTZONE_ADD				211		// 添加自定义分组
#define VIDEO_CMD_CUSTZONE_DEL				212		// 删除自定义分区
#define VIDEO_CMD_CUSTZONE_GETCAM			213		// 获取用户有权限看到的某一自定义分区摄像头信息
#define VIDEO_CMD_CUSTZONE_CPYCAM			214		// 复制摄像头到自定义分区
#define VIDEO_CMD_CUSTZONE_DELCAM			215		// 删除自定义分区中的摄像头

// 2.8 获取服务端版本号
#define VIDEO_CMD_GET_SERVICEVER			220		// 获取服务端版本号

// 2.9 摄像头相关设备
#define VIDEO_CMD_REALDEVICE_GET			230		// 获取摄像头关联的实时设备
#define VIDEO_CMD_ENCODEDEV_FREE			231		// 释放摄像头关联的编码器
#define VIDEO_CMD_HISDEVICE_GET				232		// 获取摄像头关联的历史录像设备
#define VIDEO_CMD_MONITORDEV_GET			233		// 获取摄像头关联的监视器
#define VIDEO_CMD_MONITORDEV_FREE			234		// 释放摄像头关联的监视器
#define VIDEO_CMD_CAMBYMATRIX_GET			235		// 获取矩阵关联的摄像头
#define VIDEO_CMD_CTLDEVICE_GET				236		// 获取矩阵关联的控制设备

#endif //#ifndef VIDEO_DEFINE_MACRO_H_ 
