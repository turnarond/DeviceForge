# DeviceForge 安全说明

## 加密传输

### FTPS (FTP over TLS)

在"文件部署" Tab 中勾选「FTPS 加密传输」复选框。启用后：

- 所有 FTP 连接使用 TLS 加密（`CURLOPT_USE_SSL = CURLUSESSL_ALL`）
- 证书验证开启（`CURLOPT_SSL_VERIFYPEER = 1L`）
- libcurl 协议白名单限制为 `ftp,ftps`（防止协议走私）
- 密码和文件内容在传输过程中加密

**注意**：目标设备必须支持 FTPS 协议。不支持的老旧设备请仅在隔离的 OT 网络中使用。

### WebSocket

- Server 模式默认绑定 `127.0.0.1`（仅本地可访问）
- 可配置 Token 认证：客户端连接时附加 `?token=your_token`
- 消息内容不记录到日志（仅记录字节数）

### Telnet

Telnet 协议不提供加密保护。使用时请注意：

- 首次使用时弹出安全警告对话框
- 用户名和密码以明文传输
- 建议仅在隔离的工业控制网络（OT）中使用
- 推荐优先使用 SSH（后续版本支持）

## 密码管理

- **内存安全擦除**：断开连接时自动擦除内存中的密码（`volatile` 写入 + `clear()`）
- **不持久化密码**：密码不保存到磁盘或配置文件
- **日志不记录密码**：Telnet/WebSocket 消息内容不在日志中记录

## 协议安全

| 协议 | 加密 | 认证 | 说明 |
|------|------|------|------|
| FTP | ❌ 明文 | 用户名/密码 | 建议启用 FTPS |
| FTPS | ✅ TLS | 用户名/密码 | 推荐 |
| Telnet | ❌ 明文 | 用户名/密码 | 仅限 OT 网络 |
| WebSocket | 可选 TLS | 可选 Token | Server 绑定 localhost |
| Modbus TCP | ❌ 无 | 无 | 协议本身无安全机制 |

## 已知限制

- FTPS 证书验证依赖系统 CA 证书库（Windows 内置）
- WebSocket WSS 模式 SSL 证书配置待完善
- 无内置暴力破解防护（建议通过网络防火墙控制）

## 报告安全问题

如发现安全漏洞，请通过 GitHub Issue 报告（标记 `security` 标签）或联系维护者。

## OWASP 安全审查

2026-07-05 完成全代码库 OWASP Top 10 安全审查。3 个 CRITICAL + 8 个 HIGH 问题已全部修复。详见相应 Pull Request。
