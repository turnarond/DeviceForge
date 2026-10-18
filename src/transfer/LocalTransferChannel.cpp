// LocalTransferChannel.cpp — 不经 IFileSource 的分块本地文件通道

#include "transfer/LocalTransferChannel.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cstdio>

#ifdef Q_OS_WIN
#  define NOMINMAX
#  include <windows.h>
#endif

namespace {

constexpr qint64 kCopyBlockSize = 64 * 1024;

bool replaceFile(const QString& from, const QString& to)
{
#ifdef Q_OS_WIN
    return MoveFileExW(reinterpret_cast<LPCWSTR>(from.utf16()),
                       reinterpret_cast<LPCWSTR>(to.utf16()),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
        != FALSE;
#else
    const QByteArray sourceName = QFile::encodeName(from);
    const QByteArray targetName = QFile::encodeName(to);
    return std::rename(sourceName.constData(), targetName.constData()) == 0;
#endif
}

} // namespace

bool LocalTransferChannel::connect(const DeviceInfo&, const AuthInfo&)
{
    m_error = {};
    return true;
}

bool LocalTransferChannel::reconnect()
{
    m_error = {};
    return true;
}

bool LocalTransferChannel::upload(const QString& localPath, const QString& remotePath)
{
    return copyFile(localPath, remotePath);
}

bool LocalTransferChannel::download(const QString& remotePath, const QString& localPath)
{
    return copyFile(remotePath, localPath);
}

bool LocalTransferChannel::stat(const QString& path, TransferFileStat& out)
{
    const QFileInfo info(path);
    out = {};
    if (!info.exists()) {
        m_error = {};
        return true;
    }
    if (!info.isFile()) {
        setError(TransferErrorCode::InvalidPath, QStringLiteral("本地路径不是普通文件"));
        return false;
    }
    out.exists = true;
    out.size = static_cast<quint64>(std::max<qint64>(info.size(), 0));
    out.modifiedText = QString::number(info.lastModified().toMSecsSinceEpoch());
    m_error = {};
    return true;
}

bool LocalTransferChannel::rename(const QString& from, const QString& to)
{
    if (m_cancel && m_cancel->load()) {
        setError(TransferErrorCode::Cancelled, QStringLiteral("传输已取消"));
        return false;
    }
    if (!replaceFile(from, to)) {
        setError(TransferErrorCode::LocalIo, QStringLiteral("本地原子提交失败"));
        return false;
    }
    m_error = {};
    return true;
}

bool LocalTransferChannel::remove(const QString& path)
{
    if (!QFileInfo::exists(path)) {
        m_error = {};
        return true;
    }
    if (!QFile::remove(path)) {
        setError(TransferErrorCode::LocalIo, QStringLiteral("本地临时文件清理失败"));
        return false;
    }
    m_error = {};
    return true;
}

// v2.15 波2：**单层** mkdir 语义——仅创建路径最后一段目录，父目录不存在即失败。
// 不做全路径 mkpath 递归的原因：逐级编排（stat 判定 + 自最浅缺失层起序贯创建）
// 收敛在 TransferExecutor 的上传前父目录确保步，通道与 FTP MKD/SFTP mkdir
// 等真实协议原语保持同一口径，避免两层"半递归"叠加。已存在同名目录=幂等成功。
bool LocalTransferChannel::makeDirectory(const QString& remotePath)
{
    const QFileInfo info(remotePath);
    if (info.isDir()) {
        m_error = {};
        return true;
    }
    if (info.exists()) {
        setError(TransferErrorCode::InvalidPath, QStringLiteral("目标路径已存在且不是目录"));
        return false;
    }
    QDir parent = info.dir();
    if (!parent.exists()) {
        setError(TransferErrorCode::LocalIo, QStringLiteral("父目录不存在，单层创建失败"));
        return false;
    }
    if (!parent.mkdir(info.fileName())) {
        setError(TransferErrorCode::LocalIo, QStringLiteral("本地目录创建失败"));
        return false;
    }
    m_error = {};
    return true;
}

void LocalTransferChannel::setProgressCallback(std::function<void(int)> callback)
{
    m_progress = std::move(callback);
}

void LocalTransferChannel::setCancelFlag(std::atomic_bool* flag)
{
    m_cancel = flag;
}

TransferCapabilities LocalTransferChannel::capabilities() const
{
    // 六项能力全亮（v2.15 波2 增 makeDirectory 单层创建）
    return {true, true, true, true, true, true};
}

TransferError LocalTransferChannel::lastError() const
{
    return m_error;
}

bool LocalTransferChannel::copyFile(const QString& sourcePath, const QString& targetPath)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        setError(TransferErrorCode::LocalIo, QStringLiteral("无法打开本地源文件"));
        return false;
    }
    QFile target(targetPath);
    if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(TransferErrorCode::LocalIo, QStringLiteral("无法创建本地临时文件"));
        return false;
    }

    const qint64 total = source.size();
    qint64 copied = 0;
    while (!source.atEnd()) {
        if (m_cancel && m_cancel->load()) {
            target.close();
            QFile::remove(targetPath);
            setError(TransferErrorCode::Cancelled, QStringLiteral("传输已取消"));
            return false;
        }
        const QByteArray block = source.read(kCopyBlockSize);
        if (block.isEmpty() && source.error() != QFileDevice::NoError) {
            target.close();
            QFile::remove(targetPath);
            setError(TransferErrorCode::LocalIo, QStringLiteral("读取本地源文件失败"));
            return false;
        }
        if (target.write(block) != block.size()) {
            target.close();
            QFile::remove(targetPath);
            setError(TransferErrorCode::LocalIo, QStringLiteral("写入本地临时文件失败"));
            return false;
        }
        copied += block.size();
        if (m_progress && total > 0)
            m_progress(static_cast<int>((copied * 100) / total));
    }
    if (!target.flush()) {
        target.close();
        QFile::remove(targetPath);
        setError(TransferErrorCode::LocalIo, QStringLiteral("刷新本地临时文件失败"));
        return false;
    }
    target.close();
    if (m_progress)
        m_progress(100);
    m_error = {};
    return true;
}

void LocalTransferChannel::setError(TransferErrorCode code, const QString& message)
{
    m_error = {code, message, message, false};
}
