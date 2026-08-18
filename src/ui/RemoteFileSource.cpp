#include "ui/RemoteFileSource.h"
#include "adapter/ProtocolRegistry.h"
#include "adapter/FtpAdapter.h"
#include "adapter/SshAdapter.h"
#include "framework/DeviceInfo.h"

namespace {

// 按协议取具体适配器（方法映射统一入口；返回 nullptr 表示类型不匹配）
FtpAdapter* asFtp(const std::shared_ptr<IProtocolAdapter>& a)
{
    return dynamic_cast<FtpAdapter*>(a.get());
}

SshAdapter* asSsh(const std::shared_ptr<IProtocolAdapter>& a)
{
    return dynamic_cast<SshAdapter*>(a.get());
}

// 操作后统一刷新 lastError（适配器成功时清空、失败时置错误信息）
QString adapterLastError(const std::shared_ptr<IProtocolAdapter>& a)
{
    if (!a) return QStringLiteral("远程源未连接");
    return QString::fromStdString(a->lastError());
}

} // namespace

RemoteFileSource::RemoteFileSource(const QString& protocol, const QString& displayName)
    : m_protocol(protocol)
    , m_displayName(displayName)
{
}

QString RemoteFileSource::sourceId() const { return m_protocol; }
QString RemoteFileSource::displayName() const { return m_displayName; }

bool RemoteFileSource::connect(const DeviceInfo& device, const AuthInfo& auth)
{
    QMutexLocker locker(&m_mutex);
    return connectLocked(device, auth);
}

// 无锁私有体：调用方必须已持有 m_mutex（connect/reconnect 公共入口加锁后复用）
bool RemoteFileSource::connectLocked(const DeviceInfo& device, const AuthInfo& auth)
{
    // 缓存本次连接凭证，供 reconnect() 空闲断线重连复用
    m_device = device;
    m_auth = auth;

    // 切换设备/协议时先断开旧适配器，再通过注册表重建（避免资源泄漏）
    if (m_adapter) m_adapter->disconnect();
    m_adapter = ProtocolRegistry::instance()->create(m_protocol.toStdString());
    if (!m_adapter) {
        m_lastError = QStringLiteral("协议 %1 未注册").arg(m_protocol);
        return false;
    }

    // 协议特有配置 + 传输控制转发（setter 可能先于 connect 调用，这里补转发）
    if (auto* ftp = asFtp(m_adapter)) {
        ftp->setUseFtps(m_useFtps);
        if (m_progressCb) ftp->setProgressCallback(m_progressCb);
        if (m_cancelFlag) ftp->setCancelFlag(m_cancelFlag);
    } else if (auto* ssh = asSsh(m_adapter)) {
        if (m_progressCb) ssh->sftpSetProgressCallback(m_progressCb);
        if (m_cancelFlag) ssh->setCancelFlag(m_cancelFlag);
    } else {
        m_lastError = QStringLiteral("协议 %1 不支持远程文件操作").arg(m_protocol);
        return false;
    }

    if (!m_adapter->connect(device, auth)) {
        m_lastError = adapterLastError(m_adapter);
        return false;
    }
    m_lastError.clear();
    return true;
}

bool RemoteFileSource::reconnect()
{
    QMutexLocker locker(&m_mutex);
    // 用上次 connect 缓存的凭证原位重连（等价于 v2.6 的"断开→重连"语义）
    return connectLocked(m_device, m_auth);
}

bool RemoteFileSource::isConnected() const
{
    QMutexLocker locker(&m_mutex);
    return m_adapter && m_adapter->isConnected();
}

QString RemoteFileSource::lastError() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastError;
}

std::vector<FtpFileInfo> RemoteFileSource::list(const QString& path)
{
    QMutexLocker locker(&m_mutex);
    if (!m_adapter) { m_lastError = QStringLiteral("远程源未连接"); return {}; }
    const std::string p = path.toStdString();
    std::vector<FtpFileInfo> result;
    if (auto* ftp = asFtp(m_adapter))
        result = ftp->listDirectoryParsed(p);
    else if (auto* ssh = asSsh(m_adapter))
        result = ssh->sftpListDirectory(p);
    else {
        m_lastError = QStringLiteral("协议 %1 不支持远程文件操作").arg(m_protocol);
        return {};
    }
    m_lastError = adapterLastError(m_adapter);
    return result;
}

bool RemoteFileSource::mkdir(const QString& path)
{
    QMutexLocker locker(&m_mutex);
    if (!m_adapter) { m_lastError = QStringLiteral("远程源未连接"); return false; }
    const std::string p = path.toStdString();
    bool ok = false;
    if (auto* ftp = asFtp(m_adapter))
        ok = ftp->makeDirectory(p);
    else if (auto* ssh = asSsh(m_adapter))
        ok = ssh->sftpMakeDirectory(p);
    else
        m_lastError = QStringLiteral("协议 %1 不支持远程文件操作").arg(m_protocol);
    if (!ok) m_lastError = adapterLastError(m_adapter);
    else m_lastError.clear();
    return ok;
}

bool RemoteFileSource::rename(const QString& oldPath, const QString& newPath)
{
    QMutexLocker locker(&m_mutex);
    if (!m_adapter) { m_lastError = QStringLiteral("远程源未连接"); return false; }
    const std::string oldP = oldPath.toStdString();
    const std::string newP = newPath.toStdString();
    bool ok = false;
    if (auto* ftp = asFtp(m_adapter))
        ok = ftp->renameFile(oldP, newP);   // 完整路径以 / 开头时直接作为 RNTO 目标
    else if (auto* ssh = asSsh(m_adapter))
        ok = ssh->sftpRename(oldP, newP);
    else
        m_lastError = QStringLiteral("协议 %1 不支持远程文件操作").arg(m_protocol);
    if (!ok) m_lastError = adapterLastError(m_adapter);
    else m_lastError.clear();
    return ok;
}

bool RemoteFileSource::remove(const QString& path, bool isDir)
{
    QMutexLocker locker(&m_mutex);
    if (!m_adapter) { m_lastError = QStringLiteral("远程源未连接"); return false; }
    const std::string p = path.toStdString();
    bool ok = false;
    if (auto* ftp = asFtp(m_adapter))
        ok = isDir ? ftp->deleteDirectory(p) : ftp->deleteFile(p);
    else if (auto* ssh = asSsh(m_adapter))
        ok = isDir ? ssh->sftpDeleteDirectory(p) : ssh->sftpDeleteFile(p);
    else
        m_lastError = QStringLiteral("协议 %1 不支持远程文件操作").arg(m_protocol);
    if (!ok) m_lastError = adapterLastError(m_adapter);
    else m_lastError.clear();
    return ok;
}

bool RemoteFileSource::clearDirectory(const QString& path)
{
    QMutexLocker locker(&m_mutex);
    if (!m_adapter) { m_lastError = QStringLiteral("远程源未连接"); return false; }
    const std::string p = path.toStdString();
    bool ok = false;
    if (auto* ftp = asFtp(m_adapter))
        ok = ftp->clearRemoteDirectory(p);
    else if (auto* ssh = asSsh(m_adapter))
        ok = ssh->sftpClearDirectory(p);
    else
        m_lastError = QStringLiteral("协议 %1 不支持远程文件操作").arg(m_protocol);
    if (!ok) m_lastError = adapterLastError(m_adapter);
    else m_lastError.clear();
    return ok;
}

bool RemoteFileSource::upload(const QString& localPath, const QString& remotePath)
{
    QMutexLocker locker(&m_mutex);
    // 注意：持锁期间适配器进度回调同线程执行——回调若重入本源的 isConnected/lastError
    // 将死锁（非递归 QMutex）。当前无调用方设置 setProgressCallback（部署走 backend
    // 直连适配器），此路径为预留；若未来启用需改为锁外回调或递归锁。
    if (!m_adapter) { m_lastError = QStringLiteral("远程源未连接"); return false; }
    const std::string l = localPath.toStdString();
    const std::string r = remotePath.toStdString();
    bool ok = false;
    if (auto* ftp = asFtp(m_adapter))
        ok = ftp->uploadFile(l, r);
    else if (auto* ssh = asSsh(m_adapter))
        ok = ssh->sftpUploadFile(l, r);
    else
        m_lastError = QStringLiteral("协议 %1 不支持远程文件操作").arg(m_protocol);
    if (!ok) m_lastError = adapterLastError(m_adapter);
    else m_lastError.clear();
    return ok;
}

bool RemoteFileSource::download(const QString& remotePath, const QString& localPath)
{
    QMutexLocker locker(&m_mutex);
    // 注意：持锁期间适配器进度回调同线程执行——回调若重入本源的 isConnected/lastError
    // 将死锁（非递归 QMutex）。当前无调用方设置 setProgressCallback（部署走 backend
    // 直连适配器），此路径为预留；若未来启用需改为锁外回调或递归锁。
    if (!m_adapter) { m_lastError = QStringLiteral("远程源未连接"); return false; }
    const std::string r = remotePath.toStdString();
    const std::string l = localPath.toStdString();
    bool ok = false;
    if (auto* ftp = asFtp(m_adapter))
        ok = ftp->downloadFile(r, l);
    else if (auto* ssh = asSsh(m_adapter))
        ok = ssh->sftpDownloadFile(r, l);
    else
        m_lastError = QStringLiteral("协议 %1 不支持远程文件操作").arg(m_protocol);
    if (!ok) m_lastError = adapterLastError(m_adapter);
    else m_lastError.clear();
    return ok;
}

void RemoteFileSource::setProgressCallback(std::function<void(int)> cb)
{
    QMutexLocker locker(&m_mutex);
    m_progressCb = std::move(cb);
    if (!m_adapter) return;
    if (auto* ftp = asFtp(m_adapter))
        ftp->setProgressCallback(m_progressCb);
    else if (auto* ssh = asSsh(m_adapter))
        ssh->sftpSetProgressCallback(m_progressCb);
}

void RemoteFileSource::setCancelFlag(std::atomic<bool>* flag)
{
    QMutexLocker locker(&m_mutex);
    m_cancelFlag = flag;
    if (!m_adapter) return;
    if (auto* ftp = asFtp(m_adapter))
        ftp->setCancelFlag(flag);
    else if (auto* ssh = asSsh(m_adapter))
        ssh->setCancelFlag(flag);
}

void RemoteFileSource::setUseFtps(bool useFtps)
{
    QMutexLocker locker(&m_mutex);
    m_useFtps = useFtps;
    if (auto* ftp = asFtp(m_adapter))
        ftp->setUseFtps(useFtps);
}
