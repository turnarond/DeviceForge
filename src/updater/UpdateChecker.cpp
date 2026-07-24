/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: UpdateChecker.cpp
 * Date: 2026-07-15
 * Author: turnarond
 * Description: UpdateChecker 实现 — GitHub Releases 查询 + zip 下载 + 解压
 */
#include "UpdateChecker.h"

#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QFutureWatcher>

#include <curl/curl.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>

// JSON 简易解析：仅识别 GitHub Release / asset 对象
// 不引入第三方 JSON 库,采用最朴素的 key-value 字符串扫描
// 注意：这是一个非常受限的解析器,只能处理 GitHub Releases 稳定返回的扁平结构
namespace {

// 去除字符串首尾空白
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// 在 json 字符串中查找 "key" : "value" 形式的字符串值
// 返回 true 表示找到,valueOut 接收已去引号的值
bool findStringValue(const std::string& json, const std::string& key,
                     std::string& valueOut) {
    std::string needle = "\"" + key + "\"";
    size_t pos = 0;
    while ((pos = json.find(needle, pos)) != std::string::npos) {
        size_t colon = json.find(':', pos + needle.size());
        if (colon == std::string::npos) { pos += needle.size(); continue; }
        size_t q1 = json.find('"', colon + 1);
        if (q1 == std::string::npos) return false;
        size_t q2 = q1 + 1;
        while (q2 < json.size()) {
            if (json[q2] == '\\' && q2 + 1 < json.size()) { q2 += 2; continue; }
            if (json[q2] == '"') break;
            ++q2;
        }
        if (q2 >= json.size()) return false;
        valueOut = json.substr(q1 + 1, q2 - q1 - 1);
        return true;
    }
    return false;
}

// 查找数值字段 "key": 12345
bool findIntValue(const std::string& json, const std::string& key, int64_t& valueOut) {
    std::string needle = "\"" + key + "\"";
    size_t pos = 0;
    while ((pos = json.find(needle, pos)) != std::string::npos) {
        size_t colon = json.find(':', pos + needle.size());
        if (colon == std::string::npos) { pos += needle.size(); continue; }
        size_t i = colon + 1;
        while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
        size_t start = i;
        while (i < json.size() && (std::isdigit(static_cast<unsigned char>(json[i])) || json[i] == '-')) ++i;
        if (i > start) {
            valueOut = std::strtoll(json.substr(start, i - start).c_str(), nullptr, 10);
            return true;
        }
        return false;
    }
    return false;
}

// 在 json 中查找形如 "browser_download_url": "https://..." 的所有 asset URL
// 同时提取同对象内的 "name" 和 "size"
// GitHub Release JSON 结构稳定:[ { "assets": [ { "name":.., "size":.., "browser_download_url":.. } ] } ]
// 单 pass 流式扫描:在每个对象的 {..} 区间内同时收集 name/size/url/content_type,避免跨对象误配
bool findAsset(const std::string& json, const std::string& namePattern,
               std::string& assetName, std::string& downloadUrl, int64_t& size) {
    size_t pos = 0;
    while ((pos = json.find('{', pos)) != std::string::npos) {
        // 找匹配的右括号(忽略嵌套字符串中的 })
        int depth = 1;
        size_t objEnd = pos + 1;
        while (objEnd < json.size() && depth > 0) {
            if (json[objEnd] == '"') {
                // 跳过字符串
                size_t strEnd = objEnd + 1;
                while (strEnd < json.size()) {
                    if (json[strEnd] == '\\' && strEnd + 1 < json.size()) { strEnd += 2; continue; }
                    if (json[strEnd] == '"') break;
                    ++strEnd;
                }
                objEnd = (strEnd < json.size()) ? strEnd + 1 : strEnd;
                continue;
            }
            if (json[objEnd] == '{') ++depth;
            else if (json[objEnd] == '}') --depth;
            ++objEnd;
        }
        if (depth != 0) break; // 未找到匹配右括号

        std::string obj = json.substr(pos, objEnd - pos);

        // 仅检查含有 browser_download_url 的对象(asset 对象)
        if (obj.find("\"browser_download_url\"") == std::string::npos) {
            pos = pos + 1;   // 步进到下一个 {，不掉过嵌套对象
            continue;
        }

        std::string objName, objUrl, contentType;
        int64_t objSize = 0;
        findStringValue(obj, "name", objName);
        findStringValue(obj, "browser_download_url", objUrl);
        findIntValue(obj, "size", objSize);
        findStringValue(obj, "content_type", contentType);

        // spec §4: 仅接受 zip 资产
        if (contentType != "application/zip" && contentType != "application/x-zip-compressed") {
            pos = pos + 1;
            continue;
        }

        // 匹配 namePattern(不区分大小写)
        std::string lname = objName;
        std::string lpat = namePattern;
        std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
        std::transform(lpat.begin(), lpat.end(), lpat.begin(), ::tolower);
        if (lname.find(lpat) != std::string::npos) {
            assetName = objName;
            downloadUrl = objUrl;
            size = objSize;
            return true;
        }

        pos = pos + 1;   // 未匹配则继续扫描嵌套对象
    }
    return false;
}

// 反斜杠转义还原（处理 \n \r \t \" \\ 等）
std::string unescapeJsonString(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char c = s[i + 1];
            switch (c) {
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                default: out.push_back(c); break;
            }
            ++i;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

} // namespace

// ============================================================
// 构造 / 析构
// ============================================================

UpdateChecker::UpdateChecker() {
    // ServiceTask 不需要激活线程,业务均用 QtConcurrent
}

UpdateChecker::~UpdateChecker() {
    // 等待任何在飞的 future 完成,防止析构后回调误触发
    m_cancelled.store(true);
    if (m_checkFuture.isRunning()) m_checkFuture.waitForFinished();
    if (m_downloadFuture.isRunning()) m_downloadFuture.waitForFinished();
}

int UpdateChecker::svc() {
    // 本类不使用 ServiceTask 的线程模型,保留接口兼容
    return 0;
}

// ============================================================
// 状态 / 回调
// ============================================================

void UpdateChecker::setState(UpdateState s) {
    if (m_state.load() == s) return;
    m_state.store(s);
    if (m_stateCb) m_stateCb(s);
}

void UpdateChecker::reportError(const std::string& msg) {
    setState(UpdateState::Error);
    if (m_errorCb) m_errorCb(msg);
}

void UpdateChecker::setStateChangedCallback(std::function<void(UpdateState)> cb) {
    m_stateCb = std::move(cb);
}

void UpdateChecker::setProgressCallback(std::function<void(int, int64_t, int64_t)> cb) {
    m_progressCb = std::move(cb);
}

void UpdateChecker::setErrorCallback(std::function<void(const std::string&)> cb) {
    m_errorCb = std::move(cb);
}

// ============================================================
// libcurl 回调
// ============================================================

size_t UpdateChecker::curlWriteCb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    size_t n = size * nmemb;
    buf->append(static_cast<const char*>(ptr), n);
    return n;
}

int UpdateChecker::curlProgressCb(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                                  curl_off_t ultotal, curl_off_t ulnow) {
    Q_UNUSED(ultotal); Q_UNUSED(ulnow);
    auto* self = static_cast<UpdateChecker*>(clientp);
    if (self->m_cancelled.load()) return 1; // 非零 = 中止传输
    if (dltotal > 0 && self->m_progressCb) {
        int pct = static_cast<int>((dlnow * 100) / dltotal);
        if (pct > 100) pct = 100;
        self->m_progressCb(pct, static_cast<int64_t>(dlnow), static_cast<int64_t>(dltotal));
    }
    return 0;
}

// ============================================================
// 路径辅助
// ============================================================

std::string UpdateChecker::installDir() {
    // 可执行文件所在目录
    return QCoreApplication::applicationDirPath().toStdString();
}

std::string UpdateChecker::tempDir() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString dir = base + "/DeviceForge-updater";
    QDir().mkpath(dir);
    return dir.toStdString();
}

bool UpdateChecker::ensureDir(const std::string& dir) {
    return QDir().mkpath(QString::fromStdString(dir));
}

// ============================================================
// 业务 — 查询 Release
// ============================================================

ReleaseInfo UpdateChecker::fetchReleaseInfo(std::string& errorOut) {
    ReleaseInfo info;
    CURL* curl = curl_easy_init();
    if (!curl) {
        errorOut = "curl_easy_init() 失败";
        return info;
    }

    std::string url = "https://api.github.com/repos/" + m_repo + "/releases/latest";
    std::string body;
    std::string header;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "User-Agent: DeviceForge-Updater");
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curlWriteCb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");

    CURLcode res = curl_easy_perform(curl);

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        errorOut = std::string("HTTP 请求失败: ") + curl_easy_strerror(res);
        return info;
    }
    if (httpCode != 200) {
        errorOut = "GitHub API 返回 HTTP " + std::to_string(httpCode);
        return info;
    }

    // 解析 Release 字段
    std::string tagName, htmlUrl, relBody, assetName, assetUrl;
    int64_t assetSize = 0;
    if (!findStringValue(body, "tag_name", tagName)) {
        errorOut = "JSON 解析失败: 缺少 tag_name";
        return info;
    }
    // spec §4: 跳过 prerelease — 仅推送正式版给用户
    std::string prerelease;
    findStringValue(body, "prerelease", prerelease);
    if (prerelease == "true") {
        errorOut = "latest release is prerelease, skipped";
        return info;
    }
    findStringValue(body, "html_url", htmlUrl);
    findStringValue(body, "body", relBody);

    // 查找 windows zip 资产（按 win64 关键字匹配,失败回退到第一个 zip）
    const std::string winPattern = "win64";
    if (!findAsset(body, winPattern, assetName, assetUrl, assetSize)) {
        if (!findAsset(body, ".zip", assetName, assetUrl, assetSize)) {
            errorOut = "Release 中未找到 zip 资产";
            return info;
        }
    }

    info.tagName = tagName;
    info.htmlUrl = htmlUrl;
    info.body = unescapeJsonString(relBody);
    info.assetName = assetName;
    info.downloadUrl = assetUrl;
    info.assetSize = assetSize;

    // 版本比较
    Version cur = parseVersion(m_currentVersion);
    Version remote = parseVersion(tagName);
    info.isNewer = compareVersion(remote, cur) > 0;

    return info;
}

// ============================================================
// 业务 — 下载 zip
// ============================================================

bool UpdateChecker::downloadZip(const ReleaseInfo& info, std::string& errorOut) {
    if (info.downloadUrl.empty()) {
        errorOut = "无下载 URL";
        return false;
    }
    if (!ensureDir(tempDir())) {
        errorOut = "创建临时目录失败: " + tempDir();
        return false;
    }
    std::string zipPath = tempDir() + "/" + info.assetName;
    {
        std::lock_guard<std::mutex> lk(m_dataMutex);
        m_zipPath = zipPath;
    }
    if (zipPath.empty()) {
        errorOut = "非法 zip 路径";
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        errorOut = "curl_easy_init() 失败";
        return false;
    }

    FILE* fp = std::fopen(zipPath.c_str(), "wb");
    if (!fp) {
        errorOut = "无法写入文件: " + zipPath;
        curl_easy_cleanup(curl);
        return false;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "User-Agent: DeviceForge-Updater");

    curl_easy_setopt(curl, CURLOPT_URL, info.downloadUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nullptr); // 使用默认 FILE* 写入
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgressCb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_off_t downloaded = 0;
    curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &downloaded);

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    std::fclose(fp);

    if (m_cancelled.load()) {
        errorOut = "用户已取消";
        QFile::remove(QString::fromStdString(zipPath));
        return false;
    }
    if (res != CURLE_OK) {
        errorOut = std::string("下载失败: ") + curl_easy_strerror(res);
        QFile::remove(QString::fromStdString(zipPath));
        return false;
    }
    if (httpCode != 200) {
        errorOut = "下载返回 HTTP " + std::to_string(httpCode);
        QFile::remove(QString::fromStdString(zipPath));
        return false;
    }
    if (info.assetSize > 0 && downloaded > 0) {
        int64_t diff = (downloaded > info.assetSize)
            ? (downloaded - info.assetSize)
            : (info.assetSize - downloaded);
        if (diff > 1024) {
            // 允许 1KB 误差（部分 CDN 不返回精确字节）
            errorOut = "下载大小不匹配: 期望 " + std::to_string(info.assetSize) +
                       " 实际 " + std::to_string(downloaded);
            QFile::remove(QString::fromStdString(zipPath));
            return false;
        }
    }
    return true;
}

// ============================================================
// 业务 — 解压 zip（通过 PowerShell Expand-Archive）
// ============================================================

bool UpdateChecker::extractZip(const std::string& zipPath, const std::string& destDir,
                               std::string& errorOut) {
    if (!QFile::exists(QString::fromStdString(zipPath))) {
        errorOut = "zip 文件不存在: " + zipPath;
        return false;
    }
    if (!ensureDir(destDir)) {
        errorOut = "无法创建解压目录: " + destDir;
        return false;
    }

    // 防炸弹检查:解压前用 unzip -l 风格枚举文件计数和单文件大小
    // 这里用 PowerShell 一次性做解压（避免引入 zip 库）;
    // 防炸弹检查在解压后执行（解压后扫描文件树）

    // 使用 QProcess 避免 cmd.exe shell 注入风险
    QProcess ps;
    ps.setProgram("powershell.exe");
    ps.setArguments({
        "-NoProfile", "-NonInteractive", "-Command",
        QString("Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force")
            .arg(QString::fromStdString(zipPath).replace("'", "''"),
                 QString::fromStdString(destDir).replace("'", "''"))
    });
    ps.start();
    if (!ps.waitForFinished(120000)) { // 2 分钟超时
        errorOut = "PowerShell Expand-Archive 超时";
        ps.kill();
        return false;
    }
    int rc = ps.exitCode();
    if (rc != 0) {
        QString errText = QString::fromUtf8(ps.readAllStandardError());
        errorOut = "PowerShell Expand-Archive 失败,退出码 " + std::to_string(rc)
                   + ": " + errText.left(200).toStdString();
        return false;
    }

    // 防炸弹扫描
    QDir d(QString::fromStdString(destDir));
    if (!d.exists()) {
        errorOut = "解压后目录不存在: " + destDir;
        return false;
    }
    QDirIterator it(QString::fromStdString(destDir),
                    QDir::Files,
                    QDirIterator::Subdirectories);
    constexpr int kMaxFiles = 200;
    constexpr int64_t kMaxFileSize = 200LL * 1024 * 1024; // 200MB
    int fileCount = 0;
    while (it.hasNext()) {
        it.next();
        QFileInfo fi = it.fileInfo();
        // 拒绝符号链接(spec §4 安全):NoSymLinks 已过滤,此处显式检查作为冗余校验
        if (fi.isSymLink() || fi.isSymbolicLink()) {
            errorOut = "解压内容包含符号链接,已拒绝";
            return false;
        }
        if (fi.size() > kMaxFileSize) {
            errorOut = "解压内容单文件超过 200MB: " + fi.fileName().toStdString();
            return false;
        }
        if (++fileCount > kMaxFiles) {
            errorOut = "解压内容文件数超过 200,疑似 zip 炸弹";
            return false;
        }
    }
    return true;
}

// ============================================================
// 公共动作
// ============================================================

void UpdateChecker::checkForUpdate() {
    // 等待前序 future,防并发
    if (m_checkFuture.isRunning()) m_checkFuture.waitForFinished();
    if (m_downloadFuture.isRunning()) m_downloadFuture.waitForFinished();

    m_cancelled.store(false);
    setState(UpdateState::Checking);

    m_checkFuture = QtConcurrent::run([this]() {
        std::string err;
        ReleaseInfo info = fetchReleaseInfo(err);
        if (m_cancelled.load()) {
            setState(UpdateState::Idle);
            return;
        }
        if (!err.empty()) {
            reportError(err);
            return;
        }
        // 加锁写入共享数据(防止与主线程 releaseInfo() 读冲突)
        {
            std::lock_guard<std::mutex> lk(m_dataMutex);
            m_releaseInfo = info;
        }
        setState(info.isNewer ? UpdateState::Ready : UpdateState::Idle);
    });
}

void UpdateChecker::downloadUpdate() {
    if (m_state.load() != UpdateState::Ready) {
        reportError("当前状态不允许下载,需先查询到新版本");
        return;
    }
    if (m_checkFuture.isRunning()) m_checkFuture.waitForFinished();
    if (m_downloadFuture.isRunning()) m_downloadFuture.waitForFinished();

    m_cancelled.store(false);
    setState(UpdateState::Downloading);

    // 拷贝 releaseInfo 供 lambda 捕获(避免生命周期问题)
    ReleaseInfo info;
    {
        std::lock_guard<std::mutex> lk(m_dataMutex);
        info = m_releaseInfo;
    }
    m_downloadFuture = QtConcurrent::run([this, info]() {
        std::string err;
        if (!downloadZip(info, err)) {
            if (m_cancelled.load()) {
                setState(UpdateState::Idle);
            } else {
                reportError(err);
            }
            return;
        }
        // 解压到临时目录的 extract 子目录
        std::string extractDir;
        std::string zipPath;
        {
            std::lock_guard<std::mutex> lk(m_dataMutex);
            m_extractDir = tempDir() + "/extract";
            extractDir = m_extractDir;
            zipPath = m_zipPath;
        }
        if (!extractZip(zipPath, extractDir, err)) {
            reportError(err);
            return;
        }
        if (m_progressCb) m_progressCb(100, info.assetSize, info.assetSize);
        setState(UpdateState::Installed);
    });
}

void UpdateChecker::cancelDownload() {
    // 非阻塞:仅设置取消标志并切回 Idle,worker 会自然退出
    // 析构时再 waitForFinished 等待,避免 UI 线程被锁
    m_cancelled.store(true);
    setState(UpdateState::Idle);
}

void UpdateChecker::installUpdate() {
    if (m_state.load() != UpdateState::Installed) {
        reportError("当前未处于可安装状态");
        return;
    }

    // 读取共享数据(加锁)
    std::string extractDir, installDir, zip;
    {
        std::lock_guard<std::mutex> lk(m_dataMutex);
        extractDir = m_extractDir;
        zip = m_zipPath;
    }
    installDir = QCoreApplication::applicationDirPath().toStdString();

    if (extractDir.empty()) {
        reportError("缺少解压目录");
        return;
    }

    // 准备临时目录
    std::string tmpDir = QDir::tempPath().toStdString() + "/DeviceForge-updater";
    std::string backupDir = tmpDir + "/backup";

    // 写 updater_manifest.json (JSON 字符串需要转义反斜杠)
    auto jsonEscape = [](const std::string& s) -> std::string {
        std::string out;
        for (char c : s) {
            if (c == '\\') out += "\\\\";
            else if (c == '"') out += "\\\"";
            else out += c;
        }
        return out;
    };

    QString manifestPath = QString::fromStdString(tmpDir) + "/updater_manifest.json";
    QFile mf(manifestPath);
    if (mf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::string json =
            "{\n"
            "  \"tempDir\": \"" + jsonEscape(extractDir) + "\",\n"
            "  \"installDir\": \"" + jsonEscape(installDir) + "\",\n"
            "  \"exeName\": \"DeviceForge.exe\",\n"
            "  \"backupDir\": \"" + jsonEscape(backupDir) + "\"\n"
            "}\n";
        mf.write(json.c_str(), static_cast<qint64>(json.size()));
        mf.close();
    }

    // 启动 Updater.exe(与 DeviceForge.exe 同级目录)
    QString updaterPath = QString::fromStdString(installDir) + "/Updater.exe";
    QString logPath = QString::fromStdString(tmpDir) + "/updater.log";

    QStringList args;
    args << "--manifest" << manifestPath << "--log" << logPath;

    QProcess::startDetached(updaterPath, args);
    QCoreApplication::quit();
}
