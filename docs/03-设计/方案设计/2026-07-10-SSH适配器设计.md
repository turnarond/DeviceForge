# SSH 适配器 + 批量命令扩展设计

- **日期**：2026-07-10
- **状态**：已确认（brainstorming 逐条确认）
- **模块**：`src/adapter/SshAdapter` + `src/tools/TelnetTool/` 扩展
- **前置模块**：`src/adapter/IProtocolAdapter.h`、`src/adapter/TelnetAdapter`

## 1. 背景与目标

现有「批量命令」Tab（TelnetTool）通过 Telnet 协议批量执行 Shell 命令。部分设备不支持 Telnet（或出于安全考虑禁用）,仅开放 SSH。需要增加 SSH 协议支持,在同一个 Tab 内提供 Telnet/SSH 选择,**复用现有命令输入和结果展示 UI**。

## 2. 需求确认清单

| # | 维度 | 结论 |
|---|------|------|
| 1 | 集成方式 | **扩展现有 Telnet Tab**（加 Telnet/SSH 协议选择,复用 UI） |
| 2 | SSH 库 | **libssh2**（C 库,MIT 许可,轻量,需导入） |
| 3 | 认证 | **仅密码认证**（用户名+密码） |
| 4 | 主机密钥 | **TOFU**（首次连接自动接受,Trust On First Use） |
| 5 | 协议粒度 | **Tab 级别选协议**（一次执行中所有设备统一协议） |
| 6 | 命令模型 | 复用现有模式:输入命令→批量执行→逐设备返回结果 |

## 3. 架构设计

### 3.1 新建 SshAdapter（实现 IProtocolAdapter）

```
IProtocolAdapter (纯虚接口)
  ├── FtpAdapter
  ├── TelnetAdapter
  └── SshAdapter (新增)
       ├── 内部使用 libssh2 session + channel
       ├── connect(DeviceInfo, AuthInfo) → libssh2 连接+认证
       ├── request(Request) → 打开 channel → exec(command) → 读取输出
       ├── subscribe(Request, StreamCb) → shell 模式持续读取（预留）
       └── disconnect() → 关闭 channel + 释放 session
```

### 3.2 扩展 TelnetBackend

现有 `TelnetBackend` 内部直接使用 `TelnetAdapter`。改造为**协议感知**:

- 新增成员 `QString m_selectedProtocol`（"telnet" / "ssh"）
- 新增方法 `void setProtocol(const QString& proto)` — Widget 在启动前调用
- `executeCommand` 内部根据 `m_selectedProtocol` 从 `ProtocolRegistry` 获取对应适配器（TelnetAdapter 或 SshAdapter）
- 其余逻辑（并发执行/设备遍历/回调/超时/取消）全部复用

### 3.3 扩展 TelnetWidget

在「批量命令」Tab 配置区加协议下拉:

```
协议: [Telnet ▼ / SSH ▼]
```

点击「执行」时把协议传给 Backend。原有 UI 布局（设备列表/命令输入/超时/结果表格）完全不变。

## 4. libssh2 集成

### 4.1 依赖引入

- **源码或预编译**：建议使用预编译 `libssh2.lib` + `libssh2.dll`（与 libcurl 集成方式一致,见 `lib/` 目录）
- **头文件**：`include/libssh2.h` 等放入 `include/`（已有 libcurl 头在此）
- **链接**：`CMakeLists.txt` 加 `find_library(LIBSSH2 ...)` + `target_link_libraries(... libssh2)`；`DeployMaster.vcxproj` 加 `.lib` 依赖
- **许可**：libssh2 是 BSD 3-Clause,与项目 MIT 兼容

### 4.2 运行时 DLL

- `libssh2.dll` 需随发布包分发（与 `libcurl-x64.dll` 同目录）
- CMake POST_BUILD 复制到输出目录

## 5. SshAdapter 关键实现要点

### 5.1 连接流程（connect）

```
socket = QTcpSocket(deviceIp, 22)
libssh2_session_init()
libssh2_session_handshake(socket)
→ 在此处收集 host key，TOFU 自动记录 fingerprint
libssh2_userauth_password(username, password)
→ 失败返回认证错误
```

### 5.2 命令执行（request）

```
channel = libssh2_channel_open_session(session)
libssh2_channel_exec(channel, command)
while (libssh2_channel_read(channel, buf)) → 累积输出
libssh2_channel_close(channel)
```

### 5.3 TOFU 主机密钥

`libssh2_session_hostkey(session, &type, &len)` 获取 fingerprint。首次连接记录到内存中的已知主机列表（`QSet<QString> m_knownHosts`）,后续连接校验。若 fingerprint 变化 → 返回错误"主机密钥变化,可能中间人攻击"。

> 首期不持久化 known_hosts 文件;每次运行重新审核。后续可加持久化选项。

### 5.4 超时与取消

- libssh2 阻塞操作的超时通过 `libssh2_session_set_timeout(session, timeoutMs)` 设置
- 取消:设置 `m_cancelled` atomic,在读取循环中检查并中止 channel

### 5.5 错误处理

- 连接失败:区分"主机不可达"、"认证失败"、"主机密钥变化"
- 命令执行失败:返回 exit code + stderr
- 错误信息通过 `lastError()` 返回,复制 TelnetAdapter 风格

## 6. 安全设计（与 Telnet 对齐）

| 项 | 处理 |
|----|------|
| 密码内存安全 | `AuthInfo::clear()` 已有 volatile 覆写;连接断开后调用 |
| 日志去敏感化 | SSH 命令输出不记录,仅记录字节数（与 Telnet 一致） |
| 主机密钥 | TOFU 自动接受;变化时告警（防 MITM） |
| 传输加密 | libssh2 内建加密（区别于 Telnet 明文）,无需额外处理 |

## 7. 文件变更清单

| 文件 | 动作 | 说明 |
|------|------|------|
| `src/adapter/SshAdapter.h/.cpp` | 新建 | IProtocolAdapter 的 SSH 实现 |
| `src/tools/TelnetTool/TelnetBackend.h/.cpp` | 修改 | 加 setProtocol + 适配器切换 |
| `src/tools/TelnetTool/TelnetWidget.h/.cpp` | 修改 | 加协议下拉 |
| `include/libssh2*.h` | 新建 | libssh2 头文件 |
| `lib/libssh2.lib` + `libssh2.dll` | 新建 | 预编译库（构建依赖） |
| `CMakeLists.txt` + `DeployMaster.vcxproj` | 修改 | libssh2 链接 |
| `main.cpp` | 修改 | ProtocolRegistry 注册 SshAdapter |
| `CLAUDE.md` / `README.md` / docs | 修改 | 文档同步 |

## 8. 测试策略

- **单元（无自动化）**：SshAdapter 依赖真实 SSH 服务器,自动化单测成本高。手动验证方案:本地起 OpenSSH 服务器 → 连接 + 执行 `echo hello` → 断言输出。后续可考虑用 Docker sshd 做 CI 测试。
- **集成测试**：与现有 Telnet 批量命令对比——同一批命令、同一批设备,分别走 Telnet 和 SSH,验证输出一致。
- **异常测试**：错误密码、错误端口、不可达主机、主机密钥变化——验证返回明确的错误信息而非崩溃。

## 9. 非目标（YAGNI）

- 密钥文件认证（首期仅密码）
- known_hosts 持久化文件
- SCP/SFTP 文件传输（文件部署已有 FTP,SCP 另议）
- SSH agent 转发
- SSH tunnel / 端口转发
- subscribe 流模式（预留接口,首期仅 request-响应）
