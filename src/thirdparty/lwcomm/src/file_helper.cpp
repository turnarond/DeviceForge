/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: file_helper.cpp .
 *
 * Date: 2024-03-23
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#ifndef _WIN32
#include <dirent.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#else
#include <corecrt_io.h>
#include <direct.h>
#include <sys/stat.h>
/* POSIX compatibility macros for MSVC */
#define stat    _stat64
#define fstat   _fstat64
#define S_ISDIR(m)  (((m) & _S_IFDIR) != 0)
#define S_ISREG(m)  (((m) & _S_IFREG) != 0)
#define access  _access
#define mkdir(p, m) _mkdir(p)
#define rmdir   _rmdir
#define unlink  _unlink
#define getcwd  _getcwd
#define chdir   _chdir
#define F_OK    0
/* opendir/readdir/closedir → FindFirstFile/FindNextFile/FindClose
   handled via #ifdef in individual functions */
#endif
#include "lwcomm/lwcomm.h"
#include <algorithm>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <fstream>
#include <memory>

#define ICV_CHECK_RET(expr) \
    do { \
        int _ret = (expr); \
        if (_ret != 0) return _ret; \
    } while(0)

#define CC_STRING(x)    (x == NULL ? "" : x)

/*
 * The maximum length of the full path of the file 
 * (the actual length is limited by the operating system, 
 * the length of the folder in Windows is less than 249, 
 * and the length of the full path of the file name is less than 260)
 */
#define LW_SHORTFILENAME_MAXLEN 260  /* Short file name max length */
#define LW_LONGFILENAME_MAXLEN  1024 /* Long file name max length */

static char m_szWorkingDir[LW_LONGFILENAME_MAXLEN] = "";

/**
 *  ConCat directory .
 *
 *  @param  -[in]  char *  szDirName: [dir name,e.g. cc]
 *  @param  -[in]  char *  szParentDir: [parent dir name,e.g. /aa/bb/,
 *absolutely path]
 *  @param  -[in]  char *  szResultDir: [result dir name,e.g. /aa/bb/cc/]
 *  @param  -[in]  int  lResultDirBufLen: [result dir buffer length,e.g. c/d/f]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *  @version
 */
int LWFileHelper::ConCatDir (const char *szDirName, const char *szAbsParentDir, char *szResultDir, int lResultDirBufLen)
{
    if (szDirName == NULL || szAbsParentDir == NULL || szResultDir == NULL || lResultDirBufLen <= 0) {
        return EC_ICV_CC_PARAMINVALID;
    }
    int lRet = ConCatDirAndFile(szDirName, szAbsParentDir, szResultDir, lResultDirBufLen);
    if (lRet != LW_SUCCESS) {
        return lRet;
    }
        
    /*
     * result: d:/a/b/c/
     */
    if (szResultDir[strlen(szResultDir) - 1] != LW_OS_DIR_SEPARATOR_CHAR)
        strncat(szResultDir, LW_OS_DIR_SEPARATOR, sizeof(LW_OS_DIR_SEPARATOR)); 

    RegulatePathName(szResultDir, lResultDirBufLen);
    return lRet;
}

/**
 *  ConCat directory .
 *
 *  @param  -[in]  char *  szDirName: [dir name,e.g. cc]
 *  @param  -[in]  char *  szParentDir: [parent dir name,e.g. /aa/bb/,
 *absolutely path]
 *  @param  -[in]  char *  szResultDir: [result dir name,e.g. /aa/bb/cc/]
 *  @param  -[in]  int  lResultDirBufLen: [result dir buffer length,e.g. c/d/f]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *  @version
 */
int LWFileHelper::ConCatDirAndFile (const char *szFileNameIn, const char *szAbsParentDirIn, char *szResultPathName,
                                    int lResultDirBufLen)
{
    if (szFileNameIn == NULL || szAbsParentDirIn == NULL || szResultPathName == NULL || lResultDirBufLen <= 0) {
        return EC_ICV_CC_PARAMINVALID;
    }

    int lRet = LW_SUCCESS;
    char szFileName[LW_LONGFILENAME_MAXLEN] = {0};
    char szAbsParentDir[LW_LONGFILENAME_MAXLEN] = {0};

    if (szFileNameIn) {
        strcpy(szFileName, szFileNameIn);
    }
        
    if (szAbsParentDirIn) {
        strcpy(szAbsParentDir, szAbsParentDirIn);
    }

    if ((strlen(szAbsParentDir) + strlen(szFileName)) >= (size_t)lResultDirBufLen) {
        lRet = EC_ICV_CC_PATHNAMECONVERTFAIL;
        return lRet;
    }

    strncpy(szResultPathName, szAbsParentDir, lResultDirBufLen);
    if (strlen(szResultPathName) > 0) {
        strncat(szResultPathName, LW_OS_DIR_SEPARATOR, sizeof(LW_OS_DIR_SEPARATOR));
    }
    strncat(szResultPathName, szFileName, lResultDirBufLen - strlen(szResultPathName) - 1);

    RegulatePathName(szResultPathName, lResultDirBufLen);
    return lRet;
}

/**
 *  create directory recursively.
 *
 *  @param  -[in]  char *  szAbsDirName: [dir name,e.g. /c/d/f]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *  @version
 */
int LWFileHelper::CreateDir (const char *szAbsDirName)
{
    if (szAbsDirName == NULL) {
        return EC_ICV_CC_PARAMINVALID;
    }

    int lRet = LW_SUCCESS;
    std::string result_dir(szAbsDirName);
    /*
     * get result dirname, i.e. /aa/bb/cc/
     */
    lRet = RegulatePathName((char*)result_dir.c_str(), result_dir.length());
    if (lRet != LW_SUCCESS) {
        return lRet;
    }
        
    /*
     * before write file, affirm dir's existence
     */ 
    result_dir += LW_OS_DIR_SEPARATOR;

    std::string strDir = "";
    int nPos = 0;
    while (nPos >= 0) {
        /*
         * get first directory
         */ 
        nPos = result_dir.find(LW_OS_DIR_SEPARATOR);
        if (nPos < 0) {
            /* 
             * create all subpath, exit!
             */
            break; 
        }

        /*
         * create first directory
         */ 
        std::string strTemp = result_dir.substr(0, nPos);
        strDir += strTemp; 
        strDir += LW_OS_DIR_SEPARATOR;

        /*
         * if dir isn't exist, create dir
         */ 
        if (access(strDir.c_str(), F_OK) != 0) {
            if (mkdir(strDir.c_str(), 0755) == -1) {
                return EC_ICV_CC_FILENOTEXIST;
            }
                
        }

        /*
         * continue parsing std::string left
         */
        result_dir = result_dir.substr(nPos + 1, result_dir.length());
    }

    return lRet;
}

/**
 *  Rename directory.
 *
 *  @param  -[in]  char *  szDirName: [dir name,e.g. f]
 *  @param  -[in]  char *  szNewDirName: [new dir name,e.g. g]
 *  @param  -[in]  char *  szParentDir: [parent dir name,e.g. /aa/bb or /aa/bb,
 *absolutely path]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *  @version
 */
int LWFileHelper::RenameDir (const char *szOldDirName, const char *szNewDirName, const char *szAbsParentDir)
{
    if (szOldDirName == NULL || szNewDirName == NULL || szAbsParentDir == NULL) {
        return EC_ICV_CC_PARAMINVALID;
    }

    int lRet = LW_SUCCESS;
    char szOldResultDir[LW_LONGFILENAME_MAXLEN];

    /*
     * get result dirname, i.e. /aa/bb/cc/
     */
    lRet = ConCatDir(szOldDirName, szAbsParentDir, szOldResultDir, sizeof(szOldResultDir));
    if (lRet != LW_SUCCESS) {
        return lRet;
    }
        
    char szNewResultDir[LW_LONGFILENAME_MAXLEN];

    /*
     * get result dirname, i.e. /aa/bb/dd/
     */ 
    lRet = ConCatDir(szNewDirName, szAbsParentDir, szNewResultDir, sizeof(szNewResultDir));
    if (lRet != LW_SUCCESS) {
        return lRet;
    }
        
    /*
     * if dir isn't exist, create dir
     */ 
    if (!IsDirectory(szOldResultDir)) {
        lRet = EC_ICV_CC_FAILTORENAMEFILEPATH;
        return lRet;
    }

    if (rename(szOldResultDir, szNewResultDir) == -1) {
        return EC_ICV_CC_FAILTORENAMEFILEPATH;
    }

    return lRet;
}

LWCOMM_API int LWFileHelper::DeleteDir (const char * szDirName)
{
    if (szDirName == NULL) {
        return EC_ICV_CC_PARAMINVALID;
    }

    int lRet = LW_SUCCESS;

    if (!IsDirectory(szDirName)) {
        return DeleteAFile(szDirName, NULL);
    }

#ifdef _WIN32
    char szSearchPath[LW_LONGFILENAME_MAXLEN];
    snprintf(szSearchPath, sizeof(szSearchPath), "%s\\*", szDirName);
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(szSearchPath, &findData);
    if (hFind == INVALID_HANDLE_VALUE) return EC_ICV_CC_FAILTODELETEFILEPATH;

    char szFullPath[LW_LONGFILENAME_MAXLEN];
    do {
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0)
            continue;
        snprintf(szFullPath, sizeof(szFullPath), "%s\\%s", szDirName, findData.cFileName);
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            lRet = DeleteDir(szFullPath);
        } else {
            lRet = DeleteAFile(szFullPath, NULL);
        }
        if (lRet != LW_SUCCESS) { FindClose(hFind); return lRet; }
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);

    if (rmdir(szDirName) == -1) return EC_ICV_CC_FAILTODELETEFILEPATH;
    return LW_SUCCESS;
#else
    // Open the directory
    DIR *dirp = opendir(szDirName);
    if (dirp == NULL) {
        return EC_ICV_CC_FAILTODELETEFILEPATH;
    }

    struct dirent *dp;
    char szFullPath[LW_LONGFILENAME_MAXLEN];
    while ((dp = readdir(dirp)) != NULL) {
        // Skip the current and parent directory entries
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
            continue;
        }

        snprintf(szFullPath, sizeof(szFullPath), "%s%c%s", szDirName, LW_OS_DIR_SEPARATOR_CHAR, dp->d_name);

        struct stat st;
        if (stat(szFullPath, &st) == -1) {
            closedir(dirp);
            return EC_ICV_CC_PATHNAMECONVERTFAIL;
        }

        if (S_ISDIR(st.st_mode)) {
            // Recursively delete subdirectories
            lRet = DeleteDir(szFullPath);
            if (lRet != LW_SUCCESS) {
                closedir(dirp);
                return lRet;
            }
        } else {
            // Delete files
            lRet = DeleteAFile(szFullPath, NULL);
            if (lRet != LW_SUCCESS) {
                closedir(dirp);
                return lRet;
            }
        }
        rewinddir(dirp);
    }
    closedir(dirp);
    if (rmdir(szDirName) == -1) {
        return EC_ICV_CC_FAILTODELETEFILEPATH;
    }

    return LW_SUCCESS;
#endif /* _WIN32 */
}

// Helper function to check if a directory is empty
bool LWFileHelper::IsDirectoryEmpty (const char *szAbsDirName)
{
    if (szAbsDirName == NULL) {
        return false;
    }

    if (!IsDirectory(szAbsDirName)) {
        return false;
    }

#ifdef _WIN32
    char szSearchPath[LW_LONGFILENAME_MAXLEN];
    snprintf(szSearchPath, sizeof(szSearchPath), "%s\\*", szAbsDirName);
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(szSearchPath, &findData);
    if (hFind == INVALID_HANDLE_VALUE) return false;

    bool isEmpty = true;
    do {
        if (strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0) {
            isEmpty = false;
            break;
        }
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
    return isEmpty;
#else
    DIR *dirp = opendir(szAbsDirName);
    if (dirp == NULL) {
        return false;
    }

    struct dirent *dp;
    bool isEmpty = true;
    while ((dp = readdir(dirp)) != NULL) {
        if (strcmp(dp->d_name, ".") != 0 && strcmp(dp->d_name, "..") != 0) {
            isEmpty = false;
            break;
        }
    }

    closedir(dirp);
    return isEmpty;
#endif
}
/**
 *  delete a file
 *
 *  @param  -[in]  char *  szFileName: [file name to be deleted,e.g. c/d/f]
 *  @param  -[in]  char *  szParentDir: [parent dir name,e.g. /aa/bb or /aa/bb,
 *absolutely path]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *  @version
 */
int LWFileHelper::DeleteAFile (const char *szFileName, const char *szAbsParentDir)
{
    if (szFileName == NULL || szAbsParentDir == NULL || strlen(szFileName) == 0) {
        return EC_ICV_CC_PARAMINVALID;
    }

    int lRet = LW_SUCCESS;
    char szResultDir[LW_LONGFILENAME_MAXLEN];

    /*
     * get result dirname, i.e. /aa/bb/cc/
     */ 
    lRet = ConCatDirAndFile(szFileName, szAbsParentDir, szResultDir, sizeof(szResultDir));
    if (lRet != LW_SUCCESS) {
        return lRet;
    }

    if (unlink(szResultDir) == -1) {
        return EC_ICV_CC_FAILTODELETEFILE;
    }

    return lRet;
}

/*
 * Regulate path name
 */
int LWFileHelper::RegulatePathName (char *szPathName, int lPathNameLen)
{
    if (!szPathName || strlen(szPathName) == 0 || lPathNameLen <= 0) {
        return EC_ICV_CC_PATHNAMECONVERTFAIL;
    }

    std::string sPathName = szPathName;
    std::replace(sPathName.begin(), sPathName.end(), '/', LW_OS_DIR_SEPARATOR_CHAR);
    std::replace(sPathName.begin(), sPathName.end(), '\\', LW_OS_DIR_SEPARATOR_CHAR);
    strncpy(szPathName, sPathName.c_str(), lPathNameLen);

    return LW_SUCCESS;
}

/*
 * Check whether is a directory
 */
bool LWFileHelper::IsDirectory (const char *szAbsPathNameIn)
{
    if (!szAbsPathNameIn) return false;
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(szAbsPathNameIn);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
#else
    DIR* dirp = opendir(szAbsPathNameIn);
    if (dirp) {
        closedir(dirp);
        return true;
    }
    return false;
#endif
}

bool LWFileHelper::IsDirectoryExist (const char *szAbsPathNameIn)
{
    if (IsDirectory(szAbsPathNameIn)) {
        return true;
    }
        
    return false;
}

/**
 *  Check file exist.
 *
 *  @param  -[in]  char *  szFullPathName: [file name to be checked exist,e.g. /c/d/f]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *  @version
 */
bool LWFileHelper::IsFileExist (const char *szFullPathName)
{
    if (!szFullPathName) {
        return false;
    }

    return access(szFullPathName, F_OK) == 0;
}

/**
 *  Check file exist.
 *
 *  @param  -[in]  char *  szFileName: [file name to be checked exist ,e.g. c/d/f]
 *  @param  -[in]  char *  szParentDir: [parent dir name,e.g. /aa/bb or /aa/bb,
 *absolutely path]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *  @version
 */
bool LWFileHelper::IsFileExist (const char *szFileName, const char *szAbsParentDir)
{
    if (szFileName == NULL || szAbsParentDir == NULL) {
        return false;
    }

    char szResultDir[LW_LONGFILENAME_MAXLEN];

    /*
     * get result dirname, i.e. /aa/bb/cc/
     */
    int lRet = ConCatDirAndFile(szFileName, szAbsParentDir, szResultDir, sizeof(szResultDir));
    if (lRet != LW_SUCCESS) {
        return false;
    }

    return IsFileExist(szResultDir);
}

/*
 * Separate Path to path and filename. e.g:C:\\abc\\def\\test.dat--->
 * C:\\abc\\def\\ and test.dat
 */
int LWFileHelper::SeparatePathName (const char *szPathName, char *szPath, int lPathLen, char *szFileName, int NameLen)
{
    if (szPathName == NULL || szPath == NULL || szFileName == NULL || lPathLen <= 0 || NameLen <= 0) {
        return EC_ICV_CC_PARAMINVALID;
    }
    char szFullPathName[LW_LONGFILENAME_MAXLEN];
    strncpy(szFullPathName, szPathName, sizeof(szFullPathName));

    ICV_CHECK_RET(RegulatePathName(szFullPathName, sizeof(szFullPathName)));

    char *pBeginPos = szFullPathName;
    char *pCurPos = szFullPathName + strlen(szFullPathName);

    while (pCurPos != pBeginPos) {
        if (*pCurPos == LW_OS_DIR_SEPARATOR_CHAR) {
            pCurPos++;
            break;
        }
        pCurPos--;
    }

    strncpy(szFileName, pCurPos, NameLen - 1);
    szFileName[NameLen - 1] = '\0';

    strncpy(szPath, szFullPathName, pCurPos - pBeginPos);
    szPath[pCurPos - pBeginPos] = '\0';

    return LW_SUCCESS;
}

/**
 *  Write buffer to a designated file.
 *
 *  @param  -[in]  char*  pszFilePathName: [a relative path name of file]
 *  @param  -[in]  char*  pFileContent: [pointer to begin of file]
 *  @param  -[in]  int  lFileLen: [file's length]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version
 */
int LWFileHelper::WriteToFile (const char *pathName, const char *pFileContent, int lFileLen)
{
    if (lFileLen < 0) {
        return EC_ICV_CC_PARAMINVALID;
    }

    if (!pathName || (strcmp(pathName, "") == 0)) {
        return EC_ICV_CC_PARAMINVALID;
    }

    if (!pFileContent) {
        return EC_ICV_CC_PARAMINVALID;
    }

    char szPath[LW_LONGFILENAME_MAXLEN];
    char szFileName[LW_SHORTFILENAME_MAXLEN];
    ICV_CHECK_RET(SeparatePathName(pathName, szPath, sizeof(szPath), szFileName, sizeof(szFileName)));

    ICV_CHECK_RET(CreateDir(szPath));

    FILE *pFile = NULL;
    pFile = fopen(pathName, "wb");
    if (!pFile) {
        return EC_ICV_CC_CANNOTWRITEFILE;
    }

    int nNumWritten = fwrite(pFileContent, sizeof(char), lFileLen, pFile);
    if (nNumWritten != lFileLen) {
        fclose(pFile);
        return EC_ICV_CC_CANNOTWRITEFILE;
    }

    fclose(pFile);

    return LW_SUCCESS;
}

static int filesize (int filedes)
{
    struct stat st;
    if (fstat(filedes, &st) < 0) {
        return -1;
    }

    return st.st_size;
}

int LWFileHelper::ReadFile (const char *pFileName, char **pFileContent, int *lFileSize)
{
    if ((NULL == pFileName) || (NULL == pFileContent) || (NULL == lFileSize)) {
        return EC_ICV_CC_PARAMINVALID;
    }

    FILE *pFile = fopen(pFileName, "rb");
    if (!pFile) {
        return EC_ICV_CC_FILENOTEXIST;
    }

    if (filesize(fileno(pFile)) <= 0) {
        fclose(pFile);
        return EC_ICV_CC_FILEEMPTY;
    } else {
        /*
         * read the file into buffer
         */ 
        fseek(pFile, 0L, SEEK_END);
        *lFileSize = ftell(pFile);
        *pFileContent = new char[*lFileSize + 1];

        fseek(pFile, 0L, SEEK_SET);
        int nRead = fread(*pFileContent, sizeof(char), *lFileSize, pFile);
        if (nRead != *lFileSize) {
            fclose(pFile);
            delete[] * pFileContent;
            return EC_ICV_CC_FILEREADFAILED;
        }

        fclose(pFile);
    }
    return LW_SUCCESS;
}

int LWFileHelper::ReadFile(const char *pFileName, std::vector<char>& buffer)
{
    std::ifstream file(pFileName, std::ios::binary | std::ios::ate);
    if (!file) return EC_ICV_CC_FILENOTEXIST;

    auto size = file.tellg();
    if (size == 0) return EC_ICV_CC_FILEEMPTY;

    buffer.resize(size);
    file.seekg(0, std::ios::beg);
    if (!file.read(buffer.data(), size)) {
        buffer.clear();
        return EC_ICV_CC_FILEREADFAILED;
    }
    return LW_SUCCESS;
}

/**
 *	convert relative file pathname to full(absolute) file path name.
 *
 *  @param  -[in]  char*  pszRelativePathName: [relative path name]
 *  @param  -[out]  char*  pszFullPathName: [absolute path name]
 *  @param  -[in]  int  lFullPathSize: [size of full pathname 's buffer]
 *  @return
 *	- ==0 success
 *	- !=0 failure
 *
 *  @version
 */
int LWFileHelper::RelativeToFullPathName (const char *pszRelativePathName, char *pszFullPathName, int lFullPathSize)
{
    int lRet = LW_SUCCESS;
    /*
     * Since it is possible to convert only the file path, 
     * at this point pszRelativePathName may be an empty string, 
     * so an empty std::string is allowed
     */
    if (!pszRelativePathName || (lFullPathSize <= 0) || !pszFullPathName) {
        lRet = EC_ICV_CC_PATHNAMECONVERTFAIL;
        return lRet;
    }

    char szTmpPathName[LW_LONGFILENAME_MAXLEN];
    memset(szTmpPathName, 0, sizeof(szTmpPathName));

    /*
     * Obtain the path to the centralized configuration service
     */
    ICV_CHECK_RET(GetWorkingDirectory(szTmpPathName, sizeof(szTmpPathName)));
    ICV_CHECK_RET(ConCatDirAndFile(pszRelativePathName, szTmpPathName, pszFullPathName, lFullPathSize));

    return LW_SUCCESS;
}

int LWFileHelper::RelativeToFullPathNameEx (const char *pszRelativePathName, const char *pszParentPath,
                                            char *pszFullPathName, int lFullPathSize)
{
    int lRet = LW_SUCCESS;

    if (!pszRelativePathName || !pszParentPath || (pszParentPath[0] == 0) || (lFullPathSize <= 0)) {
        lRet = EC_ICV_CC_PATHNAMECONVERTFAIL;
        return lRet;
    }

    ICV_CHECK_RET(ConCatDirAndFile(pszRelativePathName, pszParentPath, pszFullPathName, lFullPathSize));

    return LW_SUCCESS;
}

int LWFileHelper::GetWorkingDirectory (char *szWorkDirBuff, int lWorkDirBuffLen)
{
    int lRet = LW_SUCCESS;
    if (!szWorkDirBuff || (lWorkDirBuffLen <= 0)) {
        lRet = EC_ICV_CC_PATHNAMECONVERTFAIL;
        return lRet;
    }
    memset(szWorkDirBuff, 0, lWorkDirBuffLen);

    if (strcmp(m_szWorkingDir, "") != 0) {
        strncpy(szWorkDirBuff, m_szWorkingDir, lWorkDirBuffLen);
    } else {
        if (!getcwd(szWorkDirBuff, lWorkDirBuffLen)) {
            return EC_ICV_CC_FAILTOGETCWD;
        }
    }
    return lRet;
}

int LWFileHelper::SetWorkingDirectory (const char *szWorkDir)
{
    if (szWorkDir == NULL) {
        return EC_ICV_CC_PARAMINVALID;
    }
    if (chdir(szWorkDir) == -1) {
        return EC_ICV_CC_FILENOTEXIST;
    }
    strncpy(m_szWorkingDir, szWorkDir, sizeof(m_szWorkingDir));
    return LW_SUCCESS;
}

bool LWFileHelper::IsAbsFilePath (std::string strFilePath)  
{
    bool bIsAbsolutePath = false;
#ifdef _WIN32
    if ((strFilePath.length() >= 2) && (strFilePath.at(1) == ':')) {
        /*
         * D:/ c:/
         */ 
        bIsAbsolutePath = true;
    }
#else
    if ((strFilePath.length() >= 1) && (strFilePath.at(0) == '/')) {
        bIsAbsolutePath = true;
    }
        
#endif
    return bIsAbsolutePath;
}

int LWFileHelper::ListDirEntry(const char *szAbsParentDir, dir_entry_set &dirEntrySet)
{
    if (!szAbsParentDir || szAbsParentDir[0] == '\0') {
        return EC_ICV_CC_FILEPATHNOTEXIST;
    }

    if (!IsDirectoryExist(szAbsParentDir)) {
        return EC_ICV_CC_FILEPATHNOTEXIST;
    }

    // 清空旧数据
    dirEntrySet.clear();

#ifdef _WIN32
    std::string searchPath = std::string(szAbsParentDir) + "\\*";
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return EC_ICV_CC_FILEPATHNOTEXIST;

    do {
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0)
            continue;
        dir_entry de;
        de.name = findData.cFileName;
        de.is_directory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        dirEntrySet.add(std::move(de));
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
#else
    struct DirDeleter { void operator()(DIR* d) { if (d) closedir(d); } };
    std::unique_ptr<DIR, DirDeleter> dir(opendir(szAbsParentDir));
    if (!dir) {
        return EC_ICV_CC_FILEPATHNOTEXIST;
    }

    dirent *entry;
    while ((entry = readdir(dir.get())) != nullptr) {
        const char* name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        dir_entry de;
        de.name = name;
        de.is_directory = IsDirectory((std::string(szAbsParentDir) + LW_OS_DIR_SEPARATOR + name).c_str());
        dirEntrySet.add(std::move(de));
    }
#endif

    return LW_SUCCESS;
}

/*
 * list files in directory
 */
int LWFileHelper::ListFilesInDir(const char *szAbsParentDir, std::list<std::string> &fileNameList)
{
    if (!szAbsParentDir) return EC_ICV_CC_PARAMINVALID;
    dir_entry_set dirSet;
    int ret = ListDirEntry(szAbsParentDir, dirSet);
    if (ret != LW_SUCCESS) {
        return ret;
    }

    fileNameList.clear();

    // 使用范围遍历（现代 C++）
    for (const auto& entry : dirSet) {
        if (!entry.is_directory) {
            fileNameList.push_back(entry.name);
        }
    }

    return LW_SUCCESS;
}

/*
 * end
 */