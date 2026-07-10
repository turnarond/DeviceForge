# NetRelayTool 组播录制回放设计（Multicast Record/Replay）

- **日期**：2026-07-10
- **状态**：已确认（brainstorming 逐条确认）
- **模块**：`src/tools/NetRelayTool/`
- **前序设计**：`docs/superpowers/specs/2026-07-08-net-relay-design.md`（TCP/UDP 中继 + 录制回放）

## 1. 背景与目标

NetRelayTool 目前支持 TCP/UDP 透明中继 + 录制回放。新增需求：**录制组播（multicast）数据，并能回灌**。

**核心场景**：抓取雷达组播数据包（雷达持续往组播组 `239.x.x.x:port` 推数据，若干显控/处理软件已加入该组在消费），要求**不影响正在使用的功能**。

**关键洞察**：组播是订阅制。工具作为**额外的订阅者**加入组播组（IGMP join），组播源与现有消费者对此**无感知**（交换机负责组播复制，源不知道有多少接收者）。因此"加入组 + 录制"对现有系统**零影响**——这正好满足"不影响正在使用"的核心约束。

## 2. 需求确认清单（brainstorming 结论）

| # | 维度 | 结论 |
|---|------|------|
| 1 | 核心能力 | 组播**录制**（加入组抄收落盘）+ **回灌**（按原始时序重发回组播组） |
| 2 | 录制影响 | 零影响（额外订阅者，源与现有消费者无感知） |
| 3 | 回放目标 | 默认回灌录制时的**原组播组**，UI 允许改为其他组地址/端口 |
| 4 | 网卡选择 | UI 提供网卡/本地 IP 选择（多网卡工控机必需，选错网卡收不到雷达包） |
| 5 | 录制格式 | 扩展现有 `.nrec`（protocol 加 Multicast=2，用现有 reserved 空间存组地址/端口） |
| 6 | 非目标 | 转发到另一个组播组、转单播给消费者——本期不做（YAGNI） |

## 3. 数据流模型

```
【录制阶段（雷达在线，零影响）】
雷达源 ──组播──▶ 239.x.x.x:port ──▶ 现有消费者(显控/处理)  ← 照常工作，无感知
                       │
                       └──▶ 工具(加入同组,额外订阅者) ──▶ 抄收落盘 .nrec

【回放阶段（雷达离线/测试台）】
工具 ──读 .nrec──▶ 按原始时序重发 ──组播──▶ 239.x.x.x:port ──▶ 现有消费者
                                          (此时真实雷达不发,无冲突,用录制流驱动消费者测试)
```

> 回放到原组播组仅在**真实源离线时**安全（否则录制流与真实流混叠）。UI 在回放时给出提示。

## 4. 架构设计

复用现有 NetRelayTool 的 Backend/Widget 双层 + RelayRecorder/RelayPlayer 单元，**最小侵入扩展**：

### 4.1 类型扩展（`NetRelayTypes.h`）
```cpp
enum class RelayProtocol { Tcp, Udp, Multicast };   // 新增 Multicast
```

### 4.2 Backend（`NetRelayBackend`）新增组播中继
- **组播录制**：新增 `startMulticastCapture(groupAddr, port, ifaceAddr)`
  - `new QUdpSocket`，`bind(QHostAddress::AnyIPv4, port, ShareAddress | ReuseAddressHint)`
    - **关键**：必须 `AnyIPv4`（不能用 `Any`，否则 IPv4 组播 join 失败）
    - **关键**：flag 必须是 `ShareAddress | ReuseAddressHint` 二者组合——**Windows 上 `ShareAddress` 被忽略，只有 `ReuseAddressHint` 生效**（映射到 `SO_REUSEADDR`）；Linux 反之。缺 `ReuseAddressHint` 会导致工具独占端口、现有消费者收不到数据，**直接违背"零影响"**
  - `joinMulticastGroup(QHostAddress(groupAddr), QNetworkInterface)`（指定网卡）
  - `readyRead` → readDatagram → 旁路回调(dataCb) + 录制(recordData，方向固定 Upstream)
  - 复用现有 `RelayMode{Idle,Relaying,Replaying}` 状态机（组播录制归入 Relaying）
- **组播回放**：委托 `RelayPlayer`（见 4.4）
- 组播只有"源→消费者"单向流，方向恒为 `Upstream`，无回程/会话表（区别于单播 UDP）

### 4.3 录制器（`RelayRecorder`）扩展文件头
- `open(path, RelayProtocol, startEpochMs, groupAddr="", groupPort=0)` 增加两个可选参数
- protocol 字段：Tcp=0/Udp=1/**Multicast=2**
- 用现有 **16 字节 reserved 空间**（偏移 16-31）存组播信息（不涨版本、不破坏兼容）：
  - 偏移 16：`group_port` (u16)
  - 偏移 18：`group_addr_len` (u8) + 偏移 19 起：组地址字符串（IPv4 点分，最长 15 字节，如 `239.255.0.1`）
  - 现有 TCP/UDP 文件这 16 字节仍为 0，读取时组播字段为空

### 4.4 播放器（`RelayPlayer`）扩展组播回放
- `start()` 识别 `NrecFile.protocol == Multicast`：
  - `new QUdpSocket`，`setMulticastInterface(iface)`，`writeDatagram(payload, groupAddr, groupPort)`
  - 目标 groupAddr/port 默认取文件头记录值，可被 UI 传入的覆盖值替换
  - 时序调度复用现有 `bytesWritten`/timer 逻辑（组播 UDP 无连接，直接按 tsOffset 差值发）

### 4.5 Widget（`NetRelayWidget`）UI 扩展
- 协议下拉增加 `Multicast` 选项
- 选 Multicast 时，配置区显示：组播组地址、端口、**网卡下拉**（`QNetworkInterface::allInterfaces()` 填充）
- 录制勾选/回放面板复用现有；回放面板增加"目标组地址（默认原组，可改）"
- 回放到原组时提示"请确认真实源已离线，避免数据混叠"

## 5. 关键技术点与风险

1. **端口共享（不抢占现有消费者）**：`bind` 必须用 `QAbstractSocket::ShareAddress | ReuseAddressHint` **二者组合**。**Windows 上 `ShareAddress` 被忽略，只有 `ReuseAddressHint` 生效**（映射 `SO_REUSEADDR`）；Linux 上则相反。只写 `ShareAddress` 在 Windows 会让工具独占端口，现有消费者收不到数据——直接违背"零影响"。这是本设计最关键的一行。且 `bind` 地址须为 `QHostAddress::AnyIPv4`（用 `Any` 会因 IPv6 双栈导致 IPv4 组播 join 失败）。
2. **网卡指定**：`joinMulticastGroup(group, iface)` 和回放的 `setMulticastInterface(iface)` 都要用用户选的网卡；多网卡机器不指定会加入到错误网卡收不到数据。
3. **IPv4 校验**：组地址须在 `224.0.0.0`~`239.255.255.255`（D 类），UI/Backend 校验非法输入 fail-closed（复用现有绑定校验风格）。
4. **回放混叠风险**：回灌原组时若真实源仍在发，消费者收到双份——UI 提示，不做硬阻止（无法可靠检测源是否在线）。
5. **格式兼容**：扩展用 reserved 空间，version 不变；旧 `.nrec`（TCP/UDP）读取不受影响，组播字段为空。

## 6. 文件变更清单

| 文件 | 动作 | 说明 |
|------|------|------|
| `NetRelayTypes.h` | 修改 | RelayProtocol 加 Multicast |
| `RelayRecorder.h/.cpp` | 修改 | open 增组地址/端口参数，写入 reserved 区 |
| `RelayRecording.h/.cpp` | 修改 | 读取组地址/端口到 NrecFile |
| `NetRelayBackend.h/.cpp` | 修改 | startMulticastCapture + 组播回放委托 |
| `RelayPlayer.h/.cpp` | 修改 | 组播 writeDatagram 回放路径 |
| `NetRelayWidget.h/.cpp` | 修改 | 协议下拉 + 组播配置(组地址/端口/网卡) + 回放目标组 |
| `tests/NetRelayTool/tst_nrec.cpp` | 修改 | 组播 .nrec 往返测试 + 组地址读写 |
| CLAUDE.md / README / docs | 修改 | 同步组播能力 |

## 7. 测试策略

- **单元（tst_nrec）**：组播 `.nrec` 往返——写入 protocol=Multicast + 组地址 `239.1.2.3:5000` + 数据，读回校验组地址/端口/protocol/payload 一致；旧 UDP 文件读取仍正常（回归）。
- **端到端（手动/半自动）**：本机起一个组播发送器往 `239.x:port` 发 → 工具加入组录制 → 停止 → 回放到同组 → 另一个组播接收器收到相同数据序列。
- **零影响验证**：工具加入组期间，用第二个接收者确认仍能正常收到源数据（ShareAddress 生效）。

## 8. 非目标（YAGNI）

- 转发到另一个组播组（跨组桥接）——预留 RelayProtocol 可扩展，本期不做
- 转单播给指定消费者——需改消费端，违背"不影响现有"，不做
- IPv6 组播——现有雷达场景为 IPv4，不做
- 源在线检测/自动防混叠——技术上不可靠，仅 UI 提示
