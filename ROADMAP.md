# DeviceForge 路线图 (Roadmap)

> 本文件描述 DeviceForge 的发展方向。计划会随社区反馈调整——**如果你正好需要某个功能,欢迎在 [Issues](../../issues) 提出或 👍,这会直接影响优先级。**

**当前版本**：v2.1.0 · **平台**：Windows（x64）· **许可**：MIT

---

## 已交付（v2.1.0）

| 模块 | 能力 |
|------|------|
| 文件部署 | FTP/FTPS 批量上传，TLS 加密 |
| 批量命令 | Telnet / **SSH**（libssh2，密码认证 + TOFU）批量 Shell 命令 |
| Modbus 测试 | Modbus TCP 批量读写寄存器 |
| WebSocket 通信 | Server/Client，Token 认证 |
| **网络调试中继** | TCP/UDP/**组播** 透明中继旁路抓包 + 流量录制（`.nrec`）+ 按原始时序回放 + 组播回灌 |
| 远端预览 | 远程 FTP 目录浏览 + 下载 |

底层架构：可扩展 Tool 框架（Backend + Widget）+ Protocol Adapter 抽象层 + 工业仪表盘深色主题（「琴色是动词」体系）。
产品化：自定义 app.ico + exe VERSIONINFO（turnarond/DeviceForge）+ 无 console（WIN32 子系统）。
测试：`tst_nrec`（QtTest/CTest，12 用例，覆盖 .nrec 往返/损坏拒绝/回放/组播往返）。

---

## 近期（v2.2 候选）

按当前评估的优先级排列。带 🔷 的是社区呼声可显著提前的项。

- **🔷 OPC UA 客户端真实实现** — 目前为演示模式（硬编码数据）。计划接入 open62541 或 Qt OPC UA 模块，补齐功能表最后一块。
- **网络中继增强** — 非回环绑定改为模态确认弹窗、连接背压节流（`setReadBufferSize` + `bytesToWrite`）、客户端来源 allowlist（暴露到不可信网段时必需）。
- **远端预览 SCP 支持** — UI 已预留协议选择，待接入 SCP/SFTP 后端。

## 中期

- **🔷 Linux 平台适配** — 当前仅 Windows。工业/嵌入式场景（含 SylixOS）对 Linux 需求大，是跨平台化的关键一步。
- **插件化 DLL 加载** — 通过 `QPluginLoader` 支持第三方 Tool 以 DLL 形式动态加载（`ManifestParser` 清单解析已就绪）。
- **ToolHost 多 Tool 并发** — 当前 Tool 由主窗口直接创建，改为经 ToolHost 统一管理多活跃 Tool。

## 远期 / 探索

- 更多工业协议适配器（CANopen、EtherCAT 诊断等，视需求）
- 网络中继录制文件的 pcap 导出（供 Wireshark 分析）
- 配置持久化与"记住密码"（本地加密存储，可选，含安全权衡说明）

---

## 不在计划内（Non-Goals）

保持工具聚焦,以下暂不考虑：

- 云端/SaaS 化——DeviceForge 定位为本地运维工具
- 完整 SCADA/组态功能——与 PLCBasicConfigurator 分工，本项目专注部署/测试/运维
- 中继数据注入/篡改——网络中继坚持"原样转发"语义

---

## 如何参与

- 有需求或 bug → [提 Issue](../../issues)
- 想贡献代码 → 见 [CONTRIBUTING.md](CONTRIBUTING.md)
- 想加某个功能 → 在对应 Issue 下 👍 或留言你的使用场景，真实场景比投票更有说服力

> 路线图不是承诺书,而是方向说明。实际节奏取决于维护精力与社区参与度。
