// 单台部署事务：连接/清目录/目录规划/报告属于 Job，单文件交付属于 Executor。
#include "DeployJob.h"
#include "adapter/FtpAdapter.h"
#include "adapter/SshAdapter.h"
#include "adapter/IDeployable.h"
#include "adapter/ProtocolRegistry.h"
#include "transfer/AdapterTransferChannel.h"
#include <algorithm>
#include <chrono>
#include <filesystem>

namespace {
std::string joinPath(std::string parent, const std::string& name)
{
    if (!parent.empty() && parent.back() != '/') parent += '/';
    return parent + name;
}
bool ensureDirectory(IProtocolAdapter& adapter, const std::string& path)
{
    // mkdir 失败不能当作“已存在”：列表复核目录类型，避免空目录静默丢失。
    const auto slash = path.find_last_of('/');
    const auto parent = slash == std::string::npos ? "." : (slash == 0 ? "/" : path.substr(0, slash));
    const auto name = slash == std::string::npos ? path : path.substr(slash + 1);
    if (auto* ftp = dynamic_cast<FtpAdapter*>(&adapter)) {
        if (ftp->makeDirectory(path)) return true;
        const auto entries = ftp->listDirectoryParsed(parent);
        return ftp->lastError().empty() && std::any_of(entries.begin(), entries.end(),
            [&](const auto& e) { return e.name == name && e.isDir; });
    }
    if (auto* ssh = dynamic_cast<SshAdapter*>(&adapter)) {
        if (ssh->sftpMakeDirectory(path)) return true;
        const auto entries = ssh->sftpListDirectory(parent);
        return ssh->lastError().empty() && std::any_of(entries.begin(), entries.end(),
            [&](const auto& e) { return e.name == name && e.isDir; });
    }
    return false;
}
}

DeployJob::DeployJob(Params params) : m_params(std::move(params))
{
    m_result.deviceKey = m_params.device.ip + ":" + std::to_string(m_params.device.port);
}
DeployJob::~DeployJob() { m_params.auth.clear(); }

void DeployJob::run()
{
    namespace fs = std::filesystem;
    const auto started = std::chrono::steady_clock::now();
    m_result.startedAt = std::time(nullptr);
    m_result.state = DeviceResult::Failed;
    struct Cleanup {
        Params& params;
        DeviceResult& result;
        std::chrono::steady_clock::time_point started;
        std::shared_ptr<IProtocolAdapter> adapter;
        ~Cleanup() {
            if (adapter) {
                if (auto* d = dynamic_cast<IDeployable*>(adapter.get())) {
                    d->setProgressCallback({});
                    d->setCancelFlag(nullptr);
                }
                adapter->disconnect();
            }
            params.auth.clear();
            result.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
        }
    } cleanup{m_params, m_result, started, {}};
    auto log = [this](const std::string& message) {
        if (m_params.logSink) m_params.logSink("[" + m_result.deviceKey + "] " + message);
    };
    auto fail = [&](const std::string& file, const std::string& error) {
        m_result.failedFiles.push_back(file);
        m_result.lastError = error;
        log(file + " 上传失败: " + error);
    };
    if (isCancelled()) { m_result.state = DeviceResult::Cancelled; return; }
    auto adapter = ProtocolRegistry::instance()->create(m_params.protocol);
    cleanup.adapter = adapter;
    auto* deployable = adapter ? dynamic_cast<IDeployable*>(adapter.get()) : nullptr;
    if (!deployable) { m_result.lastError = "适配器不支持部署能力"; log(m_result.lastError); return; }
    if (auto* ftp = dynamic_cast<FtpAdapter*>(adapter.get())) ftp->setUseFtps(m_params.useFtps);
    log("正在连接 ...");
    if (!adapter->connect(m_params.device, m_params.auth)) {
        m_result.lastError = adapter->lastError();
        log("连接失败 — " + m_result.lastError);
        if (isCancelled()) m_result.state = DeviceResult::Cancelled;
        return;
    }
    log("已连接");
    auto* cancel = m_params.globalCancel ? const_cast<std::atomic<bool>*>(m_params.globalCancel)
        : (m_cancelFlag ? m_cancelFlag : &m_fallbackCancel);
    deployable->setCancelFlag(cancel);
    if (isCancelled()) { m_result.state = DeviceResult::Cancelled; return; }
    // 恢复标志本身不证明曾经交付；首次连接失败后的重试仍须执行清目录。
    if (m_params.clearBefore && (!m_params.resume || m_params.deliveredFiles.empty())) {
        log("清空远程目录: " + m_params.remotePath);
        if (!deployable->clearRemoteDirectory(m_params.remotePath))
            log("清空目录失败 — " + adapter->lastError());
    }

    struct Item { std::string local, remote, label; };
    std::vector<Item> items;
    std::vector<std::string> directories;
    for (const auto& source : m_params.files) {
        if (isCancelled()) break;
        std::error_code ec;
        auto local = fs::u8path(source).lexically_normal();
        if (local.filename().empty()) local = local.parent_path();
        if (!fs::is_directory(local, ec)) {
            if (ec) fail(source, "无法访问本地路径");
            else items.push_back({source, joinPath(m_params.remotePath, local.filename().u8string()),
                                  local.filename().u8string()});
            continue;
        }
        // 保留协议原有目录映射：FTP 包含顶层目录，SFTP 上传目录内容。
        auto base = m_params.protocol == "ssh" || m_params.protocol == "sftp"
            ? m_params.remotePath : joinPath(m_params.remotePath, local.filename().u8string());
        // 根目录无 basename，也不应 mkdir；普通目录去除尾斜杠后再确认。
        while (base.size() > 1 && base.back() == '/') base.pop_back();
        if (base != "/") directories.push_back(base);
        fs::recursive_directory_iterator it(local, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            if (isCancelled()) break;
            const auto relative = it->path().lexically_relative(local).generic_u8string();
            const auto remote = joinPath(base, relative);
            if (it->is_directory(ec)) directories.push_back(remote);
            else if (!ec && it->is_regular_file(ec))
                items.push_back({it->path().u8string(), remote, relative});
        }
        if (ec) fail(source, "本地目录遍历失败: " + ec.message());
    }
    for (const auto& directory : directories) {
        if (isCancelled()) break;
        const bool ok = m_params.makeDirectory ? m_params.makeDirectory(*adapter, directory)
                                               : ensureDirectory(*adapter, directory);
        if (!ok) fail(directory, "无法创建或确认远端目录");
    }

    bool cancelled = isCancelled();
    size_t completed = 0;
    for (const auto& item : items) {
        if (isCancelled()) { cancelled = true; break; }
        const auto delivered = std::find_if(m_params.deliveredFiles.begin(), m_params.deliveredFiles.end(),
            [&](const DeployDeliveredFile& f) { return f.localPath == item.local && f.remotePath == item.remote; });
        if (m_params.resume && delivered != m_params.deliveredFiles.end()) {
            m_result.deliveredFiles.push_back(*delivered);
            m_result.nonAtomic |= delivered->nonAtomic;
            log("跳过已交付文件: " + item.label);
            ++completed;
            if (m_params.progressSink) m_params.progressSink(int(completed * 100 / items.size()));
            continue;
        }
        auto channel = m_params.channelFactory ? m_params.channelFactory(adapter)
            : std::make_unique<AdapterTransferChannel>(QString::fromStdString(m_params.protocol), adapter);
        if (!channel) { fail(item.label, "传输通道不可用"); ++completed; continue; }
        channel->setProgressCallback([&, completed](int pct) {
            if (m_params.progressSink) m_params.progressSink(int((completed * 100 + std::clamp(pct, 0, 100)) / items.size()));
        });
        // 每文件使用新的凭据副本；Executor 终态断开连接并擦除这个副本。
        AuthInfo fileAuth = m_params.auth;
        TransferExecutor executor(*channel, m_params.device, fileAuth, m_params.retrySleeper);
        log("上传: " + item.label);
        const auto result = executor.execute({QString::fromUtf8(item.local), QString::fromUtf8(item.remote)}, *cancel);
        channel->setProgressCallback({});
        m_result.nonAtomic |= result.nonAtomic;
        if (result.nonAtomic) log("警告：非原子传输 (nonAtomic): " + item.label);
        if (hasDeliveredTarget(result)) {
            m_result.deliveredFiles.push_back({item.local, item.remote, result.nonAtomic});
            log(item.label + " 上传完成");
        } else {
            fail(item.label, result.error.message.toStdString());
            if (result.state == TransferState::Cancelled || isCancelled()) cancelled = true;
        }
        ++completed;
    }
    m_result.state = cancelled ? DeviceResult::Cancelled
        : (m_result.failedFiles.empty() ? DeviceResult::Ok : DeviceResult::Failed);
}
