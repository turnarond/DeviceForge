# DeviceForge v2.6：SFTP 批量部署，从「能浏览」到「能部署」

> 一个用 Qt 写的工业设备批量运维工具。v2.5 给 SFTP 补上了远程文件管理，v2.6 把最后一块拼图放上：SFTP 批量部署，与 FTP 完全对等。

---

## 它是什么

DeviceForge 是一个基于 Qt 6 + C++17 的 Windows 桌面应用，用来对 PLC 设备做批量运维：FTP/SFTP 批量部署固件、Telnet/SSH 批量执行命令、Modbus 寄存器读写、OPC UA 客户端、WebSocket 通信、TCP/UDP 网络调试中继。

前身叫 DeployMaster，2026 年 7 月改名。面向 PLC、嵌入式终端、网络设备等工业硬件的「部署 → 测试 → 运维」场景。

v2.4 把 FTP 部署从表单变成了双栏文件管理器；v2.5 补完了远程文件管理并做了 SylixOS 深度适配；v2.6 把 SFTP 从「单文件传输」变成「完整部署通道」。

**更多功能请去 github 网站查看。**

开源，GitHub：[turnarond/DeviceForge](https://github.com/turnarond/DeviceForge)

---

## 为什么会有这一版

v2.5 发布时有一个诚实的边界：**SFTP 只能浏览和管理文件，批量部署尚未接通**——点部署会提示切换 FTP。当时的判断是 SylixOS 设备走 FTP 是主流路径，SFTP 部署优先级靠后。

但这个边界撑不了太久。实际反馈中出现了越来越常见的情况：

- 标准 Linux 服务器、Ubuntu 工控机上，FTP 服务默认没开，SSH 反而是标配
- 部分支持 SSH 的新设备压根不提供 FTP 服务
- 云服务器运维场景里，安全组经常只放行 22 端口

单传一个文件，还能右键下载再拖上传；批量部署固件，就绕不过去了。于是 v2.6 把它做完：**同一双栏 UI，FTP/SFTP 一键切换，部署体验完全对等。**

---

## v2.6 做了什么

### 1. SFTP 批量部署打通（核心）

双栏文件管理器的工具栏上有协议下拉框（FTP / SFTP）。v2.5 里选 SFTP 点部署会提示「请切换 FTP」；v2.6 里直接可用：

- **协议选 SFTP → 连接 → 部署单文件或整个文件夹**，远程面板自动刷新
- **拖拽上传同样走部署通道**，从 Windows 资源管理器拖文件到远程面板即可
- **含子目录的文件夹递归上传**，远程目录结构自动创建（mkdir 先于文件上传）
- 部署能力与 FTP 完全对等：**部署前清空、进度条、取消按钮、失败收集**，一个不落

### 2. 部署循环协议化：IDeployable 接口

这是 v2.6 最核心的技术点。实现方式是**不往 IProtocolAdapter 塞部署方法**，而是新增一个可选能力接口 `IDeployable`（`src/adapter/IDeployable.h`）：

```
uploadFile()           // 上传单个文件
uploadFolder()         // 递归上传整个目录
clearRemoteDirectory() // 清空远程目录内容（保留目录本身）
setProgressCallback()  // 进度回调 0-100
setCancelFlag()        // 取消标志（每文件/每块前检查）
```

`FtpAdapter` 和 `SshAdapter` 都实现这个接口；部署循环（`FtpDeployBackend::startUpload`）通过 `dynamic_cast` 探测——**适配器支持部署能力就能进批量部署，与协议无关**。

为什么这样设计：

- FTP 已有的方法签名天然对齐（`clearRemoteDirectory`、`setCancelFlag` 等），SshAdapter 映射成本最低
- 未来新协议（如 SCP）只要实现 `IDeployable` 就能获得整套批量部署能力，部署循环零改动

### 3. 递归 mkdir 上传与清空语义

SshAdapter 的三个 SFTP 部署方法：

- **`sftpUploadFolder`**：先用 `planFolderUpload` 递归遍历生成上传计划（**目录项在前、文件项在后**，保证 mkdir 先于文件上传），再逐项执行——目录 `mkdir`（已存在 EEXIST 忽略，真失败由后续文件上传暴露），文件 `sftpUploadFile`（每项前检查取消标志）
- **`sftpClearDirectory`**：清空内容、**保留目录本身**——与 FTP `clearRemoteDirectory` 语义对齐（直接递归删目录会导致后续上传失败）
- **`sftpSetCancelFlag`**：取消标志挂载，取消后每文件/每块前检查并中止

### 4. 协议复用「ssh」注册键

SFTP 部署复用浏览用的**同一个** `SshAdapter`（ProtocolRegistry 注册键 `"ssh"`），不新建 sftp 工厂。`FtpDeployWidget::currentProtocol()` 对 SFTP 返回 `"ssh"`，部署循环直接按该键取适配器——浏览路径和部署路径天然是同一连接通道。

### 5. 测试

8 个测试目标全部通过，v2.6 新增两个：

| 目标 | 用例 | 覆盖 |
|------|------|------|
| `tst_sftp_plan` | 3 个（`plan_singleFile` / `plan_nestedDir` / `plan_emptyDir`） | 上传计划生成：单文件、嵌套目录排序、空目录边界 |
| `tst_deploy_loop` | 3 个（`deploy_twoDevices_allSuccess` / `deploy_connectFail_deviceSkipped` / `deploy_cancelStopsRemaining`） | 部署循环：多设备全成功、连接失败设备跳过、取消中止剩余 |

---

## 技术实现

### 堆栈

| 层 | 技术 |
|----|------|
| UI | Qt 6.11.1 Widgets，QSS 深色主题 |
| 构建 | CMake 3.22+，Visual Studio 2022 |
| FTP | libcurl 8.16.0，FTPS 加密 |
| SFTP/SSH | libssh2 |
| OPC UA | open62541 v1.5.5（UA_MULTITHREADING=100） |
| 配置持久化 | SQLite + Windows DPAPI 加密（ConfigStore） |

### 架构

```
Tool = ToolBackend (ServiceTask) + ToolWidget (QWidget)
         ↕ lwmsgq 消息队列解耦
统一 IProtocolAdapter 接口 + ProtocolRegistry 工厂
         ↕ 可选部署能力：IDeployable（dynamic_cast 探测）
FtpAdapter / SshAdapter 均实现 → FtpDeployBackend 部署循环与协议无关
```

部署循环一次操作覆盖多台设备（逐台执行：连接 → 可选清空 → 上传 → 断开），进度实时更新、可随时取消，失败设备自动收集跳过、其余设备不受影响。

---

## 诚实的限制

- **SCP 未做**（v2.7 候选）：`IDeployable` 接口已就位，SCP 只需实现接口 + 注册协议键即可获得整套部署能力
- **无断点续传**：传输中断需重新部署
- **密钥认证待后续**：SFTP 部署目前用账号密码认证（与浏览路径一致），SSH 私钥登录未接通

---

## 验证清单（v2.6 端到端）

> 发布前在以下环境手工验证；凭据一律用环境变量（`DF_TEST_HOST` / `DF_TEST_USER` / `DF_TEST_PASS`）或 UI 手动输入，**不要写明文密码**。

### 环境

| 设备 | 地址 | 账号 |
|------|------|------|
| 云服务器 | 47.92.110.53 | root |
| 局域网 Ubuntu | 192.168.31.208 | 常规用户 |
| SylixOS 真机 | 用户 PLC 设备 | root |

### GUI 手动步骤

1. **连接**：协议下拉框选 SFTP → 连接 → 远程面板列出目标目录（云服务器为 `/root`）内容
2. **单文件部署**：部署 1 个文件到 `/root/tmp/` → 日志显示成功 → 远程面板刷新后可见
3. **文件夹递归部署**：部署 1 个含子目录的文件夹 → 远程面板确认目录结构递归创建
4. **部署前清空**：勾选「部署前清空」→ 目标目录内容被清空且**目录本身保留**
5. **取消**：上传进行中点击取消 → 传输停止且不崩溃，日志明确
6. **多设备**：2 台设备（云服务器 + 局域网 Ubuntu）同时部署 → 各自进度/结果正确
7. **失败隔离**：失败设备（如错误密码）→ 日志明确报错，其余设备不受影响

### SylixOS 真机

- 确认 SFTP 服务可用性（SylixOS 默认走 FTP，SSH/SFTP 需设备端支持）
- 行为差异（如有）记录到 CLAUDE.md 注意事项，参照 SylixOS FTP 适配经验的记录方式

---

## 总结

v2.6 不是新功能堆叠，而是把 v2.5 留下的最后一个传输缺口补上：

1. **SFTP 批量部署与 FTP 完全对等**——同一双栏 UI 一键切换，不用再为不同设备切换工具
2. **IDeployable 接口抽象**——部署能力与协议解耦，SCP 等新协议接入成本降到最低
3. **递归 mkdir 上传 + 清空保留目录 + 取消**——语义与 FTP 对齐，行为可预期

如果你也经常给 PLC 设备、Linux 工控机、云服务器批量传固件——FTP 和 SFTP 混着用的场景，这一版值得试试。

GitHub：[turnarond/DeviceForge](https://github.com/turnarond/DeviceForge)

---

*作者：turnarond | 2026-08-07*
