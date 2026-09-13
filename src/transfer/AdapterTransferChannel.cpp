// AdapterTransferChannel.cpp — FTP/SFTP Adapter 到可靠传输通道的映射

#include "transfer/AdapterTransferChannel.h"

#include "adapter/FtpAdapter.h"
#include "adapter/SshAdapter.h"
#include "tools/FtpDeployTool/FtpFileInfo.h"

#include <utility>
#include <vector>

namespace {

QString normalizedProtocol(QString protocol)
{
    protocol = protocol.trimmed().toLower();
    return protocol == QStringLiteral("ssh") ? QStringLiteral("sftp") : protocol;
}

std::string narrowPath(const QString& path)
{
    return path.toUtf8().toStdString();
}

void copyEntries(const std::vector<FtpFileInfo>& source, QVector<TransferChannelEntry>& target)
{
    target.clear();
    target.reserve(static_cast<qsizetype>(source.size()));
    for (const FtpFileInfo& entry : source) {
        target.push_back({QString::fromUtf8(entry.name), entry.size,
                          QString::fromUtf8(entry.dateTime)});
    }
}

QString parentPath(const QString& path, QString& basename)
{
    const qsizetype separator = path.lastIndexOf(u'/');
    basename = separator >= 0 ? path.mid(separator + 1) : path;
    if (separator < 0)
        return QStringLiteral(".");
    if (separator == 0)
        return QStringLiteral("/");
    return path.left(separator);
}

} // namespace

AdapterTransferChannel::AdapterTransferChannel(QString protocol,
                                               std::shared_ptr<IProtocolAdapter> adapter)
    : AdapterTransferChannel(protocol, adapter, bindOperations(protocol, adapter))
{
}

AdapterTransferChannel::AdapterTransferChannel(QString protocol,
                                               std::shared_ptr<IProtocolAdapter> adapter,
                                               TransferChannelOps operations)
    : m_protocol(normalizedProtocol(std::move(protocol)))
    , m_adapter(std::move(adapter))
    , m_operations(std::move(operations))
{
}

AdapterTransferChannel::~AdapterTransferChannel()
{
    m_auth.clear();
}

bool AdapterTransferChannel::connect(const DeviceInfo& device, const AuthInfo& auth)
{
    m_device = device;
    m_auth.clear();
    m_auth = auth;
    m_hasConnectionParameters = true;
    if (!m_adapter)
        return failUnsupported(QStringLiteral("connect"));
    return finishConnect(m_adapter->connect(device, auth), QStringLiteral("连接失败"));
}

bool AdapterTransferChannel::reconnect()
{
    if (!m_adapter || !m_hasConnectionParameters)
        return failUnsupported(QStringLiteral("reconnect"));

    m_adapter->disconnect();
    return finishConnect(m_adapter->connect(m_device, m_auth), QStringLiteral("重连失败"));
}

bool AdapterTransferChannel::upload(const QString& localPath, const QString& remotePath)
{
    if (!m_operations.upload)
        return failUnsupported(QStringLiteral("upload"));
    return finishOperation(m_operations.upload(localPath, remotePath), QStringLiteral("上传失败"));
}

bool AdapterTransferChannel::download(const QString& remotePath, const QString& localPath)
{
    if (!m_operations.download)
        return failUnsupported(QStringLiteral("download"));
    return finishOperation(m_operations.download(remotePath, localPath), QStringLiteral("下载失败"));
}

bool AdapterTransferChannel::stat(const QString& path, TransferFileStat& out)
{
    out = {};
    if (!m_operations.list)
        return failUnsupported(QStringLiteral("stat"));

    QString basename;
    const QString parent = parentPath(path, basename);
    if (basename.isEmpty()) {
        m_lastError = {TransferErrorCode::InvalidPath, QStringLiteral("远程路径缺少文件名"),
                       QStringLiteral("远程路径缺少文件名"), false};
        return false;
    }

    QVector<TransferChannelEntry> entries;
    if (!m_operations.list(parent, entries))
        return finishOperation(false, QStringLiteral("读取远程元数据失败"));

    for (const TransferChannelEntry& entry : entries) {
        if (entry.name == basename) {
            out.exists = true;
            out.size = entry.size;
            out.modifiedText = entry.modifiedText;
            break;
        }
    }

    m_lastError = {};
    return true;
}

bool AdapterTransferChannel::rename(const QString& from, const QString& to)
{
    if (!m_operations.rename)
        return failUnsupported(QStringLiteral("rename"));
    return finishOperation(m_operations.rename(from, to), QStringLiteral("重命名失败"));
}

bool AdapterTransferChannel::remove(const QString& path)
{
    if (!m_operations.remove)
        return failUnsupported(QStringLiteral("remove"));
    return finishOperation(m_operations.remove(path), QStringLiteral("删除失败"));
}

void AdapterTransferChannel::setProgressCallback(std::function<void(int)> callback)
{
    if (m_operations.setProgressCallback)
        m_operations.setProgressCallback(std::move(callback));
}

void AdapterTransferChannel::setCancelFlag(std::atomic_bool* flag)
{
    if (m_operations.setCancelFlag)
        m_operations.setCancelFlag(flag);
}

TransferCapabilities AdapterTransferChannel::capabilities() const
{
    if (m_protocol != QStringLiteral("ftp") && m_protocol != QStringLiteral("sftp"))
        return {};

    return {static_cast<bool>(m_operations.upload),
            static_cast<bool>(m_operations.download),
            static_cast<bool>(m_operations.rename),
            static_cast<bool>(m_operations.remove),
            static_cast<bool>(m_operations.list)};
}

TransferError AdapterTransferChannel::lastError() const
{
    return m_lastError;
}

void AdapterTransferChannel::clearCredentials()
{
    if (m_adapter)
        m_adapter->disconnect();
    m_auth.clear();
    m_hasConnectionParameters = false;
}

TransferChannelOps AdapterTransferChannel::bindOperations(
    const QString& protocol, const std::shared_ptr<IProtocolAdapter>& adapter)
{
    TransferChannelOps operations;
    const QString normalized = normalizedProtocol(protocol);

    if (normalized == QStringLiteral("ftp")) {
        const auto ftp = std::dynamic_pointer_cast<FtpAdapter>(adapter);
        if (!ftp)
            return operations;
        operations.upload = [ftp](const QString& local, const QString& remote) {
            return ftp->uploadFile(narrowPath(local), narrowPath(remote));
        };
        operations.download = [ftp](const QString& remote, const QString& local) {
            return ftp->downloadFile(narrowPath(remote), narrowPath(local));
        };
        operations.rename = [ftp](const QString& from, const QString& to) {
            return ftp->renameFile(narrowPath(from), narrowPath(to));
        };
        operations.remove = [ftp](const QString& path) {
            return ftp->deleteFile(narrowPath(path));
        };
        operations.list = [ftp](const QString& path, QVector<TransferChannelEntry>& entries) {
            const auto source = ftp->listDirectoryParsed(narrowPath(path));
            if (!ftp->lastError().empty())
                return false;
            copyEntries(source, entries);
            return true;
        };
        operations.setProgressCallback = [ftp](std::function<void(int)> callback) {
            ftp->setProgressCallback(std::move(callback));
        };
        operations.setCancelFlag = [ftp](std::atomic_bool* flag) { ftp->setCancelFlag(flag); };
        return operations;
    }

    if (normalized == QStringLiteral("sftp")) {
        const auto ssh = std::dynamic_pointer_cast<SshAdapter>(adapter);
        if (!ssh)
            return operations;
        operations.upload = [ssh](const QString& local, const QString& remote) {
            return ssh->sftpUploadFile(narrowPath(local), narrowPath(remote));
        };
        operations.download = [ssh](const QString& remote, const QString& local) {
            return ssh->sftpDownloadFile(narrowPath(remote), narrowPath(local));
        };
        operations.rename = [ssh](const QString& from, const QString& to) {
            return ssh->sftpRename(narrowPath(from), narrowPath(to));
        };
        operations.remove = [ssh](const QString& path) {
            return ssh->sftpDeleteFile(narrowPath(path));
        };
        operations.list = [ssh](const QString& path, QVector<TransferChannelEntry>& entries) {
            const auto source = ssh->sftpListDirectory(narrowPath(path));
            if (!ssh->lastError().empty())
                return false;
            copyEntries(source, entries);
            return true;
        };
        operations.setProgressCallback = [ssh](std::function<void(int)> callback) {
            ssh->sftpSetProgressCallback(std::move(callback));
        };
        operations.setCancelFlag = [ssh](std::atomic_bool* flag) {
            ssh->sftpSetCancelFlag(flag);
        };
        operations.isReady = [ssh] { return ssh->isSftpReady(); };
    }

    return operations;
}

bool AdapterTransferChannel::finishConnect(bool connected, const QString& fallbackError)
{
    if (!connected)
        return finishOperation(false, fallbackError);

    if (m_operations.isReady && !m_operations.isReady()) {
        m_adapter->disconnect();
        return finishOperation(false, QStringLiteral("SFTP 子系统未就绪"));
    }

    return finishOperation(true, fallbackError);
}

bool AdapterTransferChannel::finishOperation(bool success, const QString& fallbackError)
{
    if (success) {
        m_lastError = {};
        return true;
    }

    QString message;
    if (m_adapter)
        message = QString::fromUtf8(m_adapter->lastError());
    if (message.isEmpty())
        message = fallbackError;
    m_lastError = classifyTransferError(message, 0);
    return false;
}

bool AdapterTransferChannel::failUnsupported(const QString& operation)
{
    const QString message = QStringLiteral("协议 %1 不支持 %2").arg(m_protocol, operation);
    m_lastError = {TransferErrorCode::Unsupported, message, message, false};
    return false;
}
