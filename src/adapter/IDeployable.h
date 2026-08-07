#pragma once
#include <string>
#include <functional>
#include <atomic>

// 可选部署能力：协议适配器实现此接口即成为批量部署通道。
// 部署循环（FtpDeployBackend）通过 dynamic_cast 探测该接口，
// 与协议无关地执行 上传/清空/进度/取消。
class IDeployable {
public:
    virtual ~IDeployable() = default;

    // 上传单个文件到远程路径（remotePath 含文件名）
    virtual bool uploadFile(const std::string& localPath, const std::string& remotePath) = 0;

    // 递归上传整个目录到远程路径（目录本身作为远程子目录创建）
    virtual bool uploadFolder(const std::string& localPath, const std::string& remotePath) = 0;

    // 清空远程目录内容（保留目录本身）
    virtual bool clearRemoteDirectory(const std::string& remotePath) = 0;

    // 进度回调：0-100 整数百分比
    virtual void setProgressCallback(std::function<void(int)> cb) = 0;

    // 取消标志：部署循环设置后，传输应在每文件/每块前检查并中止
    virtual void setCancelFlag(std::atomic<bool>* flag) = 0;
};
