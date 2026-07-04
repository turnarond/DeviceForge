# DeployMaster 2.0 通用设备运维平台设计文档

| 文档版本 | V1.0 |
| :--- | :--- |
| **项目名称** | DeployMaster 2.0 — 可扩展通用设备运维平台 |
| **适用阶段** | 全量架构重构：从单体 Qt 应用到插件化运维平台 |
| **最后更新** | 2026-07-04 |

---

## 1. 设计目标

将 DeployMaster 从"仅供 SylixOS PLC 使用的 Qt 单体工具"升级为"面向通用工业设备的可扩展运维平台"，兼具以下能力：

1. **可扩展**：新增协议模块无需修改主窗口，支持 DLL 插件热插拔
2. **用户友好**：DevToys 风格的精致外观，统一设计语言，零门槛上手
3. **工业级**：深色仪表盘主题，适合长时间使用和工厂环境
4. **可观测**：结构化日志 + 业务指标采集
5. **向后兼容**：渐进式迁移，不破坏现有功能

---

## 2. 总体架构

### 2.1 架构分层

采用 **lwserverbase 服务核 + Qt Widget 壳** 的双层架构：

```
┌──────────────────────────────────────────────────────────────────────┐
│                    DeployMaster 2.0 架构                              │
│                                                                      │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │                   Qt Shell（Qt 壳）                           │   │
│  │  ┌────────────┐ ┌──────────────┐ ┌────────────────────────┐  │   │
│  │  │ 设备总线    │ │ Tool 工作区   │ │ 事件总线（日志区）     │  │   │
│  │  │(QListWidget)│ │(QStacked)    │ │ (QPlainTextEdit)       │  │   │
│  │  └────────────┘ └──────┬───────┘ └────────────────────────┘  │   │
│  │                        │                                      │   │
│  │          ┌─────────────┴─────────────┐                       │   │
│  │          │     ToolHost（桥接层）     │                       │   │
│  │          │  ServiceTask → QWidget    │                       │   │
│  │          └─────────────┬─────────────┘                       │   │
│  └────────────────────────┼──────────────────────────────────────┘   │
│                           │ lwmsgq (消息队列)                        │
│  ┌────────────────────────┼──────────────────────────────────────┐   │
│  │              lwserverbase 服务核（C++ 原生）                   │   │
│  │                                                                │   │
│  │  ┌──────────────┐  ┌────────────────┐  ┌──────────────────┐  │   │
│  │  │ServiceManager│  │ ConfigManager  │  │ MetricsCollector │  │   │
│  │  │ (Tool 生命周期)│  │ (XML 配置热加载)│  │ (Prometheus 指标) │  │   │
│  │  └──────┬───────┘  └────────────────┘  └──────────────────┘  │   │
│  │         │                                                      │   │
│  │  ┌──────┴──────────────────────────────────────────────────┐  │   │
│  │  │              Tool 实例（ServiceTask 子类）               │  │   │
│  │  │  每个 Tool = ToolBackend (逻辑) + ToolWidget (UI)       │  │   │
│  │  │  通过双向 lwmsgq 解耦前后端                              │  │   │
│  │  └─────────────────────────────────────────────────────────┘  │   │
│  │                                                                │   │
│  │  ┌──────────────────────────────────────────────────────────┐  │   │
│  │  │               Protocol Adapter 层                        │  │   │
│  │  │  IProtocolAdapter 统一接口，LWConnPool 管理连接          │  │   │
│  │  └──────────────────────────────────────────────────────────┘  │   │
│  └────────────────────────────────────────────────────────────────┘  │
│                                                                      │
│  ┌────────────────────────────────────────────────────────────────┐  │
│  │  基础设施                                                       │  │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────────┐  │  │
│  │  │ lwlog    │ │ lwmsgq   │ │ nanopb   │ │ tinyxml2         │  │  │
│  │  │(管道日志) │ │(消息队列) │ │(协议序列化)│ │(插件清单/配置)   │  │  │
│  │  └──────────┘ └──────────┘ └──────────┘ └──────────────────┘  │  │
│  └────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────┘
```

### 2.2 核心设计决策

| 决策 | 选择 | 理由 |
|------|------|------|
| 宿主核 | lwserverbase::ServiceManager | 生命周期 + 配置热加载 + 指标，已有完整实现 |
| Tool 模型 | ToolBackend (ServiceTask) + ToolWidget (QWidget) | 后端可脱离 Qt 测试，前端纯 Qt Widget |
| 桥接通信 | lwmsgq 消息队列 | 线程安全、RAII 封装、消息驱逐策略 |
| 连接管理 | lwcommunicate::LWConnPool | TCP/UDP/Serial 统一抽象 + 指数退避自动重连 |
| 日志后端 | lwlog | Filter→Formatter→Appender 管道架构，支持热加载配置 |
| 插件清单 | tinyxml2 解析 XML | 单文件零依赖，lwlog 已使用相同模式 |
| 序列化 | nanopb (Protocol Buffers) | 轻量、Schema 演进、跨语言 |
| 内置协议 | FtpAdapter/TelnetAdapter 编译在主程序 | 稳定性优先，类型安全 |
| 扩展协议 | DLL + QPluginLoader | 热插拔、独立发布、第三方可开发 |

---

## 3. Tool 抽象接口

### 3.1 整体模型

```
┌─────────────────────────────────────────┐
│               ITool（总接口）             │
│                                         │
│  ┌───────────────────────────────────┐  │
│  │  ToolBackend : ServiceTask         │  │
│  │  - 业务逻辑、状态机、数据处理       │  │
│  │  - 不依赖 Qt GUI                    │  │
│  │  - 通过 lwmsgq 与 UI 通信          │  │
│  └──────────────┬────────────────────┘  │
│                 │ lwmsgq (双向)         │
│  ┌──────────────┴────────────────────┐  │
│  │  ToolWidget : QWidget             │  │
│  │  - UI 渲染、用户交互              │  │
│  │  - 消费 Backend 消息更新界面      │  │
│  │  - 将用户操作编码为消息发给 Backend│  │
│  └───────────────────────────────────┘  │
└─────────────────────────────────────────┘
```

### 3.2 ToolBackend 接口

继承自 lwserverbase::ServiceTask，新增以下纯虚函数：

```cpp
class ToolBackend : public ServiceTask {
public:
    // --- 身份 ---
    virtual std::string toolId() const = 0;       // 例 "com.deploymaster.ftp"
    virtual std::string toolName() const = 0;     // 例 "文件部署"
    virtual std::string toolVersion() const = 0;  // 语义化版本

    // --- 生命周期（继承自 ServiceTask） ---
    // OnStart()    — 初始化、绑定设备、启动监听
    // OnStop()     — 清理、断开连接、持久化状态
    // svc()        — 工作线程入口（可选，轮询类 Tool 使用）
    // OnHealthCheck() — 返回健康状态

    // --- 设备绑定 ---
    virtual void bindDevices(const std::vector<DeviceInfo>& devices) = 0;
    virtual void bindCredentials(const AuthInfo& auth) = 0;

    // --- 运行时配置 ---
    virtual void applyConfig(const ConfigValue& config) = 0;

    // --- 消息通道 ---
    // m_uiQueue  — Backend → UI 的消息（进度、结果、日志）
    // m_cmdQueue — UI → Backend 的消息（用户操作命令）
};
```

### 3.3 ToolWidget 接口

```cpp
class ToolWidget : public QWidget {
    Q_OBJECT
public:
    virtual QString toolId() const = 0;
    virtual QString toolName() const = 0;

    // 框架注入消息队列（由 ToolHost 初始化）
    virtual void setCommandQueue(std::shared_ptr<MessageQueue<ToolCommand>> q) = 0;
    virtual void setUiEventQueue(std::shared_ptr<MessageQueue<ToolEvent>> q) = 0;

    // 生命周期回调
    virtual void onToolStart() = 0;
    virtual void onToolStop() = 0;

signals:
    void toolStatusChanged(const QString& status);
};
```

### 3.4 ToolHost 桥接层

| 职责 | 实现方式 |
|------|---------|
| 创建 Tool 实例 | 内置 Tool 直接构造，插件 Tool 通过 LoadLibrary + 工厂函数 |
| 配对联接 | 创建 Backend → 创建 Widget → 注入双向 lwmsgq |
| 生命周期同步 | Widget.onToolStart → Backend.OnStart → 回填消息队列 |
| 销毁顺序 | Widget.onToolStop → Backend.OnStop → 清空队列 → 释放 |

> **注意**：Backend 层统一使用 `std::string`（与 lwserverbase/STL 生态一致），Widget 层使用 `QString`（Qt 生态）。ToolHost 负责边界处的 `QString ↔ std::string` 转换。消息队列中传输的是 nanopb 序列化字节，不存在字符串编码问题。

### 3.5 Tool 开发者视角

1. 定义协议消息（.proto 文件，通过 nanopb 生成 C 头文件）
2. 实现 Backend：继承 ToolBackend，实现约 8 个纯虚函数
3. 实现 Widget：继承 ToolWidget，用 Qt Designer 画 UI
4. 注册到清单：编写 manifest.xml 声明 id/名称/依赖/入口
5. 编译为 DLL → 放到 plugins/ 目录
6. 框架自动发现、加载、展示

---

## 4. Protocol Adapter 层

### 4.1 统一接口

```cpp
class IProtocolAdapter {
public:
    virtual ~IProtocolAdapter() = default;

    // --- 身份 ---
    virtual std::string protocolId() const = 0;   // "ftp", "telnet", "modbus", "ssh"...

    // --- 连接生命周期 ---
    virtual bool connect(const DeviceInfo& device, const AuthInfo& auth) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual std::string lastError() const = 0;

    // --- 传输抽象 ---
    virtual std::future<Response> request(const Request& req) = 0;   // 请求-响应模式
    virtual void subscribe(const Request& req, StreamCallback cb) = 0; // 流模式
    virtual void unsubscribe() = 0;

    // --- 能力声明 ---
    virtual ProtocolCapability capability() const = 0;
};
```

### 4.2 适配器实现策略

| 协议适配器 | 底层实现 | 复用/改动 |
|-----------|---------|----------|
| **FtpAdapter** | libcurl | 重构现有 FtpManager 为实现 IProtocolAdapter，保留 libcurl 核心逻辑 |
| **TelnetAdapter** | lwcommunicate::LWTcpClient | 替换现有裸 QTcpSocket |
| **ModbusAdapter** | Qt QModbusTcpClient | 封装在适配器内部 |
| **WebSocketAdapter** | Qt QWebSocket | 封装在适配器内部 |
| **SSHAdapter**（未来） | libssh2 + LWTcpClient | 新增 DLL 插件 |
| **SNMPAdapter**（未来） | net-snmp + LWTcpClient | 新增 DLL 插件 |
| **MQTTAdapter**（未来） | Qt MQTT / paho.mqtt | 新增 DLL 插件 |

### 4.3 连接池

使用 lwcommunicate::LWConnPool 替换现有 ConnectionPool（死代码）：

```
Key: "protocol://ip:port"
例:  "ftp://192.168.1.100:21"
     "telnet://192.168.1.100:23"
     "modbus://192.168.1.100:502"

内置能力：
- addConn() / getConn(name) / removeConn()
- startAll() / stopAll()
- ReconnectPolicy（指数退避、最大重试、基础/最大延迟可配置）
- LWConnEventHandler（连接状态回调）
- nativeHandle()（可集成到 Qt 事件循环）
```

---

## 5. 插件系统

### 5.1 插件清单格式（tinyxml2 解析）

```xml
<!-- plugins/ftp_deploy/manifest.xml -->
<tool>
    <id>com.deploymaster.ftp.deploy</id>
    <name>文件部署</name>
    <version>2.0.0</version>
    <category>deploy</category>
    <icon>ftp_deploy.svg</icon>
    <description>通过 FTP 协议批量上传文件/文件夹到目标设备</description>
    <author>DeployMaster Team</author>

    <requires>
        <protocol id="ftp" minVersion="1.0"/>
        <hostVersion min="2.0.0"/>
    </requires>

    <backend factory="createToolBackend" library="ftp_deploy_backend.dll"/>
    <widget factory="createToolWidget" library="ftp_deploy_widget.dll"/>

    <defaults>
        <property key="defaultRemotePath" value="/app"/>
        <property key="clearBeforeDeploy" value="false" type="bool"/>
        <property key="autoReboot" value="false" type="bool"/>
    </defaults>
</tool>
```

### 5.2 加载策略

**混合模式**：
- 内置 Tool（FTP/Telnet/Modbus/WebSocket）编译在主程序中，类型安全，稳定性优先
- 扩展 Tool 和扩展 Protocol 通过 DLL + QPluginLoader 热插拔

**发现流程**：
1. 扫描内置 Tool 注册表（编译时注册）
2. 扫描 `plugins/` 目录，逐个子目录读取 manifest.xml
3. 校验 hostVersion 兼容性
4. 检查 protocol 依赖是否满足
5. LoadLibrary(dll) → 获取工厂函数
6. 失败则跳过 + 记录日志 + 标记为不可用

### 5.3 目录布局（安装后）

```
DeployMaster/
├── DeployMaster.exe
├── plugins/
│   ├── ftp_deploy/
│   │   ├── manifest.xml
│   │   ├── ftp_deploy_backend.dll
│   │   └── ftp_deploy_widget.dll
│   ├── ssh_terminal/
│   │   ├── manifest.xml
│   │   ├── ssh_backend.dll
│   │   └── ssh_widget.dll
│   └── ...
├── protocols/
│   └── ssh_adapter.dll
├── config/
│   └── deploymaster.xml
└── logs/
    └── deploymaster.log
```

---

## 6. 配置管理

### 6.1 配置存储

复用 lwserverbase::ConfigManager，全局单例：

```
ConfigManager
 ├─→ tools/ftp_deploy/defaultRemotePath = "/app"
 ├─→ tools/ftp_deploy/clearBeforeDeploy = false
 ├─→ tools/telnet/timeout = 5000
 ├─→ devicepool/favorites = ["192.168.1.100", "192.168.1.200"]
 ├─→ window/geometry = "1200x800+100+50"
 │
 ├─→ 监听文件变更 → 热加载 → ConfigManager::RegisterChangeHandler() 通知订阅者
 └─→ Save() → 持久化到 %APPDATA%/DeployMaster/config.xml
```

### 6.2 配置热加载

```
ConfigManager::Set("tools/ftp_deploy/timeout", 10000)
  └─→ 通知所有 RegisterChangeHandler("tools/ftp_deploy/*")
        └─→ FtpDeployTool::applyConfig(newConfig)
              └─→ 更新内部参数，无需重启 Tool
```

---

## 7. 日志 & 可观测性

### 7.1 日志数据流

```
Tool Backend
  ├─→ LWLOG_I("上传完成") → lwlog 管道
  │                           ├─→ ConsoleAppender（调试用）
  │                           ├─→ FileAppender（按天滚动，保留 7 天）
  │                           └─→ lwmsgq → 事件总线 UI 显示
  │
  └─→ MetricsCollector::counter("ftp.upload.bytes")->Increment(x)
        └─→ Prometheus 格式暴露（/metrics 端点，可选）
```

### 7.2 分层职责

| 层 | 工具 | 消费者 |
|----|------|--------|
| 结构化日志 | lwlog（Filter→Formatter→Appender 管道） | 文件、控制台、事件总线 UI |
| 业务指标 | lwserverbase::MetricsCollector | Prometheus（可选）、调试面板 |
| Qt 消息 | qDebug → 安装 QtMessageHandler 桥接到 lwlog | lwlog 统一管道 |

### 7.3 日志存储

```
%APPDATA%/DeployMaster/logs/
├── deploymaster.log        ← 当前日志
├── deploymaster.1.log      ← 每天滚动
├── ...
└── deploymaster.7.log      ← 保留 7 天自动清理
```

---

## 8. UI 设计

### 8.1 设计方向

**"工业仪表盘"** — 借用工业自动化控制室的视觉语言（SCADA 监控屏、设备指示灯、DIN 导轨模块化），与市面蓝色系开发工具形成差异化。精密、冷静、长时间使用不疲劳。

### 8.2 色板

```
背景底层    #0B0E14   Signal Black  —— 控制室暗光环境
面板层      #141820   更深一层，卡片/面板载体
边框/分割   #252A33   微妙分隔，不抢注意力
文字主色    #C8CCD4   暖灰白，减少眩光
文字次要    #7B8494   低对比度，标签和描述
强调色      #F0A030   琥珀/橙色 —— 工业控制面板经典信号色
成功色      #40C8A0   青绿 —— 设备在线、操作成功
警告色      #F0D050   工业黄 —— 注意/等待
错误色      #E85848   工业红 —— 故障/离线/失败
```

选择琥珀色而非蓝色作为强调色的原因：
- 工业控制室经典配色：深色背景 + 琥珀色信号灯
- 长时间使用更护眼（琥珀色温低，比蓝光对昼夜节律影响小）
- 在工厂强光/暗光交替环境下可读性更好
- 与市面所有蓝色系开发工具形成差异化

### 8.3 字体

| 用途 | 首选字体 | 降级字体 | 说明 |
|------|---------|---------|------|
| 显示/标题 | Sarasa Gothic | Noto Sans SC → Microsoft YaHei | Tool 名称、状态指示 |
| 等宽 | Cascadia Code | JetBrains Mono → Consolas | 日志、IP 地址、命令输出 |
| UI 控件 | Segoe UI Variable | Segoe UI → 系统默认 | 表单、标签 |

### 8.4 布局

```
┌──────────────────────────────────────────────────────────────┐
│  ≡ DeployMaster                              ⚙ ─ □ ×        │
├──────────────────────────────────────────────────────────────┤
│  设备总线                                                    │
│  ⬤ .100  ⬤ .101  ◉ .102  ⬤ .103  [+添加]    [凭证...]     │
│  ▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔   │
├───────────────┬──────────────────────────────────────────────┤
│ 工具           │  工作区 (QStackedWidget)                     │
│               │                                               │
│ 📦 文件部署   │  ┌──────────────────────────────────────┐    │
│ 📥 日志查询   │  │  Tool 内容（每个 Tool 负责自己的布局） │    │
│ ⌨ 批量命令   │  │                                       │    │
│ 📊 Modbus测试 │  │  头部: Tool 标题 + 状态 + 操作按钮     │    │
│ 🌐 WebSocket  │  │  中部: 配置/操作/结果（Tool 自定）     │    │
│ 📡 OPC UA     │  │  底部: Tool 局部状态条                 │    │
│ 🔬 诊断分析   │  └──────────────────────────────────────┘    │
│               │                                               │
├───────────────┴──────────────────────────────────────────────┤
│  ▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔   │
│  [14:30:05] ✓ .100  app.exe → 2.3 MB  3.2s                  │
│  [14:30:06] ✓ .100  config.xml → 12 KB  0.1s                │
│  [14:30:07] ▶ .101  上传中...                                │
│  ═══ 事件总线 ═══════════════════════════════════════════    │
└──────────────────────────────────────────────────────────────┘
```

### 8.5 组件选型

| 界面区域 | 实现方式 | 理由 |
|---------|---------|------|
| 设备总线条 | QListWidget (horizontal, icon mode) + QSS | Qt 原生，QSS 即可定制圆形指示灯 |
| 呼吸灯动画 | QPropertyAnimation | Qt 内置，不引入新依赖 |
| Tool 导航 | QListWidget + QSS 分类样式 | 支持滚动，比 QTabWidget 灵活 |
| 工作区 | QStackedWidget | Qt 原生页面切换 |
| 日志区 | QPlainTextEdit (readonly) | 性能优于 QTextEdit，适合高频日志 |
| 配色/QSS | 修改现有 darkstyle.qss | 505 行已有完整框架，改色板即可 |
| Tool 卡片 | QFrame + QSS | 每个 ToolWidget 外层包一个 styled QFrame |

### 8.6 需要自定义实现的组件

| 组件 | 实现方式 | 代码量估计 |
|------|---------|----------|
| DeviceBusWidget | 封装 QListWidget | ~80 行 |
| 呼吸灯动画 | QPropertyAnimation on QLabel | ~30 行 |
| ToolHost 桥接 | 纯逻辑代码 | ~200 行 |
| Tool 导航分类 | QListWidget + QStyledItemDelegate | ~50 行 |

所有视觉效果通过 QSS 实现，不需要自定义 paintEvent。

### 8.7 动效

- 设备在线指示灯：QPropertyAnimation opacity 脉冲（2s 周期，0.7 ↔ 1.0）
- Tool 切换：QStackedWidget 默认过渡
- 新日志：自然滚动，无需刻意动画

---

## 9. 迁移策略

### Phase 0：基础设施对齐（不改变任何用户可见功能）

| 任务 | 内容 | 风险 |
|------|------|------|
| P0-1 | 集成 lwlog，替换散落 qDebug 日志 | 低 |
| P0-2 | 集成 lwcommunicate，写 TelnetAdapter 替换裸 QTcpSocket | 中，需回归测试 Telnet |
| P0-3 | FtpManager 重构为 FtpAdapter（实现 IProtocolAdapter） | 低，换壳不改逻辑 |
| P0-4 | 删除现有 ConnectionPool（死代码），AppState 精简 | 低 |
| P0-5 | 引入 tinyxml2，写插件清单解析器 | 低，全新功能 |

### Phase 1：框架搭建（内部重构）

| 任务 | 内容 | 风险 |
|------|------|------|
| P1-1 | 实现 ToolBackend / ToolWidget / ToolHost 三层 | 中，核心抽象 |
| P1-2 | 将 FtpManager 迁移为第一个 Tool（FtpDeployTool） | 中，需同步改 DeployMaster |
| P1-3 | 设备总线 UI 组件实现 | 低，新 QWidget |
| P1-4 | 修改 darkstyle.qss 色板 + 设备总线/Tool 导航样式 | 低，只改颜色值 + 新增样式 |

### Phase 2：全量迁移

| 任务 | 内容 | 风险 |
|------|------|------|
| P2-1 | TelnetDeploy → TelnetTool | 中 |
| P2-2 | LogQueryTab → LogQueryTool（复用 FtpAdapter） | 低 |
| P2-3 | ModbusCluster → ModbusTool | 中 |
| P2-4 | WebSocketClient → WebSocketTool | 低 |
| P2-5 | OpcUaClientTab → OpcUaTool（仍为演示模式） | 低 |
| P2-6 | DiagnosticClient → DiagnosticTool（待 tinyvsoa 补全） | 高 |

### Phase 3：插件化 & 扩展

| 任务 | 内容 | 风险 |
|------|------|------|
| P3-1 | DLL 插件加载机制（QPluginLoader + manifest.xml） | 中 |
| P3-2 | SSH 协议适配器 + SSH 终端 Tool（首个第三方 Demo 插件） | 中 |
| P3-3 | 废弃旧 DeployMaster.ui，主窗口纯代码构建布局 | 低 |

---

## 10. 测试策略

| 类型 | 内容 | 工具 |
|------|------|------|
| 单元测试 | ToolBackend 业务逻辑、IProtocolAdapter 协议适配器 | Google Test |
| 集成测试 | ToolBackend ↔ ToolWidget 消息通信、LWConnPool 连接生命周期 | 自定义测试框架 |
| 端到端测试 | 插件发现→加载→执行完整流程 | 手动 + 自动化脚本 |
| 性能测试 | 10+ 设备并发、50MB+ 文件传输、长时间运行 | 手动压测 |

---

## 11. 与现有设计文档的关系

本设计是现有架构重构文档 (`2026-06-14-refactor-design.md`) 的升级和替代：

- **保留**：EventBus 概念（升级为 lwmsgq）、MVP 分层思想（升级为 ServiceTask + Widget）、删除死代码 ConnectionPool
- **扩展**：从"7 个 Tab 的 Monolith"扩展为"可插拔 Tool 平台"、从"手工管理连接"升级为 LWConnPool
- **新增**：插件系统、Protocol Adapter 层、lwserverbase 微服务内核、工业仪表盘 UI 语言
- **废弃**：现有 `src/framework/ConnectionPool`（由 LWConnPool 替代）、`src/framework/EventBus`（由 lwmsgq 替代）、`DeployMaster.ui`（最终由代码构建替代）

---

**设计文档完成。**
