# DeployMaster 2.0 框架搭建实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 搭建 DeployMaster 2.0 的基础框架（Phase 0 + Phase 1），为后续全量模块迁移和插件化提供基础设施。

**Architecture:** lwserverbase 服务核 + Qt Widget 壳双层架构。Tool = ToolBackend (ServiceTask) + ToolWidget (QWidget)，通过 lwmsgq 双向解耦。统一 IProtocolAdapter 接口 + LWConnPool 连接池。

**Tech Stack:** Qt 6.10.1 (Core/Gui/Widgets/Network)、C++17、lwserverbase、lwcommunicate、lwlog、lwmsgq、tinyxml2、nanopb、libcurl

**Design Doc:** `docs/superpowers/specs/2026-07-04-tool-framework-design.md`

**Scope Note:** 本计划覆盖 Phase 0（基础设施对齐）和 Phase 1（框架搭建）。Phase 2-3 后续。

**状态**: Phase 0-1 ✅ 完成（15 Tasks）。Phase 2 部分完成（2026-07-05）：TelnetTool ✅、WebSocketTool ✅、FtpDeployTool ✅（含 FTPS 加密 + UI 重设计）、安全加固 ✅。待完成：LogQueryTool、ModbusTool 迁移。

## Global Constraints

- 语言：中文（注释、文档、UI 文案），专业术语除外
- 文档同步：修改接口/架构/设计时同步更新对应 docs/
- 命名：类名 PascalCase，方法 camelCase，成员变量 m_camelCase，常量 UPPER_SNAKE_CASE
- Qt 约定：UI 类继承 QWidget，使用 Q_OBJECT 宏
- 不破坏现有功能：Phase 0 产出必须功能完整可运行
- 零测试 = 零信心：核心类必须有测试

---

## File Structure Map

### 新建文件

```
src/adapter/IProtocolAdapter.h          — 协议适配器统一接口
src/adapter/FtpAdapter.h / .cpp         — FTP 适配器（重构自 FtpManager）
src/adapter/TelnetAdapter.h / .cpp      — Telnet 适配器（基于 lwcommunicate）
src/adapter/ProtocolCapability.h        — 协议能力声明结构体
src/adapter/ProtocolRegistry.h / .cpp   — 协议适配器工厂注册表

src/framework/ToolBackend.h             — Tool 后端基类（继承 ServiceTask）
src/framework/ToolWidget.h              — Tool 前端基类（继承 QWidget）
src/framework/ToolHost.h / .cpp         — 桥接层：配对 Backend + Widget
src/framework/ToolRegistry.h / .cpp     — Tool 注册表 + 插件发现
src/framework/ManifestParser.h / .cpp   — XML 插件清单解析（tinyxml2）
src/framework/DeviceInfo.h              — 设备信息 + 认证信息数据结构

src/logging/LogBridge.h / .cpp          — Qt → lwlog 桥接

src/ui/DeviceBusWidget.h / .cpp         — 设备总线 UI 组件

src/tools/FtpDeployTool/
    FtpDeployBackend.h / .cpp           — FTP 部署 Tool 后端
    FtpDeployWidget.h / .cpp            — FTP 部署 Tool 前端

tests/test_ftp_adapter.cpp              — FtpAdapter 单元测试
tests/test_telnet_adapter.cpp           — TelnetAdapter 单元测试
tests/test_tool_registry.cpp            — Tool 注册表 + 清单解析测试
tests/test_device_bus.cpp               — DeviceBusWidget 测试
```

### 修改文件

```
CMakeLists.txt                          — 添加 thirdparty 库编译 + 新源文件
main.cpp                                — 加载 lwlog，移除旧引用，注册新组件
DeployMaster.h / .cpp                   — 重构为 ToolHost 驱动 + DeviceBusWidget
darkstyle.qss                           — 色板替换 + 设备总线/Tool 导航样式
src/presenter/FtpPresenter.cpp          — 迁移到 FtpDeployTool，逐步弃用
```

### 删除文件

```
src/framework/ConnectionPool.h / .cpp   — 由 LWConnPool 替代
src/framework/EventBus.h / .cpp         — 由 lwmsgq 替代
TelnetManager.cpp                       — 1 字节空文件
FileDeploy.h / .cpp                     — 空壳类（1 行构造函数）
```

---

## Task 1: CMake 基础设施与 thirdparty 编译

**Files:**
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: 可编译链接所有 `src/thirdparty/` 库的 CMake 配置

- [ ] **Step 1: 探索 thirdparty 各库的编译需求**

```bash
# 确认每个库的源文件和编译方式
ls src/thirdparty/lwcomm/src/*.cpp
ls src/thirdparty/lwlog/src/*.cpp
ls src/thirdparty/lwcommunicate/src/*.cpp
ls src/thirdparty/lwserverbase/**/*.cpp
ls src/thirdparty/tinyxml2/tinyxml2.cpp
```

预期：每个 lw* 库都有独立的 `.cpp` 源文件。

- [ ] **Step 2: 在 CMakeLists.txt 中添加 thirdparty 库**

```cmake
# 在 CMakeLists.txt 的 find_package 后面添加：

set(THIRDPARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/thirdparty)

# === lwcomm（跨平台工具库） ===
add_library(lwcomm STATIC
    ${THIRDPARTY_DIR}/lwcomm/src/comm.cpp
    ${THIRDPARTY_DIR}/lwcomm/src/comm_impl.cpp
    ${THIRDPARTY_DIR}/lwcomm/src/file_helper.cpp
    ${THIRDPARTY_DIR}/lwcomm/src/file_system.cpp
    ${THIRDPARTY_DIR}/lwcomm/src/lwbase64.cpp
    ${THIRDPARTY_DIR}/lwcomm/src/string_helper.cpp
    ${THIRDPARTY_DIR}/lwcomm/src/system_info.cpp
    ${THIRDPARTY_DIR}/lwcomm/src/time_helper.cpp
)
target_include_directories(lwcomm PUBLIC ${THIRDPARTY_DIR}/lwcomm/include)

# === lwevent（事件信号） ===
add_library(lwevent STATIC
    ${THIRDPARTY_DIR}/lwevent/src/lwevent.cpp
)
target_include_directories(lwevent PUBLIC ${THIRDPARTY_DIR}/lwevent/include)
target_link_libraries(lwevent PUBLIC lwcomm)

# === lwlog（日志库） ===
add_library(lwlog STATIC
    ${THIRDPARTY_DIR}/lwlog/src/lwlog.cpp
    ${THIRDPARTY_DIR}/lwlog/src/lwlog_c.cpp
    ${THIRDPARTY_DIR}/lwlog/src/logger_impl.cpp
    ${THIRDPARTY_DIR}/lwlog/src/console_appender.cpp
    ${THIRDPARTY_DIR}/lwlog/src/file_appender.cpp
    ${THIRDPARTY_DIR}/lwlog/src/pattern_formatter.cpp
    ${THIRDPARTY_DIR}/lwlog/src/log_config_reader.cpp
)
target_include_directories(lwlog PUBLIC ${THIRDPARTY_DIR}/lwlog/include)
target_link_libraries(lwlog PUBLIC lwcomm)

# === lwmsgq（消息队列） ===
add_library(lwmsgq STATIC
    ${THIRDPARTY_DIR}/lwmsgq/src/lwmsgq.c
)
target_include_directories(lwmsgq PUBLIC ${THIRDPARTY_DIR}/lwmsgq/include)
target_link_libraries(lwmsgq PUBLIC lwcomm lwevent)

# === lwcommunicate（网络通信库） ===
file(GLOB LWCOMMUNICATE_SOURCES ${THIRDPARTY_DIR}/lwcommunicate/src/*.cpp)
add_library(lwcommunicate STATIC ${LWCOMMUNICATE_SOURCES})
target_include_directories(lwcommunicate PUBLIC ${THIRDPARTY_DIR}/lwcommunicate/include)
target_link_libraries(lwcommunicate PUBLIC lwcomm lwlog lwevent)

# === tinyxml2 ===
add_library(tinyxml2 STATIC
    ${THIRDPARTY_DIR}/tinyxml2/tinyxml2.cpp
)
target_include_directories(tinyxml2 PUBLIC ${THIRDPARTY_DIR}/tinyxml2)

# === nanopb ===
add_library(nanopb STATIC
    ${THIRDPARTY_DIR}/nanopb/pb_encode.c
    ${THIRDPARTY_DIR}/nanopb/pb_decode.c
    ${THIRDPARTY_DIR}/nanopb/pb_common.c
)
target_include_directories(nanopb PUBLIC ${THIRDPARTY_DIR}/nanopb)

# === lwserverbase（服务框架） ===
file(GLOB_RECURSE LWSERVERBASE_SOURCES ${THIRDPARTY_DIR}/lwserverbase/src/**/*.cpp)
add_library(lwserverbase STATIC ${LWSERVERBASE_SOURCES})
target_include_directories(lwserverbase PUBLIC ${THIRDPARTY_DIR}/lwserverbase/include)
target_link_libraries(lwserverbase PUBLIC lwcomm lwlog lwmsgq lwevent)
```

- [ ] **Step 3: 编译验证**

```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH="C:\Qt\6.10.1\msvc2022_64"
cmake --build . --config Release
```

预期：所有 thirdparty 库编译通过，DeployMaster.exe 链接成功。

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: 集成 thirdparty 库编译（lwcomm/lwlog/lwmsgq/lwcommunicate/lwserverbase/tinyxml2/nanopb）

每个库编译为 STATIC 库并建立依赖链：
lwcomm → lwevent → lwmsgq → lwlog → lwcommunicate → lwserverbase

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Task 2: LogBridge — Qt 消息桥接到 lwlog

**Files:**
- Create: `src/logging/LogBridge.h`
- Create: `src/logging/LogBridge.cpp`
- Modify: `main.cpp`

**Interfaces:**
- Produces: `LogBridge::install()` — 全局安装 QtMessageHandler，将 qDebug/qWarning/qCritical 桥接到 lwlog

- [ ] **Step 1: 写 LogBridge 头文件**

```cpp
// src/logging/LogBridge.h
#pragma once

namespace LogBridge {
    // 安装 Qt → lwlog 桥接。调用后 qDebug/qWarning/qCritical 的输出
    // 将通过 lwlog 管道分发，同时保留原有控制台输出。
    // 应在 QApplication 创建之后、任何 Qt 日志产生之前调用。
    void install();

    // 卸载桥接，恢复默认 Qt 消息处理
    void uninstall();
}
```

- [ ] **Step 2: 写 LogBridge 实现**

```cpp
// src/logging/LogBridge.cpp
#include "LogBridge.h"
#include <QtCore/QtGlobal>
#include <QtCore/QString>
#include <QDebug>
#include "lwlog/lwlog.h"

namespace {

    void qtMessageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
        std::string str = msg.toStdString();

        switch (type) {
        case QtDebugMsg:
            LWLOG_D(str.c_str());
            break;
        case QtWarningMsg:
            LWLOG_W(str.c_str());
            break;
        case QtCriticalMsg:
        case QtFatalMsg:
            LWLOG_E(str.c_str());
            break;
        case QtInfoMsg:
            LWLOG_I(str.c_str());
            break;
        }

        // 同时输出到控制台（调试用）
        fprintf(stderr, "[Qt] %s\n", str.c_str());
        if (type == QtFatalMsg) {
            abort();
        }
    }

} // anonymous namespace

void LogBridge::install() {
    qInstallMessageHandler(qtMessageHandler);
    LWLOG_I("LogBridge installed — qDebug/qWarning/qCritical → lwlog");
}

void LogBridge::uninstall() {
    qInstallMessageHandler(nullptr);
    LWLOG_I("LogBridge uninstalled");
}
```

- [ ] **Step 3: 在 main.cpp 中调用安装**

```cpp
// main.cpp — 在 QApplication app(argc, argv) 之后、window.show() 之前添加：

#include "src/logging/LogBridge.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    LogBridge::install();  // ← 新增

    qRegisterMetaType<DeployEvent>("DeployEvent");
    // ... 其余不变 ...
}
```

- [ ] **Step 4: 验证**

编译运行，确认控制台同时输出 Qt 日志和 lwlog 转发日志。

```bash
cmake --build . --config Release
```

- [ ] **Step 5: Commit**

```bash
git add src/logging/LogBridge.h src/logging/LogBridge.cpp main.cpp CMakeLists.txt
git commit -m "feat: 添加 LogBridge，将 Qt 日志桥接到 lwlog 管道

qDebug/qWarning/qCritical → lwlog（DEBUG/WARN/ERROR 级别）
同时保留 stderr 控制台输出，方便调试

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Task 3: IProtocolAdapter 统一接口 + ProtocolCapability

**Files:**
- Create: `src/adapter/ProtocolCapability.h`
- Create: `src/adapter/IProtocolAdapter.h`

**Interfaces:**
- Produces: `IProtocolAdapter`（纯虚接口），`ProtocolCapability`（结构体），`DeviceInfo`（设备信息），`AuthInfo`（认证凭证），`Request`/`Response`（请求/响应结构体）

- [ ] **Step 1: 定义数据结构**

```cpp
// src/adapter/ProtocolCapability.h
#pragma once
#include <string>
#include <cstdint>

// 协议能力声明 — 框架据此判断 Tool 需要的协议是否可用
struct ProtocolCapability {
    bool requestResponse = true;  // 支持请求-响应模式（FTP、HTTP、Modbus）
    bool streaming = false;       // 支持流模式（Telnet、WebSocket）
    bool broadcast = false;       // 支持广播/多播（UDP）
    bool publishSubscribe = false;// 支持发布/订阅（MQTT）
    int  maxConnections = 1;      // 单适配器最大并发连接数
};

// 设备信息
struct DeviceInfo {
    std::string ip;
    int         port = 0;
    std::string protocol;  // "ftp", "telnet", "modbus"...
    std::string alias;     // 用户自定义别名（可选）
};

// 认证凭证
struct AuthInfo {
    std::string user;
    std::string password;
};

// 通用请求
struct Request {
    std::string path;       // 远程路径或命令
    std::string payload;    // 数据载荷
    int         timeoutMs = 5000;
};

// 通用响应
struct Response {
    bool        success = false;
    std::string data;
    std::string errorMessage;
    int         statusCode = 0;
};
```

- [ ] **Step 2: 定义 IProtocolAdapter 接口**

```cpp
// src/adapter/IProtocolAdapter.h
#pragma once
#include "ProtocolCapability.h"
#include <string>
#include <future>
#include <functional>
#include <memory>

// 流式数据回调：每次收到新数据块时调用
using StreamCallback = std::function<void(const std::string& data, bool isFinished)>;

// 所有协议适配器的统一基类
class IProtocolAdapter {
public:
    virtual ~IProtocolAdapter() = default;

    // --- 身份 ---
    virtual std::string protocolId() const = 0;   // "ftp", "telnet", "ssh"...

    // --- 连接生命周期 ---
    virtual bool connect(const DeviceInfo& device, const AuthInfo& auth) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual std::string lastError() const = 0;

    // --- 传输模式 ---
    // 请求-响应（FTP、HTTP、Modbus 读）
    virtual std::future<Response> request(const Request& req) = 0;

    // 流模式（Telnet 逐行输出、WebSocket 消息）
    virtual void subscribe(const Request& req, StreamCallback onData) = 0;
    virtual void unsubscribe() = 0;

    // --- 能力声明 ---
    virtual ProtocolCapability capability() const = 0;
};

// 适配器工厂函数类型
using AdapterFactory = std::shared_ptr<IProtocolAdapter>(*)();
```

- [ ] **Step 3: 编译验证**

```bash
cmake --build . --config Release
# 编译应成功（纯头文件，无 .cpp）
```

- [ ] **Step 4: Commit**

```bash
git add src/adapter/ProtocolCapability.h src/adapter/IProtocolAdapter.h
git commit -m "feat: 定义 IProtocolAdapter 统一接口 + 相关数据结构

- ProtocolCapability: 声明协议能力（请求响应/流/广播/发布订阅）
- DeviceInfo / AuthInfo: 设备与认证信息
- Request / Response: 通用请求响应
- IProtocolAdapter: 纯虚接口（connect/disconnect/request/subscribe）
- StreamCallback: 流式数据回调类型

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Task 4: FtpAdapter — 重构 FtpManager 为实现 IProtocolAdapter

**Files:**
- Create: `src/adapter/FtpAdapter.h`
- Create: `src/adapter/FtpAdapter.cpp`
- Modify: `CMakeLists.txt`（添加新源文件 + 删除旧 FtpManager 引用等待 Phase 1 迁移）

**Interfaces:**
- Consumes: IProtocolAdapter（Task 3）
- Produces: FtpAdapter 类，实现 IProtocolAdapter，内部复用现有 libcurl 逻辑

> **说明**：此 Task 不删除旧 `src/model/FtpManager`，而是新建 FtpAdapter 包装其核心逻辑。旧代码在 Phase 1 迁移完成后删除。

- [ ] **Step 1: 写 FtpAdapter 头文件**

```cpp
// src/adapter/FtpAdapter.h
#pragma once
#include "IProtocolAdapter.h"
#include <string>
#include <future>
#include <memory>

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

    // --- FTP 特有操作（直接调用，不通过 request 抽象） ---
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
```

- [ ] **Step 2: 写 FtpAdapter 实现（复用现有 FtpManager 核心逻辑）**

```cpp
// src/adapter/FtpAdapter.cpp
#include "FtpAdapter.h"
#include <curl/curl.h>
#include <QtConcurrent/QtConcurrent>
#include <sstream>
#include <cstring>

struct FtpAdapter::Impl {
    std::string m_ip;
    int         m_port = 21;
    std::string m_user;
    std::string m_password;
    std::string m_remotePath;
    std::string m_lastError;
    bool        m_connected = false;
    std::function<void(int)> m_progressCb;

    // 复用自现有 FtpManager 的规模 URL 拼接、CURL 配置等辅助函数
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

    // libcurl 写回调
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        auto* str = static_cast<std::string*>(userp);
        size_t total = size * nmemb;
        str->append(static_cast<char*>(contents), total);
        return total;
    }

    // libcurl 进度回调
    static int progressCallback(void* clientp, double dltotal,
                                 double dlnow, double ultotal, double ulnow) {
        (void)dltotal; (void)dlnow;
        auto* self = static_cast<Impl*>(clientp);
        if (self->m_progressCb && ultotal > 0) {
            int pct = static_cast<int>((ulnow / ultotal) * 100.0);
            self->m_progressCb(pct);
        }
        return 0;
    }
};

FtpAdapter::FtpAdapter() : m_impl(std::make_unique<Impl>()) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

FtpAdapter::~FtpAdapter() {
    disconnect();
    curl_global_cleanup();
}

bool FtpAdapter::connect(const DeviceInfo& device, const AuthInfo& auth) {
    m_impl->m_ip = device.ip;
    m_impl->m_port = device.port > 0 ? device.port : 21;
    m_impl->m_user = auth.user;
    m_impl->m_password = auth.password;

    // 用 ping FTP 的方式验证可达性：尝试列出根目录
    std::string result;
    auto* curl = curl_easy_init();
    if (!curl) {
        m_impl->m_lastError = "curl_easy_init() 失败";
        return false;
    }

    std::string url = m_impl->buildUrl("/");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERNAME, m_impl->m_user.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, m_impl->m_password.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Impl::writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_DIRLISTONLY, 1L); // 只列目录名，节省流量

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK) {
        m_impl->m_connected = true;
        m_impl->m_lastError.clear();
        return true;
    }

    m_impl->m_lastError = curl_easy_strerror(res);
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

std::future<Response> FtpAdapter::request(const Request& req) {
    return QtConcurrent::run([this, req]() -> Response {
        Response resp;
        // 通用请求路由：根据 path 前缀判断 FTP 操作类型
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
        } else {
            resp.success = false;
            resp.errorMessage = "未知 FTP 操作: " + req.path;
        }
        return resp;
    });
}

void FtpAdapter::subscribe(const Request& /*req*/, StreamCallback /*onData*/) {
    // FTP 不支持流模式
    m_impl->m_lastError = "FTP 协议不支持流模式";
}

void FtpAdapter::unsubscribe() {}

ProtocolCapability FtpAdapter::capability() const {
    ProtocolCapability cap;
    cap.requestResponse = true;
    cap.streaming = false;
    cap.broadcast = false;
    cap.publishSubscribe = false;
    cap.maxConnections = 10;
    return cap;
}

// --- FTP 特有操作的实现（迁移自 src/model/FtpManager.cpp 的逻辑） ---

bool FtpAdapter::uploadFile(const std::string& localPath, const std::string& remotePath) {
    // 复用现有 FtpManager::uploadFile 的逻辑
    auto* curl = curl_easy_init();
    if (!curl) {
        m_impl->m_lastError = "curl_easy_init() 失败";
        return false;
    }

    std::string url = m_impl->buildUrl(remotePath);
    FILE* file = fopen(localPath.c_str(), "rb");
    if (!file) {
        m_impl->m_lastError = std::string("无法打开文件: ") + localPath;
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERNAME, m_impl->m_user.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, m_impl->m_password.c_str());
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READDATA, file);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, m_impl.get());
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, Impl::progressCallback);

    CURLcode res = curl_easy_perform(curl);
    fclose(file);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK) {
        m_impl->m_lastError.clear();
        return true;
    }

    m_impl->m_lastError = curl_easy_strerror(res);
    return false;
}

void FtpAdapter::setProgressCallback(std::function<void(int)> cb) {
    m_impl->m_progressCb = std::move(cb);
}

// uploadFolder / downloadFile / listDirectory / deleteFile / deleteDirectory
// / clearRemoteDirectory 的实现逻辑同上，均迁移自现有 FtpManager。
// 篇幅原因不在此展开，完整实现见源文件。
// (在实际编写 .cpp 时，将 FtpManager 中对应方法的逻辑复制过来即可)
```

- [ ] **Step 3: 编译验证**

```cmake
# CMakeLists.txt 的 SOURCES 中添加:
    src/adapter/FtpAdapter.cpp
```

```bash
cmake --build . --config Release
```

- [ ] **Step 4: Commit**

```bash
git add src/adapter/FtpAdapter.h src/adapter/FtpAdapter.cpp CMakeLists.txt
git commit -m "feat: 添加 FtpAdapter，实现 IProtocolAdapter 接口

复用现有 FtpManager 的 libcurl 核心逻辑，包装为 IProtocolAdapter。
FtpAdapter 额外暴露 FTP 特有操作（uploadFile/listDirectory 等），
供 Tool 层直接调用。

旧 FtpManager 暂不删除，待 Phase 1 Tool 迁移完成后移除。

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Task 5: TelnetAdapter — 基于 lwcommunicate 实现

**Files:**
- Create: `src/adapter/TelnetAdapter.h`
- Create: `src/adapter/TelnetAdapter.cpp`

**Interfaces:**
- Consumes: IProtocolAdapter (Task 3)
- Produces: TelnetAdapter 类，使用 lwcommunicate::LWTcpClient 实现 IProtocolAdapter 流模式

- [ ] **Step 1: 写 TelnetAdapter 头文件**

```cpp
// src/adapter/TelnetAdapter.h
#pragma once
#include "IProtocolAdapter.h"
#include <memory>

class TelnetAdapter : public IProtocolAdapter {
public:
    TelnetAdapter();
    ~TelnetAdapter() override;

    std::string protocolId() const override { return "telnet"; }
    bool connect(const DeviceInfo& device, const AuthInfo& auth) override;
    void disconnect() override;
    bool isConnected() const override;
    std::string lastError() const override;
    std::future<Response> request(const Request& req) override;
    void subscribe(const Request& req, StreamCallback onData) override;
    void unsubscribe() override;
    ProtocolCapability capability() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
```

- [ ] **Step 2: 写 TelnetAdapter 实现**

```cpp
// src/adapter/TelnetAdapter.cpp
#include "TelnetAdapter.h"
#include "lwcommunicate/LWTcpClient.h"
#include "lwcommunicate/LWConnManager.h"
#include <sstream>
#include <thread>
#include <atomic>

struct TelnetAdapter::Impl {
    std::shared_ptr<LWTcpClient> m_client;
    std::string m_lastError;
    std::string m_readBuffer;
    StreamCallback m_streamCb;
    std::atomic<bool> m_streaming{false};
    std::thread m_readThread;

    void dataHandler(const char* data, size_t length, const std::string& /*extra*/) {
        if (m_streaming && m_streamCb) {
            std::string chunk(data, length);
            m_streamCb(chunk, false);
        }
    }
};

TelnetAdapter::TelnetAdapter() : m_impl(std::make_unique<Impl>()) {}

TelnetAdapter::~TelnetAdapter() {
    disconnect();
}

bool TelnetAdapter::connect(const DeviceInfo& device, const AuthInfo& auth) {
    std::string name = "telnet_" + device.ip + ":" + std::to_string(device.port);

    m_impl->m_client = std::make_shared<LWTcpClient>(
        name,
        device.ip,
        device.port > 0 ? device.port : 23
    );

    m_impl->m_client->setDataHandler(
        [this](const char* data, size_t length, const std::string& extra) {
            m_impl->dataHandler(data, length, extra);
        }
    );

    if (!m_impl->m_client->start()) {
        m_impl->m_lastError = "Telnet 连接失败: " + m_impl->m_client->lastError();
        return false;
    }

    // Telnet 认证：发送用户名 + 密码
    std::string loginCmd = auth.user + "\r\n";
    m_impl->m_client->send(loginCmd.c_str(), loginCmd.size());

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::string passCmd = auth.password + "\r\n";
    m_impl->m_client->send(passCmd.c_str(), passCmd.size());

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    m_impl->m_lastError.clear();
    return true;
}

void TelnetAdapter::disconnect() {
    unsubscribe();
    if (m_impl->m_client) {
        m_impl->m_client->stop();
        m_impl->m_client.reset();
    }
}

bool TelnetAdapter::isConnected() const {
    return m_impl->m_client && m_impl->m_client->isConnected();
}

std::string TelnetAdapter::lastError() const {
    return m_impl->m_lastError;
}

std::future<Response> TelnetAdapter::request(const Request& req) {
    return std::async(std::launch::async, [this, req]() -> Response {
        Response resp;
        if (!m_impl->m_client || !m_impl->m_client->isConnected()) {
            resp.success = false;
            resp.errorMessage = "Telnet 未连接";
            return resp;
        }

        // 单次命令执行：发送命令 + \r\n，等待一段时间获取输出
        std::string cmd = req.path + "\r\n";
        size_t sent = m_impl->m_client->send(cmd.c_str(), cmd.size());
        if (sent != cmd.size()) {
            resp.success = false;
            resp.errorMessage = "命令发送失败";
            return resp;
        }

        // 等待响应（简化实现：固定等待时间）
        std::this_thread::sleep_for(
            std::chrono::milliseconds(req.timeoutMs));

        resp.success = true;
        resp.data = m_impl->m_readBuffer;
        m_impl->m_readBuffer.clear();
        return resp;
    });
}

void TelnetAdapter::subscribe(const Request& req, StreamCallback onData) {
    m_impl->m_streamCb = std::move(onData);
    m_impl->m_streaming = true;

    // 发送订阅命令
    std::string cmd = req.path + "\r\n";
    if (m_impl->m_client) {
        m_impl->m_client->send(cmd.c_str(), cmd.size());
    }
}

void TelnetAdapter::unsubscribe() {
    m_impl->m_streaming = false;
    m_impl->m_streamCb = nullptr;
}

ProtocolCapability TelnetAdapter::capability() const {
    ProtocolCapability cap;
    cap.requestResponse = true;
    cap.streaming = true;
    cap.broadcast = false;
    cap.publishSubscribe = false;
    cap.maxConnections = 50;
    return cap;
}
```

- [ ] **Step 3: 更新 CMakeLists.txt 并编译验证**

```cmake
# SOURCES 添加:
    src/adapter/TelnetAdapter.cpp
```

```bash
cmake --build . --config Release
```

- [ ] **Step 4: Commit**

```bash
git add src/adapter/TelnetAdapter.h src/adapter/TelnetAdapter.cpp CMakeLists.txt
git commit -m "feat: 添加 TelnetAdapter，基于 lwcommunicate::LWTcpClient

实现 IProtocolAdapter 的请求-响应 + 流模式。
通过 LWTcpClient 统一管理 TCP 连接，利用其 ReconnectPolicy 实现自动重连。
替换现有 TelnetClient 对裸 QTcpSocket 的依赖。

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Task 6: ProtocolRegistry — 协议适配器工厂注册

**Files:**
- Create: `src/adapter/ProtocolRegistry.h`
- Create: `src/adapter/ProtocolRegistry.cpp`

**Interfaces:**
- Consumes: IProtocolAdapter (Task 3)
- Produces: `ProtocolRegistry::instance()->registerAdapter(id, factory)` / `create(protocolId)`

- [ ] **Step 1: 写 ProtocolRegistry 头文件**

```cpp
// src/adapter/ProtocolRegistry.h
#pragma once
#include "IProtocolAdapter.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <functional>

class ProtocolRegistry {
public:
    static ProtocolRegistry* instance();

    // 注册适配器工厂（启动时调用）
    void registerFactory(const std::string& protocolId, AdapterFactory factory);

    // 创建适配器实例
    std::shared_ptr<IProtocolAdapter> create(const std::string& protocolId) const;

    // 查询已注册协议
    bool isRegistered(const std::string& protocolId) const;
    std::vector<std::string> registeredProtocols() const;

private:
    ProtocolRegistry() = default;
    std::unordered_map<std::string, AdapterFactory> m_factories;
};
```

- [ ] **Step 2: 写 ProtocolRegistry 实现**

```cpp
// src/adapter/ProtocolRegistry.cpp
#include "ProtocolRegistry.h"
#include <algorithm>

ProtocolRegistry* ProtocolRegistry::instance() {
    static ProtocolRegistry reg;
    return &reg;
}

void ProtocolRegistry::registerFactory(const std::string& protocolId,
                                        AdapterFactory factory) {
    m_factories[protocolId] = factory;
}

std::shared_ptr<IProtocolAdapter> ProtocolRegistry::create(
    const std::string& protocolId) const {

    auto it = m_factories.find(protocolId);
    if (it == m_factories.end()) {
        return nullptr;
    }
    return it->second();
}

bool ProtocolRegistry::isRegistered(const std::string& protocolId) const {
    return m_factories.find(protocolId) != m_factories.end();
}

std::vector<std::string> ProtocolRegistry::registeredProtocols() const {
    std::vector<std::string> keys;
    keys.reserve(m_factories.size());
    for (auto& [k, _] : m_factories) {
        keys.push_back(k);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}
```

- [ ] **Step 3: 在 main.cpp 中注册内置适配器**

```cpp
// main.cpp — 添加头文件 + 注册调用

#include "src/adapter/ProtocolRegistry.h"
#include "src/adapter/FtpAdapter.h"
#include "src/adapter/TelnetAdapter.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    LogBridge::install();

    // 注册内置协议适配器
    ProtocolRegistry::instance()->registerFactory("ftp",
        []() -> std::shared_ptr<IProtocolAdapter> {
            return std::make_shared<FtpAdapter>();
        });
    ProtocolRegistry::instance()->registerFactory("telnet",
        []() -> std::shared_ptr<IProtocolAdapter> {
            return std::make_shared<TelnetAdapter>();
        });

    // ... 其余不变 ...
}
```

- [ ] **Step 4: 编译验证**

```cmake
# SOURCES 添加:
    src/adapter/ProtocolRegistry.cpp
```

```bash
cmake --build . --config Release
```

- [ ] **Step 5: Commit**

```bash
git add src/adapter/ProtocolRegistry.h src/adapter/ProtocolRegistry.cpp main.cpp CMakeLists.txt
git commit -m "feat: 添加 ProtocolRegistry 协议适配器工厂 + main.cpp 注册内置适配器

内置注册：FtpAdapter、TelnetAdapter。
扩展协议通过 DLL 插件可在运行时注册新工厂。

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Task 7: ToolBackend + ToolWidget 基类定义

**Files:**
- Create: `src/framework/ToolBackend.h`
- Create: `src/framework/ToolWidget.h`
- Create: `src/framework/DeviceInfo.h`（统一设备信息数据结构）

**Interfaces:**
- Produces: `ToolBackend`（继承 ServiceTask），`ToolWidget`（继承 QWidget），`DeviceInfo` / `AuthInfo`（从 adapter 层提升到 framework 层）

- [ ] **Step 1: 写 DeviceInfo 头文件**

```cpp
// src/framework/DeviceInfo.h
#pragma once
#include <string>
#include <vector>

// 设备信息（framework 层的统一定义，与 adapter 层的同名结构体兼容）
struct DeviceInfo {
    std::string ip;
    int port = 0;
    std::string protocol; // "ftp", "telnet", "modbus"...
    std::string alias;    // 用户自定义别名
};

struct AuthInfo {
    std::string user;
    std::string password;
};
```

- [ ] **Step 2: 写 ToolBackend 头文件**

```cpp
// src/framework/ToolBackend.h
#pragma once
#include "lwserverbase/core/ServiceTask.h"
#include "DeviceInfo.h"
#include "lwserverbase/config/ConfigManager.h"
#include <string>
#include <vector>
#include <memory>

// Tool 后端基类 — 所有 Tool 的后端逻辑必须继承此类
// 继承自 lwserverbase::ServiceTask，获得完整的服务生命周期管理
class ToolBackend : public lwserverbase::core::ServiceTask {
public:
    ToolBackend() = default;
    virtual ~ToolBackend() = default;

    // --- 身份 ---
    virtual std::string toolId() const = 0;       // "com.deploymaster.ftp.deploy"
    virtual std::string toolName() const = 0;     // "文件部署"
    virtual std::string toolVersion() const = 0;  // "2.0.0"
    virtual std::string toolCategory() const = 0; // "deploy" | "command" | "test" | "diagnostic"
    virtual std::string toolIcon() const = 0;     // 资源路径或图标名

    // --- 设备绑定 ---
    virtual void bindDevices(const std::vector<DeviceInfo>& devices) = 0;
    virtual void bindCredentials(const AuthInfo& auth) = 0;

    // --- 运行时配置 ---
    // 当 ConfigManager 热加载配置变更时调用
    virtual void applyConfig(const lwserverbase::config::ConfigValue& config) = 0;
};
```

- [ ] **Step 3: 写 ToolWidget 头文件**

```cpp
// src/framework/ToolWidget.h
#pragma once
#include <QWidget>
#include <QString>
#include <memory>

// 前向声明：消息队列类型将在后续 Task 中引入 nanopb 后精确定义
// 当前阶段使用 QVariant 作为过渡方案
class QVariant;

// Tool 前端基类 — 所有 Tool 的 UI 必须继承此类
class ToolWidget : public QWidget {
    Q_OBJECT

public:
    explicit ToolWidget(QWidget* parent = nullptr) : QWidget(parent) {}
    virtual ~ToolWidget() = default;

    // --- 身份 ---
    virtual QString toolId() const = 0;
    virtual QString toolName() const = 0;

    // --- 生命周期回调（由 ToolHost 调用） ---
    virtual void onToolStart() = 0;   // Backend 启动后，可开始 UI 交互
    virtual void onToolStop() = 0;    // Backend 停止前，清理 UI 状态

signals:
    // 通知 ToolHost：当前 Tool 状态发生变化（用于状态栏显示）
    void toolStatusChanged(const QString& status);
};
```

- [ ] **Step 4: 编译验证**

```bash
cmake --build . --config Release
# 纯头文件，应编译通过
```

- [ ] **Step 5: Commit**

```bash
git add src/framework/DeviceInfo.h src/framework/ToolBackend.h src/framework/ToolWidget.h
git commit -m "feat: 定义 ToolBackend + ToolWidget 基类

ToolBackend 继承 lwserverbase::ServiceTask，获得完整生命周期管理。
ToolWidget 继承 QWidget，定义统一的 Tool 前端接口。
后端可脱离 Qt 独立测试。

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Task 8: ManifestParser — 插件清单解析器

**Files:**
- Create: `src/framework/ManifestParser.h`
- Create: `src/framework/ManifestParser.cpp`

**Interfaces:**
- Consumes: tinyxml2
- Produces: `ManifestParser::parse(xmlPath)` → `ToolManifest` 结构体

- [ ] **Step 1: 定义 ToolManifest + 写 ManifestParser 头文件**

```cpp
// src/framework/ManifestParser.h
#pragma once
#include <string>
#include <vector>
#include <optional>

// 插件清单解析结果
struct ToolManifest {
    std::string id;
    std::string name;
    std::string version;
    std::string category;
    std::string icon;
    std::string description;
    std::string author;

    // 协议依赖
    struct ProtocolDependency {
        std::string id;
        std::string minVersion;
    };
    std::vector<ProtocolDependency> protocolDeps;

    // 宿主版本要求
    std::string minHostVersion;

    // 入口点
    struct EntryPoint {
        std::string factory;   // 工厂函数名 "createToolBackend"
        std::string library;   // DLL 文件名 "ftp_deploy_backend.dll"
    };
    std::optional<EntryPoint> backendEntry;
    std::optional<EntryPoint> widgetEntry;

    // 默认配置
    struct DefaultProperty {
        std::string key;
        std::string value;
        std::string type;  // "string" | "bool" | "int" | "double"
    };
    std::vector<DefaultProperty> defaultProperties;

    // 解析状态
    bool valid = false;
    std::string parseError;
};

// XML 插件清单解析器
class ManifestParser {
public:
    // 从文件路径解析 manifest.xml，返回 ToolManifest
    static ToolManifest parse(const std::string& xmlFilePath);

    // 从内存字符串解析
    static ToolManifest parseString(const std::string& xmlContent,
                                     const std::string& sourceName = "<memory>");
};
```

- [ ] **Step 2: 写 ManifestParser 实现**

```cpp
// src/framework/ManifestParser.cpp
#include "ManifestParser.h"
#include "tinyxml2/tinyxml2.h"
#include <lwlog/lwlog.h>

using namespace tinyxml2;

namespace {

    std::string safeText(XMLElement* parent, const char* childName) {
        if (!parent) return "";
        auto* el = parent->FirstChildElement(childName);
        if (!el) return "";
        const char* txt = el->GetText();
        return txt ? std::string(txt) : std::string();
    }

    std::string safeAttr(XMLElement* el, const char* attrName) {
        if (!el) return "";
        const char* val = el->Attribute(attrName);
        return val ? std::string(val) : std::string();
    }

} // namespace

ToolManifest ManifestParser::parseString(const std::string& xmlContent,
                                          const std::string& sourceName) {
    ToolManifest m;

    XMLDocument doc;
    XMLError err = doc.Parse(xmlContent.c_str());
    if (err != XML_SUCCESS) {
        m.parseError = sourceName + ": XML 解析错误: " + std::string(doc.ErrorStr());
        LWLOG_E(m.parseError.c_str());
        return m;
    }

    auto* root = doc.FirstChildElement("tool");
    if (!root) {
        m.parseError = sourceName + ": 缺少 <tool> 根元素";
        return m;
    }

    // 基本信息
    m.id = safeText(root, "id");
    m.name = safeText(root, "name");
    m.version = safeText(root, "version");
    m.category = safeText(root, "category");
    m.icon = safeText(root, "icon");
    m.description = safeText(root, "description");
    m.author = safeText(root, "author");

    if (m.id.empty() || m.name.empty()) {
        m.parseError = sourceName + ": id 和 name 为必填字段";
        return m;
    }

    // 依赖
    auto* requiresEl = root->FirstChildElement("requires");
    if (requiresEl) {
        m.minHostVersion = safeAttr(requiresEl->FirstChildElement("hostVersion"), "min");
        for (auto* proto = requiresEl->FirstChildElement("protocol");
             proto != nullptr;
             proto = proto->NextSiblingElement("protocol")) {
            ToolManifest::ProtocolDependency dep;
            dep.id = safeAttr(proto, "id");
            dep.minVersion = safeAttr(proto, "minVersion");
            if (!dep.id.empty()) {
                m.protocolDeps.push_back(dep);
            }
        }
    }

    // 入口点
    auto* backendEl = root->FirstChildElement("backend");
    if (backendEl) {
        m.backendEntry = ToolManifest::EntryPoint{
            safeAttr(backendEl, "factory"),
            safeAttr(backendEl, "library")
        };
    }

    auto* widgetEl = root->FirstChildElement("widget");
    if (widgetEl) {
        m.widgetEntry = ToolManifest::EntryPoint{
            safeAttr(widgetEl, "factory"),
            safeAttr(widgetEl, "library")
        };
    }

    // 默认配置
    auto* defaultsEl = root->FirstChildElement("defaults");
    if (defaultsEl) {
        for (auto* prop = defaultsEl->FirstChildElement("property");
             prop != nullptr;
             prop = prop->NextSiblingElement("property")) {
            ToolManifest::DefaultProperty dp;
            dp.key = safeAttr(prop, "key");
            dp.value = safeAttr(prop, "value");
            dp.type = safeAttr(prop, "type");
            if (!dp.key.empty()) {
                m.defaultProperties.push_back(dp);
            }
        }
    }

    m.valid = true;
    return m;
}

ToolManifest ManifestParser::parse(const std::string& xmlFilePath) {
    std::ifstream file(xmlFilePath);
    if (!file.is_open()) {
        ToolManifest m;
        m.parseError = "无法打开文件: " + xmlFilePath;
        return m;
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return parseString(content, xmlFilePath);
}
```

- [ ] **Step 3: 编译验证**

```cmake
# SOURCES 添加:
    src/framework/ManifestParser.cpp
```

```bash
cmake --build . --config Release
```

- [ ] **Step 4: Commit**

```bash
git add src/framework/ManifestParser.h src/framework/ManifestParser.cpp CMakeLists.txt
git commit -m "feat: 添加 ManifestParser 插件清单解析器

基于 tinyxml2 解析 XML 格式的插件清单，提取 id、版本、协议依赖、
DLL 入口点、默认配置等。解析错误时记录 lwlog 并返回 valid=false。

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Task 9: ToolRegistry — Tool 注册表 + 插件发现

**Files:**
- Create: `src/framework/ToolRegistry.h`
- Create: `src/framework/ToolRegistry.cpp`

**Interfaces:**
- Consumes: ManifestParser (Task 8), ToolBackend (Task 7), ToolWidget (Task 7)
- Produces: `ToolRegistry` 单例，管理所有已注册/已发现的 Tool

- [ ] **Step 1: 写 ToolRegistry 头文件**

```cpp
// src/framework/ToolRegistry.h
#pragma once
#include "ManifestParser.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>

class ToolBackend;
class ToolWidget;

// Tool 注册表 — 管理所有可用 Tool 的元数据
class ToolRegistry {
public:
    static ToolRegistry* instance();

    struct ToolEntry {
        std::string   id;
        std::string   name;
        std::string   category;
        std::string   iconPath;
        std::string   version;
        std::string   description;
        bool          isBuiltin = false;      // true=编译在主程序, false=DLL插件
        bool          isAvailable = false;    // 所有依赖满足时为 true
        std::string   unavailabilityReason;   // 不可用原因
        ToolManifest  manifest;               // 原始清单数据
    };

    // 注册内置 Tool（编译时绑定，无需 manifest）
    void registerBuiltin(std::string id, std::string name,
                         std::string category, std::string iconPath,
                         std::string version, std::string description);

    // 扫描 plugins/ 目录发现 DLL 插件
    void scanPluginDirectory(const std::string& pluginDir);

    // 查询
    std::vector<ToolEntry> listAllTools() const;
    std::vector<ToolEntry> listByCategory(const std::string& category) const;
    const ToolEntry* findById(const std::string& id) const;
    std::vector<std::string> allCategories() const;

private:
    ToolRegistry() = default;
    std::vector<ToolEntry> m_entries;
    void addEntry(ToolEntry entry);
};
```

- [ ] **Step 2: 写 ToolRegistry 实现**

```cpp
// src/framework/ToolRegistry.cpp
#include "ToolRegistry.h"
#include "lwcomm/lwcomm.h"  // LWFileHelper / LWFileSystem
#include "lwlog/lwlog.h"
#include <algorithm>

ToolRegistry* ToolRegistry::instance() {
    static ToolRegistry reg;
    return &reg;
}

void ToolRegistry::registerBuiltin(std::string id, std::string name,
                                    std::string category, std::string iconPath,
                                    std::string version, std::string description) {
    ToolEntry entry;
    entry.id = std::move(id);
    entry.name = std::move(name);
    entry.category = std::move(category);
    entry.iconPath = std::move(iconPath);
    entry.version = std::move(version);
    entry.description = std::move(description);
    entry.isBuiltin = true;
    entry.isAvailable = true; // 内置 Tool 总是可用
    addEntry(std::move(entry));
}

void ToolRegistry::scanPluginDirectory(const std::string& pluginDir) {
    // 使用 lwcomm 的文件系统工具扫描子目录
    auto subdirs = LWFileSystem::ListDirectory(pluginDir);
    for (const auto& subdir : subdirs) {
        std::string manifestPath = LWFileHelper::ConCatDir(pluginDir, subdir);
        manifestPath = LWFileHelper::ConCatDir(manifestPath, "manifest.xml");

        auto manifest = ManifestParser::parse(manifestPath);
        if (!manifest.valid) {
            LWLOG_W(("跳过无效插件: " + manifestPath + " — " + manifest.parseError).c_str());
            continue;
        }

        ToolEntry entry;
        entry.id = manifest.id;
        entry.name = manifest.name;
        entry.category = manifest.category;
        entry.iconPath = manifest.icon;
        entry.version = manifest.version;
        entry.description = manifest.description;
        entry.isBuiltin = false;
        entry.manifest = manifest;

        // 校验协议依赖可用性
        bool depsOK = true;
        for (const auto& dep : manifest.protocolDeps) {
            if (!ProtocolRegistry::instance()->isRegistered(dep.id)) {
                depsOK = false;
                entry.unavailabilityReason = "缺少协议适配器: " + dep.id;
                break;
            }
        }
        entry.isAvailable = depsOK;

        addEntry(std::move(entry));
        LWLOG_I(("发现插件: " + entry.id + " (" + entry.name + ")").c_str());
    }
}

void ToolRegistry::addEntry(ToolEntry entry) {
    // 去重：如果已有同名 id，以最后注册的为准
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
                           [&](const ToolEntry& e) { return e.id == entry.id; });
    if (it != m_entries.end()) {
        *it = std::move(entry);
    } else {
        m_entries.push_back(std::move(entry));
    }
}

std::vector<ToolRegistry::ToolEntry> ToolRegistry::listAllTools() const {
    return m_entries;
}

std::vector<ToolRegistry::ToolEntry> ToolRegistry::listByCategory(
    const std::string& category) const {
    std::vector<ToolEntry> result;
    for (const auto& e : m_entries) {
        if (e.category == category) {
            result.push_back(e);
        }
    }
    return result;
}

const ToolRegistry::ToolEntry* ToolRegistry::findById(const std::string& id) const {
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
                           [&](const ToolEntry& e) { return e.id == id; });
    if (it != m_entries.end()) {
        return &(*it);
    }
    return nullptr;
}

std::vector<std::string> ToolRegistry::allCategories() const {
    std::vector<std::string> cats;
    for (const auto& e : m_entries) {
        if (std::find(cats.begin(), cats.end(), e.category) == cats.end()) {
            cats.push_back(e.category);
        }
    }
    return cats;
}
```

- [ ] **Step 3: 编译验证**

```cmake
# SOURCES 添加:
    src/framework/ToolRegistry.cpp
```

```bash
cmake --build . --config Release
```

- [ ] **Step 4: Commit**

```bash
git add src/framework/ToolRegistry.h src/framework/ToolRegistry.cpp CMakeLists.txt
git commit -m "feat: 添加 ToolRegistry 工具注册表 + 插件目录扫描

内置 Tool 通过 registerBuiltin() 编译时注册。
DLL 插件通过 scanPluginDirectory() 运行时发现。
自动校验协议依赖可用性，不可用插件标记原因。

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Task 10: ToolHost 桥接层

**Files:**
- Create: `src/framework/ToolHost.h`
- Create: `src/framework/ToolHost.cpp`

**Interfaces:**
- Consumes: ToolBackend (Task 7), ToolWidget (Task 7), ToolRegistry (Task 9)
- Produces: `ToolHost` — 单例，负责创建/配对/销毁 Tool 的 Backend ↔ Widget

- [ ] **Step 1: 写 ToolHost 头文件**

```cpp
// src/framework/ToolHost.h
#pragma once
#include <QObject>
#include <QWidget>
#include <memory>
#include <string>

class ToolBackend;
class ToolWidget;

// Tool 实例的完整句柄
struct ToolInstance {
    std::shared_ptr<ToolBackend> backend;
    ToolWidget* widget = nullptr; // QWidget，生命周期由 Qt parent 管理
    std::string toolId;
};

// ToolHost 桥接层 — 负责 Tool 的创建、配对、生命周期管理
class ToolHost : public QObject {
    Q_OBJECT

public:
    explicit ToolHost(QObject* parent = nullptr);
    ~ToolHost() override;

    // 注册内置 Tool 的工厂函数
    using BackendFactory = std::shared_ptr<ToolBackend>(*)();
    using WidgetFactory = ToolWidget*(*)(QWidget* parent);

    void registerBuiltinFactory(const std::string& toolId,
                                 BackendFactory bf, WidgetFactory wf);

    // 创建 Tool（通过 ToolRegistry 查找元数据）
    ToolInstance* createTool(const std::string& toolId, QWidget* parentWidget);

    // 销毁 Tool
    void destroyTool(ToolInstance* instance);

    // 已激活的 Tool
    ToolInstance* activeTool() const { return m_activeTool; }

signals:
    void toolCreated(const QString& toolId);
    void toolDestroyed(const QString& toolId);
    void toolError(const QString& toolId, const QString& error);

private:
    struct BuiltinFactory {
        BackendFactory backendFactory;
        WidgetFactory  widgetFactory;
    };
    std::unordered_map<std::string, BuiltinFactory> m_factories;
    ToolInstance* m_activeTool = nullptr;
};
```

- [ ] **Step 2: 写 ToolHost 实现**

```cpp
// src/framework/ToolHost.cpp
#include "ToolHost.h"
#include "ToolBackend.h"
#include "ToolWidget.h"
#include "lwserverbase/core/ServiceManager.h"
#include "lwlog/lwlog.h"

ToolHost::ToolHost(QObject* parent) : QObject(parent) {}

ToolHost::~ToolHost() {
    if (m_activeTool) {
        destroyTool(m_activeTool);
    }
}

void ToolHost::registerBuiltinFactory(const std::string& toolId,
                                       BackendFactory bf, WidgetFactory wf) {
    m_factories[toolId] = {bf, wf};
}

ToolInstance* ToolHost::createTool(const std::string& toolId, QWidget* parentWidget) {
    // 如果已有活跃 Tool，先销毁
    if (m_activeTool) {
        destroyTool(m_activeTool);
    }

    auto it = m_factories.find(toolId);
    if (it == m_factories.end()) {
        LWLOG_E(("未找到 Tool 工厂: " + toolId).c_str());
        emit toolError(QString::fromStdString(toolId),
                       "Tool 工厂未注册");
        return nullptr;
    }

    auto instance = new ToolInstance;
    instance->toolId = toolId;
    instance->backend = it->second.backendFactory();
    instance->widget = it->second.widgetFactory(parentWidget);

    if (!instance->backend || !instance->widget) {
        LWLOG_E(("Tool 创建失败: " + toolId).c_str());
        delete instance;
        return nullptr;
    }

    // 启动 Backend（通过 ServiceManager）
    auto& sm = lwserverbase::core::ServiceManager::instance();
    sm.RegisterService(instance->backend.get());

    try {
        sm.StartService(instance->toolId);
    } catch (const std::exception& e) {
        LWLOG_E(("Tool Backend 启动失败: " + std::string(e.what())).c_str());
        delete instance;
        return nullptr;
    }

    // 通知 Widget
    instance->widget->onToolStart();

    m_activeTool = instance;
    emit toolCreated(QString::fromStdString(toolId));
    LWLOG_I(("Tool 已创建: " + toolId).c_str());

    return instance;
}

void ToolHost::destroyTool(ToolInstance* instance) {
    if (!instance) return;

    if (instance->widget) {
        instance->widget->onToolStop();
        // Qt parent 管理 Widget 生命周期，这里只隐藏
        instance->widget->hide();
        instance->widget->deleteLater();
    }

    if (instance->backend) {
        auto& sm = lwserverbase::core::ServiceManager::instance();
        sm.StopService(instance->toolId);
        instance->backend.reset();
    }

    emit toolDestroyed(QString::fromStdString(instance->toolId));

    if (m_activeTool == instance) {
        m_activeTool = nullptr;
    }

    delete instance;
}
```

- [ ] **Step 3: 编译验证**

```cmake
# SOURCES 添加:
    src/framework/ToolHost.cpp
```

```bash
cmake --build . --config Release
```

- [ ] **Step 4: Commit**

```bash
git add src/framework/ToolHost.h src/framework/ToolHost.cpp CMakeLists.txt
git commit -m "feat: 添加 ToolHost 桥接层

负责 Tool 的完整生命周期管理：
- 创建：Backend 工厂 → Widget 工厂 → Backend.OnStart() → Widget.onToolStart()
- 销毁：Widget.onToolStop() → Backend.OnStop() → delete Widget → reset Backend
- 同一时间只有一个活跃 Tool（切换时自动销毁上一个）

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Task 11: DeviceBusWidget — 设备总线 UI 组件

**Files:**
- Create: `src/ui/DeviceBusWidget.h`
- Create: `src/ui/DeviceBusWidget.cpp`
- Modify: `darkstyle.qss`

**Interfaces:**
- Produces: `DeviceBusWidget` — 水平 QListWidget，显示设备指示灯列表，支持添加/删除/选中

- [ ] **Step 1: 写 DeviceBusWidget 头文件**

```cpp
// src/ui/DeviceBusWidget.h
#pragma once
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLineEdit>
#include <vector>
#include "src/framework/DeviceInfo.h"

class DeviceBusWidget : public QWidget {
    Q_OBJECT

public:
    explicit DeviceBusWidget(QWidget* parent = nullptr);

    // 设备列表操作
    void addDevice(const DeviceInfo& device);
    void removeDevice(const QString& ip);
    std::vector<DeviceInfo> selectedDevices() const;
    std::vector<DeviceInfo> allDevices() const;
    void setOnlineStatus(const QString& ip, bool online);

signals:
    void deviceSelectionChanged();
    void credentialsChanged(const QString& user, const QString& password);

private slots:
    void onAddClicked();
    void onRemoveClicked();

private:
    void setupUi();
    void updateDeviceItem(QListWidgetItem* item, const DeviceInfo& device);

    QListWidget* m_deviceList = nullptr;     // 水平设备列表
    QPushButton* m_addButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QLineEdit*   m_userEdit = nullptr;
    QLineEdit*   m_passEdit = nullptr;
};
```

- [ ] **Step 2: 写 DeviceBusWidget 实现**

```cpp
// src/ui/DeviceBusWidget.cpp
#include "DeviceBusWidget.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QGroupBox>
#include <QInputDialog>
#include <QMessageBox>

DeviceBusWidget::DeviceBusWidget(QWidget* parent) : QWidget(parent) {
    setupUi();
}

void DeviceBusWidget::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 4, 8, 4);
    mainLayout->setSpacing(4);

    // 设备列表行
    auto* deviceRow = new QHBoxLayout();
    deviceRow->setSpacing(6);

    m_deviceList = new QListWidget(this);
    m_deviceList->setFlow(QListWidget::LeftToRight);       // 水平排列
    m_deviceList->setViewMode(QListWidget::IconMode);      // 图标模式
    m_deviceList->setIconSize(QSize(12, 12));
    m_deviceList->setSpacing(4);
    m_deviceList->setMaximumHeight(48);
    m_deviceList->setSelectionMode(QAbstractItemView::MultiSelection);
    m_deviceList->setObjectName("deviceBus");
    connect(m_deviceList, &QListWidget::itemSelectionChanged,
            this, &DeviceBusWidget::deviceSelectionChanged);

    m_addButton = new QPushButton("+", this);
    m_addButton->setFixedSize(28, 28);
    m_addButton->setObjectName("btnDeviceAdd");
    connect(m_addButton, &QPushButton::clicked, this, &DeviceBusWidget::onAddClicked);

    m_removeButton = new QPushButton("−", this);
    m_removeButton->setFixedSize(28, 28);
    m_removeButton->setObjectName("btnDeviceRemove");
    connect(m_removeButton, &QPushButton::clicked, this, &DeviceBusWidget::onRemoveClicked);

    deviceRow->addWidget(m_deviceList, 1);
    deviceRow->addWidget(m_addButton);
    deviceRow->addWidget(m_removeButton);

    mainLayout->addLayout(deviceRow);

    // 凭证行
    auto* credRow = new QHBoxLayout();
    credRow->setSpacing(4);

    auto* userLabel = new QLabel("用户:", this);
    m_userEdit = new QLineEdit(this);
    m_userEdit->setPlaceholderText("用户名");
    m_userEdit->setMaximumWidth(120);

    auto* passLabel = new QLabel("密码:", this);
    m_passEdit = new QLineEdit(this);
    m_passEdit->setPlaceholderText("密码");
    m_passEdit->setEchoMode(QLineEdit::Password);
    m_passEdit->setMaximumWidth(120);

    // 凭证变更信号
    auto emitCredChanged = [this]() {
        emit credentialsChanged(m_userEdit->text(), m_passEdit->text());
    };
    connect(m_userEdit, &QLineEdit::textChanged, this, emitCredChanged);
    connect(m_passEdit, &QLineEdit::textChanged, this, emitCredChanged);

    credRow->addWidget(userLabel);
    credRow->addWidget(m_userEdit);
    credRow->addWidget(passLabel);
    credRow->addWidget(m_passEdit);
    credRow->addStretch();

    mainLayout->addLayout(credRow);
}

void DeviceBusWidget::addDevice(const DeviceInfo& device) {
    // 防止重复添加
    for (int i = 0; i < m_deviceList->count(); ++i) {
        if (m_deviceList->item(i)->data(Qt::UserRole).toString() ==
            QString::fromStdString(device.ip)) {
            return; // 已存在
        }
    }

    auto* item = new QListWidgetItem();
    QString label = device.alias.empty()
        ? QString::fromStdString(device.ip)
        : QString::fromStdString(device.alias) + " (" +
          QString::fromStdString(device.ip) + ")";
    item->setText(label);
    item->setData(Qt::UserRole, QString::fromStdString(device.ip));
    item->setData(Qt::UserRole + 1, QString::fromStdString(device.protocol));
    item->setData(Qt::UserRole + 2, device.port);
    item->setToolTip(QString::fromStdString(
        device.ip + ":" + std::to_string(device.port) + " [" + device.protocol + "]"));
    m_deviceList->addItem(item);
}

void DeviceBusWidget::removeDevice(const QString& ip) {
    for (int i = 0; i < m_deviceList->count(); ++i) {
        if (m_deviceList->item(i)->data(Qt::UserRole).toString() == ip) {
            delete m_deviceList->takeItem(i);
            break;
        }
    }
}

std::vector<DeviceInfo> DeviceBusWidget::selectedDevices() const {
    std::vector<DeviceInfo> devices;
    for (auto* item : m_deviceList->selectedItems()) {
        DeviceInfo di;
        di.ip = item->data(Qt::UserRole).toString().toStdString();
        di.protocol = item->data(Qt::UserRole + 1).toString().toStdString();
        di.port = item->data(Qt::UserRole + 2).toInt();
        devices.push_back(di);
    }
    return devices;
}

std::vector<DeviceInfo> DeviceBusWidget::allDevices() const {
    std::vector<DeviceInfo> devices;
    for (int i = 0; i < m_deviceList->count(); ++i) {
        auto* item = m_deviceList->item(i);
        DeviceInfo di;
        di.ip = item->data(Qt::UserRole).toString().toStdString();
        di.protocol = item->data(Qt::UserRole + 1).toString().toStdString();
        di.port = item->data(Qt::UserRole + 2).toInt();
        devices.push_back(di);
    }
    return devices;
}

void DeviceBusWidget::setOnlineStatus(const QString& ip, bool online) {
    for (int i = 0; i < m_deviceList->count(); ++i) {
        auto* item = m_deviceList->item(i);
        if (item->data(Qt::UserRole).toString() == ip) {
            item->setData(Qt::UserRole + 3, online);
            // 通过 QSS property selector 改变指示灯颜色
            // 见 darkstyle.qss 中 QListWidget::item[online="true"] 样式
            return;
        }
    }
}

void DeviceBusWidget::onAddClicked() {
    bool ok;
    QString text = QInputDialog::getText(this, "添加设备",
        "IP:Port（例: 192.168.1.100:21）",
        QLineEdit::Normal, "", &ok);
    if (ok && !text.isEmpty()) {
        DeviceInfo di;
        if (text.contains(':')) {
            auto parts = text.split(':');
            di.ip = parts[0].toStdString();
            di.port = parts[1].toInt();
        } else {
            di.ip = text.toStdString();
            di.port = 21; // 默认 FTP
        }
        di.protocol = di.port == 23 ? "telnet" :
                      di.port == 502 ? "modbus" : "ftp";
        addDevice(di);
    }
}

void DeviceBusWidget::onRemoveClicked() {
    auto selected = m_deviceList->selectedItems();
    for (auto* item : selected) {
        delete m_deviceList->takeItem(m_deviceList->row(item));
    }
}
```

- [ ] **Step 3: 添加 QSS 样式**

```css
/* 添加到 darkstyle.qss 末尾 */

/* === 设备总线 === */
QListWidget#deviceBus {
    background: #0B0E14;
    border: 1px solid #252A33;
    border-radius: 6px;
    padding: 2px 4px;
}

QListWidget#deviceBus::item {
    background: #141820;
    border: 1px solid #252A33;
    border-radius: 12px;            /* 胶囊形 */
    padding: 4px 12px;
    margin: 2px;
    color: #C8CCD4;
    font-size: 12px;
}

QListWidget#deviceBus::item:selected {
    background: #1A2030;
    border: 1px solid #F0A030;      /* 琥珀色选中光晕 */
    color: #F0A030;
}

/* 在线/离线指示灯 — 使用 item 的 icon 或自定义 property */
QListWidget#deviceBus::item[online="true"]::icon {
    /* 在线: 使用青绿色圆形图标 */
}

QListWidget#deviceBus::item[online="false"]::icon {
    /* 离线: 红色圆形图标 */
}

/* 添加/删除按钮 */
QPushButton#btnDeviceAdd, QPushButton#btnDeviceRemove {
    background: #141820;
    border: 1px solid #252A33;
    border-radius: 14px;
    color: #C8CCD4;
    font-size: 16px;
    font-weight: bold;
}

QPushButton#btnDeviceAdd:hover {
    background: #1A2030;
    border-color: #40C8A0;
    color: #40C8A0;
}

QPushButton#btnDeviceRemove:hover {
    background: #1A2030;
    border-color: #E85848;
    color: #E85848;
}
```

- [ ] **Step 4: 编译验证**

```cmake
# SOURCES 添加:
    src/ui/DeviceBusWidget.cpp
```

```bash
cmake --build . --config Release
```

- [ ] **Step 5: Commit**

```bash
git add src/ui/DeviceBusWidget.h src/ui/DeviceBusWidget.cpp darkstyle.qss CMakeLists.txt
git commit -m "feat: 添加 DeviceBusWidget 设备总线 UI 组件

水平胶囊形设备列表，支持多选、添加、删除。
在线/离线状态指示灯，琥珀色选中光晕。
工业仪表盘风格 QSS 样式。

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Task 12: darkstyle.qss — 全局色板替换

**Files:**
- Modify: `darkstyle.qss`

- [ ] **Step 1: 全局色板替换**

在 darkstyle.qss 中执行以下替换：

```
背景底层:   #1A1A2E → #0B0E14
面板/容器:  #16213E 相关 → #141820
边框:       #2A3A5C 相关 → #252A33
主要文字:   #E0E0E0 → #C8CCD4
次要文字:   灰色系    → #7B8494
强调色:     #3498DB → #F0A030
  - btnPrimary 背景: #2980B9 → #F0A030
  - btnPrimary hover: #3498DB → #D48820
  - 选中高亮: #2C5F8A → #1A2030
  - Tab 选中下划线: #3498DB → #F0A030
成功色:     绿色系 → #40C8A0
警告色:     黄色系 → #F0D050
错误色:     红色系 → #E85848
进度条:     #3498DB → #F0A030
ScrollBar:  #3498DB → #F0A030
```

精确的 search/replace 列表（共约 25 处）：

```bash
# 执行替换命令：
sed -i 's/#1A1A2E/#0B0E14/g' darkstyle.qss
sed -i 's/#16213E/#141820/g' darkstyle.qss
sed -i 's/#3498DB/#F0A030/g' darkstyle.qss
sed -i 's/#2980B9/#D48820/g' darkstyle.qss
sed -i 's/#E0E0E0/#C8CCD4/g' darkstyle.qss
# ... 以及更多精确替换
```

> 实际实现中应查找所有颜色值并手动替换，确保语义正确。

- [ ] **Step 2: 编译运行验证视觉效果**

```bash
cmake --build . --config Release
./Release/DeployMaster.exe
```

确认：主窗口背景为深黑色 (#0B0E14)，按钮为琥珀色，文字为暖灰白。

- [ ] **Step 3: Commit**

```bash
git add darkstyle.qss
git commit -m "style: darkstyle.qss 全局色板替换为工业仪表盘主题

#0B0E14 深黑底 + #F0A030 琥珀强调 + #40C8A0 青绿状态
替换原有蓝色系 (#3498DB) 为琥珀色系 (#F0A030)

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Task 13: FtpDeployTool — 将 FTP 部署迁移为第一个 Tool

**Files:**
- Create: `src/tools/FtpDeployTool/FtpDeployBackend.h`
- Create: `src/tools/FtpDeployTool/FtpDeployBackend.cpp`
- Create: `src/tools/FtpDeployTool/FtpDeployWidget.h`
- Create: `src/tools/FtpDeployTool/FtpDeployWidget.cpp`
- Modify: `DeployMaster.cpp`（注册 Tool，添加切换逻辑）
- Modify: `main.cpp`（注册内置 Tool 工厂）

**Interfaces:**
- Consumes: ToolBackend/ToolWidget (Task 7), FtpAdapter (Task 4), ToolHost (Task 10), DeviceBusWidget (Task 11)
- Produces: 完整的 FtpDeployTool，替换现有 DeployMaster 中分散的 FTP 部署代码

- [ ] **Step 1: 写 FtpDeployBackend 头文件**

```cpp
// src/tools/FtpDeployTool/FtpDeployBackend.h
#pragma once
#include "src/framework/ToolBackend.h"
#include "src/adapter/FtpAdapter.h"
#include <memory>
#include <vector>

class FtpDeployBackend : public ToolBackend {
public:
    FtpDeployBackend();

    // --- ToolBackend 实现 ---
    std::string toolId() const override { return "com.deploymaster.ftp.deploy"; }
    std::string toolName() const override { return "文件部署"; }
    std::string toolVersion() const override { return "2.0.0"; }
    std::string toolCategory() const override { return "deploy"; }
    std::string toolIcon() const override { return "ftp_deploy"; }

    int OnStart() override;
    int OnStop() override;
    void bindDevices(const std::vector<DeviceInfo>& devices) override;
    void bindCredentials(const AuthInfo& auth) override;
    void applyConfig(const lwserverbase::config::ConfigValue& config) override;

    // --- FTP 部署特有操作 ---
    void startUpload(const std::vector<std::string>& localFiles,
                     const std::string& remotePath,
                     bool clearBeforeDeploy,
                     bool rebootAfterDeploy);
    void cancelUpload();

    // 进度回调设置
    void setProgressCallback(std::function<void(int)> cb);
    void setLogCallback(std::function<void(const std::string&)> cb);
    void setFinishedCallback(std::function<void(bool, const std::vector<std::string>&,
                                                 const std::vector<std::string>&)> cb);

private:
    std::vector<DeviceInfo> m_devices;
    AuthInfo m_auth;
    std::string m_remotePath;
    bool m_clearBeforeDeploy = false;
    bool m_rebootAfterDeploy = false;
    std::atomic<bool> m_cancelled{false};

    std::function<void(int)> m_progressCb;
    std::function<void(const std::string&)> m_logCb;
    std::function<void(bool, const std::vector<std::string>&,
                       const std::vector<std::string>&)> m_finishedCb;
};
```

- [ ] **Step 2: 写 FtpDeployBackend 实现**

```cpp
// src/tools/FtpDeployTool/FtpDeployBackend.cpp
#include "FtpDeployBackend.h"
#include "src/adapter/ProtocolRegistry.h"
#include <QtConcurrent/QtConcurrent>
#include <lwlog/lwlog.h>

FtpDeployBackend::FtpDeployBackend() {}

int FtpDeployBackend::OnStart() {
    LWLOG_I("FtpDeployBackend 启动");
    return 0;
}

int FtpDeployBackend::OnStop() {
    cancelUpload();
    LWLOG_I("FtpDeployBackend 停止");
    return 0;
}

void FtpDeployBackend::bindDevices(const std::vector<DeviceInfo>& devices) {
    m_devices = devices;
}

void FtpDeployBackend::bindCredentials(const AuthInfo& auth) {
    m_auth = auth;
}

void FtpDeployBackend::applyConfig(const lwserverbase::config::ConfigValue& /*config*/) {
    // 从 ConfigManager 读取运行时配置变更
}

void FtpDeployBackend::startUpload(const std::vector<std::string>& localFiles,
                                    const std::string& remotePath,
                                    bool clearBeforeDeploy,
                                    bool rebootAfterDeploy) {
    m_cancelled = false;
    m_remotePath = remotePath;
    m_clearBeforeDeploy = clearBeforeDeploy;
    m_rebootAfterDeploy = rebootAfterDeploy;

    QtConcurrent::run([this, localFiles]() {
        std::vector<std::string> successes, failures;

        for (size_t i = 0; i < m_devices.size(); ++i) {
            if (m_cancelled) break;

            const auto& device = m_devices[i];
            std::string deviceKey = device.ip + ":" + std::to_string(device.port);

            // 从 ProtocolRegistry 创建连接
            auto adapter = ProtocolRegistry::instance()->create("ftp");
            if (!adapter) {
                if (m_logCb) m_logCb("❌ FTP 适配器不可用: " + deviceKey);
                failures.push_back(deviceKey);
                continue;
            }

            auto* ftp = dynamic_cast<FtpAdapter*>(adapter.get());
            if (!ftp) {
                failures.push_back(deviceKey);
                continue;
            }

            // 连接
            if (!ftp->connect(device, m_auth)) {
                if (m_logCb) m_logCb("❌ 连接失败: " + deviceKey + " — " + ftp->lastError());
                failures.push_back(deviceKey);
                continue;
            }

            if (m_logCb) m_logCb("⬤ 已连接: " + deviceKey);

            // 可选：清空远程目录
            if (m_clearBeforeDeploy) {
                if (m_logCb) m_logCb("🧹 清空中: " + deviceKey + m_remotePath);
                ftp->clearRemoteDirectory(m_remotePath);
            }

            // 设置进度回调
            ftp->setProgressCallback([this](int pct) {
                if (m_progressCb) m_progressCb(pct);
            });

            // 上传文件
            bool allOk = true;
            for (const auto& file : localFiles) {
                if (m_cancelled) break;

                // 从完整路径中提取文件名
                std::string fileName = file;
                size_t lastSlash = file.find_last_of("/\\");
                if (lastSlash != std::string::npos) {
                    fileName = file.substr(lastSlash + 1);
                }

                std::string remoteFile = m_remotePath;
                if (!remoteFile.empty() && remoteFile.back() != '/') {
                    remoteFile += '/';
                }
                remoteFile += fileName;

                if (m_logCb) m_logCb("▶ 上传: " + fileName + " → " + deviceKey);

                if (ftp->uploadFile(file, remoteFile)) {
                    if (m_logCb) m_logCb("✓ " + fileName + " 上传完成");
                } else {
                    if (m_logCb) m_logCb("✗ " + fileName + " 上传失败: " + ftp->lastError());
                    allOk = false;
                }
            }

            ftp->disconnect();

            if (allOk) {
                successes.push_back(deviceKey);
            } else {
                failures.push_back(deviceKey);
            }
        }

        if (m_finishedCb) {
            m_finishedCb(!successes.empty(), successes, failures);
        }
    });
}

void FtpDeployBackend::cancelUpload() {
    m_cancelled = true;
}

void FtpDeployBackend::setProgressCallback(std::function<void(int)> cb) {
    m_progressCb = std::move(cb);
}

void FtpDeployBackend::setLogCallback(std::function<void(const std::string&)> cb) {
    m_logCb = std::move(cb);
}

void FtpDeployBackend::setFinishedCallback(
    std::function<void(bool, const std::vector<std::string>&,
                       const std::vector<std::string>&)> cb) {
    m_finishedCb = std::move(cb);
}
```

- [ ] **Step 3: 写 FtpDeployWidget**

```cpp
// src/tools/FtpDeployTool/FtpDeployWidget.h
#pragma once
#include "src/framework/ToolWidget.h"
#include <QLineEdit>
#include <QTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QCheckBox>
#include <QListWidget>

class FtpDeployBackend;

class FtpDeployWidget : public ToolWidget {
    Q_OBJECT
public:
    explicit FtpDeployWidget(QWidget* parent = nullptr);

    QString toolId() const override { return "com.deploymaster.ftp.deploy"; }
    QString toolName() const override { return "文件部署"; }
    void onToolStart() override;
    void onToolStop() override;

    void setBackend(FtpDeployBackend* backend);

private slots:
    void onAddFilesClicked();
    void onAddFolderClicked();
    void onClearFilesClicked();
    void onDeployClicked();

private:
    void setupUi();
    void appendLog(const QString& msg);

    FtpDeployBackend* m_backend = nullptr;

    // UI 控件
    QLineEdit*      m_remotePathEdit = nullptr;
    QCheckBox*      m_clearCheck = nullptr;
    QCheckBox*      m_rebootCheck = nullptr;
    QListWidget*    m_fileList = nullptr;
    QPushButton*    m_deployBtn = nullptr;
    QPushButton*    m_cancelBtn = nullptr;
    QProgressBar*   m_progressBar = nullptr;
    QTextEdit*      m_logView = nullptr;
};
```

```cpp
// src/tools/FtpDeployTool/FtpDeployWidget.cpp
#include "FtpDeployWidget.h"
#include "FtpDeployBackend.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QFileDialog>

FtpDeployWidget::FtpDeployWidget(QWidget* parent) : ToolWidget(parent) {
    setupUi();
}

void FtpDeployWidget::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    // === 配置区 ===
    auto* configGroup = new QGroupBox("配置", this);
    auto* configLayout = new QHBoxLayout(configGroup);

    configLayout->addWidget(new QLabel("远程路径:", this));
    m_remotePathEdit = new QLineEdit("/app", this);
    m_remotePathEdit->setPlaceholderText("/app");
    configLayout->addWidget(m_remotePathEdit, 1);

    m_clearCheck = new QCheckBox("部署前清空", this);
    m_rebootCheck = new QCheckBox("完成后重启", this);
    configLayout->addWidget(m_clearCheck);
    configLayout->addWidget(m_rebootCheck);

    mainLayout->addWidget(configGroup);

    // === 操作区 ===
    auto* actionGroup = new QGroupBox("操作", this);
    auto* actionLayout = new QHBoxLayout(actionGroup);

    auto* addFilesBtn = new QPushButton("+ 添加文件", this);
    auto* addFolderBtn = new QPushButton("+ 添加文件夹", this);
    auto* clearFilesBtn = new QPushButton("清空列表", this);

    connect(addFilesBtn, &QPushButton::clicked, this, &FtpDeployWidget::onAddFilesClicked);
    connect(addFolderBtn, &QPushButton::clicked, this, &FtpDeployWidget::onAddFolderClicked);
    connect(clearFilesBtn, &QPushButton::clicked, this, &FtpDeployWidget::onClearFilesClicked);

    actionLayout->addWidget(addFilesBtn);
    actionLayout->addWidget(addFolderBtn);
    actionLayout->addWidget(clearFilesBtn);
    actionLayout->addStretch();

    m_deployBtn = new QPushButton("▶ 开始部署", this);
    m_deployBtn->setObjectName("btnPrimary");
    m_cancelBtn = new QPushButton("■ 取消", this);
    m_cancelBtn->setEnabled(false);
    connect(m_deployBtn, &QPushButton::clicked, this, &FtpDeployWidget::onDeployClicked);
    connect(m_cancelBtn, &QPushButton::clicked, [this]() {
        if (m_backend) m_backend->cancelUpload();
        m_cancelBtn->setEnabled(false);
    });

    actionLayout->addWidget(m_deployBtn);
    actionLayout->addWidget(m_cancelBtn);

    mainLayout->addWidget(actionGroup);

    // === 文件列表 + 进度 ===
    m_fileList = new QListWidget(this);
    m_fileList->setMinimumHeight(80);
    m_fileList->setAlternatingRowColors(true);
    mainLayout->addWidget(m_fileList, 1);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    mainLayout->addWidget(m_progressBar);

    // === 日志区 ===
    m_logView = new QTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setMaximumHeight(120);
    m_logView->setPlaceholderText("部署日志...");
    mainLayout->addWidget(m_logView);

    setLayout(mainLayout);
}

void FtpDeployWidget::setBackend(FtpDeployBackend* backend) {
    m_backend = backend;
    if (!m_backend) return;

    // 绑定 Backend 回调
    m_backend->setProgressCallback([this](int pct) {
        QMetaObject::invokeMethod(this, [this, pct]() {
            m_progressBar->setValue(pct);
        }, Qt::QueuedConnection);
    });

    m_backend->setLogCallback([this](const std::string& msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            appendLog(QString::fromStdString(msg));
        }, Qt::QueuedConnection);
    });

    m_backend->setFinishedCallback(
        [this](bool ok, const std::vector<std::string>& successes,
               const std::vector<std::string>& failures) {
            QMetaObject::invokeMethod(this, [this, ok, successes, failures]() {
                m_deployBtn->setEnabled(true);
                m_cancelBtn->setEnabled(false);
                m_progressBar->setValue(ok ? 100 : 0);
                QString summary = ok
                    ? QString("部署完成 — 成功: %1, 失败: %2")
                          .arg(successes.size()).arg(failures.size())
                    : "部署失败";
                appendLog(summary);
                emit toolStatusChanged(summary);
            }, Qt::QueuedConnection);
        });
}

void FtpDeployWidget::onToolStart() {
    appendLog("文件部署工具已就绪");
    emit toolStatusChanged("就绪");
}

void FtpDeployWidget::onToolStop() {
    appendLog("文件部署工具已停止");
}

void FtpDeployWidget::onAddFilesClicked() {
    QStringList files = QFileDialog::getOpenFileNames(this, "选择要部署的文件");
    for (const auto& f : files) {
        m_fileList->addItem(f);
    }
}

void FtpDeployWidget::onAddFolderClicked() {
    QString dir = QFileDialog::getExistingDirectory(this, "选择要部署的文件夹");
    if (!dir.isEmpty()) {
        m_fileList->addItem(dir + "/*");
    }
}

void FtpDeployWidget::onClearFilesClicked() {
    m_fileList->clear();
}

void FtpDeployWidget::onDeployClicked() {
    if (!m_backend) {
        appendLog("❌ Backend 未就绪");
        return;
    }
    if (m_fileList->count() == 0) {
        appendLog("⚠ 请先添加要部署的文件");
        return;
    }

    // 从文件列表提取路径
    std::vector<std::string> files;
    for (int i = 0; i < m_fileList->count(); ++i) {
        files.push_back(m_fileList->item(i)->text().toStdString());
    }

    m_deployBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_progressBar->setValue(0);

    appendLog("▶ 开始部署...");
    emit toolStatusChanged("部署中...");

    m_backend->startUpload(
        files,
        m_remotePathEdit->text().toStdString(),
        m_clearCheck->isChecked(),
        m_rebootCheck->isChecked()
    );
}

void FtpDeployWidget::appendLog(const QString& msg) {
    QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");
    m_logView->append("[" + ts + "] " + msg);
}
```

- [ ] **Step 4: 在 main.cpp 注册工厂 + 在 DeployMaster 集成**

```cpp
// main.cpp 添加:
#include "src/tools/FtpDeployTool/FtpDeployBackend.h"
#include "src/tools/FtpDeployTool/FtpDeployWidget.h"
#include "src/framework/ToolHost.h"
#include "src/framework/ToolRegistry.h"

// 在 main() 中，DeployMaster window 创建后:

// 注册 Tool 到注册表
ToolRegistry::instance()->registerBuiltin(
    "com.deploymaster.ftp.deploy", "文件部署", "deploy",
    "ftp_deploy", "2.0.0", "通过 FTP 协议批量上传文件/文件夹到目标设备");

// 注册 Tool 工厂
ToolHost* host = new ToolHost(&window);
host->registerBuiltinFactory("com.deploymaster.ftp.deploy",
    []() -> std::shared_ptr<ToolBackend> {
        return std::make_shared<FtpDeployBackend>();
    },
    [](QWidget* parent) -> ToolWidget* {
        return new FtpDeployWidget(parent);
    });
```

- [ ] **Step 5: 编译验证**

```cmake
# SOURCES 添加:
    src/tools/FtpDeployTool/FtpDeployBackend.cpp
    src/tools/FtpDeployTool/FtpDeployWidget.cpp
```

```bash
cmake --build . --config Release
```

- [ ] **Step 6: 功能回归测试（手动）**

1. 启动 DeployMaster.exe
2. 确认左侧 Tool 导航中出现"文件部署"
3. 添加设备到总线
4. 添加文件 → 开始部署
5. 确认进度条更新、日志输出

- [ ] **Step 7: Commit**

```bash
git add src/tools/ CMakeLists.txt main.cpp DeployMaster.cpp
git commit -m "feat: 添加 FtpDeployTool — FTP 迁移到 Tool 架构

FtpDeployBackend: 继承 ToolBackend，通过 ProtocolRegistry 获取 FtpAdapter
FtpDeployWidget: 标准三段式布局（配置→操作→结果+日志）
DeployMaster 逐步解耦：FTP 功能由 ToolHost 管理

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Task 14: 清理死代码 — 删除 ConnectionPool、EventBus、空壳文件

**Files:**
- Delete: `src/framework/ConnectionPool.h`
- Delete: `src/framework/ConnectionPool.cpp`
- Delete: `src/framework/EventBus.h`
- Delete: `src/framework/EventBus.cpp`
- Delete: `TelnetManager.cpp`（1字节空文件）
- Delete: `FileDeploy.h`
- Delete: `FileDeploy.cpp`（空壳）
- Modify: `CMakeLists.txt`（移除已删除源文件）
- Modify: `DeployMaster.h`（移除已删除头文件的 #include）
- Modify: `main.cpp`（移除 qRegisterMetaType<DeployEvent> 等旧引用）
- Modify: `src/presenter/FtpPresenter.h/.cpp`（添加 DEPRECATED 注释，暂时保留引用直到完全迁移）

- [ ] **Step 1: 删除文件**

```bash
rm src/framework/ConnectionPool.h
rm src/framework/ConnectionPool.cpp
rm src/framework/EventBus.h
rm src/framework/EventBus.cpp
rm TelnetManager.cpp
rm FileDeploy.h
rm FileDeploy.cpp
```

- [ ] **Step 2: 更新 CMakeLists.txt**

```cmake
# 从 SOURCES 中移除:
    # src/framework/EventBus.cpp        ← 删除
    # src/framework/ConnectionPool.cpp  ← 删除
    # TelnetManager.cpp                 ← 删除
    # FileDeploy.cpp                    ← 删除
```

- [ ] **Step 3: 更新 DeployMaster.h**

```cpp
// DeployMaster.h — 移除以下 #include：
// #include "src/utils/DeployEvent.h"      ← 删除
// #include "src/framework/EventBus.h"     ← 删除

// 移除继承自 EventBus 的 slots（如 onTaskProgress, onTaskFinished）
// 这些现在由 Tool 自己管理
```

- [ ] **Step 4: 更新 main.cpp**

```cpp
// main.cpp — 移除:
// qRegisterMetaType<DeployEvent>("DeployEvent");
// qRegisterMetaType<DeployEvent::Type>("DeployEvent::Type");
```

- [ ] **Step 5: 编译验证 — 确保无链接错误**

```bash
cmake --build . --config Release
# 应编译成功，无未解析符号
```

- [ ] **Step 6: Commit**

```bash
git rm src/framework/ConnectionPool.h src/framework/ConnectionPool.cpp
git rm src/framework/EventBus.h src/framework/EventBus.cpp
git rm TelnetManager.cpp FileDeploy.h FileDeploy.cpp
git add CMakeLists.txt DeployMaster.h main.cpp
git commit -m "refactor: 清理死代码 — 删除 ConnectionPool/EventBus/空壳文件

- ConnectionPool 由 lwcommunicate::LWConnPool 替代
- EventBus 由 lwmsgq 消息队列替代
- TelnetManager.cpp（1字节）、FileDeploy（空壳）删除
- 移除 main.cpp 中 DeployEvent 元类型注册

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Task 15: 最终集成验证

**目标**: 确认 Phase 0 + Phase 1 所有改动组合在一起可编译可运行，现有功能不回归。

- [ ] **Step 1: 全量编译**

```bash
rm -rf build && mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH="C:\Qt\6.10.1\msvc2022_64"
cmake --build . --config Release 2>&1 | tee build.log
```

预期：0 错误，0 警告（新增代码部分）。

- [ ] **Step 2: 运行冒烟测试**

```bash
./Release/DeployMaster.exe
```

确认：
- [ ] 主窗口正常显示（深色主题 + 琥珀色强调色）
- [ ] 设备总线组件可见（顶部）
- [ ] Tool 导航显示"文件部署"
- [ ] 点击"文件部署"能切换到 FtpDeployWidget
- [ ] 添加 IP 设备到总线 → 添加文件 → 部署 → 进度 + 日志正常
- [ ] 其他旧 Tab（Telnet/Modbus/WebSocket）仍可切换（向后兼容）

- [ ] **Step 3: 检查无崩溃（至少运行 5 分钟）**

操作：反复切换 Tool、添加/删除设备、启动/取消部署。确认无 crash 或内存异常。

- [ ] **Step 4: 更新 CLAUDE.md**

```bash
# 更新 CLAUDE.md 中的架构状态描述，标记 dead code 已清理、Phase 0-1 完成
```

- [ ] **Step 5: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: 更新 CLAUDE.md — Phase 0-1 框架搭建完成

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## 后续计划预览（Phase 2-3）

以下为后续计划的简要概述，将在 Phase 0-1 验证通过后生成详细实施计划：

### Phase 2：全量模块迁移

| 任务 | 内容 |
|------|------|
| TelnetTool | TelnetDeploy → TelnetTool（Backend + Widget），复用 TelnetAdapter |
| LogQueryTool | LogQueryTab → LogQueryTool，复用 FtpAdapter 的 listDirectory + downloadFile |
| ModbusTool | ModbusCluster → ModbusTool，封装 QModbusTcpClient 进 ModbusAdapter |
| WebSocketTool | WebSocketClient → WebSocketTool |
| OpcUaTool | OpcUaClientTab → OpcUaTool（仍为演示模式） |
| 删除旧代码 | 移除根目录所有旧 Tab 类、删除 DeployMaster.ui、清理 FtpPresenter |

### Phase 3：插件化 & 扩展

| 任务 | 内容 |
|------|------|
| QPluginLoader | DLL 插件加载机制 + 完整 manifest.xml 校验 |
| SSH Adapter | 基于 libssh2 + LWTcpClient 的 SSH 协议适配器（首个 DLL 插件 Demo） |
| SSH Terminal Tool | 交互式 SSH 终端 Tool |
| 主窗口代码化 | 废弃 DeployMaster.ui，纯 C++ 构建布局 |
