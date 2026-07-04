#include "FtpAdapter.h"
#include <curl/curl.h>
#include <sstream>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <future>

// ============================================================
// Pimpl 实现体 — 封装所有 libcurl 操作细节
// ============================================================
struct FtpAdapter::Impl {
    std::string m_ip;
    int         m_port = 21;
    std::string m_user;
    std::string m_password;
    std::string m_lastError;
    bool        m_connected = false;
    std::function<void(int)> m_progressCb;

    // --- URL 拼接 ---
    std::string buildUrl(const std::string& path) const {
        std::ostringstream oss;
        oss << "ftp://" << m_ip << ":" << m_port << "/";
        if (!path.empty() && path[0] == '/') {
            oss << path.substr(1);
        } else {
            oss << path;
        }
        return oss.str();
    }

    // --- 构造 user:password 凭据字符串 ---
    std::string userPwd() const {
        return m_user + ":" + m_password;
    }

    // --- libcurl 写回调（追加到 std::string） ---
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        auto* str = static_cast<std::string*>(userp);
        size_t total = size * nmemb;
        str->append(static_cast<char*>(contents), total);
        return total;
    }

    // --- libcurl 读回调（从 FILE* 读取） ---
    static size_t readCallback(void* ptr, size_t size, size_t nmemb, void* userdata) {
        FILE* file = static_cast<FILE*>(userdata);
        return fread(ptr, size, nmemb, file);
    }

    // --- libcurl 写回调（写入 FILE*） ---
    static size_t writeToFileCallback(void* ptr, size_t size, size_t nmemb, void* stream) {
        FILE* file = static_cast<FILE*>(stream);
        return fwrite(ptr, size, nmemb, file);
    }

    // --- libcurl 进度回调 ---
    static int progressCallback(void* clientp, curl_off_t /*dltotal*/, curl_off_t /*dlnow*/,
                                 curl_off_t ultotal, curl_off_t ulnow) {
        auto* self = static_cast<Impl*>(clientp);
        if (self->m_progressCb && ultotal > 0) {
            int pct = static_cast<int>((ulnow / ultotal) * 100.0);
            self->m_progressCb(pct);
        }
        return 0;
    }

    // --- 解析 FTP LIST 输出中的文件名 ---
    static std::string parseListFilename(const std::string& line) {
        // Unix ls -l 格式: "drwxr-xr-x 2 user group 4096 Jan 01 12:00 filename"
        // 文件名从第9个字段开始（或第8个,取决于格式）
        std::istringstream iss(line);
        std::vector<std::string> parts;
        std::string part;
        while (iss >> part) {
            parts.push_back(part);
        }
        if (parts.size() >= 9) {
            // 合并第9个及之后所有字段
            std::string name;
            for (size_t i = 8; i < parts.size(); ++i) {
                if (i > 8) name += " ";
                name += parts[i];
            }
            return name;
        }
        return {};
    }

    // --- 配置 CURL 通用选项（URL + 凭据 + 超时） ---
    void setupCommonOpts(CURL* curl, const std::string& url, long timeoutSec = 30) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_USERPWD, userPwd().c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSec);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    }
};

// ============================================================
// 构造 / 析构
// ============================================================

FtpAdapter::FtpAdapter() : m_impl(std::make_unique<Impl>()) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

FtpAdapter::~FtpAdapter() {
    disconnect();
    curl_global_cleanup();
}

// ============================================================
// IProtocolAdapter — 连接生命周期
// ============================================================

bool FtpAdapter::connect(const DeviceInfo& device, const AuthInfo& auth) {
    m_impl->m_ip = device.ip;
    m_impl->m_port = device.port > 0 ? device.port : 21;
    m_impl->m_user = auth.user;
    m_impl->m_password = auth.password;

    // 验证可达性：尝试列出根目录（仅列目录名,节省流量）
    CURL* curl = curl_easy_init();
    if (!curl) {
        m_impl->m_lastError = "curl_easy_init() 失败";
        m_impl->m_connected = false;
        return false;
    }

    std::string result;
    std::string url = m_impl->buildUrl("/");

    m_impl->setupCommonOpts(curl, url, 10);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Impl::writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
    curl_easy_setopt(curl, CURLOPT_DIRLISTONLY, 1L);  // 仅列文件名,节省流量

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK) {
        m_impl->m_connected = true;
        m_impl->m_lastError.clear();
        return true;
    }

    m_impl->m_lastError = std::string("FTP 连接失败: ") + curl_easy_strerror(res);
    m_impl->m_connected = false;
    return false;
}

void FtpAdapter::disconnect() {
    m_impl->m_connected = false;
}

bool FtpAdapter::isConnected() const {
    return m_impl->m_connected;
}

std::string FtpAdapter::lastError() const {
    return m_impl->m_lastError;
}

// ============================================================
// IProtocolAdapter — 传输模式
// ============================================================

std::future<Response> FtpAdapter::request(const Request& req) {
    return std::async(std::launch::async, [this, req]() -> Response {
        Response resp;

        if (req.path == "LIST") {
            std::string listing;
            bool ok = listDirectory(req.payload, listing);
            resp.success = ok;
            resp.data = listing;
            if (!ok) resp.errorMessage = m_impl->m_lastError;

        } else if (req.path == "DELETE_FILE") {
            bool ok = deleteFile(req.payload);
            resp.success = ok;
            if (!ok) resp.errorMessage = m_impl->m_lastError;

        } else if (req.path == "DELETE_DIR") {
            bool ok = deleteDirectory(req.payload);
            resp.success = ok;
            if (!ok) resp.errorMessage = m_impl->m_lastError;

        } else if (req.path == "CLEAR_DIR") {
            bool ok = clearRemoteDirectory(req.payload);
            resp.success = ok;
            if (!ok) resp.errorMessage = m_impl->m_lastError;

        } else if (req.path == "DOWNLOAD") {
            // req.payload 格式: "remotePath|localPath"
            size_t sep = req.payload.find('|');
            if (sep != std::string::npos) {
                std::string remotePath = req.payload.substr(0, sep);
                std::string localPath = req.payload.substr(sep + 1);
                bool ok = downloadFile(remotePath, localPath);
                resp.success = ok;
                if (!ok) resp.errorMessage = m_impl->m_lastError;
            } else {
                resp.success = false;
                resp.errorMessage = "DOWNLOAD 需要 remotePath|localPath 格式的 payload";
            }

        } else {
            resp.success = false;
            resp.errorMessage = "未知 FTP 操作: " + req.path;
        }
        return resp;
    });
}

void FtpAdapter::subscribe(const Request& /*req*/, StreamCallback /*onData*/) {
    // FTP 协议不支持流模式
    m_impl->m_lastError = "FTP 协议不支持流模式";
}

void FtpAdapter::unsubscribe() {
    // FTP 无流,无需操作
}

ProtocolCapability FtpAdapter::capability() const {
    ProtocolCapability cap;
    cap.requestResponse = true;
    cap.streaming = false;
    cap.broadcast = false;
    cap.publishSubscribe = false;
    cap.maxConnections = 10;
    return cap;
}

// ============================================================
// FTP 特有操作
// ============================================================

bool FtpAdapter::uploadFile(const std::string& localPath, const std::string& remotePath) {
    FILE* file = fopen(localPath.c_str(), "rb");
    if (!file) {
        m_impl->m_lastError = "无法打开本地文件: " + localPath;
        return false;
    }

    // 获取文件大小
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    CURL* curl = curl_easy_init();
    if (!curl) {
        fclose(file);
        m_impl->m_lastError = "curl_easy_init() 失败";
        return false;
    }

    std::string url = m_impl->buildUrl(remotePath);
    std::string userPwd = m_impl->userPwd();

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERPWD, userPwd.c_str());
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, Impl::readCallback);
    curl_easy_setopt(curl, CURLOPT_READDATA, file);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(fileSize));
    curl_easy_setopt(curl, CURLOPT_FTP_CREATE_MISSING_DIRS, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);

    // 进度回调
    if (m_impl->m_progressCb) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, m_impl.get());
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, Impl::progressCallback);
    }

    CURLcode res = curl_easy_perform(curl);
    fclose(file);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK) {
        m_impl->m_lastError.clear();
        return true;
    }

    m_impl->m_lastError = std::string("上传失败: ") + curl_easy_strerror(res);
    return false;
}

bool FtpAdapter::uploadFolder(const std::string& localPath, const std::string& remotePath) {
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::exists(localPath, ec) || !fs::is_directory(localPath, ec)) {
        m_impl->m_lastError = "本地文件夹不存在: " + localPath;
        return false;
    }

    // 提取文件夹名（去掉末尾 / 或 \）
    std::string cleanLocal = localPath;
    while (!cleanLocal.empty() && (cleanLocal.back() == '/' || cleanLocal.back() == '\\')) {
        cleanLocal.pop_back();
    }
    std::string folderName = fs::path(cleanLocal).filename().string();

    // 构造远程基础路径
    std::string remoteBase = remotePath;
    while (!remoteBase.empty() && remoteBase.back() == '/') {
        remoteBase.pop_back();
    }
    remoteBase += "/" + folderName;

    bool allOk = true;

    // 遍历文件夹中的所有文件
    for (const auto& entry : fs::recursive_directory_iterator(cleanLocal, ec)) {
        if (ec) break;

        if (!entry.is_regular_file(ec)) continue;
        if (ec) continue;

        std::string filePath = entry.path().string();
        // 使用正斜杠统一路径分隔符
        std::replace(filePath.begin(), filePath.end(), '\\', '/');

        // 计算相对路径
        std::string cleanBase = cleanLocal;
        std::replace(cleanBase.begin(), cleanBase.end(), '\\', '/');

        std::string relPath;
        size_t pos = filePath.find(cleanBase);
        if (pos != std::string::npos) {
            relPath = filePath.substr(pos + cleanBase.size());
            if (!relPath.empty() && relPath[0] == '/') {
                relPath = relPath.substr(1);
            }
        } else {
            relPath = entry.path().filename().string();
        }

        std::string remoteFile = remoteBase;
        if (!remoteFile.empty() && remoteFile.back() != '/') {
            remoteFile += '/';
        }
        remoteFile += relPath;

        if (!uploadFile(filePath, remoteFile)) {
            allOk = false;
            // 继续上传其余文件,不中止
        }
    }

    return allOk;
}

bool FtpAdapter::downloadFile(const std::string& remotePath, const std::string& localPath) {
    FILE* file = fopen(localPath.c_str(), "wb");
    if (!file) {
        m_impl->m_lastError = "无法创建本地文件: " + localPath;
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        fclose(file);
        m_impl->m_lastError = "curl_easy_init() 失败";
        return false;
    }

    std::string url = m_impl->buildUrl(remotePath);
    std::string userPwd = m_impl->userPwd();

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERPWD, userPwd.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Impl::writeToFileCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);

    CURLcode res = curl_easy_perform(curl);
    fclose(file);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK) {
        m_impl->m_lastError.clear();
        return true;
    }

    m_impl->m_lastError = std::string("下载失败: ") + curl_easy_strerror(res);
    return false;
}

bool FtpAdapter::listDirectory(const std::string& remotePath, std::string& outJsonList) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        m_impl->m_lastError = "curl_easy_init() 失败";
        return false;
    }

    std::string buffer;
    std::string url = m_impl->buildUrl(remotePath);
    m_impl->setupCommonOpts(curl, url);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Impl::writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_DIRLISTONLY, 0L);  // 获取详细列表

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        m_impl->m_lastError = std::string("列目录失败: ") + curl_easy_strerror(res);
        return false;
    }

    // 解析输出,构建简易 JSON 数组: ["name1","name2",...]
    std::ostringstream json;
    json << "[";
    bool first = true;

    std::istringstream stream(buffer);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        std::string name = Impl::parseListFilename(line);
        if (name.empty() || name == "." || name == "..") continue;

        if (!first) json << ",";
        json << "\"" << name << "\"";
        first = false;
    }
    json << "]";

    outJsonList = json.str();
    m_impl->m_lastError.clear();
    return true;
}

bool FtpAdapter::deleteFile(const std::string& remotePath) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        m_impl->m_lastError = "curl_easy_init() 失败";
        return false;
    }

    std::string url = m_impl->buildUrl(remotePath);
    m_impl->setupCommonOpts(curl, url);

    // 使用 QUOTE 命令发送 DELE
    std::string pathForDelete = "/" + remotePath;
    if (!remotePath.empty() && remotePath[0] == '/') {
        pathForDelete = remotePath;
    }
    std::string deleteCmd = "DELE " + pathForDelete;

    struct curl_slist* commands = nullptr;
    commands = curl_slist_append(commands, deleteCmd.c_str());
    curl_easy_setopt(curl, CURLOPT_QUOTE, commands);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(commands);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK) {
        m_impl->m_lastError.clear();
        return true;
    }

    m_impl->m_lastError = std::string("删除文件失败: ") + curl_easy_strerror(res);
    return false;
}

bool FtpAdapter::deleteDirectory(const std::string& remotePath) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        m_impl->m_lastError = "curl_easy_init() 失败";
        return false;
    }

    std::string url = m_impl->buildUrl(remotePath);
    m_impl->setupCommonOpts(curl, url);

    // 使用 QUOTE 命令发送 RMD
    std::string pathForDelete = "/" + remotePath;
    if (!remotePath.empty() && remotePath[0] == '/') {
        pathForDelete = remotePath;
    }
    std::string rmdCmd = "RMD " + pathForDelete;

    struct curl_slist* commands = nullptr;
    commands = curl_slist_append(commands, rmdCmd.c_str());
    curl_easy_setopt(curl, CURLOPT_QUOTE, commands);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(commands);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK) {
        m_impl->m_lastError.clear();
        return true;
    }

    m_impl->m_lastError = std::string("删除目录失败: ") + curl_easy_strerror(res);
    return false;
}

bool FtpAdapter::clearRemoteDirectory(const std::string& remotePath) {
    // 安全校验：不允许清空根目录
    std::string cleanPath = remotePath;
    while (!cleanPath.empty() && cleanPath.front() == '/') {
        cleanPath.erase(0, 1);
    }
    while (!cleanPath.empty() && cleanPath.back() == '/') {
        cleanPath.pop_back();
    }
    if (cleanPath.empty()) {
        m_impl->m_lastError = "不能清空根目录 '/'";
        return false;
    }

    // 列出目录内容
    std::string listing;
    if (!listDirectory(cleanPath, listing)) {
        return false;
    }

    // 解析 JSON 数组获取文件名列表
    std::vector<std::string> entries;
    size_t pos = 0;
    while ((pos = listing.find('"', pos)) != std::string::npos) {
        size_t start = pos + 1;
        size_t end = listing.find('"', start);
        if (end == std::string::npos) break;
        std::string name = listing.substr(start, end - start);
        if (name != "." && name != "..") {
            entries.push_back(name);
        }
        pos = end + 1;
    }

    // 逐一删除（先尝试作为文件,失败则尝试作为目录）
    bool allOk = true;
    for (const auto& entry : entries) {
        std::string fullPath = cleanPath + "/" + entry;

        if (!deleteFile(fullPath)) {
            // 文件删除失败,尝试作为目录删除
            if (!deleteDirectory(fullPath)) {
                allOk = false;
            }
        }
    }

    return allOk;
}

void FtpAdapter::setProgressCallback(std::function<void(int)> cb) {
    m_impl->m_progressCb = std::move(cb);
}
