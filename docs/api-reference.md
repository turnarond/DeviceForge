# DeviceForge 接口参考

## IProtocolAdapter — 协议适配器接口

所有网络协议的抽象基类，位于 `src/adapter/IProtocolAdapter.h`。

```cpp
class IProtocolAdapter {
public:
    virtual std::string protocolId() const = 0;           // "ftp" / "telnet" / "ssh"
    virtual bool connect(const DeviceInfo&, const AuthInfo&) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual std::string lastError() const = 0;
    virtual std::future<Response> request(const Request&) = 0;      // 请求-响应
    virtual void subscribe(const Request&, StreamCallback) = 0;     // 流模式
    virtual ProtocolCapability capability() const = 0;
};
```

### 添加新协议适配器

1. 创建类继承 `IProtocolAdapter`，实现所有纯虚方法
2. 在 `main.cpp` 中注册工厂：
```cpp
ProtocolRegistry::instance()->registerFactory("ssh",
    []() { return std::make_shared<SshAdapter>(); });
```

---

## ToolBackend — 工具后端基类

位于 `src/framework/ToolBackend.h`。

```cpp
class ToolBackend : public lwserverbase::core::ServiceTask {
public:
    virtual std::string toolId() const = 0;       // "com.deviceforge.ftp.deploy"
    virtual std::string toolName() const = 0;     // "文件部署"
    virtual std::string toolVersion() const = 0;  // "2.1.0"
    virtual std::string toolCategory() const = 0; // "deploy" | "command" | "test"

    virtual void bindDevices(const std::vector<DeviceInfo>&) = 0;
    virtual void bindCredentials(const AuthInfo&) = 0;
    virtual void applyConfig(const lwserverbase::config::ConfigValue&) = 0;

    int svc() override;  // 后台线程入口
};
```

### 创建新 Tool

1. 创建 `YourBackend` 继承 `ToolBackend`，实现 `svc()` 循环
2. 创建 `YourWidget` 继承 `ToolWidget`，实现 UI 和回调绑定
3. 在 `DeployMaster` 构造函数中创建 Backend+Widget 并添加 Tab

---

## ToolWidget — 工具前端基类

位于 `src/framework/ToolWidget.h`。

```cpp
class ToolWidget : public QWidget {
    Q_OBJECT
public:
    virtual QString toolId() const = 0;
    virtual QString toolName() const = 0;
    virtual void onToolStart() = 0;
    virtual void onToolStop() = 0;

signals:
    void toolStatusChanged(const QString& status);
};
```

---

## DeviceInfo / AuthInfo — 数据结构

位于 `src/framework/DeviceInfo.h`。

```cpp
struct DeviceInfo {
    std::string ip;
    int port = 0;           // 0 = 使用协议默认端口
    std::string protocol;   // "ftp" / "telnet" / "modbus" / ...
    std::string alias;      // 用户自定义别名
};

struct AuthInfo {
    std::string user;
    std::string password;
    void clear();  // 安全擦除密码
};
```

---

## FtpAdapter — FTP/FTPS 适配器

位于 `src/adapter/FtpAdapter.h`。

```cpp
class FtpAdapter : public IProtocolAdapter {
public:
    // IProtocolAdapter 实现
    std::string protocolId() const override { return "ftp"; }
    bool connect(const DeviceInfo&, const AuthInfo&) override;
    void disconnect() override;
    // ...

    // FTP 特有操作
    bool uploadFile(const std::string& localPath, const std::string& remotePath);
    bool uploadFolder(const std::string& localPath, const std::string& remotePath);
    bool downloadFile(const std::string& remotePath, const std::string& localPath);
    bool listDirectory(const std::string& remotePath, std::string& outJsonList);
    bool deleteFile(const std::string& remotePath);
    bool deleteDirectory(const std::string& remotePath);
    bool clearRemoteDirectory(const std::string& remotePath);

    void setUseFtps(bool useFtps);  // 启用 FTPS 加密
};
```

---

## TelnetAdapter — Telnet 适配器

位于 `src/adapter/TelnetAdapter.h`。

```cpp
class TelnetAdapter : public IProtocolAdapter {
    // 请求-响应模式（单次命令执行）
    // 流模式（持续接收输出）
    // 基于 lwcommunicate::LWTcpClient
};
```

---

## SshAdapter — SSH 适配器

位于 `src/adapter/SshAdapter.h`。基于 libssh2，密码认证 + TOFU 主机密钥验证。

```cpp
class SshAdapter : public IProtocolAdapter {
    std::string protocolId() const override { return "ssh"; }
    // request：打开 exec channel 执行单条命令，返回输出 + exit code
    // 首次连接自动记录主机指纹（TOFU）；指纹变化时报错（防 MITM）
    // libssh2 内建传输加密
};
```

---

## OpcUaAdapter — OPC UA 客户端适配器

位于 `src/adapter/OpcUaAdapter.h`。基于 open62541 v1.5.5，OPC UA 客户端。首期 None 安全策略 + 匿名认证。

```cpp
class OpcUaAdapter : public IProtocolAdapter {
public:
    // IProtocolAdapter 实现
    std::string protocolId() const override { return "opcua"; }
    bool connect(const DeviceInfo&, const AuthInfo&) override;  // device.ip 为完整 opc.tcp:// URL 或 host
    void disconnect() override;

    // OPC UA 特有操作（批量读/写 + 浏览）
    QVariantMap readNodes(const QStringList& nodeIds);                 // NodeId → 值（QVariant）
    QMap<QString, QString> writeNodes(const QStringList& nodeIds,      // 每节点返回状态码字符串
                                      const QVariantList& values);
    QString browseRoot();                                              // Objects 节点直接子节点，JSON

    // DataChange 订阅（MonitoredItem）
    using DataChangeCb = std::function<void(const QString& nodeId, const QVariant& value,
                                            quint64 timestampMs, const QString& quality)>;
    quint32 subscribeDataChange(const QStringList& nodeIds, DataChangeCb cb);  // 返回 subscriptionId(0=失败)
    void unsubscribeAll();
    void runIterate(int timeoutMs);   // 驱动 open62541 事件循环，派发 DataChange 回调（由 Backend svc 线程调用）
};
```

> **NodeId 格式**：`ns=<命名空间>;i=<整型>` 或 `ns=<命名空间>;s=<字符串>`，内部用 `UA_NodeId_parse` 解析。
>
> **线程安全**：open62541 以 `UA_MULTITHREADING=0` 编译，`UA_Client` 非线程安全。所有 `UA_Client` 访问由内部 `recursive_mutex` 串行化；`subscribeDataChange` 的回调在 `runIterate`（svc 线程）内触发，使用方需将 UI 更新 marshal 到 GUI 线程。

---

## DeviceBusWidget — 设备总线

位于 `src/ui/DeviceBusWidget.h`。

```cpp
class DeviceBusWidget : public QWidget {
    Q_OBJECT
public:
    void addDevice(const DeviceInfo& device);
    void removeDevice(const QString& ip);
    std::vector<DeviceInfo> selectedDevices() const;
    std::vector<DeviceInfo> allDevices() const;
    QString user() const;
    QString password() const;
signals:
    void deviceSelectionChanged();
    void credentialsChanged(const QString& user, const QString& password);
};
```

---

## NetRelayTool — 网络调试中继 + 录制回放

位于 `src/tools/NetRelayTool/`。共享类型在 `NetRelayTypes.h`：

```cpp
enum class RelayProtocol  { Tcp, Udp };
enum class RelayDirection { Upstream, Downstream };  // Upstream = 生产者→消费者
```

### NetRelayBackend — 中继引擎（`ToolBackend` 子类，非 QObject）

```cpp
class NetRelayBackend : public ToolBackend {
public:
    // 中继控制（生产者连 listenAddr:listenPort，工具转发到 upstreamHost:upstreamPort）
    void startRelay(RelayProtocol proto, const QString& listenAddr, quint16 listenPort,
                    const QString& upstreamHost, quint16 upstreamPort);
    void stopRelay();
    bool isRunning() const;

    // 录制（startRelay 前调用 enableRecording 开启）
    void enableRecording(const QString& path);   // 录制到 .nrec
    void disableRecording();

    // 回放（与中继互斥）
    void startReplay(const QString& nrecPath, const QString& consumerHost,
                     quint16 consumerPort, double speedFactor);  // speedFactor: 1.0=原速
    void pauseReplay();
    void resumeReplay();
    void stopReplay();
    bool isReplaying() const;

    // 回调（Widget 设置，跨线程经 QueuedConnection 投递）
    void setLogCallback(std::function<void(const std::string&)>);
    void setErrorCallback(std::function<void(const std::string&)>);
    void setDataCallback(std::function<void(RelayDirection, const QString& peer, int sessionId, const QByteArray&)>);
    void setSessionCallback(std::function<void(const RelaySession&)>);
    void setReplayProgressCallback(std::function<void(int played, int total, qint64 tsOffsetMs)>);
    void setReplayFinishedCallback(std::function<void()>);
    void setReplayErrorCallback(std::function<void(const std::string&)>);
};
```

内部状态机 `RelayMode{Idle,Relaying,Replaying}` 保证中继/回放互斥。

### RelayRecorder / RelayRecording — .nrec 读写

```cpp
class RelayRecorder {                 // 顺序写 .nrec
    bool open(const QString& path, RelayProtocol, qint64 startEpochMs);
    void append(RelayDirection, int sessionId, qint64 tsOffsetMs, const QByteArray& data);
    void close();
    bool isOpen() const;
};

struct NrecRecord { RelayDirection dir; int sessionId; qint64 tsOffsetMs; QByteArray payload; };
struct NrecFile   { RelayProtocol protocol; qint64 startEpochMs; QVector<NrecRecord> records; };

class RelayRecording {                // 读取并校验 .nrec（magic/版本/长度上限/截断）
    static bool load(const QString& path, NrecFile& out, QString& error);
};
```

### RelayPlayer — 按原始时序回放上行到消费者

```cpp
class RelayPlayer {                    // 非 QObject；QTimer 按 tsOffsetMs 差值调度
    bool start(const QString& nrecPath, const QString& consumerHost,
               quint16 consumerPort, double speedFactor);  // 仅重放 Upstream 记录
    void pause();  void resume();  void stop();  bool isActive() const;
    void setLogCallback(std::function<void(const std::string&)>);
    void setErrorCallback(std::function<void(const std::string&)>);
    void setProgressCallback(std::function<void(int played, int total, qint64 tsOffsetMs)>);
    void setFinishedCallback(std::function<void()>);
};
```

> `.nrec` 格式：32 字节文件头（`"NREC"` magic + u16 版本 + u8 协议 + i64 起始 epoch + 保留）+ 变长记录（u8 方向 + u32 会话号 + i64 相对时间戳 + u32 长度 + payload），全部小端。
