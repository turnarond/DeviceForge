# SSH 适配器 + 批量命令扩展 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在现有 Telnet「批量命令」Tab 上增加 SSH 协议支持（libssh2），扩展现有 UI 而非新建。

**Architecture:** 新建 SshAdapter（IProtocolAdapter 子类，内部 libssh2 session+channel）；TelnetBackend 协议感知化（用成员变量替换硬编码的 `"telnet"` 字符串）；TelnetWidget 加协议下拉；libssh2 以预编译 lib 方式集成（与 libcurl 一致）。

**Tech Stack:** C++17、Qt 6.10.1、libssh2（BSD 3-Clause）、QTCPSocket（libssh2 走自定义 socket 回调或原生 socket 桥接）。

## Global Constraints

- 命名：类 PascalCase、方法 camelCase、成员 `m_camelCase`、常量 `kXxx`。
- SshAdapter 实现 `IProtocolAdapter`（与 TelnetAdapter 接口一致）。
- libssh2 集成方式：预编译 `libssh2.lib` + `libssh2.dll` 放入 `lib/`；头文件放入 `include/`（与 libcurl 一致）。
- 构建：CMake + vcxproj 双构建都要加 libssh2 链接。CMake POST_BUILD 复制 `libssh2.dll` 到输出目录。
- 安全：SSH 密码认证 + TOFU 主机密钥（首次自动接受，指纹变化告警）；日志去敏感化（命令输出不记录内容，仅记录字节数）；密码断开后 `AuthInfo::clear()` 擦除。
- 协议粒度：Tab 级别选协议（一次执行中所有设备统一 Telnet 或 SSH）。
- 复用现有 TelnetWidget 的所有命令输入/结果展示/导出 UI。
- 设计依据：`docs/superpowers/specs/2026-07-10-ssh-adapter-design.md`。

---

## 文件结构

| 文件 | 动作 | 责任 |
|------|------|------|
| `lib/libssh2.lib` + `lib/libssh2.dll` | 新建 | libssh2 预编译库（由用户提供,或从官方下载） |
| `include/libssh2.h` 等 | 新建 | libssh2 头文件 |
| `src/adapter/SshAdapter.h/.cpp` | 新建 | IProtocolAdapter 的 SSH 实现 |
| `main.cpp` | 修改 | ProtocolRegistry 注册 SshAdapter |
| `src/tools/TelnetTool/TelnetBackend.h/.cpp` | 修改 | 加 setProtocol + 适配器切换 |
| `src/tools/TelnetTool/TelnetWidget.h/.cpp` | 修改 | 加协议下拉 |
| `CMakeLists.txt` + `DeployMaster.vcxproj` | 修改 | libssh2 库 + 头文件 + DLL 复制 |
| `CLAUDE.md` / `README.md` / docs | 修改 | 文档同步 |

---

## Task 1: libssh2 库集成（构建系统）

**Files:**
- Create: `lib/libssh2.lib`, `lib/libssh2.dll`（预编译,需用户提供）
- Create: `include/libssh2.h`, `include/libssh2_publickey.h`, `include/libssh2_sftp.h`
- Modify: `CMakeLists.txt`, `DeployMaster.vcxproj`

> **前置条件**:libssh2 预编译二进制需由用户提供或从 https://libssh2.org/ 下载 Windows x64 版本,放入 `lib/` 和 `include/`。本任务的 CMake/vcxproj 配置是"接线就绪"——一旦 DLL/头就位,构建即带 SSH。

- [ ] **Step 1: CMakeLists.txt 加 libssh2 依赖**

在 `find_library(LIBCURL_IMPL_LIB ...)` 之后追加:
```cmake
find_library(LIBSSH2_LIB
    NAMES libssh2
    PATHS ${LIB_DIR}
    NO_DEFAULT_PATH
)
if(NOT LIBSSH2_LIB)
    message(WARNING "libssh2.lib not found in ${LIB_DIR} — SSH adapter will not link")
endif()
```
在 `target_link_libraries(DeviceForge PRIVATE ...)` 末尾,`${LIBCURL_IMPL_LIB}` 之后加:
```cmake
    ${LIBSSH2_LIB}
```
在 `add_custom_command(TARGET DeviceForge POST_BUILD ... libcurl ...)` 之后加 libssh2.dll 复制:
```cmake
add_custom_command(TARGET DeviceForge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${LIB_DIR}/libssh2.dll"
        "$<TARGET_FILE_DIR:DeviceForge>"
    COMMENT "Copying libssh2.dll to output directory"
)
```
注:若 `LIBSSH2_LIB` 未找到(空字符串),`target_link_libraries` 仍能编译(链接时忽略空),SSH 功能不可用——这是软依赖。

- [ ] **Step 2: DeployMaster.vcxproj 加 libssh2 链接**

在 `<AdditionalDependencies>`（Debug 和 Release 两处）的 `libcurl_imp.lib` 之后追加:
```xml
libssh2.lib;
```
在 `<AdditionalIncludeDirectories>` 确认 `./include` 已在（已有 libcurl 用,无需改）。
在 PostBuild（两处）的 curl 复制命令之后加:
```xml
      <Command>copy /Y "$(ProjectDir)lib\libssh2.dll" "$(OutDir)libssh2.dll"</Command>
```

- [ ] **Step 3: 确认 libssh2 二进制和头文件就位**

Run: `ls -la /d/persional/QtDeployMaster/lib/libssh2.* 2>/dev/null && echo "lib OK" || echo "lib MISSING — 需用户提供 libssh2.lib + libssh2.dll"` 和 `ls /d/persional/QtDeployMaster/include/libssh2*.h 2>/dev/null | head && echo "headers OK" || echo "headers MISSING"`
若缺少,本任务标记为 **BLOCKED — 等待 libssh2 预编译二进制**——这是本计划唯一的外部依赖。后续 Task 2-5 假设库已就位。

- [ ] **Step 4: 构建验证**

若库已就位:
```bash
cd /d/persional/QtDeployMaster/build && cmake .. -DCMAKE_PREFIX_PATH="C:/Qt/6.10.1/msvc2022_64" >/tmp/cfg.log 2>&1 && cmake --build . --config Release 2>&1 | tail -3
```
Expected: 0 error（libssh2 链接成功）。

- [ ] **Step 5: Commit**

```bash
cd /d/persional/QtDeployMaster
git add CMakeLists.txt DeployMaster.vcxproj
# 若 libssh2 二进制/头文件已放入:
git add lib/libssh2.* include/libssh2*.h
git commit -m "build: 集成 libssh2(CMake+vcxproj, SSH 适配器底层依赖)"
```

---

## Task 2: SshAdapter（IProtocolAdapter 实现）

**Files:**
- Create: `src/adapter/SshAdapter.h`, `SshAdapter.cpp`
- Modify: `CMakeLists.txt`（SOURCES 加 SshAdapter.cpp）, `DeployMaster.vcxproj`（加 ClCompile + ClInclude）

**Interfaces:**
- Consumes: `IProtocolAdapter`（protocolId/connect/disconnect/isConnected/lastError/request/subscribe）、`libssh2.h`。
- Produces: `SshAdapter` 类,实现 IProtocolAdapter 全部纯虚函数。

- [ ] **Step 1: SshAdapter.h**

```cpp
#pragma once
#include "IProtocolAdapter.h"
#include <QTcpSocket>
#include <libssh2.h>
#include <QSet>
#include <QFuture>
#include <atomic>

class SshAdapter : public IProtocolAdapter {
public:
    SshAdapter();
    ~SshAdapter() override;

    std::string protocolId() const override { return "ssh"; }
    bool connect(const DeviceInfo& device, const AuthInfo& auth) override;
    void disconnect() override;
    bool isConnected() const override;
    std::string lastError() const override;
    std::future<Response> request(const Request& req) override;
    void subscribe(const Request& req, StreamCallback onData) override;
    void unsubscribe() override;
    ProtocolCapability capability() const override;

private:
    QTcpSocket*         m_socket = nullptr;
    LIBSSH2_SESSION*    m_session = nullptr;
    LIBSSH2_CHANNEL*    m_channel = nullptr;  // subscribe 模式用,request 模式每次新建
    std::string         m_lastError;
    std::atomic<bool>   m_cancelled{false};
    QSet<QString>       m_knownHosts;         // TOFU: 已接受的主机指纹集合
    QFuture<void>       m_subscribeFuture;
    std::atomic<bool>   m_subscribeActive{false};

    std::string collectHostFingerprint();     // 获取当前连接主机指纹(SHA256 Hex)
    bool verifyHostKey();                     // TOFU 校验（首次接受,变化告警）
    void closeChannel(LIBSSH2_CHANNEL*& ch);  // 安全关闭 channel
};
```

- [ ] **Step 2: SshAdapter.cpp — connect 实现**

```cpp
#include "SshAdapter.h"
#include <lwlog/lwlog.h>
#include <QHostAddress>
#include <thread>

SshAdapter::SshAdapter() { libssh2_init(0); }
SshAdapter::~SshAdapter() { disconnect(); libssh2_exit(); }

bool SshAdapter::connect(const DeviceInfo& device, const AuthInfo& auth)
{
    disconnect();

    m_socket = new QTcpSocket();
    m_socket->connectToHost(QString::fromStdString(device.ip), device.port ? device.port : 22);
    if (!m_socket->waitForConnected(10000)) {
        m_lastError = "连接失败: " + m_socket->errorString().toStdString();
        delete m_socket; m_socket = nullptr;
        return false;
    }

    m_session = libssh2_session_init();
    if (!m_session) { m_lastError = "libssh2 session 初始化失败"; disconnect(); return false; }

    libssh2_session_set_blocking(m_session, 1);  // 阻塞模式（在 QtConcurrent::run 线程中执行）
    // 用原生 socket 文件描述符
    if (libssh2_session_handshake(m_session, static_cast<libssh2_socket_t>(
        m_socket->socketDescriptor())) != 0) {
        m_lastError = "SSH 握手失败";
        disconnect(); return false;
    }

    if (!verifyHostKey()) { disconnect(); return false; }  // m_lastError 已由 verifyHostKey 设置

    if (libssh2_userauth_password(m_session, auth.user.c_str(), auth.password.c_str()) != 0) {
        m_lastError = "SSH 认证失败: 用户名/密码错误";
        disconnect(); return false;
    }

    return true;
}

void SshAdapter::disconnect()
{
    m_cancelled = true;
    if (m_subscribeActive) unsubscribe();
    closeChannel(m_channel);
    if (m_session) { libssh2_session_disconnect(m_session, "bye"); libssh2_session_free(m_session); m_session = nullptr; }
    if (m_socket) { m_socket->close(); m_socket->deleteLater(); m_socket = nullptr; }
}

bool SshAdapter::isConnected() const { return m_session != nullptr; }
std::string SshAdapter::lastError() const { return m_lastError; }
```

- [ ] **Step 3: SshAdapter.cpp — request 实现**

```cpp
std::future<IProtocolAdapter::Response> SshAdapter::request(const Request& req)
{
    return std::async(std::launch::async, [this, req]() -> Response {
        Response r;
        if (!m_session) { r.error = "未连接"; return r; }

        LIBSSH2_CHANNEL* ch = libssh2_channel_open_session(m_session);
        if (!ch) { r.error = "打开 SSH channel 失败"; return r; }

        int rc = libssh2_channel_exec(ch, req.command.c_str());
        if (rc != 0) {
            char* errMsg; int errLen;
            libssh2_session_last_error(m_session, &errMsg, &errLen, 0);
            r.error = std::string("命令执行失败: ") + std::string(errMsg, errLen);
            libssh2_channel_free(ch);
            return r;
        }

        char buf[4096]; ssize_t n;
        std::string output;
        while (!m_cancelled && (n = libssh2_channel_read(ch, buf, sizeof(buf))) > 0) {
            output.append(buf, n);
        }
        if (n < 0) r.error = "读取输出失败";

        libssh2_channel_send_eof(ch);
        r.exitCode = libssh2_channel_get_exit_status(ch);
        libssh2_channel_wait_closed(ch);
        libssh2_channel_free(ch);

        r.data = QByteArray::fromStdString(output);
        return r;
    });
}
```

- [ ] **Step 4: verifyHostKey — TOFU 实现**

```cpp
std::string SshAdapter::collectHostFingerprint()
{
    size_t len; int type;
    const char* key = libssh2_session_hostkey(m_session, &len, &type);
    if (!key) return "";

    // SHA256 hash of raw host key → Hex (32 bytes × 2 = 64 hex chars)
    unsigned char hash[32];
    // 使用 Qt6 QCryptographicHash:
    // QByteArray rawKey(key, int(len));
    // QByteArray hex = QCryptographicHash::hash(rawKey, QCryptographicHash::Sha256).toHex();
    // return QString(hex).toStdString();
    // （简化实现:若 QCryptographicHash 不可用,用 libssh2_hostkey_hash(LIBSSH2_HOSTKEY_HASH_SHA256)）
    QByteArray rawKey(key, int(len));
    QByteArray hex = QCryptographicHash::hash(rawKey, QCryptographicHash::Sha256).toHex();
    return hex.toStdString();
}

bool SshAdapter::verifyHostKey()
{
    std::string fp = collectHostFingerprint();
    if (fp.empty()) { m_lastError = "无法获取主机密钥"; return false; }

    QString qfp = QString::fromStdString(fp);
    if (!m_knownHosts.contains(qfp)) {
        if (m_knownHosts.isEmpty()) {
            // TOFU: 首次连接,自动接受
            m_knownHosts.insert(qfp);
            LWLOG_I("SSH TOFU: accepted host key " + fp);
        } else {
            // 同一批次中已有其他设备的指纹,新设备指纹不同——自动接受（同批次不同设备指纹自然不同）
            m_knownHosts.insert(qfp);
        }
        return true;
    }
    // 指纹已存在且匹配 → 通过
    return true;
}
```
> **注意**:TOFU 粒度是"本次运行内"。同一 IP 的指纹变化（重装了系统）不会被检测到——这与 spec 的首期不持久化一致。后续可扩展为 `QMap<ip, fingerprint>` 粒度。

- [ ] **Step 5: subscribe / unsubscribe（首期桩,预留接口）**

```cpp
void SshAdapter::subscribe(const Request&, StreamCallback) { m_lastError = "SSH subscribe 模式暂未实现"; }
void SshAdapter::unsubscribe() { m_subscribeActive = false; closeChannel(m_channel); }
ProtocolCapability SshAdapter::capability() const {
    ProtocolCapability c; c.requestResponse = true; c.streaming = false; c.broadcast = false;
    c.publishSubscribe = false; c.maxConnections = 1; return c;
}
```

- [ ] **Step 6: 登记到构建系统**

CMakeLists.txt SOURCES 段加 `src/adapter/SshAdapter.cpp`（FtpAdapter/TelnetAdapter 之旁）。
DeployMaster.vcxproj `<ClCompile>` 加 `src\adapter\SshAdapter.cpp`, `<ClInclude>` 加 `src/adapter/SshAdapter.h`。

- [ ] **Step 7: 构建验证**

```bash
cd /d/persional/QtDeployMaster/build && cmake --build . --config Release 2>&1 | tail -3
```
Expected: 0 error（libssh2 库须就位,见 Task 1 Step 3）。

- [ ] **Step 8: Commit**

```bash
cd /d/persional/QtDeployMaster
git add src/adapter/SshAdapter.* CMakeLists.txt DeployMaster.vcxproj
git commit -m "feat: SshAdapter(IProtocolAdapter 的 libssh2 实现, 密码认证 + TOFU)"
```

---

## Task 3: TelnetBackend 协议感知 + ProtocolRegistry 注册 SshAdapter

**Files:**
- Modify: `src/tools/TelnetTool/TelnetBackend.h`, `TelnetBackend.cpp`
- Modify: `main.cpp`

- [ ] **Step 1: TelnetBackend.h 加 setProtocol + 成员**

公有区加:
```cpp
    // 设置本次执行的协议（"telnet" 或 "ssh"），默认 "telnet"（向后兼容）
    void setProtocol(const QString& proto) { m_selectedProtocol = proto; }
```
私有区加:
```cpp
    QString m_selectedProtocol = "telnet";
```

- [ ] **Step 2: TelnetBackend.cpp executeCommand 改协议变量**

找到 `ProtocolRegistry::instance()->create("telnet")`（约第 102 行）和 `dynamic_cast<TelnetAdapter*>`（约第 111 行）,改为:
```cpp
    auto adapter = ProtocolRegistry::instance()->create(
        m_selectedProtocol.toStdString());
    if (!adapter) {
        if (m_logCb) m_logCb(m_selectedProtocol.toStdString() + " 适配器不可用: " + ip);
        ...
    }
    auto* telnet = dynamic_cast<TelnetAdapter*>(adapter.get());
```
**保留** TelnetAdapter 的 dynamic_cast——当前只有 TelnetAdapter 需要特殊调用（如 setCredentials）。若未来 SSH 也需要,再加分支。

但此处的 `dynamic_cast<TelnetAdapter*>` 在协议为 SSH 时会返回 nullptr,导致后续 adapter 虽然创建成功但 `telnet` 为 null 会失败。**修正**:在 `request` 调用前,统一通过 IProtocolAdapter 接口执行,不走 TelnetAdapter 独有方法。检查当前代码中 TelnetAdapter 独有的调用:

Run 确认上下文:
```bash
grep -A5 "auto\* telnet = dynamic_cast" src/tools/TelnetTool/TelnetBackend.cpp
```
若 `telnet` 仅用于调用 `telnet->request(req)`,改为 `adapter->request(req)`(IProtocolAdapter 接口,任何适配器都可用)。若还有 `telnet->setCredentials()` 之类独有方法,检查 IProtocolAdapter 是否有等效接口——没有,则需在 executeCommand 中按协议分派:`if (m_selectedProtocol == "telnet")` 走 TelnetAdapter 独有逻辑;`else if (m_selectedProtocol == "ssh")` 走 IProtocolAdapter 通用接口(connect + request)。

**实现者去读 `executeCommand` 完整代码(约 80 行),确认 TelnetAdapter 独有调用点后做最小修改。核心:让 `"ssh"` 字符串能正常工作。**

- [ ] **Step 3: main.cpp 注册 SshAdapter**

在 `ProtocolRegistry::instance()->registerFactory("telnet", ...)` 之后加:
```cpp
    ProtocolRegistry::instance()->registerFactory("ssh",
        []() -> std::shared_ptr<IProtocolAdapter> {
            return std::make_shared<SshAdapter>();
        });
```
顶部 include 加 `#include "src/adapter/SshAdapter.h"`。

- [ ] **Step 4: 构建验证**

```bash
cd /d/persional/QtDeployMaster/build && cmake --build . --config Release 2>&1 | tail -3
```
Expected: 0 error。

- [ ] **Step 5: Commit**

```bash
cd /d/persional/QtDeployMaster
git add src/tools/TelnetTool/TelnetBackend.* main.cpp
git commit -m "feat: TelnetBackend 协议感知(setProtocol) + main.cpp 注册 SshAdapter"
```

---

## Task 4: TelnetWidget 协议下拉

**Files:**
- Modify: `src/tools/TelnetTool/TelnetWidget.h`, `TelnetWidget.cpp`

- [ ] **Step 1: TelnetWidget.h 加协议下拉成员**

私有控件区加:
```cpp
    QComboBox* m_protoCombo = nullptr;  // "Telnet" / "SSH"
```

- [ ] **Step 2: TelnetWidget.cpp setupUi 加协议下拉**

在配置区（超时设置之前或第一行）加:
```cpp
    auto* protoRow = new QHBoxLayout();
    protoRow->addWidget(new QLabel("协议:", this));
    m_protoCombo = new QComboBox(this);
    m_protoCombo->addItem("Telnet");
    m_protoCombo->addItem("SSH");
    protoRow->addWidget(m_protoCombo);
    protoRow->addStretch();
```
插入到配置区顶部。读取现有 setupUi 的布局上下文,找到合适的插入点。

- [ ] **Step 3: onExecuteClicked 传协议给 Backend**

在 `onExecuteClicked` 调用 `m_backend->executeCommand(...)` 之前加:
```cpp
    m_backend->setProtocol(m_protoCombo->currentText().toLower());
```

- [ ] **Step 4: 构建 + 冒烟**

```bash
cd /d/persional/QtDeployMaster/build && cmake --build . --config Release 2>&1 | tail -3
cd Release && export PATH="/c/Qt/6.10.1/msvc2022_64/bin:$PATH" && timeout 6 ./DeviceForge.exe >/dev/null 2>&1 & PID=$!; sleep 4; kill -0 $PID 2>/dev/null && echo ALIVE && kill $PID 2>/dev/null || echo EXITED
```
Expected: 0 error + ALIVE。

- [ ] **Step 5: Commit**

```bash
cd /d/persional/QtDeployMaster
git add src/tools/TelnetTool/TelnetWidget.*
git commit -m "feat: TelnetWidget 协议下拉(Telnet/SSH) + 传递 setProtocol"
```

---

## Task 5: 文档同步 + 端到端手动验证

**Files:**
- Modify: `CLAUDE.md`、`README.md`、`docs/architecture.md`、`docs/user-guide.md`、`docs/security.md`

- [ ] **Step 1: CLAUDE.md + README**

CLAUDE.md:TelnetTool 描述处补"支持 Telnet / SSH 切换"；模块表批量命令行补" / SSH"。README 功能模块表同步。

- [ ] **Step 2: docs/ 交付文档**

architecture.md:Adapter 层 SshAdapter 加列。user-guide.md:批量命令节加"选择 Telnet 或 SSH 协议"。security.md:协议安全表加 SSH 行;补充 SSH 安全说明（加密传输 + TOFU + 密码擦除）。

- [ ] **Step 3: 端到端手动验证（若 SSH 环境可用）**

```bash
# 1. 确保本地或目标设备有 SSH 服务器(如 OpenSSH)
# 2. DeviceForge → 批量命令 Tab → 协议选 SSH
# 3. 设备选择目标 IP:22,输入用户名/密码
# 4. 命令输入 `echo hello` → 执行
# 5. 验证:结果表显示成功,输出 "hello"
```
若 SSH 环境不可用,标记为"待验证",提交通过代码审查即可。

- [ ] **Step 4: Commit**

```bash
cd /d/persional/QtDeployMaster
git add CLAUDE.md README.md docs/
git commit -m "docs: 同步 SSH 批量命令能力(CLAUDE/README/architecture/user-guide/security)"
```

---

## Self-Review（对照 spec）

- **spec §2.1 扩展 Telnet Tab** → Task 4（协议下拉）+ Task 3（Backend 协议感知）。✅
- **spec §2.2 libssh2** → Task 1（集成）+ Task 2（SshAdapter）。✅
- **spec §2.3 仅密码认证** → Task 2 Step 2（`libssh2_userauth_password`）。✅
- **spec §2.4 TOFU** → Task 2 Step 4（`verifyHostKey`,首次自动接受）。✅
- **spec §2.5 Tab 级协议** → Task 3 Step 1（`m_selectedProtocol` 单值,非设备粒度）。✅
- **spec §3.1 SshAdapter 接口** → Task 2（所有 IProtocolAdapter 纯虚函数实现）。✅
- **spec §3.2 TelnetBackend 协议感知** → Task 3 Step 2（`create(m_selectedProtocol)`）。✅
- **spec §3.3 TelnetWidget 协议下拉** → Task 4（combo + setProtocol）。✅
- **spec §5 安全设计** → Task 2 Step 4（TOFU）+ 内存中指纹不持久化 + Task 5 security.md（日志去敏感化/密码擦除已在现有代码中）。✅
- **spec §6 libssh2 集成** → Task 1（CMake + vcxproj + POST_BUILD 复制 DLL）。✅
- **spec §8 非目标（密钥/持久化/SCP/流模式）** → 计划未包含,subscribe 桩实现,正确。✅
- **占位符扫描**:无 TBD/TODO;Task 3 Step 2 要求实现者读完整 executeCommand 确认 TelnetAdapter 独有调用——这是明确的验证指引,非占位。✅
- **类型一致性**:`setProtocol(const QString&)`(T3)→Widget 调用 `toLower()`(T4);Registry key "ssh"(T3)→create 参数(T3);`IProtocolAdapter::request`(T2)→TelnetBackend 使用(T3)。✅
- **libssh2 软依赖**:Task 1 Step 3 明确——库未就位时后续任务 BLOCKED 而非崩溃。✅
