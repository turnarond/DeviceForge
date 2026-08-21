# 在线更新功能 — 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现 DeviceForge 应用内在线更新：GitHub Releases API 检查 → 下载 → 校验 → Updater.exe 替换 → 重启。

**Architecture:** UpdateChecker 继承 ServiceTask (同 ToolBackend 模式)，libcurl 调用 GitHub API + 下载 zip。QProcess 拉起独立 Updater.exe（无 Qt，~100KB）在 DeviceForge 退出后替换文件。UI 层：非模态 UpdateDialog + 状态栏版本标签 + 菜单"检查更新"。

**Tech Stack:** C++17, Qt 6 (Core/Widgets/Network), libcurl, Win32 API (Updater)

**Spec:** `docs/superpowers/specs/2026-07-14-online-update-design.md`

## Global Constraints

- 仅 Windows x64（Linux 适配为中期目标，更新功能预留 Win32 API 路径为平台相关）
- 复用 libcurl（已在 FtpAdapter 中使用），不引入新第三方依赖
- Updater.exe 独立 CMake target，不链接 Qt
- UI 遵循「琴色是动词」主题体系：琴色 #F0A030 仅用于可操作/活跃信号态
- 服务组件继承 ServiceTask，遵循 OnStart/OnStop/svc() 生命周期
- 测试使用 QtTest + CTest（同 tst_nrec 模式）
- 代码和注释使用中文

---

### Task 1: 数据类型与常量定义

**Files:**
- Create: `src/updater/UpdateTypes.h`

**Interfaces:**
- Produces: `UpdateState` enum, `ReleaseInfo` struct, `Version` struct, `parseVersion()`, `compareVersion()`

- [ ] **Step 1: 创建数据定义头文件**

```cpp
// src/updater/UpdateTypes.h
#pragma once
#include <string>
#include <cstdint>

// 更新检查状态机
enum class UpdateState {
    Idle,
    Checking,
    Ready,        // 有新版本可用
    Downloading,
    Installed,    // 下载+校验完成，等待安装
    Error         // 静默错误（网络/解析失败）
};

// GitHub Release 解析结果
struct ReleaseInfo {
    std::string tagName;        // "v2.2.0"
    std::string htmlUrl;        // release 页面 URL
    std::string body;           // release notes (markdown)
    std::string assetName;      // "DeviceForge-v2.2.0-win64.zip"
    std::string downloadUrl;    // browser_download_url
    int64_t     assetSize = 0;  // 字节数
    bool        isNewer = false; // tag > current?
};

// 语义化版本号
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;

    std::string toString() const {
        return std::to_string(major) + "." +
               std::to_string(minor) + "." +
               std::to_string(patch);
    }
};

// 解析 "v2.1.0" 或 "2.1.0" → Version
inline Version parseVersion(const std::string& tag) {
    Version v;
    const char* s = tag.c_str();
    if (*s == 'v' || *s == 'V') ++s;
    v.major = std::atoi(s);
    while (*s && *s != '.') ++s; if (*s == '.') ++s;
    v.minor = std::atoi(s);
    while (*s && *s != '.') ++s; if (*s == '.') ++s;
    v.patch = std::atoi(s);
    return v;
}

// 返回: <0 (a更旧), 0 (相同), >0 (a更新)
inline int compareVersion(const Version& a, const Version& b) {
    if (a.major != b.major) return a.major - b.major;
    if (a.minor != b.minor) return a.minor - b.minor;
    return a.patch - b.patch;
}
```

- [ ] **Step 2: 编译验证**

```bash
# 在 build/ 目录
cmake --build . --config Release --target DeviceForge 2>&1 | head
# 预期: UpdateTypes.h 被包含检查语法，编译通过（此任务仅头文件，后续 Task 包含它时验证）
```

- [ ] **Step 3: Commit**

```bash
git add src/updater/UpdateTypes.h
git commit -m "feat(updater): 添加 UpdateTypes — 更新状态/版本/ReleaseInfo 数据结构"
```

---

### Task 2: UpdateChecker ServiceTask

**Files:**
- Create: `src/updater/UpdateChecker.h`
- Create: `src/updater/UpdateChecker.cpp`

**Interfaces:**
- Consumes: `UpdateTypes.h` (Task 1), `ServiceTask` (lwserverbase), `curl/curl.h` (libcurl)
- Produces: `class UpdateChecker : public lwserverbase::core::ServiceTask`
  - `void checkForUpdate()` — 手动触发检查
  - `void downloadUpdate()` — 开始下载
  - `void cancelDownload()` — 取消下载
  - `UpdateState state() const`
  - `const ReleaseInfo& releaseInfo() const`
  - Signals (via callbacks): `setStateChangedCallback`, `setProgressCallback`, `setErrorCallback`

- [ ] **Step 1: 创建 UpdateChecker.h**

```cpp
// src/updater/UpdateChecker.h
#pragma once
#include "lwserverbase/core/ServiceTask.h"
#include "UpdateTypes.h"
#include <functional>
#include <string>
#include <memory>
#include <atomic>
#include <QFuture>

class UpdateChecker : public lwserverbase::core::ServiceTask {
public:
    UpdateChecker();
    ~UpdateChecker() override;

    // ServiceTask 实现
    int svc() override;

    // API: 手动触发检查
    void checkForUpdate();

    // API: 开始下载（仅在 state==Ready 时有效）
    void downloadUpdate();

    // API: 取消下载
    void cancelDownload();

    // API: 安装（启动 Updater.exe 并退出）
    void installUpdate();

    // 状态查询
    UpdateState state() const { return m_state.load(); }
    const ReleaseInfo& releaseInfo() const { return m_releaseInfo; }

    // 回调设置（线程安全，Widget 层通过 Qt::QueuedConnection 回主线程）
    void setStateChangedCallback(std::function<void(UpdateState)> cb);
    void setProgressCallback(std::function<void(int pct, int64_t downloaded, int64_t total)> cb);
    void setErrorCallback(std::function<void(const std::string&)> cb);

private:
    // 执行 GitHub API 请求（在线程中调用）
    ReleaseInfo fetchReleaseInfo(std::string& errorOut);

    // libcurl 写回调: 追加响应体到 string
    static size_t writeCb(void* ptr, size_t size, size_t nmemb, void* userdata);

    // libcurl 进度回调: 报告下载进度
    static int progressCb(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                          curl_off_t ultotal, curl_off_t ulnow);

    // 解压 zip 到临时目录
    bool extractZip(const std::string& zipPath, const std::string& destDir,
                    std::string& errorOut);

    // 获取当前程序安装目录（exe 所在目录）
    static std::string installDir();

    // 获取临时目录 (%TEMP%/DeviceForge/)
    static std::string tempDir();

    std::atomic<UpdateState> m_state{UpdateState::Idle};
    ReleaseInfo m_releaseInfo;
    std::atomic<bool> m_cancelled{false};

    // 下载产物路径
    std::string m_zipPath;
    std::string m_extractDir;

    // 回调（跨线程安全，通过 QMetaObject::invokeMethod 回调主线程）
    std::function<void(UpdateState)> m_stateCb;
    std::function<void(int, int64_t, int64_t)> m_progressCb;
    std::function<void(const std::string&)> m_errorCb;

    QFuture<void> m_checkFuture;     // 异步检查任务
    QFuture<void> m_downloadFuture;  // 异步下载任务
};
```

- [ ] **Step 2: 实现 UpdateChecker.cpp — checkForUpdate**

```cpp
// src/updater/UpdateChecker.cpp
#include "UpdateChecker.h"
#include <curl/curl.h>
#include <lwlog/lwlog.h>
#include <QtConcurrent/QtConcurrent>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <sstream>
#include <cstring>
#include <thread>
#include <chrono>

// JSON 简易解析器（避免引入第三方 JSON 库）
namespace {
    // 从 JSON 字符串中提取 "key":"value" 或 "key":value 的值
    std::string jsonString(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\":";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return {};
        pos += search.size();
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n')) ++pos;
        if (pos >= json.size()) return {};
        if (json[pos] == '"') {
            ++pos;
            size_t end = json.find('"', pos);
            if (end == std::string::npos) return {};
            return json.substr(pos, end - pos);
        } else {
            size_t end = pos;
            while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != '\n') ++end;
            return json.substr(pos, end - pos);
        }
    }

    int64_t jsonInt(const std::string& json, const std::string& key) {
        std::string v = jsonString(json, key);
        if (v.empty()) return 0;
        return std::atoll(v.c_str());
    }

    bool jsonBool(const std::string& json, const std::string& key) {
        return jsonString(json, key) == "true";
    }
} // anonymous namespace

UpdateChecker::UpdateChecker() {}

UpdateChecker::~UpdateChecker() {
    cancelDownload();
    if (m_checkFuture.isRunning()) m_checkFuture.waitForFinished();
    if (m_downloadFuture.isRunning()) m_downloadFuture.waitForFinished();
}

int UpdateChecker::svc() {
    LWLOG_I("UpdateChecker 线程启动");
    while (isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    LWLOG_I("UpdateChecker 线程退出");
    return 0;
}

void UpdateChecker::checkForUpdate() {
    if (m_state.load() == UpdateState::Checking ||
        m_state.load() == UpdateState::Downloading) {
        return; // 已在检查或下载中
    }

    m_state.store(UpdateState::Checking);
    if (m_stateCb) m_stateCb(UpdateState::Checking);

    if (m_checkFuture.isRunning()) m_checkFuture.waitForFinished();

    m_checkFuture = QtConcurrent::run([this]() {
        std::string error;
        ReleaseInfo info = fetchReleaseInfo(error);

        if (!error.empty()) {
            LWLOG_ERROR(("UpdateChecker: " + error).c_str());
            m_state.store(UpdateState::Error);
            if (m_errorCb) m_errorCb(error);
            return;
        }

        // 比较版本
        Version current = parseVersion(std::to_string(DEVICEFORGE_VERSION_MAJOR) +
                                       "." + std::to_string(DEVICEFORGE_VERSION_MINOR) +
                                       "." + std::to_string(DEVICEFORGE_VERSION_PATCH));
        Version remote = parseVersion(info.tagName);
        if (compareVersion(remote, current) > 0) {
            info.isNewer = true;
            m_releaseInfo = info;
            m_state.store(UpdateState::Ready);
            if (m_stateCb) m_stateCb(UpdateState::Ready);
        } else {
            m_state.store(UpdateState::Idle);
            if (m_stateCb) m_stateCb(UpdateState::Idle);
        }
    });
}

ReleaseInfo UpdateChecker::fetchReleaseInfo(std::string& errorOut) {
    ReleaseInfo info;
    CURL* curl = curl_easy_init();
    if (!curl) {
        errorOut = "curl_easy_init() 失败";
        return info;
    }

    std::string response;
    std::string url = "https://api.github.com/repos/turnarond/DeviceForge/releases/latest";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "DeviceForge-UpdateChecker");

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        errorOut = std::string("GitHub API 请求失败: ") + curl_easy_strerror(res);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return info;
    }

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (httpCode != 200) {
        errorOut = "GitHub API 返回 HTTP " + std::to_string(httpCode);
        return info;
    }

    // 过滤 pre-release
    if (jsonBool(response, "prerelease")) {
        errorOut = "最新版本为 pre-release，跳过";
        return info;
    }

    // 解析资产信息 (取第一个匹配 win64.zip 的 asset)
    info.tagName  = jsonString(response, "tag_name");
    info.htmlUrl  = jsonString(response, "html_url");
    info.body     = jsonString(response, "body");

    // 简单解析 assets 数组中的第一个 zip 资产
    size_t assetsPos = response.find("\"assets\":[");
    if (assetsPos == std::string::npos) {
        errorOut = "Release 中无 assets";
        return info;
    }

    // 在 assets 数组中查找第一个 zip 资产
    size_t assetPos = assetsPos;
    while ((assetPos = response.find("\"name\":", assetPos)) != std::string::npos) {
        std::string name = jsonString(response.substr(assetPos), "name");
        if (name.find("win64.zip") != std::string::npos ||
            name.find("DeviceForge") != std::string::npos) {
            info.assetName = name;
            info.downloadUrl = jsonString(response.substr(assetPos), "browser_download_url");
            info.assetSize = jsonInt(response.substr(assetPos), "size");

            // 验证 content_type
            std::string ct = jsonString(response.substr(assetPos), "content_type");
            if (ct != "application/zip" && ct != "application/x-zip-compressed") {
                errorOut = "asset content_type 非 zip: " + ct;
                return ReleaseInfo{};
            }
            break;
        }
        assetPos += 8; // 跳过当前 "name":
    }

    if (info.downloadUrl.empty()) {
        errorOut = "未找到 win64.zip 资产";
    }

    return info;
}

size_t UpdateChecker::writeCb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* str = static_cast<std::string*>(userdata);
    size_t total = size * nmemb;
    str->append(static_cast<char*>(ptr), total);
    return total;
}
```

- [ ] **Step 3: 实现 downloadUpdate、extractZip、installUpdate**

```cpp
void UpdateChecker::downloadUpdate() {
    if (m_state.load() != UpdateState::Ready) return;

    m_cancelled = false;
    m_state.store(UpdateState::Downloading);
    if (m_stateCb) m_stateCb(UpdateState::Downloading);

    if (m_downloadFuture.isRunning()) m_downloadFuture.waitForFinished();

    m_downloadFuture = QtConcurrent::run([this]() {
        // 准备临时目录
        QString tmpDir = QString::fromStdString(tempDir());
        QDir().mkpath(tmpDir);

        QString version = QString::fromStdString(m_releaseInfo.tagName);
        m_zipPath = (tmpDir + "/" + QString::fromStdString(m_releaseInfo.assetName)).toStdString();
        m_extractDir = (tmpDir + "/" + version).toStdString();

        // 下载
        CURL* curl = curl_easy_init();
        if (!curl) {
            if (m_errorCb) m_errorCb("curl_easy_init() 失败");
            m_state.store(UpdateState::Error);
            return;
        }

        FILE* file = fopen(m_zipPath.c_str(), "wb");
        if (!file) {
            curl_easy_cleanup(curl);
            if (m_errorCb) m_errorCb("无法创建临时文件: " + m_zipPath);
            m_state.store(UpdateState::Error);
            return;
        }

        curl_easy_setopt(curl, CURLOPT_URL, m_releaseInfo.downloadUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCb);

        CURLcode res = curl_easy_perform(curl);
        fclose(file);
        curl_easy_cleanup(curl);

        if (m_cancelled) {
            QFile::remove(QString::fromStdString(m_zipPath));
            m_state.store(UpdateState::Ready); // 回到 ready，可重试
            return;
        }

        if (res != CURLE_OK) {
            if (m_errorCb) {
                m_errorCb(std::string("下载失败: ") + curl_easy_strerror(res));
            }
            m_state.store(UpdateState::Error);
            return;
        }

        // 校验文件大小
        QFileInfo fi(QString::fromStdString(m_zipPath));
        if (fi.size() != static_cast<qint64>(m_releaseInfo.assetSize)) {
            if (m_errorCb) m_errorCb("文件大小校验失败");
            m_state.store(UpdateState::Error);
            return;
        }

        // 解压
        std::string err;
        if (!extractZip(m_zipPath, m_extractDir, err)) {
            if (m_errorCb) m_errorCb("解压失败: " + err);
            m_state.store(UpdateState::Error);
            return;
        }

        m_state.store(UpdateState::Installed);
        if (m_stateCb) m_stateCb(UpdateState::Installed);
    });
}

void UpdateChecker::cancelDownload() {
    m_cancelled = true;
}

void UpdateChecker::installUpdate() {
    if (m_state.load() != UpdateState::Installed) return;

    // 写 manifest
    QString manifestPath = QString::fromStdString(tempDir()) + "/updater_manifest.json";
    QFile mf(manifestPath);
    if (mf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::string json =
            "{\n"
            "  \"tempDir\": \"" + m_extractDir + "\",\n"
            "  \"installDir\": \"" + installDir() + "\",\n"
            "  \"exeName\": \"DeviceForge.exe\",\n"
            "  \"backupDir\": \"" + tempDir() + "/backup\"\n"
            "}\n";
        mf.write(json.c_str(), static_cast<qint64>(json.size()));
        mf.close();
    }

    // 启动 Updater.exe（与 DeviceForge.exe 同级目录）
    QString updaterPath = QString::fromStdString(installDir()) + "/Updater.exe";
    QString logPath = QString::fromStdString(tempDir()) + "/updater.log";

    QStringList args;
    args << "--manifest" << manifestPath << "--log" << logPath;

    QProcess::startDetached(updaterPath, args);
    QCoreApplication::quit();
}

int UpdateChecker::progressCb(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                               curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    auto* self = static_cast<UpdateChecker*>(clientp);
    if (self->m_progressCb && dltotal > 0) {
        int pct = static_cast<int>((static_cast<double>(dlnow) / dltotal) * 100.0);
        self->m_progressCb(pct, static_cast<int64_t>(dlnow), static_cast<int64_t>(dltotal));
    }
    return self->m_cancelled ? 1 : 0;
}

bool UpdateChecker::extractZip(const std::string& zipPath, const std::string& destDir,
                                std::string& errorOut) {
    // 使用 Windows 内置 COM 接口解压 zip（无额外依赖）
    // 备选方案：调用 PowerShell Expand-Archive
    QDir().mkpath(QString::fromStdString(destDir));

    QProcess ps;
    ps.start("powershell", {
        "-NoProfile", "-Command",
        QString("Expand-Archive -Path '%1' -DestinationPath '%2' -Force")
            .arg(QString::fromStdString(zipPath))
            .arg(QString::fromStdString(destDir))
    });
    if (!ps.waitForFinished(60000)) {
        errorOut = "解压超时";
        return false;
    }
    if (ps.exitCode() != 0) {
        errorOut = ps.readAllStandardError().toStdString();
        return false;
    }

    // 防 zip 炸弹：检查文件数
    int fileCount = 0;
    QDirIterator it(QString::fromStdString(destDir),
                    QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        if (++fileCount > 200) {
            errorOut = "zip 内文件数超过限制 (200)";
            return false;
        }
        if (it.fileInfo().size() > 200 * 1024 * 1024) {
            errorOut = "zip 内单文件超过 200MB";
            return false;
        }
    }
    return true;
}

std::string UpdateChecker::installDir() {
    return QCoreApplication::applicationDirPath().toStdString();
}

std::string UpdateChecker::tempDir() {
    return QDir::tempPath().toStdString() + "/DeviceForge";
}

// 回调设置
void UpdateChecker::setStateChangedCallback(std::function<void(UpdateState)> cb) {
    m_stateCb = std::move(cb);
}
void UpdateChecker::setProgressCallback(
    std::function<void(int, int64_t, int64_t)> cb) {
    m_progressCb = std::move(cb);
}
void UpdateChecker::setErrorCallback(std::function<void(const std::string&)> cb) {
    m_errorCb = std::move(cb);
}
```

- [ ] **Step 4: 编译验证**

```bash
cmake --build . --config Release 2>&1
# 预期: UpdateChecker.cpp 通过编译（需要先完成 Task 7 CMake 变更）
```

- [ ] **Step 5: Commit**

```bash
git add src/updater/UpdateChecker.h src/updater/UpdateChecker.cpp
git commit -m "feat(updater): UpdateChecker ServiceTask — GitHub API 查询 + 下载 + 校验 + 解压"
```

---

### Task 3: Updater.exe 独立替换进程

**Files:**
- Create: `src/updater/UpdaterMain.cpp`

**Interfaces:**
- Consumes: `updater_manifest.json`（由 UpdateChecker::installUpdate() 写入）
- Produces: `Updater.exe`（独立可执行文件，无 Qt）

- [ ] **Step 1: 创建 UpdaterMain.cpp**

```cpp
// src/updater/UpdaterMain.cpp
// Updater.exe — DeviceForge 在线更新文件替换工具
// 无 Qt 依赖，纯 Win32 API + CRT，~100KB 静态链接

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

// 简易 JSON 解析（不引入第三方库）
namespace {
    std::string jsonString(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\": \"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) {
            // 尝试无引号值（数值）
            search = "\"" + key + "\": ";
            pos = json.find(search);
            if (pos == std::string::npos) return {};
            pos += search.size();
        } else {
            pos += search.size();
        }
        size_t end = json.find(pos, '"');
        if (end == std::string::npos) {
            // 无引号值，找逗号或换行
            end = json.find_first_of(",\n}", pos);
        }
        if (end == std::string::npos) return {};
        std::string val = json.substr(pos, end - pos);
        // 去尾换行
        while (!val.empty() && (val.back() == '\r' || val.back() == '\n')) val.pop_back();
        return val;
    }
}

struct Manifest {
    std::string tempDir;
    std::string installDir;
    std::string exeName;
    std::string backupDir;
};

bool readManifest(const std::string& path, Manifest& m, std::string& err) {
    std::ifstream f(path);
    if (!f.is_open()) {
        err = "无法打开 manifest: " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string json = ss.str();
    f.close();

    m.tempDir    = jsonString(json, "tempDir");
    m.installDir = jsonString(json, "installDir");
    m.exeName    = jsonString(json, "exeName");
    m.backupDir  = jsonString(json, "backupDir");

    if (m.tempDir.empty() || m.installDir.empty() || m.exeName.empty()) {
        err = "manifest 字段不完整";
        return false;
    }
    return true;
}

FILE* g_logFile = nullptr;

void log(const char* fmt, ...) {
    if (!g_logFile) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(g_logFile, fmt, args);
    fflush(g_logFile);
    va_end(args);
}

// 等待进程退出
bool waitForProcessExit(const char* exeName, DWORD timeoutMs) {
    log("等待 %s 退出...\n", exeName);
    DWORD elapsed = 0;
    const DWORD interval = 500;
    while (elapsed < timeoutMs) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) break;

        PROCESSENTRY32 pe{};
        pe.dwSize = sizeof(pe);
        bool found = false;
        if (Process32First(snapshot, &pe)) {
            do {
                if (_stricmp(pe.szExeFile, exeName) == 0) {
                    found = true;
                    break;
                }
            } while (Process32Next(snapshot, &pe));
        }
        CloseHandle(snapshot);

        if (!found) {
            log("%s 已退出\n", exeName);
            return true;
        }
        Sleep(interval);
        elapsed += interval;
    }

    log("等待 %s 退出超时 (%lu ms)\n", exeName, timeoutMs);
    return false;
}

// 递归复制目录
bool copyDirectory(const char* src, const char* dst) {
    // 确保目标目录存在
    CreateDirectoryA(dst, nullptr);

    std::string searchPath = std::string(src) + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        log("FindFirstFile 失败: %s (err=%lu)\n", searchPath.c_str(), GetLastError());
        return false;
    }

    bool ok = true;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;

        std::string srcPath = std::string(src) + "\\" + fd.cFileName;
        std::string dstPath = std::string(dst) + "\\" + fd.cFileName;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!copyDirectory(srcPath.c_str(), dstPath.c_str())) ok = false;
        } else {
            if (!CopyFileA(srcPath.c_str(), dstPath.c_str(), FALSE)) {
                log("CopyFile 失败: %s -> %s (err=%lu)\n",
                    srcPath.c_str(), dstPath.c_str(), GetLastError());
                ok = false;
            }
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
    return ok;
}

// 递归删除目录
bool removeDirectory(const char* path) {
    std::string searchPath = std::string(path) + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return false;

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        std::string full = std::string(path) + "\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            removeDirectory(full.c_str());
        } else {
            DeleteFileA(full.c_str());
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
    return RemoveDirectoryA(path);
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // 解析命令行参数
    int argc;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::string manifestPath;
    std::string logPath;

    for (int i = 1; i < argc; ++i) {
        char buf[1024];
        WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, buf, sizeof(buf), nullptr, nullptr);
        if (strcmp(buf, "--manifest") == 0 && i + 1 < argc) {
            char buf2[1024];
            WideCharToMultiByte(CP_UTF8, 0, wargv[i + 1], -1, buf2, sizeof(buf2), nullptr, nullptr);
            manifestPath = buf2;
            ++i;
        } else if (strcmp(buf, "--log") == 0 && i + 1 < argc) {
            char buf2[1024];
            WideCharToMultiByte(CP_UTF8, 0, wargv[i + 1], -1, buf2, sizeof(buf2), nullptr, nullptr);
            logPath = buf2;
            ++i;
        }
    }
    LocalFree(wargv);

    // 打开日志
    if (!logPath.empty()) {
        g_logFile = fopen(logPath.c_str(), "a");
    }
    log("=== Updater 启动 ===\n");
    log("manifest: %s\n", manifestPath.c_str());

    // 读 manifest
    Manifest m;
    std::string err;
    if (!readManifest(manifestPath, m, err)) {
        log("FATAL: %s\n", err.c_str());
        if (g_logFile) fclose(g_logFile);
        MessageBoxA(nullptr, (std::string("更新失败: ") + err).c_str(),
                    "DeviceForge Updater", MB_ICONERROR);
        return 1;
    }

    // Step 1: 等待 DeviceForge 退出
    if (!waitForProcessExit(m.exeName.c_str(), 30000)) {
        int ret = MessageBoxA(nullptr,
            "DeviceForge 未能在 30 秒内退出。\n请手动关闭 DeviceForge 后点击 [重试]，或 [取消] 中止更新。",
            "DeviceForge Updater", MB_RETRYCANCEL | MB_ICONWARNING);
        if (ret == IDRETRY) {
            if (!waitForProcessExit(m.exeName.c_str(), 60000)) {
                MessageBoxA(nullptr, "更新失败：DeviceForge 仍未退出。", "DeviceForge Updater", MB_ICONERROR);
                if (g_logFile) fclose(g_logFile);
                return 1;
            }
        } else {
            log("用户取消更新\n");
            if (g_logFile) fclose(g_logFile);
            return 0;
        }
    }

    // Step 2: 备份
    log("创建备份: %s -> %s\n", m.installDir.c_str(), m.backupDir.c_str());
    if (!copyDirectory(m.installDir.c_str(), m.backupDir.c_str())) {
        log("WARN: 备份部分文件失败，继续...\n");
    }

    // Step 3: 替换文件
    log("替换文件: %s -> %s\n", m.tempDir.c_str(), m.installDir.c_str());
    if (!copyDirectory(m.tempDir.c_str(), m.installDir.c_str())) {
        log("ERROR: 文件替换失败，开始回滚\n");
        // 回滚
        copyDirectory(m.backupDir.c_str(), m.installDir.c_str());
        MessageBoxA(nullptr,
            "更新失败：文件替换出错。\n旧版本已恢复，请稍后重试。",
            "DeviceForge Updater", MB_ICONERROR);
        if (g_logFile) fclose(g_logFile);
        return 1;
    }

    // Step 4: 清理
    log("清理临时文件\n");
    removeDirectory(m.backupDir.c_str());
    removeDirectory(m.tempDir.c_str());
    // 保留 zip 和 manifest 供日志引用，也可删除
    DeleteFileA(manifestPath.c_str());

    // Step 5: 启动新版
    std::string exePath = m.installDir + "\\" + m.exeName;
    log("启动新版: %s\n", exePath.c_str());

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    if (!CreateProcessA(exePath.c_str(), nullptr,
                        nullptr, nullptr, FALSE,
                        0, nullptr, m.installDir.c_str(),
                        &si, &pi)) {
        log("ERROR: 启动新版失败 (err=%lu)\n", GetLastError());
        MessageBoxA(nullptr,
            "更新完成但启动新版失败。\n请手动运行 DeviceForge.exe。",
            "DeviceForge Updater", MB_ICONWARNING);
        if (g_logFile) fclose(g_logFile);
        return 1;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    log("=== Updater 完成 ===\n");
    if (g_logFile) fclose(g_logFile);
    return 0;
}
```

- [ ] **Step 2: Commit**

```bash
git add src/updater/UpdaterMain.cpp
git commit -m "feat(updater): Updater.exe — 独立 Win32 文件替换进程，无 Qt 依赖"
```

---

### Task 4: UpdaterProcess 封装 + UpdateDialog

**Files:**
- Create: `src/updater/UpdateDialog.h`
- Create: `src/updater/UpdateDialog.cpp`

**Interfaces:**
- Consumes: `UpdateChecker` (Task 2), `UpdateTypes.h` (Task 1)
- Produces: `class UpdateDialog : public QDialog` — 非模态更新对话框

- [ ] **Step 1: 创建 UpdateDialog.h**

```cpp
// src/updater/UpdateDialog.h
#pragma once
#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTextBrowser>
#include "UpdateTypes.h"

class UpdateChecker;

class UpdateDialog : public QDialog {
    Q_OBJECT
public:
    explicit UpdateDialog(UpdateChecker* checker, QWidget* parent = nullptr);

    void setReleaseInfo(const ReleaseInfo& info);
    void setState(UpdateState state);
    void setProgress(int pct, int64_t downloaded, int64_t total);

private slots:
    void onActionClicked();
    void onCancelClicked();

private:
    void setupUi();
    QString formatSize(int64_t bytes) const;

    UpdateChecker* m_checker;
    ReleaseInfo m_info;

    QLabel*       m_currentVersion = nullptr;
    QLabel*       m_newVersion     = nullptr;
    QLabel*       m_fileSize       = nullptr;
    QTextBrowser* m_releaseNotes   = nullptr;
    QProgressBar* m_progress       = nullptr;
    QLabel*       m_progressLabel  = nullptr;
    QPushButton*  m_actionBtn     = nullptr;
    QPushButton*  m_cancelBtn     = nullptr;
};
```

- [ ] **Step 2: 创建 UpdateDialog.cpp**

```cpp
// src/updater/UpdateDialog.cpp
#include "UpdateDialog.h"
#include "UpdateChecker.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QCoreApplication>
#include <QStyle>

UpdateDialog::UpdateDialog(UpdateChecker* checker, QWidget* parent)
    : QDialog(parent), m_checker(checker)
{
    setupUi();
    setModal(false);
    setWindowTitle("DeviceForge 在线更新");
    resize(520, 480);
}

void UpdateDialog::setupUi() {
    auto* main = new QVBoxLayout(this);
    main->setSpacing(10);

    // 版本信息区
    auto* infoGroup = new QGroupBox("版本信息", this);
    auto* infoLayout = new QVBoxLayout(infoGroup);

    m_currentVersion = new QLabel(this);
    m_newVersion     = new QLabel(this);
    m_fileSize       = new QLabel(this);

    QString currentVer = QString("%1.%2.%3")
        .arg(DEVICEFORGE_VERSION_MAJOR)
        .arg(DEVICEFORGE_VERSION_MINOR)
        .arg(DEVICEFORGE_VERSION_PATCH);
    m_currentVersion->setText("当前版本: v" + currentVer);

    infoLayout->addWidget(m_currentVersion);
    infoLayout->addWidget(m_newVersion);
    infoLayout->addWidget(m_fileSize);
    main->addWidget(infoGroup);

    // Release Notes
    auto* notesGroup = new QGroupBox("更新内容", this);
    auto* notesLayout = new QVBoxLayout(notesGroup);
    m_releaseNotes = new QTextBrowser(this);
    m_releaseNotes->setMaximumHeight(200);
    notesLayout->addWidget(m_releaseNotes);
    main->addWidget(notesGroup);

    // 进度
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setVisible(false);
    m_progressLabel = new QLabel(this);
    m_progressLabel->setVisible(false);
    main->addWidget(m_progress);
    main->addWidget(m_progressLabel);

    // 按钮
    auto* btnLayout = new QHBoxLayout();
    m_cancelBtn = new QPushButton("稍后再说", this);
    m_actionBtn = new QPushButton("下载并更新", this);
    m_actionBtn->setObjectName("btnPrimary");

    connect(m_cancelBtn, &QPushButton::clicked, this, &UpdateDialog::onCancelClicked);
    connect(m_actionBtn, &QPushButton::clicked, this, &UpdateDialog::onActionClicked);

    btnLayout->addStretch();
    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addWidget(m_actionBtn);
    main->addLayout(btnLayout);
}

void UpdateDialog::setReleaseInfo(const ReleaseInfo& info) {
    m_info = info;
    m_newVersion->setText(QString("最新版本: %1").arg(QString::fromStdString(info.tagName)));
    m_fileSize->setText(QString("文件大小: %1").arg(formatSize(info.assetSize)));
    m_releaseNotes->setMarkdown(QString::fromStdString(info.body));
}

void UpdateDialog::setState(UpdateState state) {
    switch (state) {
    case UpdateState::Ready:
        m_actionBtn->setText("下载并更新");
        m_actionBtn->setEnabled(true);
        m_progress->setVisible(false);
        m_progressLabel->setVisible(false);
        break;
    case UpdateState::Downloading:
        m_actionBtn->setText("正在下载...");
        m_actionBtn->setEnabled(false);
        m_progress->setVisible(true);
        m_progress->setValue(0);
        m_progressLabel->setVisible(true);
        break;
    case UpdateState::Installed:
        m_actionBtn->setText("安装并重启");
        m_actionBtn->setEnabled(true);
        m_progress->setValue(100);
        m_progressLabel->setText("下载完成，点击安装并重启");
        break;
    case UpdateState::Error:
        m_actionBtn->setText("重试");
        m_actionBtn->setEnabled(true);
        break;
    default:
        break;
    }
}

void UpdateDialog::setProgress(int pct, int64_t downloaded, int64_t total) {
    m_progress->setValue(pct);
    m_progressLabel->setText(
        QString("已下载: %1 / %2").arg(formatSize(downloaded)).arg(formatSize(total)));
}

void UpdateDialog::onActionClicked() {
    auto state = m_checker->state();
    if (state == UpdateState::Ready) {
        m_checker->downloadUpdate();
    } else if (state == UpdateState::Installed) {
        m_checker->installUpdate();
        // 应用即将退出
    } else if (state == UpdateState::Error) {
        m_checker->checkForUpdate();
    }
}

void UpdateDialog::onCancelClicked() {
    if (m_checker->state() == UpdateState::Downloading) {
        m_checker->cancelDownload();
    }
    close();
}

QString UpdateDialog::formatSize(int64_t bytes) const {
    if (bytes < 1024) return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024) return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    if (bytes < 1024 * 1024 * 1024) return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
}
```

- [ ] **Step 3: Commit**

```bash
git add src/updater/UpdateDialog.h src/updater/UpdateDialog.cpp
git commit -m "feat(updater): UpdateDialog — 非模态更新对话框（版本信息/Release Notes/进度/操作）"
```

---

### Task 5: UI 集成 — 菜单栏 + 状态栏

**Files:**
- Modify: `DeployMaster.h`
- Modify: `DeployMaster.cpp`

**Interfaces:**
- Consumes: `UpdateChecker` (Task 2), `UpdateDialog` (Task 4)
- Produces: 菜单栏 "检查更新" + 状态栏版本标签（可点击）

- [ ] **Step 1: DeployMaster.h 新增成员**

在 `DeployMaster.h` 中添加:

```cpp
// 在现有 forward declaration 区域添加:
class UpdateChecker;
class UpdateDialog;

// 在 private: 区域添加成员:
std::shared_ptr<UpdateChecker> m_updateChecker;
UpdateDialog* m_updateDialog = nullptr;
QAction* m_checkUpdateAction = nullptr;
QLabel* m_versionLabel = nullptr;  // 状态栏版本标签

// 新增方法声明 (在 private slots: 或 private:):
void setupUpdateChecker();
void onCheckUpdateTriggered();
void onVersionLabelClicked();
void onUpdateStateChanged(UpdateState state);
```

- [ ] **Step 2: DeployMaster.cpp — setupUpdateChecker 方法**

```cpp
// 在构造函数或 initToolTabs() 附近，添加:
// (需要 #include "src/updater/UpdateChecker.h" 和 "src/updater/UpdateDialog.h")

void DeployMaster::setupUpdateChecker() {
    m_updateChecker = std::make_shared<UpdateChecker>();

    // 状态栏版本标签
    m_versionLabel = new QLabel(this);
    QString currentVer = QString("v%1.%2.%3")
        .arg(DEVICEFORGE_VERSION_MAJOR)
        .arg(DEVICEFORGE_VERSION_MINOR)
        .arg(DEVICEFORGE_VERSION_PATCH);
    m_versionLabel->setText(currentVer + " (已是最新)");
    m_versionLabel->setStyleSheet(
        "color: #7B8494; padding: 0 8px; font-size: 12px;");
    m_versionLabel->setCursor(Qt::PointingHandCursor);
    m_versionLabel->installEventFilter(this);  // 或使用 signal handler

    // 点击版本标签触发检查（使用 event filter 或继承 QLabel）
    // 简单方案：使用 clickable label helper
    connect(m_versionLabel, &QLabel::linkActivated, [this](const QString&) {
        onVersionLabelClicked();
    });
    // 备选：使用 eventFilter 或子类化

    // 添加到状态栏
    statusBar()->addPermanentWidget(m_versionLabel);

    // 菜单: 帮助 → 检查更新
    QMenu* helpMenu = menuBar()->addMenu("帮助");
    m_checkUpdateAction = helpMenu->addAction("检查更新...");
    connect(m_checkUpdateAction, &QAction::triggered,
            this, &DeployMaster::onCheckUpdateTriggered);

    // UpdateChecker 回调 → 主线程安全
    m_updateChecker->setStateChangedCallback([this](UpdateState state) {
        QMetaObject::invokeMethod(this, [this, state]() {
            onUpdateStateChanged(state);
        }, Qt::QueuedConnection);
    });

    m_updateChecker->setProgressCallback(
        [this](int pct, int64_t downloaded, int64_t total) {
        QMetaObject::invokeMethod(this, [this, pct, downloaded, total]() {
            if (m_updateDialog) {
                m_updateDialog->setProgress(pct, downloaded, total);
            }
        }, Qt::QueuedConnection);
    });

    m_updateChecker->setErrorCallback([this](const std::string& msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            if (m_updateDialog) {
                m_updateDialog->setState(UpdateState::Error);
            }
            // 静默错误不弹窗，仅日志
        }, Qt::QueuedConnection);
    });

    // 延迟 5 秒自动检查更新
    QTimer::singleShot(5000, this, &DeployMaster::onCheckUpdateTriggered);
}

void DeployMaster::onCheckUpdateTriggered() {
    m_checkUpdateAction->setEnabled(false);  // 防止重复点击
    m_versionLabel->setText(m_versionLabel->text().split(" ").first() + " ...");

    if (!m_updateChecker) setupUpdateChecker();
    m_updateChecker->checkForUpdate();
}

void DeployMaster::onUpdateStateChanged(UpdateState state) {
    m_checkUpdateAction->setEnabled(state != UpdateState::Checking &&
                                     state != UpdateState::Downloading);

    switch (state) {
    case UpdateState::Checking:
        m_versionLabel->setStyleSheet("color: #7B8494; padding: 0 8px;");
        break;
    case UpdateState::Ready: {
        const auto& info = m_updateChecker->releaseInfo();
        QString tag = QString::fromStdString(info.tagName);
        // 琴色可点击
        m_versionLabel->setText(QString("<a href='#' style='color:#F0A030;text-decoration:none;'>%1 可用 ▾</a>").arg(tag));
        m_checkUpdateAction->setText("下载 " + tag + "...");

        // 自动弹出 UpdateDialog（仅自动检查时，手动检查时也弹出）
        // 创建并显示对话框
        break;
    }
    case UpdateState::Idle:
        m_versionLabel->setText(m_versionLabel->text().split(" ").first() + " (已是最新)");
        m_versionLabel->setStyleSheet("color: #7B8494; padding: 0 8px;");
        m_checkUpdateAction->setText("检查更新...");
        break;
    case UpdateState::Error:
        m_versionLabel->setText(m_versionLabel->text().split(" ").first() + " (检查失败)");
        m_versionLabel->setStyleSheet("color: #7B8494; padding: 0 8px;");
        m_checkUpdateAction->setText("检查更新...");
        break;
    default:
        break;
    }
}

void DeployMaster::onVersionLabelClicked() {
    auto state = m_updateChecker->state();
    if (state == UpdateState::Ready || state == UpdateState::Downloading ||
        state == UpdateState::Installed) {
        // 显示/创建对话框
        if (!m_updateDialog) {
            m_updateDialog = new UpdateDialog(m_updateChecker.get(), this);
        }
        if (state == UpdateState::Ready) {
            m_updateDialog->setReleaseInfo(m_updateChecker->releaseInfo());
            m_updateDialog->setState(state);
        }
        m_updateDialog->show();
        m_updateDialog->raise();
    } else {
        // 手动触发检查
        onCheckUpdateTriggered();
        // 延迟弹出对话框（等检查完成）
        QTimer::singleShot(3000, this, [this]() {
            if (m_updateChecker->state() == UpdateState::Ready) {
                if (!m_updateDialog) {
                    m_updateDialog = new UpdateDialog(m_updateChecker.get(), this);
                }
                m_updateDialog->setReleaseInfo(m_updateChecker->releaseInfo());
                m_updateDialog->setState(m_updateChecker->state());
                m_updateDialog->show();
                m_updateDialog->raise();
            } else if (m_updateChecker->state() == UpdateState::Idle) {
                // 已是最新版本
                if (!m_updateDialog) {
                    m_updateDialog = new UpdateDialog(m_updateChecker.get(), this);
                }
                m_updateDialog->setState(UpdateState::Idle);
                m_updateDialog->show();
            }
        });
    }
}
```

- [ ] **Step 3: 在构造函数中调用 setupUpdateChecker**

在 `DeployMaster` 构造函数中（`setupRemotePreview()` 之后）添加:
```cpp
setupUpdateChecker();
```

并将 `initToolTabs()` 中的 UpdateChecker 初始化改为由 `setupUpdateChecker()` 统一管理。

- [ ] **Step 4: Commit**

```bash
git add DeployMaster.h DeployMaster.cpp
git commit -m "feat(updater): UI 集成 — 菜单栏检查更新 + 状态栏版本标签"
```

---

### Task 6: CMakeLists.txt + main.cpp 集成

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `main.cpp`

**Interfaces:**
- Consumes: 所有 Task 1-5
- Produces: 编译 Updater.exe target + tst_updatechecker test target + 版本宏定义

- [ ] **Step 1: CMakeLists.txt 变更**

在 `CMakeLists.txt` 中添加:

```cmake
# === 版本宏定义（供 UpdateChecker 使用） ===
target_compile_definitions(DeviceForge PRIVATE
    DEVICEFORGE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    DEVICEFORGE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    DEVICEFORGE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

# === UpdateChecker（链接进 DeviceForge） ===
target_sources(DeviceForge PRIVATE
    src/updater/UpdateChecker.h
    src/updater/UpdateChecker.cpp
    src/updater/UpdateTypes.h
    src/updater/UpdateDialog.h
    src/updater/UpdateDialog.cpp
)

# === Updater.exe（独立 target，无 Qt） ===
add_executable(Updater
    src/updater/UpdaterMain.cpp
)
set_property(TARGET Updater PROPERTY WIN32_EXECUTABLE ON)
target_link_libraries(Updater PRIVATE
    # 仅 CRT — kernel32, shell32, user32 通过 MSVC 隐式链接
)
# 确保 Updater.exe 随 DeviceForge 一起复制到输出目录
add_custom_command(TARGET DeviceForge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Updater>"
        "$<TARGET_FILE_DIR:DeviceForge>"
    COMMENT "Copying Updater.exe to output directory"
)

# === 测试 ===
# (追加到现有 tests/CMakeLists.txt 或根 CMakeLists.txt)
# add_executable(tst_updatechecker
#     tests/tst_updatechecker.cpp
#     src/updater/UpdateChecker.cpp
# )
# target_include_directories(tst_updatechecker PRIVATE src)
# target_link_libraries(tst_updatechecker PRIVATE Qt6::Core Qt6::Test
#     ${CURL_LIBRARIES})
# add_test(NAME tst_updatechecker COMMAND tst_updatechecker)
```

- [ ] **Step 2: main.cpp 变更**

在 `main.cpp` 的 `ProtocolRegistry` 注册和 `window.initToolTabs()` 之间，无需额外注册 UpdateChecker（UpdateChecker 由 DeployMaster::setupUpdateChecker 创建）。

- [ ] **Step 3: 编译 + 验证构建**

```bash
cmake --build . --config Release 2>&1
# 预期: DeviceForge.exe 和 Updater.exe 均编译成功
# 验证 Updater.exe 无 Qt 依赖:
dumpbin /dependents Release/Updater.exe | grep -i qt
# 预期: 无 Qt DLL
```

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt main.cpp
git commit -m "build(updater): CMake 集成 UpdateChecker + Updater target + 版本宏"
```

---

### Task 7: 单元测试

**Files:**
- Create: `tests/tst_updatechecker.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `UpdateTypes.h` (Task 1), `UpdateChecker.h` (Task 2)
- Produces: QtTest 测试目标，覆盖版本解析 + JSON 解析 + 状态机

- [ ] **Step 1: 创建测试文件**

```cpp
// tests/tst_updatechecker.cpp
#include <QtTest/QtTest>
#include "updater/UpdateTypes.h"

class TstUpdateChecker : public QObject {
    Q_OBJECT
private slots:
    void sanity() { QVERIFY(true); }

    void parseVersion_vPrefix() {
        Version v = parseVersion("v2.1.0");
        QCOMPARE(v.major, 2);
        QCOMPARE(v.minor, 1);
        QCOMPARE(v.patch, 0);
    }

    void parseVersion_noPrefix() {
        Version v = parseVersion("3.14.5");
        QCOMPARE(v.major, 3);
        QCOMPARE(v.minor, 14);
        QCOMPARE(v.patch, 5);
    }

    void parseVersion_badFormat() {
        Version v = parseVersion("not-a-version");
        QCOMPARE(v.major, 0);
        QCOMPARE(v.minor, 0);
        QCOMPARE(v.patch, 0);
    }

    void compareVersion_newer() {
        Version a = parseVersion("v2.2.0");
        Version b = parseVersion("v2.1.0");
        QVERIFY(compareVersion(a, b) > 0);
    }

    void compareVersion_older() {
        Version a = parseVersion("v2.0.0");
        Version b = parseVersion("v2.1.0");
        QVERIFY(compareVersion(a, b) < 0);
    }

    void compareVersion_equal() {
        Version a = parseVersion("v2.1.0");
        Version b = parseVersion("v2.1.0");
        QCOMPARE(compareVersion(a, b), 0);
    }

    void compareVersion_patchBump() {
        Version a = parseVersion("v2.1.1");
        Version b = parseVersion("v2.1.0");
        QVERIFY(compareVersion(a, b) > 0);
    }

    void compareVersion_majorBump() {
        Version a = parseVersion("v3.0.0");
        Version b = parseVersion("v2.9.9");
        QVERIFY(compareVersion(a, b) > 0);
    }
};

QTEST_MAIN(TstUpdateChecker)
#include "tst_updatechecker.moc"
```

- [ ] **Step 2: 更新 tests/CMakeLists.txt**

```cmake
# 追加到 tests/CMakeLists.txt:
add_executable(tst_updatechecker
    tst_updatechecker.cpp
    ${CMAKE_SOURCE_DIR}/src/updater/UpdateChecker.cpp
)
target_include_directories(tst_updatechecker PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_updatechecker PRIVATE
    Qt6::Core Qt6::Network Qt6::Test
    ${CURL_LIBRARIES}
)
target_compile_definitions(tst_updatechecker PRIVATE
    DEVICEFORGE_VERSION_MAJOR=2
    DEVICEFORGE_VERSION_MINOR=1
    DEVICEFORGE_VERSION_PATCH=0
)
add_test(NAME tst_updatechecker COMMAND tst_updatechecker)

# PATH 注入（复用 tst_nrec 的模板）
get_target_property(_qt_core_dll Qt6::Core IMPORTED_LOCATION_RELEASE)
if(NOT _qt_core_dll)
    get_target_property(_qt_core_dll Qt6::Core IMPORTED_LOCATION)
endif()
if(_qt_core_dll)
    get_filename_component(_qt_bin_dir "${_qt_core_dll}" DIRECTORY)
    set_tests_properties(tst_updatechecker PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}")
endif()
```

- [ ] **Step 3: 编译 + 运行测试**

```bash
cmake --build . --config Release --target tst_updatechecker
ctest -C Release -R tst_updatechecker --output-on-failure
# 预期: 8 tests passed
```

- [ ] **Step 4: Commit**

```bash
git add tests/tst_updatechecker.cpp tests/CMakeLists.txt
git commit -m "test(updater): tst_updatechecker — 版本解析+比较 8 用例"
```

---

### Task 8: 端到端集成验证 + 文档收尾

**Files:**
- Modify: `docs/superpowers/specs/2026-07-14-online-update-design.md`（确认一致）
- Create: `docs/superpowers/plans/2026-07-14-online-update-plan.md`（本文件）

- [ ] **Step 1: 检查 CTest 全部通过**

```bash
ctest -C Release --output-on-failure
# 预期: 100% tests passed (tst_nrec + tst_updatechecker)
```

- [ ] **Step 2: 确认 Updater.exe 体积**

```bash
ls -la build/Release/Updater.exe
# 预期: < 200KB
```

- [ ] **Step 3: 确认 DeviceForge.exe 依赖中无多余项**

```bash
dumpbin /dependents build/Release/DeviceForge.exe
# 预期: Qt DLLs + libcurl-x64.dll + libssh2.dll + Updater.exe 不在依赖表中（独立进程）
```

- [ ] **Step 4: 更新 CHANGELOG 和 ROADMAP**

在 `CHANGELOG.md` 添加:
```markdown
## v2.2.0 (未发布)

### 新增
- 在线更新功能：启动自动检查 + 应用内一键下载更新（GitHub Releases API）
```

- [ ] **Step 5: 保存计划文件 + Commit**

```bash
git add docs/superpowers/plans/2026-07-14-online-update-plan.md CHANGELOG.md
git commit -m "docs: 在线更新实施计划 + CHANGELOG 更新"
```

---

## 依赖关系

```
Task 1 (UpdateTypes) ────┬──→ Task 2 (UpdateChecker)
                          │
                          ├──→ Task 4 (UpdateDialog)
                          │
Task 3 (Updater.exe) ─────┼──→ Task 6 (CMake + main)
                          │
Task 2 + 4 ───────────────┼──→ Task 5 (UI 集成)
                          │
Task 6 ───────────────────┼──→ Task 7 (单元测试)
                          │
Task 7 ───────────────────┴──→ Task 8 (集成验证)
```

**可并行**: Task 2 和 Task 3 可同时开发（UpdateChecker 和 Updater.exe 互不依赖）。
