#pragma once
#include <QString>
#include <atomic>
#include <functional>
#include <vector>
#include "tools/FtpDeployTool/FtpFileInfo.h"

// 设备信息与认证信息（定义在 src/framework/DeviceInfo.h，前向声明即可）
struct DeviceInfo;
struct AuthInfo;

// 文件源统一接口 — 双栏面板（FileBrowserPanel）与协议无关地操作文件系统。
//
// 两个实现：
//   - LocalFileSource   纯本地文件系统（std::filesystem，可单测）
//   - RemoteFileSource 包装 FtpAdapter（FTP/FTPS）/ SshAdapter（SFTP）
//
// 约定：
//   - path 均为完整路径（QString），list 返回 FtpFileInfo 统一结构
//   - list 不返回 "." / ".."，由面板层统一补充 ".."
//   - remove(path, isDir)：远程删除需区分文件/目录 API（FTP DELE/RMD、SFTP）
//   - upload/download 仅单文件；目录传输由面板层递归逐文件调用
//   - 所有操作失败时置 lastError（成功时清空），面板据此提示
class IFileSource {
public:
    virtual ~IFileSource() = default;

    // --- 身份 ---
    virtual QString sourceId() const = 0;      // "local" / "ftp" / "ssh"（注册表键；SFTP 走 SshAdapter）
    virtual QString displayName() const = 0;   // "本地" / "192.168.1.100 (FTP)"

    // --- 文件操作 ---
    virtual std::vector<FtpFileInfo> list(const QString& path) = 0;
    virtual bool mkdir(const QString& path) = 0;
    virtual bool rename(const QString& oldPath, const QString& newPath) = 0;
    virtual bool remove(const QString& path, bool isDir) = 0;
    virtual bool clearDirectory(const QString& path) = 0;

    // --- 传输（单文件） ---
    virtual bool upload(const QString& localPath, const QString& remotePath) = 0;
    virtual bool download(const QString& remotePath, const QString& localPath) = 0;

    // --- 连接生命周期 ---
    virtual bool connect(const DeviceInfo& device, const AuthInfo& auth) = 0;
    virtual bool isConnected() const = 0;
    virtual QString lastError() const = 0;

    // --- 传输控制 ---
    virtual void setProgressCallback(std::function<void(int)> cb) = 0;
    virtual void setCancelFlag(std::atomic<bool>* flag) = 0;
};
