#ifndef MM_SVR_AUTH_LOCAL_DATAACCESS_H_
#define MM_SVR_AUTH_LOCAL_DATAACCESS_H_

#ifdef _WIN32
#define MM_AUTH_API extern "C" __declspec(dllexport)
#ifndef API_CALL_METHOD
#define API_CALL_METHOD	
#endif
#else
#define MM_AUTH_API
#define API_CALL_METHOD
#endif

/**
 *  初始化资源.
 *
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     09/10/2008  fengjuanyong  Initial Version.
 */
MM_AUTH_API long API_CALL_METHOD InitLockAuth();

/**
 *  释放资源.
 *
 *  @return
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     09/10/2008  fengjuanyong  Initial Version.
 */
MM_AUTH_API long API_CALL_METHOD ExitLockAuth();

/**
 *  获取用户信息.
 *
 *  @param  -[in]  char*  szUserName: [用户名]
 *  @param  -[in]    char*szLoginGroupName: [群组名]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     09/16/2008  fengjuanyong  Initial Version.
 */
MM_AUTH_API long API_CALL_METHOD GetCurrentUserInfo(char* szUserName, char* szLoginGroup);

/**
 *  获取所有权限项.
 *
 *  @param  -[in]  char*  szItemInfo: [权限项列表]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     09/16/2008  fengjuanyong  Initial Version.
 */
MM_AUTH_API long API_CALL_METHOD GetAllAuthItem(char* szItemInfo);

/**
 *  验证用户权限.
 *
 *  @param  -[in]  char*  szUserName: [用户名]
 *  @param  -[in]    char*szLoginGroupName: [群组名]
 *  @param  -[in]  char*  szAuthLabel: [资源标签]
 *  @param  -[out]  long*  plAuth: [权限]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     09/16/2008  fengjuanyong  Initial Version.
 */
MM_AUTH_API long API_CALL_METHOD VerifyUserRight(char *szUserName, char*szLoginGroupName, char *szAuthLabel, long *plAuth);  

/**
 *  设置自动锁定时间.
 *
 *  @param  -[in]  long  lAutoTimeOut: [自动锁定时间]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     09/10/2008  fengjuanyong  Initial Version.
 */
MM_AUTH_API long API_CALL_METHOD SetAutoUnLockTime(long lAutoTimeOut);

/**
 *  用户自动锁定对象.
 *
 *  @param  -[in]  char*  szUserName: [用户名]
 *  @param  -[in]    char*szLoginGroupName: [群组名]
 *  @param  -[in]  long  lObjID: [对象ID]
 *  @param  -[in]  long  lObjType: [对象类型]
 *  @param  -[in]  char*  szAuthLabel: [资源名]
 *  @return 0-锁定对象 
 *			1-无权限，不能锁定
 *			2-有权限，已被优先级更高的人锁定
 *			3-有权限，已被优先级相同的人锁定.
 *
 *  @version     09/16/2008  fengjuanyong  Initial Version.
 */
MM_AUTH_API long API_CALL_METHOD AcquireLock(char *szUserName, char*szLoginGroupName, long lObjID, long lObjType, char *szAuthLabel);  

/**
 *  用户手动锁定对象.
 *
 *  @param  -[in]  char*  szUserName: [用户名]
 *  @param  -[in]    char*szLoginGroupName: [群组名]
 *  @param  -[in]  long  lObjID: [对象ID]
 *  @param  -[in]  long  lObjType: [对象类型]
 *  @param  -[in]  char*  szAuthLabel: [资源名]
 *  @param  -[in]  long  lManualTimeOut: [超时]
 *  @return 0-锁定对象 
 *			1-无权限，不能锁定
 *			2-有权限，已被优先级更高的人锁定
 *			3-有权限，已被优先级相同的人锁定.
 *
 *  @version     09/16/2008  fengjuanyong  Initial Version.
 */
MM_AUTH_API long API_CALL_METHOD ManualAcquireLock(char *szUserName, char*szLoginGroupName, long lObjID, long lObjType, char *szAuthLabel, long lManualTimeOut); 

/**
 *  用户手动释放对象.
 *
 *  @param  -[in]  char*  szUserName: [用户名]
 *  @param  -[in]    char*szLoginGroupName: [群组名]
 *  @param  -[in]  long  lObjID: [对象ID]
 *  @param  -[in]  long  lObjType: [对象类型]
 *  @param  -[in]  char*  szAuthLabel: [资源名]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     09/16/2008  fengjuanyong  Initial Version.
 */
MM_AUTH_API long API_CALL_METHOD ManualReleaseLock(char *szUserName, char*szLoginGroupName, long lObjID, long lObjType, char *szAuthLabel);

/**
 *  获取用户等级.
 *
 *  @param  -[in]  char *szUserName: [用户名]
 *  @param  -[in,out]  long &lLevel: [等级]
 *  @return long.
 *		- ==0 成功
 *		- !=0 出现异常
 *
 *  @version     01/18/2011  fengjuanyong  Initial Version.
 */
MM_AUTH_API long API_CALL_METHOD GetUserPRI(char *szUserName, char *szLoginGroupName, long &lLevel);
#endif // MM_SVR_AUTH_LOCAL_DATAACCESS_H_


