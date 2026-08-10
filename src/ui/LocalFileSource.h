#pragma once
#include "ui/IFileSource.h"
#include <filesystem>

// 本地文件源 — 纯 std::filesystem 操作，无网络依赖（可单测）。
// 上传/下载在本地源语义下均为文件复制（fs::copy_file）。
class LocalFileSource : public IFileSource {
public:
    QString sourceId() const override { return QStringLiteral("local"); }
    QString displayName() const override { return QStringLiteral("本地"); }
    std::vector<FtpFileInfo> list(const QString& path) override;
    bool mkdir(const QString& path) override { return std::filesystem::create_directories(path.toStdWString()); }
    bool rename(const QString& oldPath, const QString& newPath) override;
    bool remove(const QString& path, bool isDir) override;
    bool clearDirectory(const QString& path) override;
    bool upload(const QString& localPath, const QString& remotePath) override;   // 本地源：复制
    bool download(const QString& remotePath, const QString& localPath) override; // 本地源：复制
    bool connect(const DeviceInfo&, const AuthInfo&) override { return true; }   // no-op
    bool isConnected() const override { return true; }
    QString lastError() const override { return m_lastError; }
    void setProgressCallback(std::function<void(int)>) override {}
    void setCancelFlag(std::atomic<bool>*) override {}
private:
    QString m_lastError;
};
