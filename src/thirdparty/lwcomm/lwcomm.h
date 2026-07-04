/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: lwcomm.h .
 *
 * Date: 2024-03-09
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#pragma once

#ifdef _WIN32
#include <winsock2.h>  // 必须在 windows.h 之前，避免与旧 winsock.h 冲突
#include <ws2tcpip.h>  // 提供 INET_ADDRSTRLEN, inet_ntop 等
#include <windows.h>   // 必须在 STL 头文件和 "using namespace std;" 之前
#ifdef lwcomm_EXPORTS
#define LWCOMM_API __declspec(dllexport)
#else
#define LWCOMM_API __declspec(dllimport)
#endif
#else /* _WIN32 */
#define LWCOMM_API __attribute__((visibility("default")))
#include <sys/types.h>
#endif /* _WIN32 */

#include <list>
#include <string>
#include <time.h>
#include <vector>

#if __cplusplus >= 201703L || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
  #define LWFS_HAS_CPP17 1
  #include <string_view>
#else
  #define LWFS_HAS_CPP17 0
#endif

#ifndef LW_SUCCESS
#define LW_SUCCESS 0
#endif

#define SAFE_FREE(p) if (p) { \
                         free(p); \
                         p = NULL;\
                     }

#ifndef MAX_PATH
#define MAX_PATH         260
#endif

#define LW_LONGFILENAME_MAXLEN  1024
#define LW_SHORTFILENAME_MAXLEN 260

#ifdef _WIN32
#define LW_OS_DIR_SEPARATOR "\\"
#else
#define LW_OS_DIR_SEPARATOR "/"
#endif

#ifdef _WIN32
#define LW_OS_DIR_SEPARATOR_CHAR '\\'
#else
#define LW_OS_DIR_SEPARATOR_CHAR '/'
#endif

/* Mutex and spinlock definitions */
#ifdef _WIN32
#include <windows.h>
#define hmutex_t CRITICAL_SECTION
#define hmutex_init(mtx) (InitializeCriticalSectionEx(mtx, 8192, CRITICAL_SECTION_NO_DEBUG_INFO) ? 0 : -1)
#define hmutex_destroy(mtx) DeleteCriticalSection(mtx)
#define hmutex_lock(mtx) EnterCriticalSection(mtx)
#define hmutex_unlock(mtx) LeaveCriticalSection(mtx)
#define hmutex_trylock(mtx) (TryEnterCriticalSection(mtx) ? VEOK : VEFAIL)

#define hspin_t CRITICAL_SECTION
#define hspin_init(spin) (InitializeCriticalSectionEx(spin, 81920, CRITICAL_SECTION_NO_DEBUG_INFO) - 1)
#define hspin_destroy(spin) DeleteCriticalSection(spin)
#define hspin_lock(spin) EnterCriticalSection(spin)
#define hspin_unlock(spin) LeaveCriticalSection(spin)
#define hspin_trylock(spin) (TryEnterCriticalSection(spin) - 1)
#else
#include <pthread.h>
#include <sys/time.h>  
#define hmutex_t pthread_mutex_t
#define hmutex_init(mtx) (pthread_mutex_init(mtx, 0) ? -1 : 0)
#define hmutex_destroy(mtx) (pthread_mutex_destroy(mtx) ? -1 : 0)
#define hmutex_lock(mtx) pthread_mutex_lock(mtx)
#define hmutex_unlock(mtx) pthread_mutex_unlock(mtx)
#define hmutex_trylock(mtx) (pthread_mutex_trylock(mtx) ? -1 : 0)

#define hspin_t pthread_spinlock_t
#define hspin_init(spin) pthread_spin_init(spin, 0)
#define hspin_destroy(spin) pthread_spin_destroy(spin)
#define hspin_lock(spin) pthread_spin_lock(spin)
#define hspin_unlock(spin) pthread_spin_unlock(spin)
#define hspin_trylock(spin) pthread_spin_trylock(spin)
#endif

#define EC_ICV_CC_PATHNAMETOOLLONG          11201                /* Path name too long */
#define EC_ICV_CC_FAILTOGETCWD              11202                /* Fail to get current working directory */
#define EC_ICV_COMM_INVALIDHEXSTR           11203                /* Invalid hex string */
#define EC_ICV_CC_FILENOTEXIST              11204                /* File not exist */
#define EC_ICV_COMM_BUFFERTOOSHORT          11205                /* Buffer too short */
#define EC_ICV_CC_PARAMINVALID              11210                /* Invalid parameter */
#define EC_ICV_CC_FAILTOCREATEFILEPATH      11233                /* Fail to create directory */
#define EC_ICV_CC_FAILTODELETEFILEPATH      11234                /* Fail to delete directory */
#define EC_ICV_CC_FAILTORENAMEFILEPATH      11235                /* Fail to rename directory */
#define EC_ICV_CC_FAILTODELETEFILE          11236                /* Fail to remove file */
#define EC_ICV_CC_FILEPATHNOTEXIST          11237                /* File path not exist */
#define EC_ICV_CC_PATHNAMECONVERTFAIL       11250                /* Fail to convert path name */
#define EC_ICV_CC_CANNOTWRITEFILE           11251                /* Can not write file */
#define EC_ICV_CC_FILEEMPTY                 11252                /* File empty */
#define EC_ICV_CC_FILEREADFAILED            11253                /* Fail to read file */

class LWComm
{
private:
    /* Private constructor */
    LWComm(void);

    public:
    /* Destructor */
    ~LWComm();

    /* Get home directory path */
    LWCOMM_API static const char *GetHomePath(void);

    /* Get configuration directory path */
    LWCOMM_API static const char *GetConfigPath(void);

    /* Get binary directory path */
    LWCOMM_API static const char *GetBinPath(void);

    /* Get library directory path */
    LWCOMM_API static const char *GetLibPath(void);

    /* Get model directory path */
    LWCOMM_API static const char *GetModelPath(void);

    /* Get data directory path */
    LWCOMM_API static const char *GetDataPath(void);

    /* Get image directory path */
    LWCOMM_API static const char *GetImagePath(void);

    /* Get current process name */
    LWCOMM_API static const char *GetProcessName(void);

    /* Get log directory path */
    LWCOMM_API static const char *GetLogPath(void);

    /* Get runtime data directory path */
    LWCOMM_API static const char *GetRunTimeDataPath(void);

    /* Sleep with millisecond precision */
    LWCOMM_API static void Sleep(unsigned int ms);
};

class LWFileHelper
{
public:

struct dir_entry {
    bool is_directory;
    std::string name;
};
class dir_entry_set  {
private:
    std::vector<dir_entry> entries;
public:
    /* Add directory entry */
    void add(const dir_entry& e) { entries.push_back(e); }

    /* Get number of entries */
    size_t size() const { return entries.size(); }
    
    /* Get entry at index (with bounds check) */
    const dir_entry& at(size_t i) const { return entries.at(i); }

    /* Get entry at index (returns nullptr if out of bounds) */
    const dir_entry* get(size_t i) const { 
        return i < entries.size() ? &entries[i] : nullptr; 
    }

    /* Get entry at index (may be out-of-bounds, but consistent with STL style) */
    const dir_entry& operator[](size_t index) const {
        return entries[index];
    }

    /* Get begin iterator */
    auto begin() const -> decltype(entries.begin()) {
        return entries.begin();
    }
    /* Get end iterator */
    auto end() const -> decltype(entries.end()) {
        return entries.end();
    }

    friend int ListDirEntry(const char*, dir_entry_set&);

    /* Clear all entries */
    void clear() { entries.clear(); }

    /* Default constructor */
    dir_entry_set() = default;

    /* Default destructor */
    ~dir_entry_set() = default;
};


    /* Write content to file */
    LWCOMM_API static int WriteToFile(const char *szAbsPathName, const char *pFileContent, int lFileLen);

    /* Concatenate directory paths */
    LWCOMM_API static int ConCatDir(const char *szDirName, const char *szAbsParentDir, char *szResultDir,
                                    int lResultDirBufLen);

    /* Concatenate directory and file paths */
    LWCOMM_API static int ConCatDirAndFile(const char *szFileName, const char *szAbsParentDir, char *szResultPathName,
                                           int lResultDirBufLen);

    /* Create directory recursively */
    LWCOMM_API static int CreateDir(const char *szAbsDirPath);

    /* Check if directory is empty */
    LWCOMM_API static bool IsDirectoryEmpty (const char *szAbsDirName);

    /* Rename a directory */
    LWCOMM_API static int RenameDir(const char *szOldDirName, const char *szNewDirName, const char *szAbsParentDir);

    /* Check if pathname is a directory */
    LWCOMM_API static bool IsDirectoryExist(const char *szAbsDir);

    /* Delete directory recursively */
    LWCOMM_API static int DeleteDir(const char * szAbsParentDir);

    /* List subdirectories */
    LWCOMM_API static int ListSubDir(const char *szAbsParentDir, std::list<std::string> &dirNameList);

    /* List files in directory */
    LWCOMM_API static int ListFilesInDir(const char *szAbsParentDir, std::list<std::string> &fileNameList);

    /* Delete a file */
    LWCOMM_API static int DeleteAFile(const char *szFileName, const char *szAbsParentDir);

    /* Copy directory recursively */
    LWCOMM_API static int CopyDir(const char *szAbsSrcFilePath, const char *szAbsDsFilePath);

    /* Copy a file */
    LWCOMM_API static int CopyAFile(const char *szAbsSrcFilePath, const char *szAbsDsFilePath);

    /* Read file (caller must free pFileContent) */
    LWCOMM_API static int ReadFile(const char *pFileName, char **pFileContent, int *lFileSize);

    /* Read file into vector buffer */
    LWCOMM_API static int ReadFile(const char *pFileName, std::vector<char>& buffer);

    /* Check if file exists */
    LWCOMM_API static bool IsFileExist(const char *szFileName, const char *szAbsParentDir);

    /* Check if file exists (full path) */
    LWCOMM_API static bool IsFileExist(const char *szFullPathName);

    /* Normalize file path separators */
    LWCOMM_API static int RegulatePathName(char *szPathName, int lPathNameLen);

    /* Check if pathname is a directory */
    LWCOMM_API static bool IsDirectory(const char *szAbsPathNameIn);

    /* Convert relative pathname to full pathname */
    LWCOMM_API static int RelativeToFullPathName(const char *pszRelativePathName, char *pszFullPathName,
                                                 int lFullPathSize);

    /* Convert relative pathname to full pathname with parent path */
    LWCOMM_API static int RelativeToFullPathNameEx(const char *pszRelativePathName, const char *pszParentPath,
                                                   char *pszFullPathName, int lFullPathSize);

    /* Get current working directory */
    LWCOMM_API static int GetWorkingDirectory(char *szWorkDirBuff, int lWorkDirBuffLen);

    /* Set current working directory */
    LWCOMM_API static int SetWorkingDirectory(const char *szWorkDirBuff);

    /* Separate pathname into path and filename */
    LWCOMM_API static int SeparatePathName(const char *szPathName, char *szPath, int lPathLen, char *szName,
                                           int NameLen);

    /* Check if file path is absolute */
    LWCOMM_API static bool IsAbsFilePath(std::string strFilePath);

    /* List directory entries */
    LWCOMM_API static int ListDirEntry(const char *szAbsParentDir, dir_entry_set &dirEntrySet);
};

#if LWFS_HAS_CPP17
class LWFileSystem
{
public:
    struct Entry  {
        bool is_directory;
        std::string name;
        std::string path;                               /* Full path */
    };

    /* Write content to file */
    LWCOMM_API static bool WriteToFile(std::string_view file_path, std::string_view content);

    /* Write binary data to file */
    LWCOMM_API static bool WriteToFile(std::string_view path, const std::vector<char>& data);

    /* Read file content */
    LWCOMM_API static std::vector<char> ReadFile(std::string_view path);

    /* Create an empty file */
    LWCOMM_API static bool CreateFile(std::string_view path);

    /* Delete a file */
    LWCOMM_API static bool DeleteFile(std::string_view path);

    /* Check if file exists */
    LWCOMM_API static bool FileExist(std::string_view path);

    /* Check if path is a file */
    LWCOMM_API static bool IsFile(std::string_view path);

    /* Create directory */
    LWCOMM_API static bool CreateDirectory(std::string_view path, bool recursive = true);

    /* Remove directory */
    LWCOMM_API static bool RemoveDirectory(std::string_view path, bool recursive = true);

    /* Rename path */
    LWCOMM_API static bool RenamePath(std::string_view oldPath, std::string_view newPath);

    /* Check if path is a directory */
    LWCOMM_API static bool IsDirectory(std::string_view path);

    /* Check if directory is empty */
    LWCOMM_API static bool IsDirectoryEmpty(std::string_view path);

    /* Concatenate paths */
    LWCOMM_API static std::string ConcatPath(std::string_view parent, std::string_view child);

    /* List directory entries */
    LWCOMM_API static std::vector<Entry> ListDirectory(std::string_view path);

    /* Get absolute path */
    LWCOMM_API static std::string AbsolutePath(std::string_view path);

    /* Get current working directory */
    LWCOMM_API static std::string CurrentWorkingDirectory();

    /* Change working directory */
    LWCOMM_API static bool ChangeWorkingDirectory(std::string_view path);

    /* Split path into directory and filename */
    LWCOMM_API static std::pair<std::string, std::string> SplitPath(std::string_view fullPath);

    /* Check if path is absolute */
    LWCOMM_API static bool IsAbsolutePath(std::string_view path);
};
#endif

class LWStringHelper
{
public:
    /* Safely copy string with length limit */
    LWCOMM_API static int SafeStrNCpy(char *szDest, const char *szSource, int nDestBuffLen);

    /* Convert string to integer */
    LWCOMM_API static int StringToInt(const char *szValue);

    /* Convert string to integer with default value */
    LWCOMM_API static int StringToIntEx(const char *szValue, int nDefault);

    /* Convert string to double */
    LWCOMM_API static double StringToDouble(const char *szValue);

    /* Case-insensitive string comparison */
    LWCOMM_API static int StriCmp(const char *szCmp1, const char *szCmp2);

    /* Escape characters in string */
    LWCOMM_API static int StrEscape(const char *szSrc, char *szEscape, size_t nEscapeBuffLen, char cEscaped,
                                    char cEscaping);

    /* Split string by pattern */
    LWCOMM_API static std::vector<std::string> StriSplit(const std::string str, const std::string pattern);

    /* Replace substring in string */
    LWCOMM_API static void Replace(std::string &sBig, const std::string &sSrc, const std::string &sDst);

    /* Convert buffer to hex dump string */
    LWCOMM_API static int HexDumpBuf(const char *szBuffer, unsigned int nCharBuffLen, char *szHexBuf,
                                     unsigned int nHexBufLen, unsigned int *pnRetHexBufLen);

    /* Convert hex string to buffer */
    LWCOMM_API static int HexStr2Buffer(const char *szHexStr, char *szBuffer, unsigned int *pnBufLen);
};

class LWTimeHelper {
public:
    /* Convert time string (with milliseconds) to timestamp in milliseconds */
    LWCOMM_API static int String2Timestamp(const char* time_str, int64_t* timestamp_ms);

    /* Convert timestamp in milliseconds to formatted time string */
    LWCOMM_API static int Timestamp2String(int64_t timestamp_ms, char* buffer, size_t buf_len, bool include_ms = false);

    /* Convert UTC time string (with milliseconds) to timestamp in milliseconds */
    LWCOMM_API static int64_t UtcString2Timestamp(const char* time_str);

    /* Convert UTC timestamp in milliseconds to formatted time string */
    LWCOMM_API static int Timestamp2UtcString(int64_t timestamp_ms, char* buffer, size_t buf_len, bool include_ms = false);

    /* Get current timestamp in milliseconds */
    LWCOMM_API static int64_t GetCurrentTimestampMs();

    /* Get current time string */
    LWCOMM_API static int GetCurrentTimeString(char* buffer, size_t buf_len, bool include_ms = false);

    /* Get current UTC time string */
    LWCOMM_API static int GetCurrentUtcString(char* buffer, size_t buf_len, bool include_ms = false);
};

class LWBase64
{
private:
    LWBase64();
public:
    ~LWBase64();

    /* Encode data to base64 string */
    LWCOMM_API static int base64_encode(const char *in_str, int in_len, std::string &out_str);

    /* Decode base64 string to data */
    LWCOMM_API static int base64_decode(const char *in_str, int in_len, std::string &out_str);
};

class LWSystemInfo
{
public:
    /* Get IP address list */
    LWCOMM_API static int SysInfoIpList(std::list<std::string> &ip_list);

    /* Check if IP address is valid IPv4 */
    LWCOMM_API static bool Ipv4Valid(std::string &ip);
};

/*
 * end
 */
