/**************************************************************
 *  Filename:    VideoDBLibrary.cpp
 *  Copyright:   Shanghai Baosight Software Co., Ltd.
 *
 *  Description: 视频监控系统数据库操作实现文件.
 *
 *  @author:     xulizai
 *  @version     2008/05/16  xulizai  Initial Version
 *  @version     2009/04/15  chenzhan 增加了自定义分区和自定义分区摄像头两张表共7个函数
 *  @version     2009/06/11  zhucongfeng 修改VideoReadSnapPicBuffer，查询个数为0时进行处理
 *  @version     2009/08/11  zhucongfeng 根据静态代码检查修改VideoUpdateSnapPicture开始的参数合法性验证
 *  @version     2009/08/25  chenzhan  增加了抓拍累加函数
 *				 08/31/2009  chenzhan  增加录像片段管理.
				 05/03/2010  chenzhan  优化摄像机列表添加函数.
				 14/04/2010  chenzhan  增加设备级连控制当前通道号数据库表.
				 04/05/2012  zhucongfeng 修改VideoUpdateMode，布局为空的时候，不更新布局，不为空的时候更新
**************************************************************/

#include "common/CppSQLite3.h"
#include "multimedia/video/VideoDBLibrary.h"
#include "common/CVLog.h"
#include "common/CommHelper.h"
#include "errcode/ErrCode_iCV_CCTV.hxx"
#include <sstream>
#include <stdio.h>

extern CCVLog CVLog;

#define VIDEO_SQL_MAX_SIZE				1024					// SQL语句长度
#define	VIDEO_BEGIN_TRANSACTION			"begin transaction;"	// 事务开始
#define VIDEO_END_TRANSACTION			"commit transaction;"	// 事务结束
#define VIDEO_ROLLBACK_TRANSACTION		"rollback transaction;"	// 事务结束

#define VIDEO_TRANSACTION_PROC(x) m_pdbCfg->execDML(VIDEO_BEGIN_TRANSACTION); \
	long lRet = x; \
	if (ICV_SUCCESS != lRet) \
		m_pdbCfg->execDML(VIDEO_ROLLBACK_TRANSACTION); \
	else \
		m_pdbCfg->execDML(VIDEO_END_TRANSACTION); \
	return lRet;

#define VIDEOPRODUCT "CREATE TABLE t_video_product(\
    fd_product_id          INTEGER PRIMARY KEY,\
    fd_product_name        VARCHAR(32)    NOT NULL,\
    fd_dll_name          VARCHAR(32)    NOT NULL,\
    fd_device_type_id    INTEGER        NOT NULL,\
    UNIQUE(fd_product_name,fd_dll_name)\
);"

#define VIDEOCAMERA "CREATE TABLE t_video_camera(\
    fd_camera_id         INTEGER PRIMARY KEY,\
    fd_camera_name       VARCHAR(32)    NOT NULL,\
    fd_description       VARCHAR(64)	NULL,\
    fd_remote_file       INTEGER        NOT NULL,\
    fd_local_file        INTEGER        NOT NULL,\
    fd_remote_time       INTEGER        NOT NULL,\
    fd_orient_ctrl       INTEGER        NOT NULL,\
    fd_lens_ctrl         INTEGER        NOT NULL,\
    fd_psp_ctrl          INTEGER        NOT NULL,\
    fd_quality_ctrl      INTEGER        NOT NULL,\
    fd_snap_ctrl         INTEGER        NOT NULL,\
    fd_heater_ctrl       INTEGER        NOT NULL,\
    fd_rainbrush_ctrl    INTEGER        NOT NULL,\
    fd_snap_width        INTEGER        NULL,\
    fd_snap_height       INTEGER        NULL,\
    fd_snap_bitdeep      INTEGER        NULL,\
    fd_psp_maxcount      INTEGER        NULL,\
    fd_server_id         INTEGER        NOT NULL,\
    fd_zone_id           INTEGER        NOT NULL,\
    UNIQUE(fd_camera_name)\
);"

#define VIDEOCAMERALINK "CREATE TABLE t_video_camera_link(\
    fd_channel_id       INTEGER PRIMARY KEY,\
    fd_camera_id        INTEGER    	NOT NULL,\
    fd_device_ctrl      INTEGER    	NOT NULL,\
    fd_device_source    INTEGER    	NOT NULL,\
	fd_device_history   INTEGER    	NOT NULL\
);"

#define VIDEOCHANNEL "CREATE TABLE t_video_channel(\
    fd_channel_id        INTEGER PRIMARY KEY,\
    fd_channel_index     VARCHAR(256)  	NOT NULL,\
    fd_device_id         INTEGER    	NOT NULL,\
    fd_linkchannel_id    INTEGER    	NULL\
);"

#define VIDEODEVICE "CREATE TABLE t_video_device(\
    fd_device_id       INTEGER PRIMARY KEY,\
    fd_device_name     VARCHAR(32)    	NOT NULL,\
    fd_device_ip       VARCHAR(16)    	NOT NULL,\
    fd_device_port     INTEGER          NOT NULL,\
    fd_ctrl_channel    INTEGER          NULL,\
	fd_device_extent   VARCHAR(256)    	NULL,\
    fd_description     VARCHAR(64)    	NULL,\
    fd_server_id       INTEGER          NOT NULL,\
    fd_product_id      INTEGER          NOT NULL,\
    UNIQUE(fd_device_name)\
);"

#define VIDEODEVICETYPE "CREATE TABLE t_video_device_type(\
    fd_device_type_id      INTEGER PRIMARY KEY,\
    fd_device_type_text    VARCHAR(32)  NOT NULL,\
    UNIQUE(fd_device_type_text)\
);"

//#define INSERTMATRIXTYPE "INSERT INTO t_video_device_type (fd_device_type_id,fd_device_type_text) VALUES (1,'视频矩阵');"
//#define INSERTDVRTYPE "INSERT INTO t_video_device_type (fd_device_type_id,fd_device_type_text) VALUES (2,'硬盘录像机');"
//#define INSERTENCODERTYPE "INSERT INTO t_video_device_type (fd_device_type_id,fd_device_type_text) VALUES (3,'编码器');"
//#define INSERTDECODERTYPE "INSERT INTO t_video_device_type (fd_device_type_id,fd_device_type_text) VALUES (4,'解码器');"
//#define INSERTCAMERATYPE "INSERT INTO t_video_device_type (fd_device_type_id,fd_device_type_text) VALUES (5,'摄像头');"
//#define INSERTMONITORTYPE "INSERT INTO t_video_device_type (fd_device_type_id,fd_device_type_text) VALUES (6,'监视器');"
//#define INSERTNVRTYPE "INSERT INTO t_video_device_type (fd_device_type_id,fd_device_type_text) VALUES (7,'网络视频服务');"

#define VIDEOMONITOR "CREATE TABLE t_video_monitor(\
    fd_monitor_id      INTEGER PRIMARY KEY,\
    fd_monitor_name    VARCHAR(32)    	NOT NULL,\
    fd_description     VARCHAR(64)    	NULL,\
    fd_server_id       INTEGER          NOT NULL,\
    fd_zone_id         INTEGER          NOT NULL,\
    fd_channel_id      INTEGER          NULL,\
    UNIQUE(fd_monitor_name,fd_channel_id)\
);"

#define VIDEOSERVER "CREATE TABLE t_video_server(\
    fd_server_id      INTEGER PRIMARY KEY,\
    fd_server_name    VARCHAR(32)    	NOT NULL,\
    fd_server_ip      VARCHAR(16)    	NOT NULL,\
    fd_bak            INTEGER           NOT NULL,\
    fd_bak_ip         VARCHAR(16)    	NULL,\
	fd_lock_seconds	  INTEGER			NOT NULL,\
    fd_description    VARCHAR(64)    	NULL,\
    UNIQUE(fd_server_name,fd_server_ip)\
);"

#define VIDEOSNAPTYPE "CREATE TABLE t_video_snap_type(\
    fd_snap_type_id      INTEGER PRIMARY KEY,\
    fd_snap_type_text    VARCHAR(32)    NOT NULL,\
    UNIQUE(fd_snap_type_text)\
);"

#define VIDEOZONE "CREATE TABLE t_video_zone(\
    fd_zone_id        INTEGER PRIMARY KEY,\
    fd_zone_name      VARCHAR(32)    	NOT NULL,\
    fd_description    VARCHAR(64)    	NULL,\
    fd_auth_label     VARCHAR(32)    	NOT NULL,\
    UNIQUE(fd_zone_name)\
);"

#define VIDEOMODE "CREATE TABLE t_video_mode(\
    fd_mode_name      VARCHAR(32)      	PRIMARY KEY,\
    fd_mode_type      INTEGER          	NOT NULL,\
    fd_description    VARCHAR(64)      	NULL,\
    fd_mode_layout    BLOB    		NOT NULL\
);"

#define VIDEOPSP "CREATE TABLE t_video_psp(\
    fd_camera_id      INTEGER		NOT NULL,\
    fd_psp_name       VARCHAR(32)    	NOT NULL,\
    fd_psp_index      INTEGER          	NOT NULL,\
    fd_description    VARCHAR(64)    	NULL,\
    PRIMARY KEY(fd_camera_id,fd_psp_name)\
);"

#define VIDEOSNAP "CREATE TABLE t_video_snap(\
    fd_snap_id           INTEGER        PRIMARY KEY,\
    fd_camera_id         INTEGER        NOT NULL,\
    fd_snap_time         INTEGER        NOT NULL,\
    fd_snap_type_id      INTEGER        NOT NULL,\
	fd_snap_extent		 VARCHAR(256)   NULL,\
    fd_description       VARCHAR(64)    NULL,\
    fd_snap_count        INTEGER        NOT NULL,\
    fd_picture_buffer    BLOB		NOT NULL\
);"

#define VIDEOCUSTOMZONE "CREATE TABLE t_video_customzone(\
    fd_customzone_id     INTEGER        PRIMARY KEY,\
    fd_customzone_name   VARCHAR(32)    NOT NULL,\
    fd_customzone_user   VARCHAR(32)    NOT NULL,\
    fd_description       VARCHAR(64)    NULL,\
	UNIQUE(fd_customzone_name)\
);"

#define VIDEOCAMERAINDEX "CREATE TABLE t_video_cameraindex(\
	fd_primary_id        INTEGER        PRIMARY KEY,\
    fd_camera_id         INTEGER        NOT NULL,\
    fd_customzone_id     INTEGER        NOT NULL\
);"

#define VIDEORECORD "CREATE TABLE t_video_record(\
	fd_record_id		 INTEGER        PRIMARY KEY,\
	fd_camera_id		 INTEGER        NOT NULL,\
	fd_start_time		 INTEGER        NOT NULL,\
	fd_end_time			 INTEGER        NOT NULL,\
	fd_record_extent	 VARCHAR(256)   NULL,\
	fd_file_name		 VARCHAR(99)   NOT NULL,\
	fd_description		 VARCHAR(64)    NULL,\
	fd_snap_strat			 INTEGER         NULL,\
	fd_snap_mid			 INTEGER         NULL,\
	fd_snap_end			 INTEGER         NULL,\
	fd_record_reserve1	 VARCHAR(256)   NULL,\
	fd_record_reserve2	 VARCHAR(256)   NULL,\
	fd_record_reserve3	 VARCHAR(256)   NULL,\
	UNIQUE(fd_file_name)\
);"

#define VIDEODEVMULTILINK "CREATE TABLE t_video_devmultilink(\
	fd_nearcamdevice_id		 INTEGER        NOT NULL,\
	fd_nearmondevice_id		 INTEGER        NOT NULL,\
	fd_nearmonchannel_index	 VARCHAR(256)   NOT NULL\
);"

#define VIDEOCFGVERSION "CREATE TABLE t_version(version INT default 0,desc VARCHAR(256));"
#define INSERTDEFAULTVERSION "insert into t_version values(0,'db config version');"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
CVideoDBLibrary::CVideoDBLibrary()
{
	m_pdbCfg = NULL;
	m_pdbInfo = NULL;
    CVLog.SetLogFileName("VideoDBLibrary");
}

CVideoDBLibrary::~CVideoDBLibrary()
{

}

bool CVideoDBLibrary::CreateCfgTable(const char *szSQL, const char *szTable)
{
	if(m_pdbCfg->tableExists(szTable))
		return true;
	try
	{
		m_pdbCfg->execDML(szSQL);
	}
	catch (CppSQLite3Exception &e) 
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return false;
	}
	
	return true;
}

bool CVideoDBLibrary::CreateInfoTable(const char *szSQL, const char *szTable)
{
	if(m_pdbInfo->tableExists(szTable))
		return true;
	try
	{
		m_pdbInfo->execDML(szSQL);
	}
	catch (CppSQLite3Exception &e) 
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return false;
	}
	
	return true;
}

bool CVideoDBLibrary::CreateDBCfg()
{
	//创建数据库表
	if(!CreateCfgTable(VIDEOPRODUCT, "t_video_product"))
		return false;
	
	if(!CreateCfgTable(VIDEOCAMERA, "t_video_camera"))
		return false;

	if(!CreateCfgTable(VIDEOCAMERALINK, "t_video_camera_link"))
		return false;
	
	if(!CreateCfgTable(VIDEOCHANNEL, "t_video_channel"))
		return false;

	if(!CreateCfgTable(VIDEODEVICE, "t_video_device"))
		return false;

	if(!m_pdbCfg->tableExists("t_video_device_type"))
	{
		try
		{
			m_pdbCfg->execDML(VIDEODEVICETYPE);
			m_pdbCfg->execDML(m_strInsertMatrixType.c_str());
			m_pdbCfg->execDML(m_strInsertDvrType.c_str());
			m_pdbCfg->execDML(m_strInsertEncoderType.c_str());
			m_pdbCfg->execDML(m_strInsertDecoderType.c_str());
			m_pdbCfg->execDML(m_strInsertCameraType.c_str());
			m_pdbCfg->execDML(m_strInsertMonitorType.c_str());
		}
		catch (CppSQLite3Exception &e)
		{
			CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
			return false;
		}
	}

	if(!CreateCfgTable(VIDEOMONITOR, "t_video_monitor"))
		return false;
	
	if(!CreateCfgTable(VIDEOSERVER, "t_video_server"))
		return false;

	if(!CreateCfgTable(VIDEOSNAPTYPE, "t_video_snap_type"))
		return false;

	if(!m_pdbCfg->tableExists("t_video_zone"))
	{
		try
		{
			m_pdbCfg->execDML(VIDEOZONE);
			std::stringstream ss;
			ss << "INSERT INTO t_video_zone (fd_zone_id,fd_zone_name,fd_description,fd_auth_label) \
				          VALUES (1,'" << m_strVideoDefaultZoneName << "',null,'');" 
						  << std::ends;
			m_pdbCfg->execDML(ss.str().c_str());
		}
		catch (CppSQLite3Exception &e) 
		{
			CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
			return false;
		}
	}

	//版本
	if(!CreateCfgTable(VIDEOCFGVERSION, "t_version"))
		return false;

	if(m_pdbCfg->tableExists("t_version"))
	{
        char szSQL[VIDEO_SQL_MAX_SIZE];
        memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);

        try
        { 
            sprintf(szSQL,"SELECT version FROM t_version");
            CppSQLite3Table Table = m_pdbCfg->getTable(szSQL);
            if(Table.numRows() <= 0)
            {
			    m_pdbCfg->execDML(INSERTDEFAULTVERSION);
            }
		}
		catch (CppSQLite3Exception &e)
		{
			CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
			return false;
		}
	}

	return true;
}

bool CVideoDBLibrary::CreateDBInfo()
{
	//创建数据库表
	if(!CreateInfoTable(VIDEOMODE, "t_video_mode"))
		return false;
	
	if(!CreateInfoTable(VIDEOPSP, "t_video_psp"))
		return false;

	if(!CreateInfoTable(VIDEOSNAP, "t_video_snap"))
		return false;

	if(!CreateInfoTable(VIDEOCUSTOMZONE, "t_video_customzone"))
		return false;

	if(!CreateInfoTable(VIDEOCAMERAINDEX, "t_video_cameraindex"))
		return false;

	if(!CreateInfoTable(VIDEORECORD, "t_video_record"))
		return false;

	if(!CreateInfoTable(VIDEODEVMULTILINK, "t_video_devmultilink"))
		return false;

	//版本
	if(!CreateInfoTable(VIDEOCFGVERSION, "t_version"))
		return false;

	if(m_pdbInfo->tableExists("t_version"))
	{
        char szSQL[VIDEO_SQL_MAX_SIZE];
        memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);

        try
        { 
            sprintf(szSQL,"SELECT version FROM t_version");
            CppSQLite3Table Table = m_pdbInfo->getTable(szSQL);
            if(Table.numRows() <= 0)
            {
			    m_pdbInfo->execDML(INSERTDEFAULTVERSION);
            }
		}
		catch (CppSQLite3Exception &e)
		{
			CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
			return false;
		}
	}

	return true;
}

/**
 *  $(获取数据库文件版本号).
 *
 *	@return $(long): ICV_SUCCESS成功，其他失败.
 *
 *  @version  2012-05-15 huangming Initial Version.
 */
long CVideoDBLibrary::GetCfgDBVersion(int& nCfgVersion)
{
    int nVersion = 0;
    char szSQL[VIDEO_SQL_MAX_SIZE];
    memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
    sprintf(szSQL, "SELECT version FROM t_version LIMIT 1;");
    
    try
    {
        CppSQLite3Query Query = m_pdbCfg->execQuery(szSQL);
        //while(!Query.eof())
        //{
            nVersion = Query.getIntField("version");
            //Query.nextRow();
        //}
    }
    catch(CppSQLite3Exception &e)
    {
        CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
        return e.errorCode();
    }

    nCfgVersion = nVersion;
    return ICV_SUCCESS;
}

/**
 *  $(更新包含“网络视频服务器”（NVR）设备类型的数据库文件版本).
 *
 *	@return $(long): ICV_SUCCESS成功，其他失败.
 *
 *  @version  2012-05-15 huangming Initial Version.
 */
long CVideoDBLibrary::UpdateCfgDBVersion(int nCfgVersion)
{
    char szSQL[VIDEO_SQL_MAX_SIZE];
    memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
    sprintf(szSQL, "UPDATE t_version SET version=%d WHERE version= (SELECT version FROM t_version LIMIT 1);", nCfgVersion);

    try
    {
        m_pdbCfg->execDML(szSQL);
    }
    catch(CppSQLite3Exception &e)
    {
        CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
        return e.errorCode();
    }

    return ICV_SUCCESS;
}

//默认升级：加入默认支持的设备类型信息
long CVideoDBLibrary::UpdateCfgDB()
{
    char szSQL[VIDEO_SQL_MAX_SIZE];

    /***first, update the error name***/
    try
    {
        memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
        sprintf(szSQL, "UPDATE t_video_product SET fd_product_name='%s' WHERE fd_product_name='%s';", m_strHaikangWeishi1.c_str(), m_strHaikangWeishi2.c_str());
        m_pdbCfg->execDML(szSQL);
    }
    catch (CppSQLite3Exception &e) 
    {
        CVLog.LogMessage(LOG_LEVEL_INFO, e.errorMessage());
        //return e.errorCode();
    }
    catch(...)
    {
        //return -1; // unknown error
    }

    try
    {
        memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
        sprintf(szSQL, "UPDATE t_video_device SET fd_device_name='%s' WHERE fd_device_name='%s';", m_strHaikangRecorder1.c_str(), m_strHaikangRecorder2.c_str());
        m_pdbCfg->execDML(szSQL);
    }
    catch (CppSQLite3Exception &e) 
    {
        CVLog.LogMessage(LOG_LEVEL_INFO, e.errorMessage());
        //return e.errorCode();
    }
    catch(...)
    {
        //return -1; // unknown error
    }

    /***second, try to add default products that we have***/
    try
    {
        memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
        sprintf(szSQL, "INSERT INTO t_video_product \
                       (fd_product_id,fd_product_name,fd_dll_name,fd_device_type_id) VALUES (null,'%s','%s',%d)",
                       m_strADMatrix.c_str(), "ADDriver", 1);
        m_pdbCfg->execDML(szSQL);
    }
    catch (CppSQLite3Exception &e) 
    {
        CVLog.LogMessage(LOG_LEVEL_INFO, e.errorMessage());
        //return e.errorCode();
    }
    catch(...)
    {
        //return -1; // unknown error
    }

    try
    {
        memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
        sprintf(szSQL, "INSERT INTO t_video_product \
                       (fd_product_id,fd_product_name,fd_dll_name,fd_device_type_id) VALUES (null,'%s','%s',%d)",
                       m_strHaikangrRcorder3.c_str(), "DVRHK3", 2);
        m_pdbCfg->execDML(szSQL);
    }
    catch (CppSQLite3Exception &e) 
    {
        CVLog.LogMessage(LOG_LEVEL_INFO, e.errorMessage());
        //return e.errorCode();
    }
    catch(...)
    {
        //return -1; // unknown error
    }

    try
    {
        memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
        sprintf(szSQL, "INSERT INTO t_video_product \
                       (fd_product_id,fd_product_name,fd_dll_name,fd_device_type_id) VALUES (null,'%s','%s',%d)",
                       m_strHuasanRecorder.c_str(), "DVRH3C", 2);
        m_pdbCfg->execDML(szSQL);
    }
    catch (CppSQLite3Exception &e) 
    {
        CVLog.LogMessage(LOG_LEVEL_INFO, e.errorMessage());
        //return e.errorCode();
    }
    catch(...)
    {
        //return -1; // unknown error
    }

    try
    {
        memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
        sprintf(szSQL, "INSERT INTO t_video_product \
                       (fd_product_id,fd_product_name,fd_dll_name,fd_device_type_id) VALUES (null,'%s','%s',%d)",
                       m_strDahuaRecorder.c_str(), "DVRDH", 2);
        m_pdbCfg->execDML(szSQL);
    }
    catch (CppSQLite3Exception &e) 
    {
        CVLog.LogMessage(LOG_LEVEL_INFO, e.errorMessage());
        //return e.errorCode();
    }
    catch(...)
    {
        //return -1; // unknown error
    }

    return ICV_SUCCESS;
}

/**
 *  $(添加“网络视频服务器”（NVR）设备类型).
 *
 *	@return $(long): ICV_SUCCESS成功，false失败.
 *
 *  @version  2012-05-15 huangming Initial Version.
 */
long CVideoDBLibrary::AddNVRDeviceType()
{
    if(m_pdbCfg->tableExists("t_video_device_type"))
    {
        try
        {
            m_pdbCfg->execDML(m_strInsertNvrType.c_str());
        }
        catch (CppSQLite3Exception &e)
        {
            CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
            return e.errorCode();
        }
    }
    else
    {
        CVLog.LogMessage(LOG_LEVEL_ERROR, "AddNVRDeviceType: t_video_device_type is not existed.");
        return EC_ICV_CCTV_FAILTOEXEDB;
    }

    return ICV_SUCCESS;
}

long CVideoDBLibrary::VideoAddNVRDeviceType()
{
    VIDEO_TRANSACTION_PROC(AddNVRDeviceType())
}

/**
 *  $(视频服务是否存在).
 *
 *  @param  -[in] const long nServerID: 服务ID.
 *	@return $(bool): true成功，false失败.
 *
 *  @version  05/21/2008  xulizai  Initial Version.
 */
bool CVideoDBLibrary::IsServerExist(const long nServerID)
{
	bool bRet = false;

	std::stringstream ss;
	ss << "SELECT fd_server_id FROM t_video_server WHERE fd_server_id="
		<< nServerID << std::ends;
	std::string strSQL = ss.str();
	const char * szSQL = strSQL.c_str();
	try
	{
		CppSQLite3Table Table = m_pdbCfg->getTable(szSQL);
		if(Table.numRows() > 0)
		{
			bRet = true;
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		bRet = false;
	}
	return bRet;
}

/**
 *  $(分区是否存在).
 *
 *  @param  -[in] const long nZoneID: 分区ID.
 *	@return $(bool): true成功，false失败.
 *
 *  @version  05/21/2008  xulizai  Initial Version.
 *  @version  29/06/2011  wuhaochi convert nZoneID in sprintf to int.
 */
bool CVideoDBLibrary::IsZoneExist(const long nZoneID)
{
	bool bRet = false;
	if(nZoneID == VIDEO_ID_INVALIDATION)
	{
		return false;
	}
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL,"SELECT fd_zone_id FROM t_video_zone WHERE fd_zone_id=%d", (int)nZoneID);
	try
	{
		CppSQLite3Table Table = m_pdbCfg->getTable(szSQL);
		if(Table.numRows() > 0)
		{
			bRet = true;
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		bRet = false;
	}
	return bRet;
}

/**
 *  $(设备类型是否存在).
 *
 *  @param  -[in] const long nDeviceTypeID: 设备类型ID.
 *	@return $(bool): true成功，false失败.
 *
 *  @version  05/21/2008  xulizai  Initial Version.
 */
bool CVideoDBLibrary::IsDeviceTypeExist(const long nDeviceTypeID)
{
	bool bRet = false;
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL,"SELECT * FROM t_video_device_type WHERE fd_device_type_id=%d", (int)nDeviceTypeID);
	try
	{
		CppSQLite3Table Table = m_pdbCfg->getTable(szSQL);
		if(Table.numRows() > 0)
		{
			bRet = true;
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		bRet = false;
	}
	return bRet;
}

/**
 *  $(产品型号是否存在).
 *
 *  @param  -[in] const long nProductID: 产品型号ID.
 *	@return $(bool): true成功，false失败.
 *
 *  @version  05/21/2008  xulizai  Initial Version.
 */
bool CVideoDBLibrary::IsProductExist(const long nProductID)
{
	bool bRet = false;
	if(nProductID == VIDEO_ID_INVALIDATION)
	{
		return false;
	}
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL,"SELECT fd_product_id FROM t_video_product WHERE fd_product_id=%d", (int)nProductID);
	try
	{
		CppSQLite3Table Table = m_pdbCfg->getTable(szSQL);
		if(Table.numRows() > 0)
		{
			bRet = true;
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		bRet = false;
	}
	return bRet;
}

/**
 *  $(设备是否存在).
 *
 *  @param  -[in] const long nDeviceID: 设备ID.
 *	@return $(bool): true成功，false失败.
 *
 *  @version  05/21/2008  xulizai  Initial Version.
 */
bool CVideoDBLibrary::IsDeviceExist(const long nDeviceID)
{
	bool bRet = false;
	if(nDeviceID == VIDEO_ID_INVALIDATION)
	{
		return false;
	}
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL,"SELECT fd_device_id FROM t_video_device WHERE fd_device_id=%d", (int)nDeviceID);
	try
	{
		CppSQLite3Table Table = m_pdbCfg->getTable(szSQL);
		if(Table.numRows() > 0)
		{
			bRet = true;
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		bRet = false;
	}
	return bRet;
}

/**
 *  $(预置位是否存在).
 *
 *  @param  -[in] const long nCamID: 摄像头ID.
 *  @param  -[in] const long nCamID: 摄像头ID.
 *	@return $(bool): true成功，false失败.
 *
 *  @version  05/21/2008  xulizai  Initial Version.
 *  @version  29/06/2011  wuhaochi add pszPSPName null case, and convert nCamID to int.
 */
bool CVideoDBLibrary::IsPspExist(const long nCamID, const char* pszPSPName)
{
	bool bRet = false;
	if(nCamID == VIDEO_ID_INVALIDATION || pszPSPName == NULL || pszPSPName[0] == '\0')
	{
		return false;
	}
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL,"SELECT * FROM t_video_psp WHERE fd_camera_id=%d AND fd_psp_name='%s'", (int)nCamID, pszPSPName);
	try
	{
		CppSQLite3Table Table = m_pdbInfo->getTable(szSQL);
		if(Table.numRows() > 0)
		{
			bRet = true;
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		bRet = false;
	}
	return bRet;
}

/**
 *  $(获取通道ID，若不存在，则添加一个通道).
 *
 *  @param  -[in] const long nDeviceID: 设备ID.
 *  @param  -[in] const long nIndex: 设备通道ID.
 *	@return $(long) 执行结果.
 *
 *  @version  05/21/2008  xulizai  Initial Version.
 *  @version  06/29/2011  wuhaochi type conversion in sprintf
 */
long CVideoDBLibrary::GetChannelID(const long nDeviceID, const char* szChannel)
{
	long nChannelID = VIDEO_ID_INVALIDATION;
	// 连接通道表中查找通道ID
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL, "SELECT fd_channel_id FROM t_video_channel WHERE fd_device_id=%d AND fd_channel_index='%s'", (int)nDeviceID, (const char*)szChannel);
	try
	{
		CppSQLite3Query Query = m_pdbCfg->execQuery(szSQL);
		while(!Query.eof())
		{
			nChannelID = Query.getIntField("fd_channel_id");
            Query.nextRow();
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return VIDEO_ID_INVALIDATION;
	}
	
	if(nChannelID == VIDEO_ID_INVALIDATION)
	{
		// 插入一条记录
		if(AddChannel(nDeviceID, szChannel) == ICV_SUCCESS)
		{
			nChannelID = (long)m_pdbCfg->lastRowId();
		}
	}
	return nChannelID;
}

/**
 *  $(添加摄像头的连接设备).
 *
 *  @param  -[in] const long nChannelID: 通道ID.
 *  @param  -[in] const long nCameraID: 摄像头ID.
 *  @param  -[in] const bool bCtrl: 是否控制设备.
 *  @param  -[in] const bool bSrc: 是否信号源设备.
 *  @param  -[in] const bool bHis: 是否历史管理设备.
 *	@return $(long) 执行结果.
 *
 *  @version  05/21/2008  xulizai  Initial Version.
 */
long CVideoDBLibrary::AddCameraLinkDevice(const long nChannelID, const long nCameraID, const bool bCtrl, const bool bSrc, const bool bHis)
{
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);

	sprintf(szSQL, "INSERT INTO t_video_camera_link \
			(fd_channel_id,fd_camera_id,fd_device_ctrl,fd_device_source,fd_device_history) VALUES (%d,%d,%d,%d,%d)", 
			(int)nChannelID, (int)nCameraID, (int)bCtrl, (int)bSrc, (int)bHis);
	try
	{
		m_pdbCfg->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}
	return ICV_SUCCESS;
}

/**
 *  $(添加通道).
 *
 *  @param  -[in] const long nDeviceID: 设备ID.
 *  @param  -[in] const long nChannelIndex: 通道的序号.
 *	@return $(long) 执行结果.
 *
 *  @version  05/21/2008  xulizai  Initial Version.
 */
long CVideoDBLibrary::AddChannel(const long nDeviceID, const char* szChannel)
{
	// 插入一条记录
	if(!IsDeviceExist(nDeviceID))
	{
		return EC_ICV_CCTV_DEVNOTEXIST;
	}
	
	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "INSERT INTO t_video_channel \
					(fd_channel_id,fd_channel_index,fd_device_id,fd_linkchannel_id) VALUES (null,'%s',%d,null)",
					(const char*)szChannel, (int)nDeviceID);
		m_pdbCfg->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}
	return ICV_SUCCESS;
}

/**
 *  $(设置通道表中的连接设备通道ID).
 *
 *  @param  -[in] const long nChannelID: 通道ID.
 *  @param  -[in] const long nLinkChannelID: 连接通道的ID.
 *	@return $(long) 执行结果.
 *
 *  @version  05/21/2008  xulizai  Initial Version.
 */
long CVideoDBLibrary::SetLinkChannel(const long nChannelID, const long nLinkChannelID)
{
	if(nChannelID == VIDEO_ID_INVALIDATION || nLinkChannelID == VIDEO_ID_INVALIDATION
		|| nChannelID == nLinkChannelID)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL, "UPDATE t_video_channel SET fd_linkchannel_id=%d WHERE fd_channel_id=%d", (int)nLinkChannelID, (int)nChannelID);
	try
	{
		m_pdbCfg->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}
	return ICV_SUCCESS;
}

/**
 *  $(通过摄像头ID删除摄像头的连接设备).
 *
 *  @param  -[in] const long nCameraID: 摄像头ID.
 *	@return $(long) 执行结果.
 *
 *  @version  05/21/2008  xulizai  Initial Version.
 */
long CVideoDBLibrary::DeleteCameraLinkDeviceByCameraID(const long nCameraID)
{
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL, "DELETE FROM t_video_camera_link WHERE fd_camera_id=%d", (int)nCameraID);
	try
	{
		m_pdbCfg->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}
	return ICV_SUCCESS;
}

/**
 *  $(通过通道ID删除摄像头的连接设备信息).
 *
 *  @param  -[in] const long nChannelID: 通道ID.
 *	@return $(long) 执行结果.
 *
 *  @version  05/21/2008  xulizai  Initial Version.
 */
long CVideoDBLibrary::DeleteCameraLinkDeviceByChannelID(const long nChannelID)
{
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL, "DELETE FROM t_video_camera_link WHERE fd_channel_id=%d", (int)nChannelID);
	try
	{
		m_pdbCfg->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}
	return ICV_SUCCESS;
}

/**
 *  $(通过通道ID删除监视器的连接设备信息).
 *
 *  @param  -[in] const long nChannelID: 通道ID.
 *	@return $(long) 执行结果.
 *
 *  @version  05/21/2008  xulizai  Initial Version.
 */
long CVideoDBLibrary::SetMonitorLinkDeviceNullByChannelID(const long nChannelID)
{
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL, "UPDATE t_video_monitor SET fd_channel_id=null WHERE fd_channel_id=%d", (int)nChannelID);
	try
	{
		m_pdbCfg->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();	
	}
	return ICV_SUCCESS;	
}

/**
 *  $(通过通道ID删除设备的连接设备信息).
 *
 *  @param  -[in] const long nChannelID: 通道ID.
 *	@return $(long) 执行结果.
 *
 *  @version  05/21/2008  xulizai  Initial Version.
 */
long CVideoDBLibrary::SetDeviceLinkChannelNullByLinkChannelID(const long nChannelID)
{
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL, "UPDATE t_video_channel SET fd_linkchannel_id=null WHERE fd_linkchannel_id=%d", (int)nChannelID);
	try
	{
		m_pdbCfg->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}
	return ICV_SUCCESS;
}

/**
 *  $(通过设备ID删除设备的连接设备信息).
 *
 *  @param  -[in] const long nDeviceID: 设备ID.
 *	@return $(long) 执行结果.
 *
 *  @version  05/21/2008  xulizai  Initial Version.
 */
long CVideoDBLibrary::SetDeviceLinkChannelNullByDeviceID(const long nDeviceID)
{
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL, "UPDATE t_video_channel SET fd_linkchannel_id=null WHERE fd_device_id=%d", (int)nDeviceID);
	try
	{
		m_pdbCfg->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}
	return ICV_SUCCESS;
}

/**
 *  $(通过通道ID删除该通道).
 *
 *  @param  -[in] const long nChannelID: 通道ID.
 *	@return $(long) 执行结果.
 *
 *  @version  05/21/2008  xulizai  Initial Version.
 */
long CVideoDBLibrary::DeleteChannelByChannelID(const long nChannelID)
{
	// 删除需要以下步骤
	long nRet = DeleteCameraLinkDeviceByChannelID(nChannelID);
	if(nRet != ICV_SUCCESS)
		return nRet;
	nRet = SetMonitorLinkDeviceNullByChannelID(nChannelID);
	if(nRet != ICV_SUCCESS)
		return nRet;
	nRet = SetDeviceLinkChannelNullByLinkChannelID(nChannelID);
	if(nRet != ICV_SUCCESS)
		return nRet;

	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL, "DELETE FROM t_video_channel WHERE fd_channel_id=%d", (int)nChannelID);
	try
	{
		m_pdbCfg->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}
	return ICV_SUCCESS;
}

/**
 *  $(通过设备ID删除相关的通道).
 *
 *  @param  -[in] const long nDeviceID: 设备ID.
 *	@return $(long) 执行结果.
 *
 *  @version  05/21/2008  xulizai  Initial Version.
 */
long CVideoDBLibrary::DeleteChannelByDeviceID(const long nDeviceID)
{
	// 只能逐个删除
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL, "SELECT fd_channel_id FROM t_video_channel WHERE fd_device_id=%d", (int)nDeviceID);
	try
	{
		CppSQLite3Query Query = m_pdbCfg->execQuery(szSQL);
		while(!Query.eof())
		{
			long nChannelID = VIDEO_ID_INVALIDATION;
			nChannelID = Query.getIntField("fd_channel_id");
			long nRet = DeleteChannelByChannelID(nChannelID);
			if(nRet != ICV_SUCCESS)
			{	
				CVLog.LogMessage(LOG_LEVEL_ERROR, "DeleteChannelByDeviceID return error code: %d", nRet);
				return nRet;
			}

            Query.nextRow();
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}
	return ICV_SUCCESS;
}

/**
 *  $(通过分区ID删除摄像头的分区信息).
 *
 *  @param  -[in] const long nZoneID: 分区ID.
 *	@return $(long) 执行结果.
 *
 *  @version  05/21/2008  xulizai  Initial Version.
 */
long CVideoDBLibrary::SetCameraZoneDefaultByZoneID(const long nZoneID)
{
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL, "UPDATE t_video_camera SET fd_zone_id=%d WHERE fd_zone_id=%d", VIDEO_DEFAULT_ZONE_ID, (int)nZoneID);
	try
	{
		m_pdbCfg->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}
	return ICV_SUCCESS;
}

/**
 *  $(通过分区ID删除监视器的分区信息).
 *
 *  @param  -[in] const long nZoneID: 分区ID.
 *	@return $(long) 执行结果.
 *
 *  @version  05/21/2008  xulizai  Initial Version.
 */
long CVideoDBLibrary::SetMonitorZoneDefaultByZoneID(const long nZoneID)
{
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL, "UPDATE t_video_monitor SET fd_zone_id=%d WHERE fd_zone_id=%d", VIDEO_DEFAULT_ZONE_ID, (int)nZoneID);
	try
	{
		m_pdbCfg->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}
	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoInitString(const vector<string>& strVector)
{
	//strVector的size固定，由前台videoconfig传入，大小为16
	if(strVector.size() != 16)
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	m_strVideoDefaultZoneName = strVector[0];

	m_strInsertMatrixType = strVector[1];
	m_strInsertDvrType = strVector[2];
	m_strInsertEncoderType = strVector[3];
	m_strInsertDecoderType = strVector[4];
	m_strInsertCameraType = strVector[5];
	m_strInsertMonitorType = strVector[6];
	m_strInsertNvrType = strVector[7];

	m_strHaikangWeishi1 = strVector[8];
	m_strHaikangWeishi2 = strVector[9];
	m_strHaikangRecorder1 = strVector[10];
	m_strHaikangRecorder2 = strVector[11];
	m_strADMatrix = strVector[12];
	m_strHaikangrRcorder3 = strVector[13];
	m_strHuasanRecorder = strVector[14];
	m_strDahuaRecorder = strVector[15];


	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoInitCfgDB(const char* pszDBFileName)
{
	if(pszDBFileName == NULL)
	{
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	}
	if(m_pdbCfg == NULL)
	{
		m_pdbCfg = new CppSQLite3DB;
	}
	if(m_pdbCfg == NULL)
	{
		return EC_ICV_CCTV_FAILTONEWMEM;
	}

	try
	{
		m_pdbCfg->open(pszDBFileName);
		if(!CreateDBCfg())
			return EC_ICV_CCTV_FAILTOCREATEDB;

        UpdateCfgDB();
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoInitInfoDB(const char* pszDBFileName)
{
	if(pszDBFileName == NULL)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	if(m_pdbInfo == NULL)
	{
		m_pdbInfo = new CppSQLite3DB;
	}
	if(m_pdbInfo == NULL)
	{
		return EC_ICV_CCTV_FAILTONEWMEM;
	}

	try
	{
		m_pdbInfo->open(pszDBFileName);
		if(!CreateDBInfo())
			return EC_ICV_CCTV_FAILTOCREATEDB;
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoExitCfgDB()
{
	if(m_pdbCfg == NULL)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		m_pdbCfg->close();
		SAFE_DELETE(m_pdbCfg);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoExitInfoDB()
{
	if(m_pdbInfo == NULL)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		m_pdbInfo->close();
		SAFE_DELETE(m_pdbInfo);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoReadCameraList(CVideoCameraList* plistCamera)
{
	if((m_pdbCfg == NULL) || (plistCamera == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	plistCamera->clear();
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL, "SELECT * FROM t_video_camera ORDER BY fd_camera_id");
	try
	{
		CppSQLite3Query QueryCam = m_pdbCfg->execQuery(szSQL);
		// 依次读取所有的摄像头信息
		while(!QueryCam.eof())
		{
			CVideoCamera theCam;
			theCam.SetID(QueryCam.getIntField("fd_camera_id"));
			theCam.SetName(QueryCam.getStringField("fd_camera_name"));
			theCam.SetDescription(QueryCam.getStringField("fd_description"));
			theCam.SetIsRemoteFileCtrl(QueryCam.getIntField("fd_remote_file") == VIDEO_CTRL_STATUS_ENABLE);
			theCam.SetIsLocalFileCtrl(QueryCam.getIntField("fd_local_file") == VIDEO_CTRL_STATUS_ENABLE);
			theCam.SetIsRemoteTimeCtrl(QueryCam.getIntField("fd_remote_time") == VIDEO_CTRL_STATUS_ENABLE);
			theCam.SetIsOrientCtrl(QueryCam.getIntField("fd_orient_ctrl") == VIDEO_CTRL_STATUS_ENABLE);
			theCam.SetIsLensCtrl(QueryCam.getIntField("fd_lens_ctrl") == VIDEO_CTRL_STATUS_ENABLE);
			theCam.SetIsPSPCtrl(QueryCam.getIntField("fd_psp_ctrl") == VIDEO_CTRL_STATUS_ENABLE);
			theCam.SetIsQualityCtrl(QueryCam.getIntField("fd_quality_ctrl") == VIDEO_CTRL_STATUS_ENABLE);
			theCam.SetIsSnapCtrl(QueryCam.getIntField("fd_snap_ctrl") == VIDEO_CTRL_STATUS_ENABLE);
			theCam.SetIsHeaterCtrl(QueryCam.getIntField("fd_heater_ctrl") == VIDEO_CTRL_STATUS_ENABLE);
			theCam.SetIsRainBrushCtrl(QueryCam.getIntField("fd_rainbrush_ctrl") == VIDEO_CTRL_STATUS_ENABLE);
			theCam.SetSnapWidth(QueryCam.getIntField("fd_snap_width"));
			theCam.SetSnapHeight(QueryCam.getIntField("fd_snap_height"));
			theCam.SetSnapBitDeep(QueryCam.getIntField("fd_snap_bitdeep"));
			theCam.SetPSPMaxCount(QueryCam.getIntField("fd_psp_maxcount"));
			theCam.SetServerID(QueryCam.getIntField("fd_server_id"));
			theCam.SetZoneID(QueryCam.getIntField("fd_zone_id"));
			// 查找连接设备
			memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
			sprintf(szSQL, "SELECT A.fd_device_ctrl,A.fd_device_source,A.fd_device_history,B.fd_channel_index,\
							B.fd_device_id FROM t_video_camera_link A,t_video_channel B \
							WHERE A.fd_camera_id=%d AND A.fd_channel_id=B.fd_channel_id", theCam.GetID());
			CppSQLite3Query QueryLink = m_pdbCfg->execQuery(szSQL);
			// 依次读取某一摄像头的所有连接设备信息
			while(!QueryLink.eof())
			{
				CVideoLinkDevice theLink;
				theLink.SetIsCtrlDevice(QueryLink.getIntField(0) == VIDEO_CTRL_STATUS_ENABLE);
				theLink.SetIsVideoSourceDevice(QueryLink.getIntField(1) == VIDEO_CTRL_STATUS_ENABLE);
				theLink.SetIsHistoryDevice(QueryLink.getIntField(2) == VIDEO_CTRL_STATUS_ENABLE);
				theLink.SetChannel(QueryLink.getStringField(3));
				theLink.SetDeviceID(QueryLink.getIntField(4));
				theCam.GetLinkDev()->InsertObject(theLink);
				QueryLink.nextRow();
			}// end while

			plistCamera->InsertObject(theCam);
			QueryCam.nextRow();
		}// end while
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::AddCamera(CVideoCamera* pCamera)
{
	long nRet = EC_ICV_CCTV_FAILTOEXEDB;

	if((m_pdbCfg == NULL) || (pCamera == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	if(!IsServerExist(pCamera->GetServerID()))
		return EC_ICV_CCTV_SERVERNOTEXIST;
	if(!IsZoneExist(pCamera->GetZoneID()))
		return EC_ICV_CCTV_ZONENOTEXIST;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		// 插入摄像头
		sprintf(szSQL, "INSERT INTO t_video_camera \
				 (fd_camera_id,fd_camera_name,fd_description,fd_remote_file,fd_local_file,\
				 fd_remote_time,fd_orient_ctrl,fd_lens_ctrl,fd_psp_ctrl,fd_quality_ctrl,\
				 fd_snap_ctrl,fd_heater_ctrl,fd_rainbrush_ctrl,fd_snap_width,fd_snap_height,\
				 fd_snap_bitdeep,fd_psp_maxcount,fd_server_id,fd_zone_id) VALUES (null,'%s','%s',%d,\
				 %d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d)", pCamera->GetName(),
				 pCamera->GetDescription(), pCamera->GetIsRemoteFileCtrl(), pCamera->GetIsLocalFileCtrl(),
				 pCamera->GetIsRemoteTimeCtrl(), pCamera->GetIsOrientCtrl(), pCamera->GetIsLensCtrl(),
				 pCamera->GetIsPSPCtrl(), pCamera->GetIsQualityCtrl(), pCamera->GetIsSnapCtrl(),
				 pCamera->GetIsHeaterCtrl(), pCamera->GetIsRainBrushCtrl(), pCamera->GetSnapWidth(),pCamera->GetSnapHeight(),
				 pCamera->GetSnapBitDeep(), pCamera->GetPSPMaxCount(), pCamera->GetServerID(), pCamera->GetZoneID());
		
		m_pdbCfg->execDML(szSQL);

		pCamera->SetID((long)m_pdbCfg->lastRowId());
		CVideoLinkDeviceList* plistLinkDev = pCamera->GetLinkDev();
		// 插入连接信息
		for(int i=0; i<plistLinkDev->GetSize(); i++)
		{
			// 获取摄像头连接设备的通道ID
			long nChannelID = VIDEO_ID_INVALIDATION;
			nChannelID = GetChannelID(plistLinkDev->GetAt(i)->GetDeviceID(), plistLinkDev->GetAt(i)->GetChannel());
			if(nChannelID != VIDEO_ID_INVALIDATION)
			{
				nRet = AddCameraLinkDevice(nChannelID, pCamera->GetID(), plistLinkDev->GetAt(i)->GetIsCtrlDevice(),
					plistLinkDev->GetAt(i)->GetIsVideoSourceDevice(), plistLinkDev->GetAt(i)->GetIsHistoryDevice());
				if(nRet != ICV_SUCCESS)
				{
					CVLog.LogMessage(LOG_LEVEL_ERROR, "AddCameraLinkDevice, return error code:%d", nRet);
					return nRet;
				}
			}
			else
				CVLog.LogMessage(LOG_LEVEL_ERROR, "VideoAddCamera catch exception, invalid channel ID:%d", nChannelID);
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
        CVLog.LogMessage(LOG_LEVEL_ERROR, "Add camera <%s> catches exception, return error code <%d>.", pCamera->GetName(), e.errorCode());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::UpdateCamera(CVideoCamera* pNewCamera, const long nOldCameraID, bool bLinkChange)
{
	long nRet = EC_ICV_CCTV_FAILTOEXEDB;

	if((m_pdbCfg == NULL) || (pNewCamera == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	if(!IsServerExist(pNewCamera->GetServerID()))
		return EC_ICV_CCTV_SERVERNOTEXIST;
	if(!IsZoneExist(pNewCamera->GetZoneID()))
		return EC_ICV_CCTV_ZONENOTEXIST;
	
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL, "UPDATE t_video_camera SET fd_camera_name='%s',\
				                              fd_description='%s',\
			                                  fd_remote_file=%d,\
											  fd_local_file=%d,\
											  fd_remote_time=%d,\
											  fd_orient_ctrl=%d,\
											  fd_lens_ctrl=%d,\
											  fd_psp_ctrl=%d,\
											  fd_quality_ctrl=%d,\
											  fd_snap_ctrl=%d,\
											  fd_heater_ctrl=%d,\
											  fd_rainbrush_ctrl=%d,\
											  fd_snap_width=%d,\
											  fd_snap_height=%d,\
											  fd_snap_bitdeep=%d,\
											  fd_psp_maxcount=%d,\
											  fd_server_id=%d,\
											  fd_zone_id=%d \
										  WHERE fd_camera_id=%d", 
										        pNewCamera->GetName(),
												pNewCamera->GetDescription(), 
												pNewCamera->GetIsRemoteFileCtrl(), 
												pNewCamera->GetIsLocalFileCtrl(),
												pNewCamera->GetIsRemoteTimeCtrl(), 
												pNewCamera->GetIsOrientCtrl(), 
												pNewCamera->GetIsLensCtrl(),
												pNewCamera->GetIsPSPCtrl(), 
												pNewCamera->GetIsQualityCtrl(),
												pNewCamera->GetIsSnapCtrl(),
												pNewCamera->GetIsHeaterCtrl(), 
												pNewCamera->GetIsRainBrushCtrl(), 
												pNewCamera->GetSnapWidth(),
												pNewCamera->GetSnapHeight(),
												pNewCamera->GetSnapBitDeep(),
												pNewCamera->GetPSPMaxCount(),
												pNewCamera->GetServerID(), 
												pNewCamera->GetZoneID(), 
												nOldCameraID);

	try
	{
		m_pdbCfg->execDML(szSQL);
		if(bLinkChange)
		{
			// 删除摄像头连接设备
			nRet = DeleteCameraLinkDeviceByCameraID(nOldCameraID);
			if(nRet == ICV_SUCCESS)
			{
				// 插入连接信息
				CVideoLinkDeviceList* plistLinkDev = pNewCamera->GetLinkDev();
				for(int i=0; i<plistLinkDev->GetSize(); i++)
				{
					// 获取摄像头连接设备的通道ID
					long nChannelID = VIDEO_ID_INVALIDATION;
					nChannelID = GetChannelID(plistLinkDev->GetAt(i)->GetDeviceID(), plistLinkDev->GetAt(i)->GetChannel());
					if(nChannelID != VIDEO_ID_INVALIDATION)
					{
						nRet = AddCameraLinkDevice(nChannelID, pNewCamera->GetID(), plistLinkDev->GetAt(i)->GetIsCtrlDevice(),
							plistLinkDev->GetAt(i)->GetIsVideoSourceDevice(), plistLinkDev->GetAt(i)->GetIsHistoryDevice());
						if(nRet != ICV_SUCCESS)
						{
							CVLog.LogMessage(LOG_LEVEL_ERROR, "UpdateCamera, AddCameraLinkDevice, catches error, return error code:%d", nRet);
							return nRet;
						}
					}
					else
						CVLog.LogMessage(LOG_LEVEL_ERROR, "UpdateCamera, VideoUpdateCamera, catches error, invalid channel ID:%d", nChannelID);
				}// end for
			}
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::UpdateCameraZone(CVideoList<long>* plistCameraID, const long nZoneID)
{
	long nRet = EC_ICV_CCTV_FAILTOEXEDB;

	if((m_pdbCfg == NULL) || (plistCameraID == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	if(!IsZoneExist(nZoneID))
		return EC_ICV_CCTV_ZONENOTEXIST;
	
	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		for(int i=0; i<plistCameraID->GetSize(); i++)
		{
			memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
			sprintf(szSQL, "UPDATE t_video_camera SET fd_zone_id=%d WHERE fd_camera_id=%d", nZoneID, *plistCameraID->GetAt(i));
			
			m_pdbCfg->execDML(szSQL);
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::DeleteCamera(const long nCameraID)
{
	long nRet = EC_ICV_CCTV_FAILTOEXEDB;

	if(m_pdbCfg == NULL)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		// 删除摄像头连接设备
		nRet = DeleteCameraLinkDeviceByCameraID(nCameraID);
		if(nRet == ICV_SUCCESS)
		{
			char szSQL[VIDEO_SQL_MAX_SIZE];
			memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
			// 删除摄像头
			sprintf(szSQL, "DELETE FROM t_video_camera WHERE fd_camera_id=%d", nCameraID);
			m_pdbCfg->execDML(szSQL);
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoDeleteCameraByServerID(const long nServerID)
{
	if(m_pdbCfg == NULL)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	// 需要逐个删除，否则无法删除摄像头的连接设备
	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "SELECT fd_camera_id FROM t_video_camera	WHERE fd_server_id=%d", nServerID);
		CppSQLite3Query Query = m_pdbCfg->execQuery(szSQL);
		
		while(!Query.eof())
		{
			long nCameraID = VIDEO_ID_INVALIDATION;
			nCameraID = Query.getIntField("fd_camera_id");
			long nRet = DeleteCamera(nCameraID);
			if(nRet != ICV_SUCCESS)
			{
				CVLog.LogMessage(LOG_LEVEL_ERROR, "When deleting the information by service ID, VideoDeleteCamera, catches exception, return error code:%d", nRet);
				return nRet;
			}

            Query.nextRow();
		}	
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoReadMonitorList(CVideoMonitorList* plistMonitor)
{
	if((m_pdbCfg == NULL) || (plistMonitor == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	plistMonitor->clear();
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL, "SELECT * FROM t_video_monitor ORDER BY fd_monitor_id");
	try
	{
		CppSQLite3Query QueryMon = m_pdbCfg->execQuery(szSQL);
		long nLinkChannelID = VIDEO_ID_INVALIDATION;
		while(!QueryMon.eof())
		{
			CVideoMonitor theMon;
			theMon.SetID(QueryMon.getIntField("fd_monitor_id"));
			theMon.SetName(QueryMon.getStringField("fd_monitor_name"));
			theMon.SetDescription(QueryMon.getStringField("fd_description"));
			theMon.SetServerID(QueryMon.getIntField("fd_server_id"));
			theMon.SetZoneID(QueryMon.getIntField("fd_zone_id"));
			if(!QueryMon.fieldIsNull("fd_channel_id"))
			{
				// 查找连接设备
				memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
				sprintf(szSQL, "SELECT fd_channel_index,fd_device_id FROM t_video_channel \
								WHERE fd_channel_id=%d", QueryMon.getIntField("fd_channel_id"));
				CppSQLite3Query QueryLink = m_pdbCfg->execQuery(szSQL);
				while(!QueryLink.eof())
				{
					theMon.GetLinkDev()->SetChannel(QueryLink.getStringField("fd_channel_index"));
					theMon.GetLinkDev()->SetDeviceID(QueryLink.getIntField("fd_device_id"));
					QueryLink.nextRow();
				}

                //增加对连接设备的设备类型的读取
                memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
                sprintf(szSQL, "SELECT fd_device_type_id FROM t_video_product \
                               WHERE fd_product_id= ( \
                               SELECT fd_product_id FROM t_video_device \
                               WHERE fd_device_id=%d)", theMon.GetLinkDev()->GetDeviceID());
                CppSQLite3Query QueryLinkType = m_pdbCfg->execQuery(szSQL);
                while(!QueryLinkType.eof())
                {
                    theMon.GetLinkDev()->SetLinkDevType(QueryLinkType.getIntField("fd_device_type_id"));
                    QueryLinkType.nextRow();
                }
			}
			plistMonitor->InsertObject(theMon);
            QueryMon.nextRow();
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoAddMonitor(CVideoMonitor* pMonitor)
{
	if((m_pdbCfg == NULL) || (pMonitor == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	// 监视器的所属视频服务和分区必须存在
	if(!IsServerExist(pMonitor->GetServerID()))
		return EC_ICV_CCTV_SERVERNOTEXIST;
	if(!IsZoneExist(pMonitor->GetZoneID()))
		return EC_ICV_CCTV_ZONENOTEXIST;

	//query if this monitor exist, if yes, return error.
	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "SELECT * FROM t_video_monitor WHERE fd_monitor_name='%s'", pMonitor->GetName());
		CppSQLite3Query Query = m_pdbCfg->execQuery(szSQL);
		if ( !Query.eof() ) //Query is not empty
		{
			return -1;
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}
	// 获取监视器连接设备的通道ID
	bool bLinkDev = false;
	long nChannelID = VIDEO_ID_INVALIDATION;
	if(pMonitor->GetLinkDev()->GetDeviceID() != VIDEO_ID_INVALIDATION 
		&& 0 != strcmp( pMonitor->GetLinkDev()->GetChannel(), VIDEO_ID_STRING_INVALIDATION))
	{
		bLinkDev = true;
		nChannelID = GetChannelID(pMonitor->GetLinkDev()->GetDeviceID(), pMonitor->GetLinkDev()->GetChannel());
	}
	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		// 插入监视器
		if(bLinkDev && nChannelID != VIDEO_ID_INVALIDATION)
		{
			sprintf(szSQL, "INSERT INTO t_video_monitor \
					 (fd_monitor_id,fd_monitor_name,fd_description,fd_server_id,fd_zone_id,fd_channel_id) \
					 VALUES (null,'%s','%s',%d,%d,%d)", pMonitor->GetName(), pMonitor->GetDescription(),
					 pMonitor->GetServerID(), pMonitor->GetZoneID(), nChannelID);
		}
		else
		{
			sprintf(szSQL, "INSERT INTO t_video_monitor \
					 (fd_monitor_id,fd_monitor_name,fd_description,fd_server_id,fd_zone_id,fd_channel_id) \
					 VALUES (null,'%s','%s',%d,%d,null)", pMonitor->GetName(), pMonitor->GetDescription(),
					 pMonitor->GetServerID(), pMonitor->GetZoneID());
		}
		
		m_pdbCfg->execDML(szSQL);
		pMonitor->SetID((long)m_pdbCfg->lastRowId());
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoUpdateMonitor(CVideoMonitor* pNewMonitor, const long nOldMonitorID)
{
	if((m_pdbCfg == NULL) || (pNewMonitor == NULL) || (pNewMonitor->GetID() != nOldMonitorID))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	// 监视器的所属视频服务和分区必须存在
	if(!IsServerExist(pNewMonitor->GetServerID()))
		return EC_ICV_CCTV_SERVERNOTEXIST;
	if(!IsZoneExist(pNewMonitor->GetZoneID()))
		return EC_ICV_CCTV_ZONENOTEXIST;
	
	// 获取监视器连接设备的通道ID
	bool bLinkDev = false;
	long nChannelID = VIDEO_ID_INVALIDATION;
	if(pNewMonitor->GetLinkDev()->GetDeviceID() != VIDEO_ID_INVALIDATION 
		&& 0 != strcmp( pNewMonitor->GetLinkDev()->GetChannel(), VIDEO_ID_STRING_INVALIDATION ))
	{
		bLinkDev = true;
		nChannelID = GetChannelID(pNewMonitor->GetLinkDev()->GetDeviceID(), pNewMonitor->GetLinkDev()->GetChannel());
	}
		
	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		// 更新监视器
		if(bLinkDev && nChannelID != VIDEO_ID_INVALIDATION)
		{
			sprintf(szSQL, "UPDATE t_video_monitor SET fd_monitor_name='%s',fd_description='%s',\
					fd_server_id=%d,fd_zone_id=%d,fd_channel_id=%d WHERE fd_monitor_id=%d",
					pNewMonitor->GetName(), pNewMonitor->GetDescription(), pNewMonitor->GetServerID(), 
					pNewMonitor->GetZoneID(), nChannelID, nOldMonitorID);
		}
		else
		{
			sprintf(szSQL, "UPDATE t_video_monitor SET fd_monitor_name='%s',fd_description='%s',\
					fd_server_id=%d,fd_zone_id=%d,fd_channel_id=null WHERE fd_monitor_id=%d",
					pNewMonitor->GetName(), pNewMonitor->GetDescription(), pNewMonitor->GetServerID(), 
					pNewMonitor->GetZoneID(), nOldMonitorID);
		}

		m_pdbCfg->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::UpdateMonitorZone(CVideoList<long>* plistMonitorID, const long nZoneID)
{
	long nRet = EC_ICV_CCTV_FAILTOEXEDB;

	if((m_pdbCfg == NULL) || (plistMonitorID == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	if(!IsZoneExist(nZoneID))
		return EC_ICV_CCTV_ZONENOTEXIST;
	
	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		for(int i=0; i<plistMonitorID->GetSize(); i++)
		{
			memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
			sprintf(szSQL, "UPDATE t_video_monitor SET fd_zone_id=%d WHERE fd_monitor_id=%d", nZoneID, *plistMonitorID->GetAt(i));
			m_pdbCfg->execDML(szSQL);
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoDeleteMonitor(const long nMonitorID)
{
	if(m_pdbCfg == NULL)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "DELETE FROM t_video_monitor WHERE fd_monitor_id=%d", nMonitorID);
		m_pdbCfg->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoDeleteMonitorByServerID(const long nServerID)
{
	if(m_pdbCfg == NULL)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "DELETE FROM t_video_monitor WHERE fd_server_id=%d", nServerID);
		m_pdbCfg->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoReadServerList(CVideoServerList* plistServer)
{
	if((m_pdbCfg == NULL) || (plistServer == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	plistServer->clear();
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL, "SELECT * FROM t_video_server ORDER BY fd_server_name");

	try
	{
		CppSQLite3Query Query = m_pdbCfg->execQuery(szSQL);
		while(!Query.eof())
		{
			CVideoServer theServer;
			theServer.SetID(Query.getIntField("fd_server_id"));
			theServer.SetName(Query.getStringField("fd_server_name"));
			theServer.SetIP(Query.getStringField("fd_server_ip"));
			theServer.SetIsBak(Query.getIntField("fd_bak") == VIDEO_SERVER_HAS_BAK);
			theServer.SetBakIP(Query.getStringField("fd_bak_ip"));
			theServer.SetLockSeconds(Query.getIntField("fd_lock_seconds"));
			theServer.SetDescription(Query.getStringField("fd_description"));
			plistServer->InsertObject(theServer);
            Query.nextRow();
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoAddServer(CVideoServer* pServer)
{
	if((m_pdbCfg == NULL) || (pServer == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		// 插入视频服务
		sprintf(szSQL, "INSERT INTO t_video_server \
				 (fd_server_id,fd_server_name,fd_server_ip,fd_bak,fd_bak_ip,fd_lock_seconds,fd_description) \
				 VALUES (null,'%s','%s',%d,'%s',%d,'%s')", pServer->GetName(), pServer->GetIP(),
				 pServer->GetIsBak(), pServer->GetBakIP(), pServer->GetLockSeconds(), pServer->GetDescription());
		m_pdbCfg->execDML(szSQL);

		pServer->SetID((long)m_pdbCfg->lastRowId());
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoUpdateServer(CVideoServer* pNewServer, const long nOldServerID)	
{
	if((m_pdbCfg == NULL) || (pNewServer == NULL) || (nOldServerID != pNewServer->GetID()))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		// 更新视频服务
		sprintf(szSQL, "UPDATE t_video_server SET fd_server_name='%s',fd_server_ip='%s',\
				fd_bak=%d,fd_bak_ip='%s',fd_lock_seconds=%d,fd_description='%s' WHERE fd_server_id=%d",
				pNewServer->GetName(), pNewServer->GetIP(), pNewServer->GetIsBak(), 
				pNewServer->GetBakIP(), pNewServer->GetLockSeconds(), pNewServer->GetDescription(), nOldServerID);
		m_pdbCfg->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::DeleteServer(const long nServerID)
{
	long nRet = EC_ICV_CCTV_FAILTOEXEDB;

	if(m_pdbCfg == NULL)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		// 删除视频服务过程
		// 1.删除该服务下的所有监视器
		// 2.删除该服务下的所有摄像头
		// 3.删除该服务下的所有设备
		// 4.删除该服务
		nRet = VideoDeleteMonitorByServerID(nServerID);
		if(nRet == ICV_SUCCESS)
			nRet = VideoDeleteCameraByServerID(nServerID);
		if(nRet == ICV_SUCCESS)
			nRet = VideoDeleteDeviceByServerID(nServerID);
		if(nRet == ICV_SUCCESS)
		{
			sprintf(szSQL, "DELETE FROM t_video_server WHERE fd_server_id=%d", nServerID);
			m_pdbCfg->execDML(szSQL);
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return nRet;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoReadZoneList(CVideoZoneList* plistZone)
{
	if((m_pdbCfg == NULL) || (plistZone == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	plistZone->clear();
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL, "SELECT * FROM t_video_zone ORDER BY fd_zone_name");
	try
	{
		CppSQLite3Query Query = m_pdbCfg->execQuery(szSQL);
		while(!Query.eof())
		{
			CVideoZone theZone;
			theZone.SetID(Query.getIntField("fd_zone_id"));
			theZone.SetName(Query.getStringField("fd_zone_name"));
			theZone.SetDescription(Query.getStringField("fd_description"));
			theZone.SetAuthLabel(Query.getStringField("fd_auth_label"));
			plistZone->InsertObject(theZone);
            Query.nextRow();
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoAddZone(CVideoZone* pZone)
{
	if((m_pdbCfg == NULL) || (pZone == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	//do not allow 'add default zone'
	if ( 0 == strcmp(m_strVideoDefaultZoneName.c_str(), pZone->GetName()) )
	{
		return ICV_SUCCESS;
	}

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		// 插入分区
		sprintf(szSQL, "INSERT INTO t_video_zone (fd_zone_id,fd_zone_name,fd_description,fd_auth_label) \
				VALUES (null,'%s','%s','%s')", 
				pZone->GetName(), pZone->GetDescription(), pZone->GetAuthLabel());
		m_pdbCfg->execDML(szSQL);

		pZone->SetID((long)m_pdbCfg->lastRowId());
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoUpdateZone(CVideoZone* pNewZone, const long nOldZoneID)	
{
	if((m_pdbCfg == NULL) || (pNewZone == NULL) || (pNewZone->GetID() != nOldZoneID))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		// 更新分区
		sprintf(szSQL, "UPDATE t_video_zone SET fd_zone_name='%s',fd_description='%s',\
				fd_auth_label='%s' WHERE fd_zone_id=%d", pNewZone->GetName(),
				pNewZone->GetDescription(), pNewZone->GetAuthLabel(), nOldZoneID);
		m_pdbCfg->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::DeleteZone(const long nZoneID)
{
	long nRet = EC_ICV_CCTV_FAILTOEXEDB;

	//if((m_pdbCfg == NULL) || (nZoneID == VIDEO_DEFAULT_ZONE_ID))
	//allow import default zone
	if( NULL == m_pdbCfg )
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	if (VIDEO_DEFAULT_ZONE_ID == nZoneID)    //default zone
		return ICV_SUCCESS;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		// 删除分区过程
		// 1.修改该分区下的所有监视器
		// 2.修改该分区下的所有摄像头
		// 3.删除分区
		nRet = SetCameraZoneDefaultByZoneID(nZoneID);
		if(nRet == ICV_SUCCESS)
			nRet = SetMonitorZoneDefaultByZoneID(nZoneID);
		if (nRet == ICV_SUCCESS)
		{
			sprintf(szSQL, "DELETE FROM t_video_zone WHERE fd_zone_id=%d", nZoneID);
			m_pdbCfg->execDML(szSQL);
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoReadDeviceList(CVideoDeviceList* plistDevice)
{
	if((m_pdbCfg == NULL) || (plistDevice == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	plistDevice->clear();
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL, "SELECT * FROM t_video_device");
	try
	{
		CppSQLite3Query QueryDev = m_pdbCfg->execQuery(szSQL);
		while(!QueryDev.eof())
		{
			CVideoDevice theDev;
			theDev.SetID(QueryDev.getIntField("fd_device_id"));
			theDev.SetName(QueryDev.getStringField("fd_device_name"));
			theDev.SetIP(QueryDev.getStringField("fd_device_ip"));
			theDev.SetPort(QueryDev.getIntField("fd_device_port"));
			theDev.SetCtrlPort(QueryDev.getIntField("fd_ctrl_channel", VIDEO_CHANNEL_INVALIDATION));
			theDev.SetExtent(QueryDev.getStringField("fd_device_extent"));
			theDev.SetDescription(QueryDev.getStringField("fd_description"));
			theDev.SetServerID(QueryDev.getIntField("fd_server_id"));
			theDev.SetProductID(QueryDev.getIntField("fd_product_id"));
			// 查找连接设备
			memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
			sprintf(szSQL, "SELECT A.fd_channel_index,B.fd_channel_index,B.fd_device_id \
							FROM t_video_channel A,t_video_channel B \
							WHERE A.fd_device_id=%d AND A.fd_linkchannel_id=B.fd_channel_id", theDev.GetID());
			CppSQLite3Query QueryLink = m_pdbCfg->execQuery(szSQL);
			while(!QueryLink.eof())
			{
				CVideoLinkDevice theLinkDev;
				theLinkDev.SetLocalChannel(QueryLink.getStringField(0));
				theLinkDev.SetChannel(QueryLink.getStringField(1));
				theLinkDev.SetDeviceID(QueryLink.getIntField(2));
				theDev.GetLinkDev()->InsertObject(theLinkDev);
				QueryLink.nextRow();
			}
			plistDevice->InsertObject(theDev);
            QueryDev.nextRow();
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::AddDevice(CVideoDevice* pDevice)
{
	long nRet = EC_ICV_CCTV_FAILTOEXEDB;

	if((m_pdbCfg == NULL) || (pDevice == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	if(!IsServerExist(pDevice->GetServerID()))
		return EC_ICV_CCTV_SERVERNOTEXIST;
	if(!IsProductExist(pDevice->GetProductID()))
		return EC_ICV_CCTV_PRODUCTNOTEXIST;
	
	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "INSERT INTO t_video_device \
						(fd_device_id,fd_device_name,fd_device_ip,fd_device_port,fd_ctrl_channel,fd_device_extent,fd_description,\
						fd_server_id,fd_product_id) VALUES (null,'%s','%s',%d,%d,'%s','%s',%d,%d)", 
						pDevice->GetName(), pDevice->GetIP(), pDevice->GetPort(), pDevice->GetCtrlPort(), pDevice->GetExtent(),
						pDevice->GetDescription(), pDevice->GetServerID(), pDevice->GetProductID());
		m_pdbCfg->execDML(szSQL);

		pDevice->SetID((long)m_pdbCfg->lastRowId());
		CVideoLinkDeviceList* plistLinkDev = pDevice->GetLinkDev();
		// 插入连接信息
		for(int i=0; i<plistLinkDev->GetSize(); i++)
		{
			long nChannelID = VIDEO_ID_INVALIDATION;
			long nLinkChannelID = VIDEO_ID_INVALIDATION;
			nChannelID = GetChannelID(pDevice->GetID(), plistLinkDev->GetAt(i)->GetLocalChannel());
			nLinkChannelID = GetChannelID(plistLinkDev->GetAt(i)->GetDeviceID(), plistLinkDev->GetAt(i)->GetChannel());
			nRet = SetLinkChannel(nChannelID, nLinkChannelID);
			if(nRet != ICV_SUCCESS)
			{
				CVLog.LogMessage(LOG_LEVEL_ERROR, "When modifying the information of the device, SetLinkChannel,catches exception, return error code:%d", nRet);
				return nRet;
			}
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::UpdateDevice(CVideoDevice* pNewDevice, const long nOldDeviceID, bool bLinkChange)
{
	long nRet = EC_ICV_CCTV_FAILTOEXEDB;

	if((m_pdbCfg == NULL) || (pNewDevice == NULL) || (nOldDeviceID != pNewDevice->GetID()))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	if(!IsServerExist(pNewDevice->GetServerID()))
		return EC_ICV_CCTV_SERVERNOTEXIST;
	if(!IsProductExist(pNewDevice->GetProductID()))
		return EC_ICV_CCTV_PRODUCTNOTEXIST;
	
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL, "UPDATE t_video_device SET fd_device_name='%s',fd_device_ip='%s',\
			fd_device_port=%d,fd_ctrl_channel=%d,fd_device_extent='%s',fd_description='%s',fd_server_id=%d,fd_product_id=%d \
			WHERE fd_device_id=%d", pNewDevice->GetName(), pNewDevice->GetIP(), pNewDevice->GetPort(),
			pNewDevice->GetCtrlPort(), pNewDevice->GetExtent(), pNewDevice->GetDescription(), pNewDevice->GetServerID(),
			pNewDevice->GetProductID(), nOldDeviceID);
	try
	{
		m_pdbCfg->execDML(szSQL);

		if(bLinkChange)
		{
			// 删除连接设备
			nRet = SetDeviceLinkChannelNullByDeviceID(nOldDeviceID);
			if(nRet == ICV_SUCCESS)
			{
				CVideoLinkDeviceList* plistLinkDev = pNewDevice->GetLinkDev();
				// 插入连接信息
				for(int i=0; i<plistLinkDev->GetSize(); i++)
				{
					long nChannelID = VIDEO_ID_INVALIDATION;
					long nLinkChannelID = VIDEO_ID_INVALIDATION;
					nChannelID = GetChannelID(pNewDevice->GetID(), plistLinkDev->GetAt(i)->GetLocalChannel());
					nLinkChannelID = GetChannelID(plistLinkDev->GetAt(i)->GetDeviceID(), plistLinkDev->GetAt(i)->GetChannel());
					nRet = SetLinkChannel(nChannelID, nLinkChannelID);
					if(nRet != ICV_SUCCESS)
					{
						CVLog.LogMessage(LOG_LEVEL_ERROR, "When modifying the information of the device, SetLinkChannel,catches exception, return error code:%d", nRet);
						return nRet;
					}
				}
			}
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoDeleteDevice(const long nDeviceID)
{
	if(m_pdbCfg == NULL)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		// 删除设备相关的channel
		long nRet = DeleteChannelByDeviceID(nDeviceID);
		if(nRet == ICV_SUCCESS)
		{
			sprintf(szSQL, "DELETE FROM t_video_device WHERE fd_device_id=%d", nDeviceID);
			m_pdbCfg->execDML(szSQL);
		}
		else
			return nRet;
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoDeleteDeviceByServerID(const long nServerID)
{
	if(m_pdbCfg == NULL)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	// 只能逐个删除
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL, "SELECT fd_device_id FROM t_video_device WHERE fd_server_id=%d", nServerID);
	try
	{
		CppSQLite3Query Query = m_pdbCfg->execQuery(szSQL);
		while(!Query.eof())
		{
			long nDeviceID = VIDEO_ID_INVALIDATION;
			nDeviceID = Query.getIntField("fd_device_id");
			long nRet = VideoDeleteDevice(nDeviceID);
			if(nRet != ICV_SUCCESS)
			{
				CVLog.LogMessage(LOG_LEVEL_ERROR, "When deleting the information by service ID, VideoDeleteDeviceByServerID,catches exception, return error code:%d", nRet);
				return nRet;
			}

            Query.nextRow();
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoDeleteDeviceByProductID(const long nProductID)
{
	if(m_pdbCfg == NULL)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	// 只能逐个删除
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL, "SELECT fd_device_id FROM t_video_device WHERE fd_product_id=%d", nProductID);
	try
	{
		CppSQLite3Query Query = m_pdbCfg->execQuery(szSQL);
		while(!Query.eof())
		{
			long nDeviceID = VIDEO_ID_INVALIDATION;
			nDeviceID = Query.getIntField("fd_device_id");
			long nRet = VideoDeleteDevice(nDeviceID);
			if(nRet != ICV_SUCCESS)
			{
				CVLog.LogMessage(LOG_LEVEL_ERROR, "When deleting the information by product type ID, VideoDeleteDeviceByProductID, catches exception, return error code:%d", nRet);
				return nRet;
			}

            Query.nextRow();
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoReadProductList(CVideoProductList* plistProduct)
{
	if((m_pdbCfg == NULL) || (plistProduct == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	plistProduct->clear();
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL, "SELECT * FROM t_video_product ORDER BY fd_product_name");
	try
	{
		CppSQLite3Query Query = m_pdbCfg->execQuery(szSQL);
		while(!Query.eof())
		{
			CVideoProduct theProduct;
			theProduct.SetID(Query.getIntField("fd_product_id"));
			theProduct.SetName(Query.getStringField("fd_product_name"));
			theProduct.SetDllName(Query.getStringField("fd_dll_name"));
			theProduct.SetDeviceType(Query.getIntField("fd_device_type_id"));
			plistProduct->InsertObject(theProduct);
            Query.nextRow();
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoAddProduct(CVideoProduct* pProduct)
{
	if((m_pdbCfg == NULL) || (pProduct == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	// 设备类型必须存在
	if(!IsDeviceTypeExist(pProduct->GetDeviceType()))
		return EC_ICV_CCTV_DEVTYPENOTEXIST;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "INSERT INTO t_video_product \
				 (fd_product_id,fd_product_name,fd_dll_name,fd_device_type_id) VALUES (null,'%s','%s',%d)",
				 pProduct->GetName(), pProduct->GetDllName(), pProduct->GetDeviceType());
		m_pdbCfg->execDML(szSQL);

		pProduct->SetID((long)m_pdbCfg->lastRowId());
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoUpdateProduct(CVideoProduct* pNewProduct, const long nOldProductID)	
{
	if((m_pdbCfg == NULL) || (pNewProduct == NULL) || (pNewProduct->GetID() != nOldProductID))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	// 设备类型必须存在
	if(!IsDeviceTypeExist(pNewProduct->GetDeviceType()))
		return EC_ICV_CCTV_DEVTYPENOTEXIST;
	
	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "UPDATE t_video_product SET fd_product_name='%s',fd_dll_name='%s',\
				fd_device_type_id=%d WHERE fd_product_id=%d", pNewProduct->GetName(), 
				pNewProduct->GetDllName(), pNewProduct->GetDeviceType(), nOldProductID);

		m_pdbCfg->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::DeleteProduct(const long nProductID)
{
	long nRet = EC_ICV_CCTV_FAILTOEXEDB;

	if(m_pdbCfg == NULL)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		nRet = VideoDeleteDeviceByProductID(nProductID);
		if(nRet == ICV_SUCCESS)
		{
			sprintf(szSQL, "DELETE FROM t_video_product WHERE fd_product_id=%d", nProductID);
			m_pdbCfg->execDML(szSQL);
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoReadDeviceTypeList(CVideoDevTypeList* plistDeviceType)
{
	if((m_pdbCfg == NULL) || (plistDeviceType == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	plistDeviceType->clear();
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL, "SELECT * FROM t_video_device_type ORDER BY fd_device_type_id");
	try
	{
		CppSQLite3Query Query = m_pdbCfg->execQuery(szSQL);
		while(!Query.eof())
		{
			CVideoDevType theSnapType;
			theSnapType.SetID(Query.getIntField("fd_device_type_id"));
			theSnapType.SetName(Query.getStringField("fd_device_type_text"));
			plistDeviceType->InsertObject(theSnapType);
            Query.nextRow();
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoReadSnapTypeInfoList(CIDMapTextList* plistSnapTypeInfo)
{
	if((m_pdbCfg == NULL) || (plistSnapTypeInfo == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	plistSnapTypeInfo->clear();
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL, "SELECT * FROM t_video_snap_type ORDER BY fd_snap_type_text");
	try
	{
		CppSQLite3Query Query = m_pdbCfg->execQuery(szSQL);
		while(!Query.eof())
		{
			CIDMapText theIDText;
			theIDText.SetID(Query.getIntField("fd_snap_type_id"));
			theIDText.SetText(Query.getStringField("fd_snap_type_text"));
			plistSnapTypeInfo->InsertObject(theIDText);
            Query.nextRow();
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoAddSnapTypeInfo(const char* pszSnapTypeInfo)
{
	if((m_pdbCfg == NULL) || (pszSnapTypeInfo == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "INSERT INTO t_video_snap_type \
				(fd_snap_type_id,fd_snap_type_text) VALUES (null,'%s')", pszSnapTypeInfo);
		m_pdbCfg->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoUpdateSnapTypeInfo(const char* pszNewSnapTypeInfo, const char* pszOldSnapTypeInfo)	
{
	if((m_pdbCfg == NULL) || (pszNewSnapTypeInfo == NULL) || (pszOldSnapTypeInfo == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "UPDATE t_video_snap_type SET fd_snap_type_text='%s'	WHERE fd_snap_type_text='%s'", 
					pszNewSnapTypeInfo, pszOldSnapTypeInfo);
		m_pdbCfg->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoDeleteSnapTypeInfo(const char* pszSnapTypeInfo)	
{
	if((m_pdbCfg == NULL) || (pszSnapTypeInfo == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "DELETE FROM t_video_snap_type WHERE fd_snap_type_text='%s'", pszSnapTypeInfo);
		m_pdbCfg->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
// 以此为界,以上部分为配置数据库操作,以下部分为过程数据存储数据库操作
//////////////////////////////////////////////////////////////////////////////////////////////////

// 说明参见.h文件
long CVideoDBLibrary::VideoReadPSPListOfCamera(CVideoPSPList* plistPSP, const long nCameraID)
{
	if((m_pdbInfo == NULL) || (plistPSP == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	plistPSP->clear();
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL, "SELECT * FROM t_video_psp WHERE fd_camera_id=%d ORDER BY fd_psp_name", nCameraID);
	try
	{
		CppSQLite3Query Query = m_pdbInfo->execQuery(szSQL);
		while(!Query.eof())
		{
			CVideoPSP thePSP;
			thePSP.SetName(Query.getStringField("fd_psp_name"));
			thePSP.SetDescription(Query.getStringField("fd_description"));		
			thePSP.SetPresetIndex(Query.getIntField("fd_psp_index"));
			plistPSP->InsertObject(thePSP);
            Query.nextRow();
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoAddPSPOfCamera(CVideoPSP* pPSP, const long nCameraID)
{
	if((m_pdbInfo == NULL) || (pPSP == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "INSERT INTO t_video_psp \
				(fd_camera_id,fd_psp_name,fd_psp_index,fd_description) VALUES (%d,'%s',%d,'%s')",
				nCameraID, pPSP->GetName(), pPSP->GetPresetIndex(), pPSP->GetDescription());
		m_pdbInfo->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoUpdatePSPOfCamera(CVideoPSP* pNewPSP, const char* pszOldPSPName, const long nCameraID)	
{
	if((m_pdbInfo == NULL) || (pNewPSP == NULL) || (pszOldPSPName == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	if (!IsPspExist(nCameraID, pszOldPSPName))
		return EC_ICV_CCTV_PSPNOTEXIST;

	if (0 != strcmp(pszOldPSPName, pNewPSP->GetName()))
	{
		if (IsPspExist(nCameraID, pNewPSP->GetName()))
			return EC_ICV_CCTV_PSPALREADYEXIST;
	}
	
	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "UPDATE t_video_psp SET fd_psp_name='%s',fd_description='%s' \
				WHERE fd_camera_id=%d AND fd_psp_name='%s'", pNewPSP->GetName(), pNewPSP->GetDescription(),
				nCameraID, pszOldPSPName);
		m_pdbInfo->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoDeletePSPOfCamera(const char* pszPSPName, const long nCameraID)	
{
	if((m_pdbInfo == NULL) || (pszPSPName == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	if (!IsPspExist(nCameraID, pszPSPName))
		return EC_ICV_CCTV_PSPNOTEXIST;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "DELETE FROM t_video_psp WHERE fd_camera_id=%d AND fd_psp_name='%s'", 
					nCameraID, pszPSPName);
		m_pdbInfo->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoReadModeList(CVideoModeList* plistMode)
{
	if((m_pdbInfo == NULL) || (plistMode == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	plistMode->clear();
	char szSQL[VIDEO_SQL_MAX_SIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
	sprintf(szSQL, "SELECT * FROM t_video_mode ORDER BY fd_mode_name");
	try
	{
		CppSQLite3Query Query = m_pdbInfo->execQuery(szSQL);
		while(!Query.eof())
		{
			CVideoMode theMode;
			int nBlobSize = 0;
			theMode.SetName(Query.getStringField("fd_mode_name"));
			theMode.SetType(Query.getIntField("fd_mode_type"));
			theMode.SetDescription(Query.getStringField("fd_description"));		
			theMode.SetPara((char*)Query.getBlobField("fd_mode_layout", nBlobSize));
			if(nBlobSize >= VIDEO_MODE_PARA_MAXSIZE)
				CVLog.LogMessage(LOG_LEVEL_ERROR, "When reading the mode information, VideoReadModeList,catches exception, return error code:%d", 
					EC_ICV_CCTV_PARSERBUFFERTOOSHORT);

			plistMode->InsertObject(theMode);
            Query.nextRow();
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoReadModeByName(const char* pszModeName, CVideoMode* pMode)
{
	if((m_pdbInfo == NULL) || (pMode == NULL) || (pszModeName == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "SELECT * FROM t_video_mode WHERE fd_mode_name='%s'", pszModeName);

		CppSQLite3Query Query = m_pdbInfo->execQuery(szSQL);
		while(!Query.eof())
		{
			int nBlobSize = 0;
			pMode->SetName(Query.getStringField("fd_mode_name"));
			pMode->SetType(Query.getIntField("fd_mode_type"));
			pMode->SetDescription(Query.getStringField("fd_description"));		
			pMode->SetPara((char*)Query.getBlobField("fd_mode_layout", nBlobSize));

			if(nBlobSize >= VIDEO_MODE_PARA_MAXSIZE)
			{
				CVLog.LogMessage(LOG_LEVEL_ERROR, "When reading the mode information, VideoReadModeByName,catches exception, return error code:%d", 
					EC_ICV_CCTV_PARSERBUFFERTOOSHORT);
				return EC_ICV_CCTV_PARSERBUFFERTOOSHORT;
			}

            Query.nextRow();
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoAddMode(CVideoMode* pMode)
{
	if((m_pdbInfo == NULL) || (pMode == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	char szSQL[VIDEO_SQL_MAX_SIZE+VIDEO_MODE_PARA_MAXSIZE];
	memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE+VIDEO_MODE_PARA_MAXSIZE);
	sprintf(szSQL, "INSERT INTO t_video_mode \
			 (fd_mode_name,fd_mode_type,fd_description,fd_mode_layout) VALUES ('%s',%d,'%s','%s')",
			 pMode->GetName(), pMode->GetType(), pMode->GetDescription(), pMode->GetPara());	
	try
	{

		m_pdbInfo->execDML(szSQL);

	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 修改模式
long CVideoDBLibrary::VideoUpdateMode(CVideoMode* pNewMode, const char* pszOldModeName, const bool bParaChange)
{
	if((m_pdbInfo == NULL) || (pNewMode == NULL) || (pszOldModeName == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE+VIDEO_MODE_PARA_MAXSIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE+VIDEO_MODE_PARA_MAXSIZE);
		if(bParaChange)
		{
			sprintf(szSQL, "UPDATE t_video_mode SET fd_mode_name='%s',fd_mode_type=%d,\
					fd_description='%s',fd_mode_layout='%s' WHERE fd_mode_name='%s'", pNewMode->GetName(),
					pNewMode->GetType(), pNewMode->GetDescription(), pNewMode->GetPara(), pszOldModeName);
		}
		else
		{
			//布局为空的时候，不更新布局，不为空的时候更新
			if((pNewMode->GetPara() == NULL) || ((pNewMode->GetPara())[0] == '\0'))
				sprintf(szSQL, "UPDATE t_video_mode SET fd_mode_name='%s',fd_description='%s' WHERE fd_mode_name='%s'",
						pNewMode->GetName(), pNewMode->GetDescription(), pszOldModeName);
			else
				sprintf(szSQL, "UPDATE t_video_mode SET fd_mode_name='%s',fd_description='%s',fd_mode_layout='%s' WHERE fd_mode_name='%s'",
						pNewMode->GetName(), pNewMode->GetDescription(), pNewMode->GetPara(), pszOldModeName);
		}

		m_pdbInfo->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoDeleteMode(const char* pszModeName)
{
	if((m_pdbInfo == NULL) || (pszModeName == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "DELETE FROM t_video_mode WHERE fd_mode_name='%s'", pszModeName);
		m_pdbInfo->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoReadSnapPictureList(CVideoSnapFileList* plistSnapPic, CIDMapTextList* plistSnapType)
{
	if((m_pdbInfo == NULL) || (plistSnapPic == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	plistSnapPic->clear();
	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "SELECT * FROM t_video_snap");

		CppSQLite3Query Query = m_pdbInfo->execQuery(szSQL);
		while(!Query.eof())
		{
			CVideoSnapFile snapfile;
			snapfile.SetID(Query.getIntField("fd_snap_id"));
			snapfile.SetCameraID(Query.getIntField("fd_camera_id"));
			int nSnapTypeID = Query.getIntField("fd_snap_type_id");
			snapfile.SetExtent(Query.getStringField("fd_snap_extent"));
			snapfile.SetDescription(Query.getStringField("fd_description"));
			snapfile.SetSnapCount(Query.getIntField("fd_snap_count"));
			snapfile.SetSnapTime(Query.getIntField("fd_snap_time"));
			Query.nextRow();

			// 获取抓拍类型
			CIDMapText* pIDMax = plistSnapType->FindIDMapbyID(nSnapTypeID);
			if (NULL == pIDMax)
				continue;

			snapfile.SetInfoType(pIDMax->GetText());
			plistSnapPic->InsertObject(snapfile);
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoReadSnapPicBuffer(const long nSnapID, long &nCamID, long &nCount, char *&pBuffer, long &nPicLen)
{
	if((m_pdbInfo == NULL) || (0 >= nSnapID))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "SELECT * FROM t_video_snap WHERE fd_snap_id=%d", nSnapID);

		CppSQLite3Query Query = m_pdbInfo->execQuery(szSQL);
		
		nCamID = Query.getIntField("fd_camera_id");
		nCount = Query.getIntField("fd_snap_count");

		//检测是否读取到图片
		if(nCount<= 0)
		{
			pBuffer = NULL;
			nPicLen = 0;
			return EC_ICV_CCTV_SNAPPICNOTEXIST;
		}

		unsigned char* pBlobVlaue = (unsigned char*)Query.fieldValue("fd_picture_buffer");
		CppSQLite3Binary blob;
		blob.setEncoded((unsigned char*)pBlobVlaue);
		char *pPicBuf = (char*)blob.getBinary();
		nPicLen = blob.getBinaryLength();

		pBuffer = new char[nPicLen+VIDEO_SERIAL_SNAP_COUNT_SIZE];
		long nCountLen = sprintf((char*)pBuffer, "%02d", nCount);
		memcpy((void*)(pBuffer+nCountLen), pPicBuf, nPicLen);
		nPicLen += nCountLen;
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoAddSnapPicture(CVideoSnapFile* pSnapPic, const char* pszPicBuf, const long nSize, CIDMapTextList* plistSnapType, long &nSnapID)
{
	if((m_pdbInfo == NULL) || (pSnapPic == NULL) || (pszPicBuf == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	CIDMapText* pIDMap = plistSnapType->FindIDMapbyText(pSnapPic->GetInfoType());
	if (NULL == pIDMap)
		return EC_ICV_CCTV_SNAPTYPENOTEXIST;

	long nBineryLen = nSize;

	try
	{
		CppSQLite3Binary blobFile;
		blobFile.setBinary((const unsigned char*)pszPicBuf, nBineryLen);

		CppSQLite3Buffer bufSQL;
		bufSQL.format("INSERT INTO t_video_snap (fd_camera_id,fd_snap_time,fd_snap_type_id,fd_snap_extent,fd_description,\
				fd_snap_count,fd_picture_buffer) VALUES (%d,%d,%d,'%s','%s',%d,%Q)",
				pSnapPic->GetCameraID(), (long)pSnapPic->GetSnapTime(), pIDMap->GetID(), pSnapPic->GetExtent(), pSnapPic->GetDescription(),
				pSnapPic->GetSnapCount(), blobFile.getEncoded());
		m_pdbInfo->execDML(bufSQL);

		nSnapID = (long)m_pdbInfo->lastRowId();
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoUpdateSnapPicture(CVideoSnapFile* pNewSnapPic, const long nSnapPicID, CIDMapTextList* plistSnapType)
{
	if((m_pdbInfo == NULL) || (pNewSnapPic == NULL) || (nSnapPicID <= 0))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	CIDMapText* pIDMap = plistSnapType->FindIDMapbyText(pNewSnapPic->GetInfoType());
	if (NULL == pIDMap)
		return EC_ICV_CCTV_SNAPTYPENOTEXIST;
	
	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "UPDATE t_video_snap SET fd_snap_type_id=%d,fd_snap_extent='%s',fd_description='%s' WHERE fd_snap_id=%d",
			pIDMap->GetID(), pNewSnapPic->GetExtent(), pNewSnapPic->GetDescription(), nSnapPicID);

		m_pdbInfo->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoDeleteSnapPicture(const long nSnapPicID)
{
	if((m_pdbInfo == NULL) || (nSnapPicID <= 0))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "DELETE FROM t_video_snap WHERE fd_snap_id=%d", nSnapPicID);
		m_pdbInfo->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoCapturePicAddTo(const long nCaptureID, const char* pszPicBuff, const long nSize)
{
	if((m_pdbInfo == NULL) || (NULL == pszPicBuff) || (0 >= nCaptureID) || (0 >= nSize))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	long nCamID = 0;
	long nCount = 0;
	char* pszOldBuf = NULL;
	long nOldSize = 0;
	long lRet = VideoReadSnapPicBuffer(nCaptureID, nCamID, nCount,pszOldBuf, nOldSize);
	if (ICV_SUCCESS != lRet)
		return lRet;

	nOldSize -= VIDEO_SERIAL_SNAP_COUNT_SIZE;
	long nBineryLen = nSize+nOldSize;
	char* pszBuff = new char[nBineryLen+1];
	memset(pszBuff, 0x00, nBineryLen);
	memcpy(pszBuff, pszOldBuf+VIDEO_SERIAL_SNAP_COUNT_SIZE, nOldSize);
	memcpy(pszBuff+nOldSize, pszPicBuff, nSize);
	try
	{
		CppSQLite3Binary blobFile;
		blobFile.setBinary((const unsigned char*)pszBuff, nBineryLen);
		
		CppSQLite3Buffer bufSQL;
		bufSQL.format("UPDATE t_video_snap SET fd_snap_count=%d,fd_picture_buffer=%Q WHERE fd_snap_id=%d", nCount+1, blobFile.getEncoded(), nCaptureID);
		m_pdbInfo->execDML(bufSQL);
		delete pszBuff;
	}
	catch(CppSQLite3Exception &e)
	{
		delete pszBuff;
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoReadCustomZone(const char* pszUserName, CVideoCustomZoneList *plistCustZone)
{
	if((m_pdbInfo == NULL) || (NULL == pszUserName) || (NULL == plistCustZone))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	plistCustZone->clear();

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "SELECT * FROM t_video_customzone WHERE fd_customzone_user='%s'", pszUserName);
		CppSQLite3Query Query = m_pdbInfo->execQuery(szSQL);
		
		// 依次读取所有自定义分区信息
		while(!Query.eof())
		{
			CVideoCustomZone theZone;
			theZone.SetID(Query.getIntField("fd_customzone_id"));
			theZone.SetName(Query.getStringField("fd_customzone_name"));
			theZone.SetUserName(Query.getStringField("fd_customzone_user"));
			theZone.SetDesc(Query.getStringField("fd_description"));
			plistCustZone->InsertObject(theZone);
			Query.nextRow();
		}// end while
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoAddCustomZone(const char* pszUserName, const char* pszCustZoneName, const char* pszCustZoneDesc, long &nID)
{
	if((m_pdbInfo == NULL) || (pszUserName == NULL) || (pszCustZoneName == NULL) || (pszCustZoneDesc == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		// 插入自定义分区
		sprintf(szSQL, "INSERT INTO t_video_customzone (fd_customzone_id,fd_customzone_name,fd_customzone_user,\
            fd_description) VALUES (null,'%s','%s','%s')", pszCustZoneName, pszUserName, pszCustZoneDesc);
		m_pdbInfo->execDML(szSQL);
		
		nID = (long)m_pdbInfo->lastRowId();
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoUpdateCustomZone(CVideoCustomZone* pNewCustZone, const long nCustZoneID)
{
	if((NULL == m_pdbInfo) || (NULL == pNewCustZone) || (0 > nCustZoneID))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		// 更新自定义分区
		sprintf(szSQL, "UPDATE t_video_customzone SET fd_customzone_name='%s',fd_customzone_user='%s',\
            fd_description='%s' WHERE fd_customzone_id=%d",
			pNewCustZone->GetName(), pNewCustZone->GetRelateUserName(), pNewCustZone->GetDesc(), nCustZoneID);
		m_pdbInfo->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoDeleteCustomZone(const long nCustZoneID)
{
	if((m_pdbInfo == NULL) || (0 > nCustZoneID))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];

		// 删除摄像头索引
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "DELETE FROM t_video_cameraindex WHERE fd_customzone_id=%d", nCustZoneID);
		m_pdbInfo->execDML(szSQL);

		// 删除自定义分区
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "DELETE FROM t_video_customzone WHERE fd_customzone_id=%d", nCustZoneID);
		m_pdbInfo->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoReadCamIndex(CVideoList<long>* plistCameraID, const long nCustZoneID)
{
	if((m_pdbInfo == NULL) || (NULL == plistCameraID) || (0 >= nCustZoneID))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	plistCameraID->clear();

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "SELECT * FROM t_video_cameraindex WHERE fd_customzone_id=%d", nCustZoneID);
		CppSQLite3Query Query = m_pdbInfo->execQuery(szSQL);
		
		// 依次读取所有摄像头ID
		while(!Query.eof())
		{
			plistCameraID->InsertObject(Query.getIntField("fd_camera_id"));
			Query.nextRow();
		}// end while
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoAddCamIndex(CVideoList<long>* plistCameraID, const long nCustZoneID)
{
	if((m_pdbInfo == NULL) || (NULL == plistCameraID) || (0 >= nCustZoneID))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		for (int i=0; i< plistCameraID->GetSize(); i++)
		{
			memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
			// 插入自定义分区
			sprintf(szSQL, "INSERT INTO t_video_cameraindex (fd_primary_id, fd_camera_id,fd_customzone_id) VALUES (null,%d,%d)", 
				*(plistCameraID->GetAt(i)), nCustZoneID);
			m_pdbInfo->execDML(szSQL);
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoDeleteCamIndex(CVideoList<long>* plistCameraID, const long nCustZoneID)
{
	if((m_pdbInfo == NULL) || (NULL == plistCameraID) || (0 >= nCustZoneID))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		for (int i=0; i< plistCameraID->GetSize(); i++)
		{
			memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
			sprintf(szSQL, "DELETE FROM t_video_cameraindex WHERE fd_camera_id=%d AND fd_customzone_id=%d", *(plistCameraID->GetAt(i)), nCustZoneID);
			m_pdbInfo->execDML(szSQL);
		}
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoAddCameraList(CVideoCameraList* pCamList)
{
	VIDEO_TRANSACTION_PROC(AddCameraList(pCamList))
}

// 说明参见.h文件
long CVideoDBLibrary::AddCameraList(CVideoCameraList* pCamList)
{
	CVideoCameraList::iterator it = pCamList->begin();
	for (; it!=pCamList->end(); it++)
	{
		long lRet = AddCamera(&(*it));
		if (ICV_SUCCESS != lRet)
			return lRet;
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoAddCamera(CVideoCamera* pCamera)
{
	VIDEO_TRANSACTION_PROC(AddCamera(pCamera))
}

// 说明参见.h文件
long CVideoDBLibrary::VideoUpdateCamera(CVideoCamera* pNewCamera, const long nOldCameraID, bool bLinkChange)
{
	VIDEO_TRANSACTION_PROC(UpdateCamera(pNewCamera, nOldCameraID, bLinkChange))
}

// 说明参见.h文件
long CVideoDBLibrary::VideoUpdateCameraZone(CVideoList<long>* plistCameraID, const long nZoneID)
{
	VIDEO_TRANSACTION_PROC(UpdateCameraZone(plistCameraID, nZoneID))
}

// 说明参见.h文件
long CVideoDBLibrary::VideoDeleteCamera(const long nCameraID)
{
	VIDEO_TRANSACTION_PROC(DeleteCamera(nCameraID))
}

// 说明参见.h文件
long CVideoDBLibrary::VideoUpdateMonitorZone(CVideoList<long>* plistMonitorID, const long nZoneID)
{
 VIDEO_TRANSACTION_PROC(UpdateMonitorZone(plistMonitorID, nZoneID))
}

// 说明参见.h文件
long CVideoDBLibrary::VideoDeleteServer(const long nServerID)
{
	VIDEO_TRANSACTION_PROC(DeleteServer(nServerID))
}

// 说明参见.h文件
long CVideoDBLibrary::VideoDeleteZone(const long nZoneID)
{
	VIDEO_TRANSACTION_PROC(DeleteZone(nZoneID))
}

// 说明参见.h文件
long CVideoDBLibrary::VideoAddDevice(CVideoDevice* pDevice)
{
	VIDEO_TRANSACTION_PROC(AddDevice(pDevice))
}

// 说明参见.h文件
long CVideoDBLibrary::VideoUpdateDevice(CVideoDevice* pNewDevice, const long nOldDeviceID, bool bLinkChange)
{
	VIDEO_TRANSACTION_PROC(UpdateDevice(pNewDevice, nOldDeviceID, bLinkChange))
}

// 说明参见.h文件
long CVideoDBLibrary::VideoDeleteProduct(const long nProductID)
{
	VIDEO_TRANSACTION_PROC(DeleteProduct(nProductID))
}

// 说明参见.h文件
long CVideoDBLibrary::VideoAddRecord(CRecord *pRec, long &nRecordID)
{
	if((m_pdbInfo == NULL) || (NULL == pRec))
		return EC_ICV_CCTV_FUNCPARAMINVALID;
	
	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		// 插入自定义分区
		sprintf(szSQL, "INSERT INTO t_video_record (fd_record_id, fd_camera_id, fd_start_time, fd_end_time, fd_record_extent, fd_file_name, fd_description,\
					   fd_snap_strat, fd_snap_mid, fd_snap_end, fd_record_reserve1, fd_record_reserve2, fd_record_reserve3) VALUES (null,%d,%I64d,%I64d,'%s','%s','%s',%d,%d,%d,'%s','%s','%s')",
			pRec->GetCameraID(), pRec->GetStartTime(), pRec->GetEndTime(), pRec->GetExtent(), pRec->GetFileName(), pRec->GetDesc(), pRec->GetStartSnapID(), pRec->GetMidSnapID()
			, pRec->GetEndSnapID(), pRec->GetReserve1(), pRec->GetReserve2(), pRec->GetReserve3());
		m_pdbInfo->execDML(szSQL);

		nRecordID = (long)m_pdbInfo->lastRowId();
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}
	
	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoGetRecord(const long nRecordID, CRecord* pRec)
{
	if((m_pdbInfo == NULL) || (0 >= nRecordID) || (pRec == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{	
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "SELECT * FROM t_video_record WHERE fd_record_id=%d", nRecordID);
		CppSQLite3Query Query = m_pdbInfo->execQuery(szSQL);
		if (Query.eof())
			return EC_ICV_CCTV_RECORDISNOTEXIST;
		
		// 读取录像信息
		pRec->SetID(Query.getIntField("fd_record_id"));
		pRec->SetCameraID(Query.getIntField("fd_camera_id"));
		pRec->SetTimeStart(Query.getIntField("fd_start_time"));
		pRec->SetTimeEnd(Query.getIntField("fd_end_time"));
		pRec->SetExtent(Query.getStringField("fd_record_extent"));
		pRec->SetFileName(Query.getStringField("fd_file_name"));
		pRec->SetDesc(Query.getStringField("fd_description"));
		pRec->SetStartSnapID(Query.getIntField("fd_snap_strat"));
		pRec->SetMidSnapID(Query.getIntField("fd_snap_mid"));
		pRec->SetEndSnapID(Query.getIntField("fd_snap_end"));
		pRec->SetReserve1(Query.getStringField("fd_record_reserve1"));
		pRec->SetReserve2(Query.getStringField("fd_record_reserve2"));
		pRec->SetReserve3(Query.getStringField("fd_record_reserve3"));
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoModifyRecord(const long nRecordID, CRecord* pRec)
{
	if((m_pdbInfo == NULL) || (0 >= nRecordID) || (pRec == NULL))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		// 更新自定义分区
		sprintf(szSQL, "UPDATE t_video_record SET fd_record_extent='%s',fd_description='%s',fd_snap_strat=%d,fd_snap_mid=%d,fd_snap_end=%d,\
			fd_record_reserve1='%s',fd_record_reserve2='%s',fd_record_reserve3='%s' WHERE fd_record_id=%d",
			pRec->GetExtent(), pRec->GetDesc(), pRec->GetStartSnapID(), pRec->GetMidSnapID(), pRec->GetEndSnapID(),
			pRec->GetReserve1(), pRec->GetReserve2(), pRec->GetReserve3(), nRecordID);
		m_pdbInfo->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoReadRecordList(OUT CRecordList *plistRec)
{
	if (NULL == plistRec)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	plistRec->clear();
	
	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "SELECT * FROM t_video_record");
		CppSQLite3Query Query = m_pdbInfo->execQuery(szSQL);
		
		// 依次读取所有信息
		while(!Query.eof())
		{
			CRecord rec;
			rec.SetID(Query.getIntField("fd_record_id"));
			rec.SetCameraID(Query.getIntField("fd_camera_id"));
			rec.SetTimeStart(Query.getIntField("fd_start_time"));
			rec.SetTimeEnd(Query.getIntField("fd_end_time"));
			rec.SetExtent(Query.getStringField("fd_record_extent"));
			rec.SetFileName(Query.getStringField("fd_file_name"));
			rec.SetDesc(Query.getStringField("fd_description"));
			rec.SetStartSnapID(Query.getIntField("fd_snap_strat"));
			rec.SetMidSnapID(Query.getIntField("fd_snap_mid"));
			rec.SetEndSnapID(Query.getIntField("fd_snap_end"));
			rec.SetReserve1(Query.getStringField("fd_record_reserve1"));
			rec.SetReserve2(Query.getStringField("fd_record_reserve2"));
			rec.SetReserve3(Query.getStringField("fd_record_reserve3"));
			plistRec->InsertObject(rec);
			Query.nextRow();
		}// end while
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoDeleteRecord(IN const long nRecordID)
{
	if (0 >= nRecordID)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "DELETE FROM t_video_record WHERE fd_record_id=%d", nRecordID);
		m_pdbInfo->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}

// 说明参见.h文件
long CVideoDBLibrary::VideoQueryLinkChannel(IN const long nNearCamDevID, IN const long nNearMonDevID, OUT char* szNearMonChannel)
{
	if (0 >= nNearCamDevID)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	if (0 >= nNearMonDevID)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "SELECT * FROM t_video_devmultilink WHERE fd_nearcamdevice_id=%d AND\
			fd_nearmondevice_id=%d", nNearCamDevID, nNearMonDevID);
		CppSQLite3Query Query = m_pdbInfo->execQuery(szSQL);
		if (Query.eof())
			return EC_ICV_CCTV_RECORDISNOTEXIST;

		Safe_CopyString(szNearMonChannel, Query.getStringField("fd_nearmonchannel_index"), sizeof(szNearMonChannel));
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}
	
// 说明参见.h文件
long CVideoDBLibrary::VideoUpdateLinkChannel(IN const long nNearCamDevID, IN const long nNearMonDevID, IN const char* szNearMonChannel)
{
	if (0 >= nNearCamDevID)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	if (0 >= nNearMonDevID)
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	if (0 == strcmp(szNearMonChannel, VIDEO_STRING_CHANNEL_INVALIDATION))
		return EC_ICV_CCTV_FUNCPARAMINVALID;

	try
	{
		char szSQL[VIDEO_SQL_MAX_SIZE];
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "DELETE FROM t_video_devmultilink WHERE fd_nearcamdevice_id=%d AND\
			fd_nearmondevice_id=%d", nNearCamDevID, nNearMonDevID);
		m_pdbInfo->execDML(szSQL);
		memset(szSQL, 0x00, VIDEO_SQL_MAX_SIZE);
		sprintf(szSQL, "INSERT INTO t_video_devmultilink (fd_nearcamdevice_id, fd_nearmondevice_id, fd_nearmonchannel_index)\
			VALUES (%d,%d,'%s')", nNearCamDevID, nNearMonDevID, szNearMonChannel);
		m_pdbInfo->execDML(szSQL);
	}
	catch(CppSQLite3Exception &e)
	{
		CVLog.LogMessage(LOG_LEVEL_ERROR, e.errorMessage());
		return e.errorCode();
	}

	return ICV_SUCCESS;
}
