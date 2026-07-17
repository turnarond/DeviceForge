# OPC UA Client 真实实现设计

- **日期**：2026-07-10
- **状态**：已确认（brainstorming 逐条确认）
- **模块**：`src/tools/OpcUaClientTool/` + `src/adapter/OpcUaAdapter`
- **技术栈**：open62541（C 库，LGPL/MPL）
- **目标版本**：v2.2

---

## 1. 背景与目标

现有「OPC UA 客户端」Tab（`OpcUaClientTab`）是演示模式，仅含硬编码假数据。v2.2 核心目标是实现真实的 OPC UA Client，连接现场 OPC UA 服务器，批量读/写数据点，订阅数据变化。

**核心功能（三件套）**：
- 读/写 OPC UA 数据点（Read/Write）
- 订阅数据变化（DataChange Subscription）
- 地址空间浏览（Server Object 节点树）

**非功能约束**：
- 首期安全策略：无加密（None）+ 匿名认证
- Client 模式，不做 Server
- 内置 Tool，不做插件

---

## 2. 架构设计

沿用 DeviceForge Tool 双层模式：`OpcUaClientBackend`（ServiceTask）+ `OpcUaClientWidget`（QWidget）。

### 2.1 open62541 事件驱动模型

open62541 的 `UA_Client_runIterate()` 是轮询式事件驱动（类似 libcurl 的 `easy_perform`）：
- 每次调用处理网络 I/O + 回调派发
- Backend 后台线程以固定周期（100ms）调用 `UA_Client_runIterate()`，驱动整个客户端事件循环
- 数据变化通过 `UA_Client_DataChangeNotificationCallback` 回调写入队列
- Widget 侧通过 `QMetaObject::invokeMethod(Qt::QueuedConnection)` 跨线程取回

```
OpcUaClientBackend (ServiceTask, svc() 后台线程)
   │
   ├── UA_Client_new() → UA_Client 实例
   ├── UA_ClientConfig_setAnonymous()
   ├── UA_Client_connect(endpoint)
   │
   ├── svc() 后台线程:
   │    while (!cancelled) {
   │        UA_Client_runIterate(client, 100ms);
   │        // 处理: DataChange 回调写入 m_dataQueue
   │        //       MonitoredItem 变化写入 m_monitoredQueue
   │        //       Publish 响应派发
   │    }
   │
   ├── readNodes(nodeIdList) → QVariantMap
   ├── writeNodes(nodeIdList, valueList) → QMap<nodeId, status>
   ├── subscribe(nodeIdList) → subscriptionId
   ├── unsubscribe(subscriptionId)
   │
   └── ~OpcUaClientBackend: UA_Client_disconnect + UA_Client_delete

OpcUaClientWidget (QWidget, 主线程)
   ├── 连接配置（endpoint / 安全策略 / 认证）
   ├── 地址空间浏览（QTreeWidget，双击叶子节点填入读写面板）
   ├── 读写面板（手动输入 NodeId，单个/批量读写）
   ├── 订阅面板（实时最新值 / 时间戳 / 质量码）
   └── 数据回调经 QMetaObject::invokeMethod 取回
```

### 2.2 OpcUaAdapter（实现 IProtocolAdapter）

```
IProtocolAdapter
  └── OpcUaAdapter（新增，ProtocolRegistry 注册）
        ├── protocolId() → "opcua"
        ├── m_client（UA_Client*）
        ├── m_connected（bool）
        ├── m_lastError（QString）
        │
        ├── connect(endpointUrl) → 配置 + connect
        ├── disconnect()
        ├── isConnected()
        ├── lastError()
        ├── request(Request) → Read（适配 IProtocolAdapter 统一接口）
        ├── subscribe(Request, StreamCb) → Subscription（适配流模式）
        │
        ├── readNodes(nodeIdList) → QVariantMap（批量读，扩展接口）
        ├── writeNodes(nodeIdList, valueList) → QMap（批量写，扩展接口）
        ├── browseRoot() → QList<UA_ReferenceDescription>（浏览地址空间）
        └── subscribeDataChange(nodeIdList, cb) → subscriptionId
```

> 注：`IProtocolAdapter` 的 `request(Request)`/`subscribe(Request, StreamCb)` 是通用接口，具体到 OPC UA Client，request 映射为 Read，subscribe 映射为 Subscription。但为保持接口通用，Adapter 内部做了转换，不需要改动调用方。

### 2.3 数据结构

**OPC UA NodeId**（三种编码，UI 支持手动输入字符串格式）：
```
ns=2;i=2255        // 整型编码
ns=3;s=Device.Temp // 字符串编码
ns=4;g=...         // GUID 编码（少见）
```

Widget 内部解析字符串为 `UA_NodeId`，用 `UA_NodeId_parse()`。

**数据值**：`UA_Variant` → Qt `QVariant`。类型映射：

| OPC UA 类型 | Qt 类型 |
|-------------|---------|
| UA_Boolean | bool |
| UA_Int16/Int32/Int64 | int/qint64 |
| UA_UInt16/UInt32/UInt64 | uint/quint64 |
| UA_Float/Double | double |
| UA_String | QString |
| UA_DateTime | QDateTime |
| UA_ByteString | QByteArray |

**订阅数据项**：
```cpp
struct OpcUaDataItem {
    QString nodeId;
    QVariant value;
    quint64 timestamp;     //毫秒 epoch
    UA_DataValueQuality quality; // Good/Bad/Uncertain
};
```

---

## 3. OpcUaClientWidget UI 设计

三个面板：

```
┌─ 连接配置 ───────────────────────────────────────────────────────┐
│  Endpoint: [opc.tcp://192.168.1.10:4840         ]                │
│  [连接] [断开]                                                    │
│  安全策略: [None ▼]    认证: [匿名 ▼]   状态: ● 已连接            │
├─ 地址空间浏览 ────────┬─ 读/写 ──────────────────────────────────┤
│ (QTreeWidget)         │  NodeId: [Input/Data.001           ]    │
│ Root                  │  值:     [123                      ]    │
│ ├─ Objects            │  [读] [写]                              │
│ │  ├─ Device          │  ────────────────────────────────────    │
│ │  │  ├─ Temperature  │  批量读列表:                              │
│ │  │  └─ Pressure     │  [+] [-]  NodeId           值     质量  │
│ │  └─ Server          │               Input/Data.001   123   Good│
│ └─ Types              │               Input/Data.002    --    -- │
├───────────────────────┴─────────────────────────────────────────┤
│ 订阅面板:                                                          │
│ [+] [-]  NodeId              最新值          时间戳          [停止]│
│      Input/Temperature       25.6 °C        10:23:45              │
│      Input/Pressure          1.23 bar       10:23:45              │
└──────────────────────────────────────────────────────────────────┘
```

### 面板职责

| 面板 | 职责 |
|------|------|
| 连接配置 | endpoint URL 输入，连接/断开按钮，安全策略下拉（首期仅 None），认证下拉（首期仅匿名），连接状态指示灯 |
| 地址空间浏览 | `browseRoot()` 读 Server Object 节点树，`browseRecursive()` 按需展开子节点，双击叶子节点 → 自动填入读写面板 NodeId |
| 读写面板 | 手动输入 NodeId + 值，点读/写；下方表格支持批量读列表 |
| 订阅面板 | 已订阅节点列表，实时最新值 + 时间戳 + 质量码，行内停止按钮 |

### 地址空间浏览实现

`UA_Server_readBrowse` / `UA_Server_readBrowseNext` 浏览服务器地址空间。由于是 Client，浏览的是 Server 的地址空间（`UA_Client_readServerInfo` 可获取基本 Server Info，浏览节点用 `UA_Client_browse`）。

---

## 4. open62541 集成

### 4.1 依赖引入

| 项 | 说明 |
|----|------|
| **源码** | open62541-www 官方 release（single-file `open62541.h` + `open62541.c` 或多文件版本） |
| **构建** | 建议 **合并进 thirdparty**（`src/thirdparty/open62541/`），与 lw* 库相同布局 |
| **CMake** | `add_subdirectory(thirdparty/open62541)` → `ua_nodestore` + `ua_client` + `ua_types`；链接到 DeviceForge |
| **vcxproj** | 同上，手动加文件 |
| **许可** | LGPL/MPL 双许可，兼容 MIT（项目主许可），开源合规 |

### 4.2 功能开关

open62541 提供插件编译选项（`ua_config.h`）：
- `UA_ENABLE_CLIENT` = YES（必须）
- `UA_ENABLE_SUBSCRIPTIONS` = YES（必须，DataChange Subscription）
- `UA_ENABLE_METHODCALL` = NO（首期不做）
- `UA_ENABLE_SERVER` = NO（仅 Client，不需要）
- `UA_ENABLE_PUBSUB` = NO（不需要）
- `UA_ENABLE_ENCRYPTION` = NO（首期无加密）
- `UA_ENABLE_NODELIST` = YES（Browse 需要）

### 4.3 运行时

- 无额外 DLL：open62541 编译为静态库直接链接（与 lw* 库相同）
- CMake `add_subdirectory` 后自动链接，无需手动处理 DLL

---

## 5. 与现有 OpcUaClientTab 的关系

| 项 | 现有 | 新建 |
|----|------|------|
| 位置 | `DeployMaster.cpp` 直接创建 `OpcUaClientTab` | `src/tools/OpcUaClientTool/` |
| 架构 | 旧架构，演示数据 | Tool 双层模式（Backend + Widget） |
| 替代关系 | 隐藏/移除 | 替换现有 Tab |

**迁移步骤**：
1. `DeployMaster.cpp` 中隐藏旧 `ui->tabWidget_OpcUa->removeTab()`（或注释掉 `setupOpcUaTab()` 调用）
2. `initToolTabs()` 中加入 `setupOpcUaClientTool()`（与其他 Tool 统一入口）
3. `main.cpp` 注册 `OpcUaAdapter` 到 ProtocolRegistry

---

## 6. 文件变更清单

| 文件 | 动作 | 说明 |
|------|------|------|
| `src/thirdparty/open62541/` | 新建 | open62541 源码（single-file 或多文件） |
| `src/adapter/OpcUaAdapter.h/.cpp` | 新建 | 实现 IProtocolAdapter |
| `src/tools/OpcUaClientTool/OpcUaClientBackend.h/.cpp` | 新建 | Backend（ServiceTask） |
| `src/tools/OpcUaClientTool/OpcUaClientWidget.h/.cpp` | 新建 | Widget（QWidget） |
| `src/tools/OpcUaClientTool/OpcUaClientTool.ini` | 新建 | Tool 元数据（id/name/version/category） |
| `DeployMaster.cpp` | 修改 | 移除/隐藏旧 OpcUaClientTab，加入新 Tool |
| `main.cpp` | 修改 | ProtocolRegistry 注册 OpcUaAdapter |
| `CMakeLists.txt` | 修改 | add_subdirectory(open62541) + target_link_libraries |
| `DeployMaster.vcxproj` | 修改 | open62541 文件加入 |
| `docs/` | 修改 | 文档同步（architecture.md / user-guide.md / security.md） |

---

## 7. 测试策略

- **单元测试**：无（open62541 依赖真实 OPC UA Server）。手动验证：用Kepware/Prosys OPC UA 模拟器 或开源 `open62541-python` 起一个本地 UA Server，Client 连上去验证读/写/订阅。
- **集成测试**：现有 `tst_nrec` 框架预留扩展位；可新增 `tst_opcua`（如 CI 能连通 OPC UA 模拟器）。
- **异常测试**：
  - endpoint 不可达 → `UA_Client_connect` 返回错误，检查 `lastError()`
  - 节点不存在 → Read 返回 `UA_STATUSCODE_BADNODEIDUNKNOWN`
  - 写只读节点 → 返回 `UA_STATUSCODE_BADWRITENOTSUPPORTED`
  - 订阅断连 → 自动重连（open62541 内置） + 订阅面板显示断开状态

---

## 8. 非目标（YAGNI）

- 安全策略加密（Basic256Sha256 / Aes128Sha256RsaOaep）
- 用户名+密码认证、证书认证
- Method 调用
- Server 模式（仅 Client）
- 读写历史数据
- pki 证书持久化（Trust on First Use 在内存中即可）
- PubSub（发布/订阅子框架）
