#pragma once
#include "IProtocolAdapter.h"
#include "adapter/IDeployable.h"
#include <string>
#include <atomic>
#include <future>
#include <memory>
#include <functional>
#include <vector>

// FTP 协议适配器 — 封装 libcurl FTP 操作,实现 IProtocolAdapter 统一接口
// 使用 Pimpl 模式隐藏 libcurl 实现细节

struct FtpFileInfo;  // 前向声明（定义在 src/tools/FtpDeployTool/FtpFileInfo.h）

class FtpAdapter : public IProtocolAdapter, public IDeployable {
public:
    FtpAdapter();
    ~FtpAdapter() override;

    // --- IProtocolAdapter 实现 ---
    std::string protocolId() const override { return "ftp"; }
    bool connect(const DeviceInfo& device, const AuthInfo& auth) override;
    void disconnect() override;
    bool isConnected() const override;
    std::string lastError() const override;
    std::future<Response> request(const Request& req) override;
    void subscribe(const Request& req, StreamCallback onData) override;
    void unsubscribe() override;
    ProtocolCapability capability() const override;

    // --- FTP 特有操作（直接调用,不通过 request 抽象） ---
    bool uploadFile(const std::string& localPath, const std::string& remotePath);
    bool uploadFolder(const std::string& localPath, const std::string& remotePath);
    bool downloadFile(const std::string& remotePath, const std::string& localPath);
    bool listDirectory(const std::string& remotePath, std::string& outJsonList);
    // 结构化列目录（解析 LIST 原始输出为 FtpFileInfo 列表，便于 UI 渲染）
    // 依赖 FtpListParser (src/tools/FtpDeployTool/FtpListParser.h)
    std::vector<FtpFileInfo> listDirectoryParsed(const std::string& remotePath);
    bool deleteFile(const std::string& remotePath);
    bool deleteDirectory(const std::string& remotePath);
    // 重命名远程文件/目录（FTP RNFR + RNTO 命令）
    bool renameFile(const std::string& remotePath, const std::string& newName);
    // 新建远程目录（FTP MKD 命令）
    bool makeDirectory(const std::string& remotePath);
    bool clearRemoteDirectory(const std::string& remotePath);
    void setProgressCallback(std::function<void(int)> cb);
    void setUseFtps(bool useFtps);
    void cancelTransfer();
    void setCancelFlag(std::atomic<bool>* flag);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
