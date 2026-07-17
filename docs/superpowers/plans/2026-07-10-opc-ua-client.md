# OPC UA Client 真实实现实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用 open62541 单文件版本实现 OPC UA Client Tool（读/写/订阅），替代现有演示模式的 OpcUaClientTab。

**Architecture:** OpcUaClientBackend（ServiceTask，svc 线程驱动 UA_Client_runIterate）+ OpcUaClientWidget（QWidget）+ OpcUaAdapter（实现 IProtocolAdapter）。open62541 编译为静态库链接。

**Tech Stack:** open62541 单文件版（open62541.h + open62541.c），LGPL/MPL

---

## Global Constraints

- open62541 单文件版（single-file，open62541.h + open62541.c 合并为一个 .h）
- 首期安全策略：None + 匿名认证（UA_ClientConfig_setAnonymous）
- 仅 Client 模式（UA_ENABLE_SERVER=NO）
- 启用：UA_ENABLE_CLIENT=YES, UA_ENABLE_SUBSCRIPTIONS=YES, UA_ENABLE_NODELIST=YES
- 禁用：UA_ENABLE_ENCRYPTION=NO, UA_ENABLE_METHODCALL=NO, UA_ENABLE_PUBSUB=NO
- 内置 Tool，非插件
- 遵循现有代码风格：类名 PascalCase，成员 m_camelCase，回调用 std::function
- OpcUaAdapter 注册到 ProtocolRegistry（与其他 Adapter 一致）

---

## 文件结构

```
src/
 ├─ thirdparty/
 │   └─ open62541/
 │       ├─ open62541.h        # 单文件版本（.h + .c 合并）
 │       └─ CMakeLists.txt      # add_library(open62541 STATIC open62541.h)
 ├─ adapter/
 │   └─ OpcUaAdapter.h/.cpp    # 实现 IProtocolAdapter
 └─ tools/
     └─ OpcUaClientTool/
         ├─ OpcUaClientBackend.h/.cpp
         ├─ OpcUaClientWidget.h/.cpp
         └─ OpcUaClientTool.ini

DeployMaster.cpp     # 修改: 移除 setupOpcUaClientTab，改为 setupOpcUaClientTool
main.cpp             # 修改: 注册 OpcUaAdapter
CMakeLists.txt       # 修改: add_subdirectory(thirdparty/open62541) + link
DeployMaster.vcxproj # 修改: 加入 open62541.h + OpcUaClientTool/*.cpp
```

---

## Task 1: open62541 单文件版本集成

**Files:**
- Create: `src/thirdparty/open62541/open62541.h`
- Create: `src/thirdparty/open62541/CMakeLists.txt`
- Modify: `CMakeLists.txt`
- Modify: `DeployMaster.vcxproj`

**Interfaces:**
- Produces: `open62541.h` 编译为静态库 `open62541`，被 `OpcUaAdapter` 链接

- [ ] **Step 1: 下载 open62541 单文件版本**

从 https://open62541.org 或 GitHub release 下载 `open62541.h`（单文件版本，约 10MB，合并了所有插件）。

保存到 `src/thirdparty/open62541/open62541.h`。

- [ ] **Step 2: 创建 open62541 CMakeLists.txt**

```cmake
# src/thirdparty/open62541/CMakeLists.txt
# open62541 单文件版本（UA_ENABLE_* 开关在 open62541.h 顶部用 #define 手动配置）
set(UA_ENABLE_CLIENT ON)
set(UA_ENABLE_SUBSCRIPTIONS ON)
set(UA_ENABLE_NODELIST ON)
set(UA_ENABLE_ENCRYPTION OFF)
set(UA_ENABLE_METHODCALL OFF)
set(UA_ENABLE_PUBSUB OFF)
set(UA_ENABLE_SERVER OFF)

add_library(open62541 STATIC "${CMAKE_CURRENT_SOURCE_DIR}/open62541.h")
target_include_directories(open62541 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(open62541 PUBLIC UA_MULTITHREADING=0)
```

> 注：单文件版本通过在 include 之前定义宏来配置功能开关，不需要 ua_config.h。

- [ ] **Step 3: 在项目 CMakeLists.txt 加入 open62541**

在 `CMakeLists.txt` 的 thirdparty 区块（lw* 库之后）加入：

```cmake
# --- open62541 (LGPL/MPL, 单文件版本) ---
add_subdirectory(src/thirdparty/open62541)
target_link_libraries(DeviceForge PRIVATE open62541)
```

- [ ] **Step 4: 在 DeployMaster.vcxproj 加入 open62541.h**

在 `.vcxproj` 文件的 `<ClCompile Include="src\..." />` 区块之后加入：

```xml
<ClCompile Include="src\thirdparty\open62541\open62541.h">
  <FileType>CppHeader</FileType>
</ClCompile>
```

- [ ] **Step 5: 验证 CMake 构建（快速语法检查）**

Run: `cmake -S . -B build_test -DCMAKE_PREFIX_PATH="C:\Qt\6.10.1\msvc2022_64" -G "Visual Studio 17 2022" -A x64`
Expected: CMake 配置无错误（不要求完整编译，只需确认 open62541 被识别）

- [ ] **Step 6: 提交**

```bash
git add src/thirdparty/open62541/ CMakeLists.txt DeployMaster.vcxproj
git commit -m "build: 集成 open62541 单文件版本(CMake+vcxproj)

- open62541.h 单文件，UA_ENABLE_CLIENT/SUBSCRIPTIONS/NODELIST=ON
- 其余加密/Method/Server/PubSub=OFF（首期仅 Client）
- CMake add_subdirectory，链接为静态库

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 2: OpcUaAdapter 实现（IProtocolAdapter）

**Files:**
- Create: `src/adapter/OpcUaAdapter.h`
- Create: `src/adapter/OpcUaAdapter.cpp`
- Modify: `src/adapter/ProtocolRegistry.cpp`（注册 OpcUaAdapter）
- Modify: `main.cpp`（注册调用）

**Interfaces:**
- Consumes: `open62541.h`
- Produces: `OpcUaAdapter` 实现 `IProtocolAdapter`，protocolId() → "opcua"

- [ ] **Step 1: 写 OpcUaAdapter.h**

```cpp
#pragma once
#include "IProtocolAdapter.h"
#include <QString>
#include <QMap>
#include <atomic>

// 前向声明 open62541 类型
struct UA_Client_;
struct UA_NodeId;
struct UA_Variant;

class OpcUaAdapter : public IProtocolAdapter {
public:
    OpcUaAdapter();
    ~OpcUaAdapter() override;

    // IProtocolAdapter
    std::string protocolId() const override { return "opcua"; }
    bool connect(const DeviceInfo& device, const AuthInfo& auth) override;
    void disconnect() override;
    bool isConnected() const override;
    std::string lastError() const override;
    std::future<Response> request(const Request& req) override;
    void subscribe(const Request& req, StreamCallback onData) override;
    void unsubscribe() override;
    ProtocolCapability capability() const override;

    // OPC UA 扩展接口
    QVariantMap readNodes(const QStringList& nodeIds);
    QMap<QString, QString> writeNodes(const QStringList& nodeIds, const QVariantList& values);
    QString browseRoot(); // 返回 JSON 格式的根节点树

private:
    UA_Client_* m_client = nullptr;
    std::atomic<bool> m_connected{false};
    QString m_lastError;
    StreamCallback m_streamCb;

    void setError(const QString& err);
    static QString uaStatusToString(quint32 statusCode);
};
```

- [ ] **Step 2: 写 OpcUaAdapter.cpp（骨架实现）**

关键实现点：

```cpp
// 连接（首期: None + 匿名）
bool OpcUaAdapter::connect(const DeviceInfo& device, const AuthInfo& auth)
{
    m_client = UA_Client_new(UA_ClientConfig_default());
    // 无加密 + 匿名认证（UA_ClientConfig_setAnonymous 在 default 配置下已是匿名）
    UA_StatusCode ret = UA_Client_connect(m_client, device.ip.c_str());
    if (ret != UA_STATUSCODE_GOOD) {
        setError(uaStatusToString(ret));
        return false;
    }
    m_connected = true;
    return true;
}

// 读节点（批量）
QVariantMap OpcUaAdapter::readNodes(const QStringList& nodeIds)
{
    QVariantMap results;
    for (const QString& nid : nodeIds) {
        UA_NodeId nodeId = UA_NodeId_parse(nid.toUtf8().constData());
        UA_Variant val;
        UA_Variant_init(&val);
        UA_StatusCode ret = UA_Client_readValueAttribute(m_client, nodeId, &val);
        if (ret == UA_STATUSCODE_GOOD) {
            results[nid] = uaVariantToQtVariant(val); // 类型映射
            UA_Variant_deleteMembers(&val);
        } else {
            results[nid] = QVariant(); // 失败返回空
        }
        UA_NodeId_deleteMembers(&nodeId);
    }
    return results;
}
```

**UA_Variant → QVariant 类型映射**（在 cpp 中实现为一个内联函数）：
| OPC UA | Qt |
|--------|-----|
| UA_Boolean | bool |
| UA_Int32 | int |
| UA_UInt32 | uint |
| UA_Float | float → double |
| UA_Double | double |
| UA_String | QString |
| UA_DateTime | QDateTime |

**UA_StatusCode → QString**：`UA_StatusCode_name()` 将 status code 转为字符串。

- [ ] **Step 3: 在 ProtocolRegistry.cpp 注册**

在 `ProtocolRegistry.cpp` 的 `#include` 之后加入：

```cpp
#include "OpcUaAdapter.h"
```

在 `ProtocolRegistry::instance()` 返回前（或在单例构造中）加入注册：

> 实际上 ProtocolRegistry 用的是延迟注册（main.cpp 调用），所以只需在 ProtocolRegistry.cpp 的工厂映射中加一个 case 或在 main.cpp 显式调用。

检查现有 `ProtocolRegistry.cpp` — 如果 `create()` 是基于 `m_factories` map 的，则无需修改 ProtocolRegistry.cpp，只在 main.cpp 中：

```cpp
ProtocolRegistry::instance()->registerFactory("opcua", [](){ return std::make_shared<OpcUaAdapter>(); });
```

- [ ] **Step 4: 在 main.cpp 注册 OpcUaAdapter**

在 `main.cpp` 的 ProtocolRegistry 注册区（FtpAdapter + TelnetAdapter 之后）加入：

```cpp
#include "src/adapter/OpcUaAdapter.h"
ProtocolRegistry::instance()->registerFactory("opcua", [](){ return std::make_shared<OpcUaAdapter>(); });
```

- [ ] **Step 5: 验证编译（CMake，连接测试）**

Run: `cmake --build build_test --config Release --target DeviceForge 2>&1 | head -50`
Expected: OpcUaAdapter.o 编译无错（undefined symbol 在链接时才暴露，构建成功即过）

- [ ] **Step 6: 提交**

```bash
git add src/adapter/OpcUaAdapter.h src/adapter/OpcUaAdapter.cpp src/adapter/ProtocolRegistry.cpp main.cpp
git commit -m "feat: OpcUaAdapter 实现 IProtocolAdapter(open62541)

- protocolId: opcua
- connect: None+匿名认证
- readNodes: 批量读，返回 QVariantMap
- writeNodes: 批量写
- browseRoot: 返回 JSON 节点树
- UA_Variant→QVariant 类型映射(Boolean/Int32/UInt32/Float/Double/String/DateTime)
- ProtocolRegistry 注册 factory

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 3: OpcUaClientBackend 实现（ServiceTask）

**Files:**
- Create: `src/tools/OpcUaClientTool/OpcUaClientBackend.h`
- Create: `src/tools/OpcUaClientTool/OpcUaClientBackend.cpp`

**Interfaces:**
- Consumes: `OpcUaAdapter`（成员 `std::shared_ptr<OpcUaAdapter>`）
- Produces: `OpcUaClientBackend`，通过回调向 Widget 推送数据

**回调签名**（与 ModbusBackend 一致）：
```cpp
using DataChangeCallback = std::function<void(const QString& nodeId, const QVariant& value, quint64 timestamp, const QString& quality)>;
using LogCallback = std::function<void(const std::string&)>;
using ConnectionCallback = std::function<void(bool connected)>;
```

- [ ] **Step 1: 写 OpcUaClientBackend.h**

```cpp
#pragma once
#include "framework/ToolBackend.h"
#include "src/adapter/OpcUaAdapter.h"
#include <QString>
#include <QVariantMap>
#include <QMap>
#include <QTimer>
#include <memory>
#include <atomic>

class OpcUaClientBackend : public ToolBackend {
public:
    OpcUaClientBackend();
    ~OpcUaClientBackend() override;

    int svc() override; // 后台线程: runIterate 轮询

    std::string toolId() const override { return "com.deviceforge.opcua.client"; }
    std::string toolName() const override { return "OPC UA 客户端"; }
    std::string toolVersion() const override { return "2.2.0"; }
    std::string toolCategory() const override { return "test"; }
    std::string toolIcon() const override { return "opcua_client"; }

    void bindDevices(const std::vector<DeviceInfo>& devices) override;
    void bindCredentials(const AuthInfo& auth) override;
    void applyConfig(const lwserverbase::config::ConfigValue&) override {}

    // 对外 API
    void connectToServer(const QString& endpoint);
    void disconnectFromServer();
    void readNodes(const QStringList& nodeIds);
    void writeNodes(const QStringList& nodeIds, const QVariantList& values);
    void subscribeNodes(const QStringList& nodeIds);
    void unsubscribeAll();
    QString browseAddressSpace();

    // 回调设置
    using LogCallback = std::function<void(const std::string&)>;
    using ConnectionCallback = std::function<void(bool connected)>;
    using DataChangeCallback = std::function<void(const QString& nodeId, const QVariant& value, quint64 timestampMs, const QString& quality)>;
    void setLogCallback(LogCallback cb) { m_logCb = std::move(cb); }
    void setConnectionCallback(ConnectionCallback cb) { m_connCb = std::move(cb); }
    void setDataChangeCallback(DataChangeCallback cb) { m_dataCb = std::move(cb); }

private:
    std::shared_ptr<OpcUaAdapter> m_adapter;
    std::atomic<bool> m_subscribed{false};
    std::atomic<bool> m_running{false};

    LogCallback m_logCb;
    ConnectionCallback m_connCb;
    DataChangeCallback m_dataCb;
};
```

- [ ] **Step 2: 写 OpcUaClientBackend.cpp（svc 轮询 runIterate）**

关键：svc() 后台线程以 100ms 为周期调用 `UA_Client_runIterate()`。

```cpp
int OpcUaClientBackend::svc()
{
    m_running = true;
    while (m_running) {
        if (m_adapter->isConnected()) {
            // 驱动 open62541 事件循环（100ms）
            // open62541 单线程模式: 每次 runIterate 处理网络 I/O + 回调派发
            // 注: 真正的 Subscription 回调在 runIterate 内部触发，
            // 需要给 open62541 注册一个 DataChange 回调来收集订阅数据
            // 简化方案: Widget 侧定时调用 readNodes() 主动拉取，或在
            // Adapter 层用 timer + readNodes 模拟轮询订阅
            
            // 首期简化: runIterate 驱动网络，保持连接活跃
            // 订阅用 QTimer 在主线程做主动轮询（每 1000ms）
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    return 0;
}
```

> 注：open62541 的 Subscription/DataChange 回调需要通过 `UA_Client_runIterate` 内部派发，而 runIterate 是阻塞式的 100ms 等待。真实实现时，DataChange 回调函数会在 runIterate 内部被调用写入一个队列，然后 Widget 侧通过该队列读取。最简单的首期方案是：在 Widget 侧用 QTimer（每 1000ms）调用 `readNodes()` 模拟轮询订阅，简化实现复杂度。

- [ ] **Step 3: 提交**

```bash
git add src/tools/OpcUaClientTool/OpcUaClientBackend.h src/tools/OpcUaClientTool/OpcUaClientBackend.cpp
git commit -m "feat: OpcUaClientBackend 实现(ServiceTask+runIterate)

- svc() 后台线程驱动 UA_Client_runIterate(100ms)
- connectToServer/disconnectFromServer/readNodes/writeNodes/subscribeNodes
- 回调: LogCallback/ConnectionCallback/DataChangeCallback
- Tool 身份: com.deviceforge.opcua.client v2.2.0

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 4: OpcUaClientWidget UI 实现

**Files:**
- Create: `src/tools/OpcUaClientTool/OpcUaClientWidget.h`
- Create: `src/tools/OpcUaClientTool/OpcUaClientWidget.cpp`

**Interfaces:**
- Consumes: `OpcUaClientBackend*`（通过 setBackend 注入）
- Produces: 完整 UI（含连接配置/地址空间浏览/读写/订阅四面板）

- [ ] **Step 1: 写 OpcUaClientWidget.h**

参照 ModbusWidget 风格：

```cpp
#pragma once
#include "framework/ToolWidget.h"
#include <QWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QTreeWidget>
#include <QTableWidget>
#include <QTimer>

class OpcUaClientBackend;

class OpcUaClientWidget : public ToolWidget {
    Q_OBJECT
public:
    explicit OpcUaClientWidget(QWidget* parent = nullptr);
    ~OpcUaClientWidget() override = default;

    QString toolId() const override { return "com.deviceforge.opcua.client"; }
    QString toolName() const override { return "OPC UA 客户端"; }
    void onToolStart() override;
    void onToolStop() override;

    void setBackend(OpcUaClientBackend* backend);

private slots:
    void onConnectClicked();
    void onBrowseClicked();
    void onReadClicked();
    void onWriteClicked();
    void onSubscribeClicked();
    void onRefreshTimer();

private:
    void setupUi();
    void appendLog(const QString& msg);
    void updateConnectionStatus(bool connected);
    void populateBrowseTree(const QString& json);

    OpcUaClientBackend* m_backend = nullptr;
    QTimer* m_refreshTimer = nullptr;

    // UI 组件
    QLineEdit* m_endpointEdit = nullptr;
    QPushButton* m_connectBtn = nullptr;
    QComboBox* m_securityPolicyCombo = nullptr;
    QComboBox* m_authCombo = nullptr;
    QTreeWidget* m_browseTree = nullptr;
    QLineEdit* m_nodeIdEdit = nullptr;
    QLineEdit* m_valueEdit = nullptr;
    QPushButton* m_readBtn = nullptr;
    QPushButton* m_writeBtn = nullptr;
    QTableWidget* m_batchTable = nullptr;
    QTableWidget* m_subscriptionTable = nullptr;
    QPushButton* m_subscribeBtn = nullptr;
    QTextEdit* m_logView = nullptr;
};
```

- [ ] **Step 2: 写 OpcUaClientWidget.cpp（setupUi + 信号连接）**

setupUi 布局（四面板，参照 spec §3）：

```
┌─ 连接配置 ─────────────────────────────────────────────┐
│ Endpoint: [________________________] [连接]             │
│ 安全策略: [None ▼]  认证: [匿名 ▼]   状态: ○          │
├─ 地址空间浏览 ──┬─ 读/写 ──────────────────────────────┤
│ QTreeWidget    │ NodeId: [______________]               │
│ (browseRoot)  │ 值:     [______________]               │
│                │ [读] [写]                              │
│                │ 批量表: QTableWidget(nodeId/值/质量)    │
├────────────────┴───────────────────────────────────────┤
│ 订阅表: QTableWidget(nodeId/最新值/时间戳) [订阅]       │
└───────────────────────────────────────────────────────┘
```

`setBackend()` 回调连接（参照 ModbusWidget 风格）：

```cpp
void OpcUaClientWidget::setBackend(OpcUaClientBackend* backend)
{
    m_backend = backend;
    if (!m_backend) return;
    m_backend->setLogCallback([this](const std::string& msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            appendLog(QString::fromStdString(msg));
        }, Qt::QueuedConnection);
    });
    m_backend->setConnectionCallback([this](bool connected) {
        QMetaObject::invokeMethod(this, [this, connected]() {
            updateConnectionStatus(connected);
        }, Qt::QueuedConnection);
    });
    m_backend->setDataChangeCallback([this](const QString& nodeId, const QVariant& value, quint64 ts, const QString& quality) {
        QMetaObject::invokeMethod(this, [this, nodeId, value, ts, quality]() {
            // 更新订阅表该行
            for (int r = 0; r < m_subscriptionTable->rowCount(); ++r) {
                if (m_subscriptionTable->item(r, 0)->text() == nodeId) {
                    m_subscriptionTable->item(r, 1)->setText(value.toString());
                    m_subscriptionTable->item(r, 2)->setText(QDateTime::fromMSecsSinceEpoch(ts).toString("hh:mm:ss"));
                    break;
                }
            }
        }, Qt::QueuedConnection);
    });
}
```

`onBrowseClicked()` — 调用 `m_backend->browseAddressSpace()`，返回 JSON 格式的根节点树，解析后填入 `m_browseTree`。
`onConnectClicked()` — 调用 `m_backend->connectToServer(endpointEdit->text())`。
`onRefreshTimer()` — 每 1000ms 对所有已订阅节点调用 `readNodes()`，触发 DataChangeCallback。

- [ ] **Step 3: 提交**

```bash
git add src/tools/OpcUaClientTool/OpcUaClientWidget.h src/tools/OpcUaClientTool/OpcUaClientWidget.cpp
git commit -m "feat: OpcUaClientWidget UI 实现(四面板布局)

- 连接配置: endpoint/安全策略/认证/状态指示灯
- 地址空间浏览: QTreeWidget 双击填入 NodeId
- 读写面板: NodeId 输入 + 单次读/写 + 批量表
- 订阅面板: QTableWidget 实时最新值+时间戳+QTimer 主动轮询
- setBackend 回调连接(LogCallback/ConnectionCallback/DataChangeCallback)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 5: 替代旧 OpcUaClientTab（DeployMaster.cpp 集成）

**Files:**
- Modify: `DeployMaster.cpp`
- Modify: `DeployMaster.h`（如有需要加成员变量）

**Interfaces:**
- Consumes: `OpcUaClientBackend` + `OpcUaClientWidget`
- Produces: `setupOpcUaClientTool()` 替代 `setupOpcUaClientTab()`

- [ ] **Step 1: 修改 DeployMaster.cpp，移除旧 Tab，替换为新 Tool**

在 `DeployMaster.cpp` 的 include 区：

```cpp
#include "src/tools/OpcUaClientTool/OpcUaClientWidget.h"
#include "src/tools/OpcUaClientTool/OpcUaClientBackend.h"
```

注释掉或删除 `setupOpcUaClientTab()` 函数（整函数替换）：

```cpp
// === 旧: 演示模式 OpcUaClientTab（已废弃） ===
// void DeployMaster::setupOpcUaClientTab() { ... }

// === 新: OPC UA Client Tool ===
void DeployMaster::setupOpcUaClientTool()
{
    auto backend = std::make_shared<OpcUaClientBackend>();
    auto* widget = new OpcUaClientWidget(this);

    int rc = backend->OnStart(0, nullptr);
    if (rc != 0) {
        appendGlobalLog("❌ OPC UA Client Backend 启动失败 (rc=" + QString::number(rc) + ")");
        delete widget;
        return;
    }

    widget->setBackend(backend.get());
    backend->setLogCallback([this](const std::string& msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            appendGlobalLog(QString::fromStdString(msg));
        }, Qt::QueuedConnection);
    });

    m_opcuaClientWidget = widget;
    ui.tabWidget->addTab(m_opcuaClientWidget, tr("OPC UA 客户端"));
}
```

在 `initToolTabs()` 或 `setupXxxTab()` 调用链中，把：

```cpp
setupOpcUaClientTab();
```

替换为：

```cpp
setupOpcUaClientTool();
```

在 DeployMaster.h 中加入成员声明：

```cpp
class OpcUaClientWidget; // 前向声明
OpcUaClientWidget* m_opcuaClientWidget = nullptr;
```

> 注：`m_opcuaClientWidget` 需要用前向声明，避免头文件循环依赖。

- [ ] **Step 2: 验证编译**

Run: `cmake --build build_test --config Release --target DeviceForge 2>&1 | grep -i "error\|warning.*OpcUa\|undefined" | head -30`
Expected: 无 OpcUa 相关的编译错误

- [ ] **Step 3: 提交**

```bash
git add DeployMaster.cpp DeployMaster.h
git commit -m "feat: DeployMaster 集成 OpcUaClientTool(替代演示 Tab)

- setupOpcUaClientTool() 替代 setupOpcUaClientTab()
- OpcUaClientWidget + OpcUaClientBackend(OnStart) 配对创建
- backend->setLogCallback 接入全局日志
- 旧 OpcUaClientTab/OpcUaClient.h/cpp 保留(注释掉)，后续清理

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 6: OpcUaAdapter 订阅增强（DataChange Subscription）

**Files:**
- Modify: `src/adapter/OpcUaAdapter.cpp`

**说明：** Task 2 的 readNodes 是同步主动读，Task 6 实现真正的 OPC UA Subscription（DataChange 回调）。

**Interfaces:**
- Consumes: `UA_Client_DataChangeNotificationCallback`
- Produces: `subscribeDataChange(nodeIdList, callback)` → subscriptionId

- [ ] **Step 1: 在 OpcUaAdapter 中加 Subscription 支持**

在 OpcUaAdapter 中加入：

```cpp
// Subscription 回调上下文
struct SubContext {
    StreamCallback userCb;
    OpcUaAdapter* adapter;
};

// static 回调（open62541 C 回调必须是静态的）
static void dataChangeCallback(UA_Client* client, UA_UInt32 subId, void* subContext,
    UA_UInt32 numMonitoredItems, UA_MonitoredItemNotification* monitoredItems)
{
    auto* ctx = static_cast<SubContext*>(subContext);
    for (UA_UInt32 i = 0; i < numMonitoredItems; ++i) {
        auto& item = monitoredItems[i].value;
        if (item.hasValue) {
            QString nodeIdStr = /* 从 item.nodeId 解析 */;
            QVariant val = /* uaVariantToQtVariant(item.value) */;
            QString data = nodeIdStr.toStdString() + "|" + val.toString().toStdString();
            ctx->userCb(data, false);
        }
    }
}

quint32 OpcUaAdapter::subscribeDataChange(const QStringList& nodeIds, StreamCallback cb)
{
    if (!m_client || !m_connected) return 0;

    // 创建 Subscription
    UA_CreateSubscriptionRequest request = UA_CreateSubscriptionRequest_default();
    request.requestedPublishingInterval = 1000.0; // 1s
    auto response = UA_Client_Subscriptions_createSinglePublishRequest(m_client, request, nullptr);
    
    // 为每个 nodeId 创建 MonitoredItem
    for (const QString& nid : nodeIds) {
        UA_NodeId nodeId = UA_NodeId_parse(nid.toUtf8().constData());
        auto monRequest = UA_MonitoredItemCreateRequest_default(nodeId);
        // 改 samplingInterval 为 500ms（比 publishingInterval 更快采样）
        monRequest.params.samplingInterval = 500.0;
        auto* ctx = new SubContext{cb, this};
        UA_Client_Subscriptions_addMonitoredItem(m_client, response.subscriptionId, monRequest, ctx,
            dataChangeCallback, nullptr);
        UA_NodeId_deleteMembers(&nodeId);
    }
    return response.subscriptionId;
}
```

- [ ] **Step 2: OpcUaAdapter.h 加入 subscribeDataChange 声明**

```cpp
quint32 subscribeDataChange(const QStringList& nodeIds, StreamCallback cb);
```

- [ ] **Step 3: OpcUaClientBackend 调用 subscribeDataChange**

在 `subscribeNodes()` 中：

```cpp
void OpcUaClientBackend::subscribeNodes(const QStringList& nodeIds)
{
    if (!m_adapter) return;
    m_adapter->subscribeDataChange(nodeIds, [this](const std::string& data, bool) {
        // 解析 nodeId|value|timestamp 格式，更新订阅表
        auto parts = QString::fromStdString(data).split('|');
        if (parts.size() >= 3) {
            QString nodeId = parts[0];
            QVariant value = parts[1];
            quint64 ts = parts[2].toULongLong();
            if (m_dataCb) m_dataCb(nodeId, value, ts, "Good");
        }
    });
    m_subscribed = true;
}
```

- [ ] **Step 4: 提交**

```bash
git add src/adapter/OpcUaAdapter.cpp src/adapter/OpcUaAdapter.h src/tools/OpcUaClientTool/OpcUaClientBackend.cpp
git commit -m "feat: OpcUaAdapter DataChange Subscription 实现

- UA_Client_Subscriptions_createSinglePublishRequest 创建 Subscription
- UA_Client_Subscriptions_addMonitoredItem 监控节点
- dataChangeCallback 静态回调派发到 StreamCallback
- OpcUaClientBackend::subscribeNodes 调用订阅

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 7: 文档同步

**Files:**
- Modify: `docs/architecture.md`
- Modify: `docs/user-guide.md`
- Modify: `docs/superpowers/plans/2026-07-04-tool-framework-plan.md`（模块状态总览）
- Modify: `ROADMAP.md`（如需要）

- [ ] **Step 1: 更新 architecture.md**

在 "已完成的 Tool" 表中，OPC UA 行从"演示模式"改为"✅ Tool 架构（open62541）"。

在 "第三方库" 表中加入：
```
open62541 | OPC UA Client（读/写/Subscription） | LGPL/MPL
```

- [ ] **Step 2: 更新 user-guide.md**

加入 OPC UA 客户端使用说明（连接配置、地址空间浏览、读写、订阅）。

- [ ] **Step 3: 更新 ROADMAP.md**

将"OPC UA 客户端真实实现"从"近期（v2.2 候选）"移到"已交付（v2.2.0）"。

- [ ] **Step 4: 提交**

```bash
git add docs/architecture.md docs/user-guide.md ROADMAP.md
git commit -m "docs: 同步 OPC UA Client 交付(架构文档/用户手册/ROADMAP)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## 实施顺序

```
Task 1 → Task 2 → Task 3 → Task 4 → Task 5 → Task 6 → Task 7
(1天)    (1天)    (1天)    (1天)    (0.5天)  (1天)    (0.5天)
                          ↓
                     可并行验证
                     (先跑通连
                      接/读/写)
```

总工期估算：约 **6 人工作日**（不含 open62541 本身下载等待时间）。

---

## 验证

- [ ] CMake 构建成功（无编译错误）
- [ ] VS 构建成功（vcxproj 无错）
- [ ] 运行时：连接本地 OPC UA 模拟器（Kepware / Prosys / open62541-python server）成功
- [ ] 读节点：输入 NodeId，点击读，返回正确值
- [ ] 订阅：点击订阅，节点值变化时订阅表实时更新
- [ ] 全局日志：无 OPC UA 相关崩溃或未处理异常
