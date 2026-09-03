# DeviceForge 路线图 (Roadmap)

> 本文件描述 DeviceForge 的发展方向。计划会随社区反馈调整——**如果你正好需要某个功能,欢迎在 [Issues](../../issues) 提出或 👍,这会直接影响优先级。**

**当前版本**：v2.8.0 · **平台**：Windows（x64）· **许可**：MIT

---

## 已交付（至 v2.8.0）

| 模块 | 能力 |
|------|------|
| 文件部署 | FTP/FTPS + **SFTP** 并行批量部署（并发度可配 1-8，默认 1；IDeployable 统一单台事务），每设备实时进度、CSV/HTML 报告、失败设备一键重试、全局取消，TLS 加密，双栏远程文件管理（重命名/新建目录/精确对比/递归删除） |
| SFTP 文件管理 | 列目录/上传/下载/删除/重命名/新建目录，双栏 FTP/SFTP 协议一键切换（端口自动 21/22） |
| 批量命令 | Telnet / **SSH**（libssh2，密码认证 + TOFU）批量 Shell 命令 |
| Modbus 测试 | Modbus TCP 批量读写寄存器 |
| WebSocket 通信 | Server/Client，Token 认证 |
| **网络调试中继** | TCP/UDP/**组播** 透明中继旁路抓包 + 流量录制（`.nrec`）+ 按原始时序回放 + 组播回灌 + M→U/M→M 实时转发 |
| **OPC UA 客户端** | open62541 客户端：连接（None+匿名）+ 批量读/写节点 + DataChange 订阅 + 地址空间浏览 |
| **OTA 在线更新** | 主程序内检查 + 独立 Updater.exe 双进程替换（备份/回滚/重启） |
| **配置持久化** | ConfigStore（SQLite）+ DPAPI 凭证加密，设备/凭证/端点历史/Tool 设置持久化 |
| 日志统一 | 所有 Tool 日志统一路由到底部可折叠全局日志面板 |

### 版本节奏

- **v2.1**（2026-07-09）：Modbus Tool 迁移 + NetRelayTool 网络中继（录制回放 .nrec）+ SSH 适配器
- **v2.2**（2026-07-18）：OTA 在线更新 + 远端预览重构 + CMake 标准构建
- **v2.3**（2026-07-24）：ConfigStore 配置持久化（SQLite + DPAPI）+ OPC UA 订阅卡死修复
- **v2.4**（2026-07-26）：FTP 双栏重构 + 主窗口布局现代化（NavBar/胶囊设备栏/可折叠日志）+ 日志统一
- **v2.5**（2026-07-26）：远程文件管理补完（重命名/新建目录/精确对比）+ SFTP 文件管理 + SylixOS 适配（EPSV/MULTICWD/递归删除）
- **v2.6**（2026-08-07）：SFTP 批量部署——IDeployable 部署能力接口 + SshAdapter 部署链路 + 部署循环协议化（FTP/SFTP 同一逻辑）+ UI 解锁 + 双主题/紧凑密度
- **v2.7**（2026-08-18）：UX 收尾——远程列表/连接异步化（QtConcurrent + 代际令牌，慢速目录不冻结 UI）+ 面板源选择器（本地/FTP/SFTP 独立浏览）+ 系统文件拖入上传恢复 + 顺带项（readdir 错误上报/sort 降序 SWO/双主题像素验证）
- **v2.8**（2026-08-22）：并行批量部署——并发度 1-8（默认 1）+ 每设备实时进度 + CSV/HTML 部署报告 + 失败设备一键重试；顺带修复 FtpListParser 格式兼容和更新检查退出阻塞

底层架构：可扩展 Tool 框架（Backend + Widget）+ Protocol Adapter 抽象层 + IDeployable 部署能力接口 + 工业仪表盘深色主题（「琴色是动词」体系）。
产品化：自定义 app.ico + exe VERSIONINFO（turnarond/DeviceForge）+ 无 console（WIN32 子系统）。
测试：20 个 QtTest/CTest 目标（以 `tests/CMakeLists.txt` 的 `add_test()` 为准），覆盖录制回放、组播地址、配置/DPAPI、OPC UA、SFTP、部署循环与并发调度、部署报告、文件面板、主题、Modbus、Telnet、Updater 和 FTP LIST 解析。

---

## 近期候选

按当前评估的优先级排列。带 🔷 的是社区呼声可显著提前的项。

- **真实 FTP/SFTP 集成验证** — 在可控真实设备上覆盖连接、目录、上传、取消、并发与失败重试，保留可复查日志。
- **长稳验证** — 按验收计划执行 16 台 50 轮，持续采集内存、句柄、线程与连接指标；未取得证据前不宣称完成。
- **发布自动化闭环** — 运行并固化手动发布验收工作流证据，再由人工确认是否创建 GitHub Release/Tag。
- **SCP 部署上传** — 仅在出现真实设备需求时评估；SCP 无目录列表，不作为当前发布承诺。

## 中期

- **OPC UA 安全增强** — 首期为 None+匿名连接。后续按真实场景评估安全策略加密（Basic256Sha256）、用户名/证书认证。
- **网络中继安全增强** — 非回环绑定确认与客户端来源 allowlist（暴露到不可信网段时必需）。
- **🔷 Linux 平台适配** — 当前仅 Windows。工业/嵌入式场景（含 SylixOS）对 Linux 需求大，是跨平台化的关键一步。
- **插件化 DLL 加载** — 通过 `QPluginLoader` 支持第三方 Tool 以 DLL 形式动态加载（`ManifestParser` 清单解析已就绪）。
- **ToolHost 多 Tool 并发** — 当前 Tool 由主窗口直接创建，改为经 ToolHost 统一管理多活跃 Tool。

## 远期 / 探索

- 更多工业协议适配器（CANopen、EtherCAT 诊断等，视需求）
- 网络中继录制文件的 pcap 导出（供 Wireshark 分析）

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
