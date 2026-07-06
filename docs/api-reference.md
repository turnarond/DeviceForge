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
