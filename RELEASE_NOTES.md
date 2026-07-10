# DeviceForge v2.1.0 Release Notes

> 2026-07-10 · [完整变更日志](CHANGELOG.md) · [路线图](ROADMAP.md)

---

## 本版本亮点

### 🔁 网络调试中继（NetRelayTool）

**TCP/UDP 透明中继旁路抓包 + 流量录制 + 按原始时序回放 + 组播**

- 数据生产者→工具→消费者双向原样转发，不影响生产链路
- 实时 Hex+ASCII 双栏视图（上行青绿/下行琥珀）
- 流量录制为 `.nrec` 自定义格式，**按原始时间间隔回放**上行数据到消费者
- **组播录制零影响**：以额外订阅者加入组播组抓收雷达/设备组播数据，源与现有消费者无感知
- 组播回灌原组（默认原组可改），**网卡选择**

### 🔐 SSH 批量命令

原「批量命令」Tab 新增 **Telnet / SSH 双协议切换**（libssh2）。SSH 密码认证 + TOFU 主机密钥，比明文 Telnet 更安全。

### 🎨 工业仪表盘主题（「琴色是动词」体系）

深色主题重构：强调色仅用于标记可操作/活跃状态，静止边框统一石墨。输入框凹陷·按钮凸起·容器扁平的形态语言——**哪里能输入、哪里能点击，一眼可辨**。

### 🏭 产品化就绪

- 自定义 `app.ico` 应用图标（任务栏 + 窗口左上角 + exe 图标）
- exe 版本/版权信息（DeviceForge / turnarond / 2.1.0）
- 双击无 console 黑框
- 首个单元测试目标 `tst_nrec`（12 用例）

---

## 功能模块（6 模块）

| 模块 | 协议 | 状态 |
|------|------|------|
| 文件部署 | FTP/FTPS | ✅ |
| 批量命令 | Telnet / **SSH** | ✅ |
| WebSocket 通信 | WS/WSS | ✅ |
| Modbus 测试 | Modbus TCP | ✅ |
| 网络调试中继 | TCP/UDP/**组播** + 录制回放 | ✅ |
| OPC UA 客户端 | OPC UA | ⚠ 演示模式 |

---

## 下载

- **预编译包**：`DeviceForge-v2.1.0-win64.zip`（Windows x64，解压即用，需 VC++ Redistributable）
- **源码**：`git clone https://github.com/turnarond/DeviceForge.git`（Qt 6.10.1 + CMake 3.16+）

## 系统要求

- Windows 10/11 x64
- [Visual C++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe)
