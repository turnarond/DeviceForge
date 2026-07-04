#pragma once
#include "IProtocolAdapter.h"
#include <string>
#include <future>
#include <memory>
#include <functional>

// FTP 协议适配器 — 封装 libcurl FTP 操作,实现 IProtocolAdapter 统一接口
// 使用 Pimpl 模式隐藏 libcurl 实现细节
class FtpAdapter : public IProtocolAdapter {
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
    bool deleteFile(const std::string& remotePath);
    bool deleteDirectory(const std::string& remotePath);
    bool clearRemoteDirectory(const std::string& remotePath);
    void setProgressCallback(std::function<void(int)> cb);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
