/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: comm_impl.cpp .
 *
 * Date: 2024-03-23
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#include "comm_impl.h"
#include "lwcomm/lwcomm.h"
#include <string.h>
#include <stdio.h>
#ifndef _WIN32
#include <unistd.h>
#include <dirent.h>
#endif

#define LW_BIN_DIR_NAME "bin"
#define LW_LIB_DIR_NAME "lib"
#define LW_MODEL_DIR_NAME "model"
#define LW_CONFIG_DIR_NAME "config"
#define LW_LOG_DIR_NAME "logs"
#define LW_DATA_DIR_NAME "data"
#define LW_IMAGE_DIR_NAME "image"

#define _(STRING) STRING
#define ICV_NIL_RM_SEQUENCE -1
#define ICV_MAIN_RM_SEQUENCE 0

#ifdef _WIN32
#endif //#ifdef _WIN32

#define CVCOMM_CHECK_RETURN_NULL(x)  \
    {                                \
        if (!x) {                    \
            return NULL;             \
        }                            \
    }

#define CVCOMM_STRING(x)       (x)


#define CVCOMM_GET_FUNC_DEF(name, opt)    \
const char *CLWCommImpl::Get##name##Path (void)               \
{                                                             \
    static char szCVCfgPath[LW_LONGFILENAME_MAXLEN] = {0};    \
    if (szCVCfgPath[0] != '\0') {                             \
        return szCVCfgPath;                                   \
    }                                                         \
                                                              \
    if (szCVCfgPath[0] == '\0') {                             \
        const char *pszHomePath = GetHomePath();              \
        if (pszHomePath[0] != '\0') {                         \
            std::string strCVCfgPath;                         \
            strCVCfgPath = std::string(pszHomePath) + LW_OS_DIR_SEPARATOR + (opt); \
            snprintf(szCVCfgPath, sizeof(szCVCfgPath), "%s", strCVCfgPath.c_str()); \
        }                                                     \
    }                                                         \
                                                              \
    return szCVCfgPath;                                       \
}

/*
 * Get configuration path.
 */
CVCOMM_GET_FUNC_DEF(Config, LW_CONFIG_DIR_NAME)
/*
 * Get binary path.
 */
CVCOMM_GET_FUNC_DEF(Bin, LW_BIN_DIR_NAME)
/*
 * Get lib path.
 */
CVCOMM_GET_FUNC_DEF(Lib, LW_LIB_DIR_NAME)
/*
 * Get model path.
 */
CVCOMM_GET_FUNC_DEF(Model, LW_MODEL_DIR_NAME)
/*
 * Get data path.
 */
CVCOMM_GET_FUNC_DEF(Data, LW_DATA_DIR_NAME)
/*
 * Get image path.
 */
CVCOMM_GET_FUNC_DEF(Image, LW_IMAGE_DIR_NAME)
/*
 * Get log path.
 */
CVCOMM_GET_FUNC_DEF(Log, LW_LOG_DIR_NAME)
/*
 * Get runtime data path.
 */
const char *CLWCommImpl::GetRunTimeDataPath (void)
{
    static char szRuntimeDataPath[LW_LONGFILENAME_MAXLEN] = {0};

    char szTmp[LW_LONGFILENAME_MAXLEN] = {0};
    const char *szHomePath = GetHomePath();
    snprintf(szTmp, sizeof(szTmp), "%s%s%s", szHomePath, LW_OS_DIR_SEPARATOR, LW_DATA_DIR_NAME);
    strcpy(szRuntimeDataPath, szTmp);
    return szRuntimeDataPath;
}

/*
 * Get pid path on QNX.
 */
#ifdef  __QNX__
static bool GetPidFromName (char *homePath)
{
    DIR *dir;
    struct dirent *ptr;
    FILE *fp;
    char filePath[50];
    char curTaskName[50];
    char buf[512];
    dir = opendir("/proc");
    if (NULL != dir) {
        while ((ptr = readdir(dir)) != NULL) {
            if ((strcmp(ptr->d_name, ".") == 0) || (strcmp(ptr->d_name, "..") == 0)) {
                continue;
            }

            memset(buf, 0, 512 * sizeof(char));
            if (sscanf(ptr->d_name, "%[^0-9]",buf)) {
                continue;
            }

            sprintf(filePath, "/proc/%s/exefile", ptr->d_name);
            fp = fopen(filePath, "r");
            if (NULL != fp) {
                memset(buf, 0, sizeof(buf));
                if (fgets(buf, sizeof(buf), fp) == NULL) {
                    fclose(fp);
                    continue;
                }
                char *pch = strrchr(buf, '/');
                if (pch) {
                    strcpy(curTaskName, pch + 1);
                    strncpy(homePath, buf, pch - buf);
                    homePath[pch - buf + 1] = '\0';
                } else {
                    strcpy(curTaskName, buf);
                }
                if (!strcmp("vsoa_master",curTaskName)) {
                    fclose(fp);
                    closedir(dir);
                    return true;
                }
                fclose(fp);
            }
        }
        closedir(dir);
    }
    return false;
}
#endif

/*
 * Get home path.
 */
const char *CLWCommImpl::GetHomePath (void)
{
    static char szHomePath[LW_LONGFILENAME_MAXLEN] = {0};

    if (szHomePath[0] != '\0') {
        return szHomePath;
    }

    char szExePath[LW_LONGFILENAME_MAXLEN] = {0};
#if defined SYLIXOS
    pid_t pid = getpid();
    char szExefile[LW_SHORTFILENAME_MAXLEN] = {0};
    sprintf(szExefile, "/proc/%d/exe", pid);
    readlink(szExefile, szExePath, sizeof(szExePath));
#elif defined __QNX__
    readlink("/proc/self/exefile", szExePath, sizeof(szExePath));
    GetPidFromName(szExePath);
#elif defined _WIN32
    GetModuleFileName(NULL, szExePath, sizeof(szExePath));
#else
    readlink("/proc/self/exe", szExePath, sizeof(szExePath));
#endif
    std::string strPath = szExePath;

    std::string toFind = std::string(LW_OS_DIR_SEPARATOR) + std::string(LW_BIN_DIR_NAME) + std::string(LW_OS_DIR_SEPARATOR); 
    std::size_t pos = strPath.rfind(toFind);
    if (pos != std::string::npos) {
        strPath = strPath.substr(0, pos);
    } else {
        std::size_t pos = strPath.rfind(LW_OS_DIR_SEPARATOR);
        if (pos != std::string::npos) {
            strPath = strPath.substr(0, pos);
        }
    }

    strncpy(szHomePath, strPath.c_str(), sizeof(szHomePath));

    return szHomePath;
}

/*
 * Get process path.
 */
const char *CLWCommImpl::GetProcessName (void)
{
    static char szProcessName[LW_LONGFILENAME_MAXLEN] = {0};

    if (szProcessName[0] != '\0') {
        return szProcessName;
    }        

    char szExePath[LW_LONGFILENAME_MAXLEN] = {0};
#if defined SYLIXOS
    pid_t pid = getpid();
    char szExefile[LW_SHORTFILENAME_MAXLEN] = {0};
    sprintf(szExefile, "/proc/%d/exe", pid);
    readlink(szExefile, szExePath, sizeof(szExePath));
#elif defined _WIN32
    GetModuleFileName(NULL, szExePath, sizeof(szExePath));
#else
    readlink("/proc/self/exe", szExePath, sizeof(szExePath));
#endif

    std::vector<std::string> vecPathPart = LWStringHelper::StriSplit(szExePath, LW_OS_DIR_SEPARATOR);
    if (vecPathPart.size() <= 0) {
        return szProcessName;
    }
        
    std::string strExeNameWithExt = vecPathPart[vecPathPart.size() - 1];
    std::string strExeName = "";

    if (strExeNameWithExt.find(".") != std::string::npos) {
        std::vector<std::string> vecExeExt = LWStringHelper::StriSplit(strExeNameWithExt, "-");
        if (vecExeExt.size() <= 1) {
            vecExeExt = LWStringHelper::StriSplit(strExeNameWithExt, ".");
            if (vecExeExt.size() <= 0) {
                return szProcessName;
            }                
        }
        strExeName = vecExeExt[0];
    } else {
        strExeName = strExeNameWithExt;
    }        

    strcpy(szProcessName, strExeName.c_str());
    return szProcessName;
}

/*
 * Sleep
 */
void CLWCommImpl::Sleep (unsigned int nMilSeconds)
{
#ifndef _WIN32
    usleep(nMilSeconds * 1000);
#else
    ::Sleep(nMilSeconds);  // 调用 Win32 API，避免与类方法自身递归
#endif
}

/*
 * Constructure
 */
CLWCommImpl::CLWCommImpl (void)
{
}

/*
 * end
 */
