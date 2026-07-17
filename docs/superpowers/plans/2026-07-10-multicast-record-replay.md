# NetRelayTool 组播录制回放 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在现有 TCP/UDP 中继基础上，新增组播（multicast）数据的录制与回灌能力，核心场景为抓雷达组播包且不影响现有消费者。

**Architecture:** 复用现有 NetRelayTool 双层架构与 RelayRecorder/RelayRecording/RelayPlayer/RelayMode 单元，最小侵入扩展：`.nrec` 格式加 protocol=Multicast(2) 并用文件头 reserved 区存组地址/端口；Backend 新增组播抓收；Player 新增组播回灌；Widget 加组播协议选项 + 网卡选择。

**Tech Stack:** C++17、Qt 6.10.1 (QUdpSocket 组播 API / QNetworkInterface)、QDataStream(LittleEndian)、QtTest + CTest。

## Global Constraints

- 端口共享（不抢占现有消费者，"零影响"命门）：组播抓收 socket `bind(QHostAddress::AnyIPv4, port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)`——**二者组合必须都写**：Windows 只有 `ReuseAddressHint` 生效（`ShareAddress` 被忽略），Linux 反之。缺 `ReuseAddressHint` 会在 Windows 独占端口，现有消费者收不到数据。
- 组播 bind 地址必须 `QHostAddress::AnyIPv4`（不能用 `Any`，IPv6 双栈会导致 IPv4 组播 join 失败）。
- 组播地址须为 IPv4 D 类（`224.0.0.0`~`239.255.255.255`），非法输入 fail-closed 拒绝。
- 网卡：`joinMulticastGroup(group, iface)` 与回放 `setMulticastInterface(iface)` 都用用户选定的 `QNetworkInterface`。
- 组播为单向流，录制方向恒为 `RelayDirection::Upstream`，无回程/会话表。
- `.nrec` 扩展**不涨 version**（仍为 `nrec::kVersion`=1）：用现有文件头 16 字节 reserved 区（偏移 16-31）存组播信息，旧 TCP/UDP 文件该区为 0、读取兼容。
- 命名：类 PascalCase、方法 camelCase、成员 `m_camelCase`、常量 `kXxx`。
- 非 QObject 约定：Recorder/Recording/Player/Backend 均纯 C++ 类；Qt 对象裸指针 + `new`，`QObject::connect` 用无 context lambda 重载。
- 跨线程：Backend→Widget 回调经 `QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection)`。
- 双构建：新代码无新增文件（全为现有文件扩展），无需改 CMakeLists/vcxproj 的源清单；测试仍走 CMake `tst_nrec`。
- 环境：跑测试/程序前 `export PATH="/c/Qt/6.10.1/msvc2022_64/bin:$PATH"`；输出目录需 `libcurl.dll`（POST_BUILD 自动复制）。
- 设计依据：`docs/superpowers/specs/2026-07-10-multicast-record-replay-design.md`。

---

## 文件结构（全部为现有文件扩展，无新建）

| 文件 | 动作 | 责任 |
|------|------|------|
| `NetRelayTypes.h` | 修改 | RelayProtocol 加 Multicast |
| `RelayRecorder.h/.cpp` | 修改 | open 增组地址/端口参数，写入 reserved 区 |
| `RelayRecording.h/.cpp` | 修改 | NrecFile 加组地址/端口字段，load 读取 |
| `RelayPlayer.h/.cpp` | 修改 | 组播回灌路径（setMulticastInterface + writeDatagram 到组地址） |
| `NetRelayBackend.h/.cpp` | 修改 | startMulticastCapture + 组播录制钩子 + 回放委托传组信息 |
| `NetRelayWidget.h/.cpp` | 修改 | 协议下拉加 Multicast + 组播配置(组地址/端口/网卡) + 回放目标组 |
| `tests/NetRelayTool/tst_nrec.cpp` | 修改 | 组播 .nrec 往返 + 旧文件兼容回归 |
| `CLAUDE.md` / `README.md` / docs | 修改 | 同步组播能力 |

---

## Task 1: RelayProtocol 加 Multicast + .nrec 格式读写扩展

**Files:**
- Modify: `src/tools/NetRelayTool/NetRelayTypes.h`
- Modify: `src/tools/NetRelayTool/RelayRecorder.h`, `RelayRecorder.cpp`
- Modify: `src/tools/NetRelayTool/RelayRecording.h`, `RelayRecording.cpp`
- Test: `tests/NetRelayTool/tst_nrec.cpp`

**Interfaces:**
- Produces:
  - `enum class RelayProtocol { Tcp, Udp, Multicast };`
  - `RelayRecorder::open(const QString& path, RelayProtocol proto, qint64 startEpochMs, const QString& groupAddr = QString(), quint16 groupPort = 0)`
  - `struct NrecFile { RelayProtocol protocol; qint64 startEpochMs; QString groupAddr; quint16 groupPort; QVector<NrecRecord> records; };`
  - `RelayRecording::load(const QString& path, NrecFile& out, QString& error)`（签名不变，out 多填 groupAddr/groupPort）

- [ ] **Step 1: 写失败测试（组播 .nrec 往返 + 旧文件兼容）**

在 `tests/NetRelayTool/tst_nrec.cpp` 的 `TstNrec` 类加两个 private slot：
```cpp
    void multicastRoundTrip() {
        QString path = QDir::temp().filePath("tst_mcast.nrec");
        RelayRecorder rec;
        QVERIFY(rec.open(path, RelayProtocol::Multicast, 5000, "239.1.2.3", 6000));
        rec.append(RelayDirection::Upstream, 1, 0,  QByteArray("radar-a"));
        rec.append(RelayDirection::Upstream, 1, 33, QByteArray("radar-b"));
        rec.close();

        NrecFile f; QString err;
        QVERIFY2(RelayRecording::load(path, f, err), qPrintable(err));
        QCOMPARE(f.protocol, RelayProtocol::Multicast);
        QCOMPARE(f.groupAddr, QString("239.1.2.3"));
        QCOMPARE(f.groupPort, quint16(6000));
        QCOMPARE(f.records.size(), 2);
        QCOMPARE(f.records[0].payload, QByteArray("radar-a"));
        QCOMPARE(f.records[1].tsOffsetMs, qint64(33));
        QFile::remove(path);
    }

    void udpFileStillLoadsAfterMulticastExtension() {
        // 旧式 UDP 录制（不传组参数）读取应正常，组字段为空
        QString path = QDir::temp().filePath("tst_udp_compat.nrec");
        RelayRecorder rec;
        QVERIFY(rec.open(path, RelayProtocol::Udp, 1000));
        rec.append(RelayDirection::Upstream, 1, 0, QByteArray("x"));
        rec.close();
        NrecFile f; QString err;
        QVERIFY2(RelayRecording::load(path, f, err), qPrintable(err));
        QCOMPARE(f.protocol, RelayProtocol::Udp);
        QVERIFY(f.groupAddr.isEmpty());
        QCOMPARE(f.groupPort, quint16(0));
        QCOMPARE(f.records.size(), 1);
        QFile::remove(path);
    }
```

- [ ] **Step 2: 运行确认失败**

Run: `cd /d/persional/QtDeployMaster/build && cmake --build . --config Release --target tst_nrec 2>&1 | tail -5`
Expected: 编译失败（RelayProtocol::Multicast 未定义 / open 参数不匹配 / NrecFile 无 groupAddr）。

- [ ] **Step 3: NetRelayTypes.h 加 Multicast**

```cpp
enum class RelayProtocol { Tcp, Udp, Multicast };
```

- [ ] **Step 4: RelayRecorder.h open 加参数**

```cpp
    bool open(const QString& path, RelayProtocol proto, qint64 startEpochMs,
              const QString& groupAddr = QString(), quint16 groupPort = 0);
```

- [ ] **Step 5: RelayRecorder.cpp 写入组信息到 reserved 区**

把 `open` 里写文件头的部分改为（protocol 支持 3 值，reserved 区存组端口+地址）：
```cpp
    // --- 文件头（32 字节）---
    m_stream->writeRawData(nrec::kMagic, 4);                 // 0: magic
    *m_stream << quint16(nrec::kVersion);                    // 4: version
    quint8 protoByte = (proto == RelayProtocol::Tcp) ? 0
                     : (proto == RelayProtocol::Udp) ? 1 : 2; // 6: protocol (2=Multicast)
    *m_stream << protoByte;
    *m_stream << quint8(0);                                  // 7: reserved
    *m_stream << qint64(startEpochMs);                       // 8: start_epoch_ms
    // 16..31: reserved 区，组播时存 group_port(u16) + addr_len(u8) + addr[13]
    QByteArray addrBytes = groupAddr.toLatin1();
    if (addrBytes.size() > 13) addrBytes.truncate(13);       // IPv4 点分最长 15，此处上限 13 够 239.255.255.255=15? 见注
    *m_stream << quint16(groupPort);                         // 16: group_port
    *m_stream << quint8(addrBytes.size());                   // 18: addr_len
    m_stream->writeRawData(addrBytes.constData(), addrBytes.size()); // 19..: addr
    int pad = 16 - 2 - 1 - addrBytes.size();                 // 补齐到 32 字节
    for (int i = 0; i < pad; ++i) *m_stream << quint8(0);
    return true;
```
> **注意**：IPv4 点分十进制最长是 `255.255.255.255` = 15 字节，但 reserved 区在 port(2)+len(1) 后仅剩 13 字节。组播地址范围 `224.*`~`239.*`，最长如 `239.255.255.255` = 15 字节 > 13。**因此 reserved 16 字节不够存最长地址**。解决：port 用偏移 16(u16)，len 用偏移 18(u8)，地址从偏移 19 开始最多 13 字节——**改为存"地址去掉固定前缀"不可靠**。正确做法见 Step 5b。

- [ ] **Step 5b: 修正——地址存储改用足够空间**

reserved 只有 16 字节，存不下 port+len+最长 15 字节地址（需 18 字节）。**改用方案**：地址用 4 字节存 IPv4 二进制（`QHostAddress::toIPv4Address()` 返回 quint32），而非点分字符串：
```cpp
    // 16..31: reserved 区 → 组播: group_port(u16) + group_ipv4(u32) + 剩余保留
    *m_stream << quint16(groupPort);                          // 16: group_port
    quint32 ipv4 = 0;
    if (!groupAddr.isEmpty()) {
        QHostAddress a(groupAddr);
        ipv4 = a.toIPv4Address();                             // 点分→u32；非法为 0
    }
    *m_stream << quint32(ipv4);                               // 18: group_ipv4 (big-endian value, stored LE)
    for (int i = 0; i < 10; ++i) *m_stream << quint8(0);      // 22..31: reserved (2+4+10=16)
    return true;
```
用这个版本替换 Step 5 的 reserved 写入部分（Step 5 的注意说明保留作为决策记录）。需 `#include <QHostAddress>` 到 RelayRecorder.cpp。

- [ ] **Step 6: RelayRecording.h NrecFile 加字段**

```cpp
struct NrecFile {
    RelayProtocol       protocol = RelayProtocol::Tcp;
    qint64              startEpochMs = 0;
    QString             groupAddr;          // 组播组地址（点分），非组播为空
    quint16             groupPort = 0;      // 组播端口，非组播为 0
    QVector<NrecRecord> records;
};
```

- [ ] **Step 7: RelayRecording.cpp load 读取组信息 + protocol=2**

在读文件头处：protocol 支持 2=Multicast；reserved 区读组端口/地址。找到读 protocol 和 reserved 的部分，改为：
```cpp
    quint16 version; quint8 proto, r0;
    in >> version >> proto >> r0;
    if (version != nrec::kVersion) { error = QString("不支持的版本: %1").arg(version); return false; }
    qint64 startEpoch; in >> startEpoch;
    // reserved 区（16 字节）：组播时含 port(u16)+ipv4(u32)
    quint16 groupPort; quint32 groupIpv4;
    in >> groupPort >> groupIpv4;
    for (int i = 0; i < 10; ++i) { quint8 x; in >> x; }      // 剩余 reserved

    out.protocol = (proto == 0) ? RelayProtocol::Tcp
                 : (proto == 1) ? RelayProtocol::Udp
                 : RelayProtocol::Multicast;
    out.startEpochMs = startEpoch;
    if (out.protocol == RelayProtocol::Multicast) {
        out.groupPort = groupPort;
        out.groupAddr = QHostAddress(groupIpv4).toString();
    } else {
        out.groupPort = 0;
        out.groupAddr.clear();
    }
```
需 `#include <QHostAddress>` 到 RelayRecording.cpp。旧 UDP 文件 reserved 区为 0 → groupPort=0/groupIpv4=0，protocol!=Multicast 时忽略，兼容。

- [ ] **Step 8: 在 tests/CMakeLists.txt 确认 RelayRecorder/RelayRecording 已在 tst_nrec 源列表**

Run: `grep -n "RelayRecorder\|RelayRecording" /d/persional/QtDeployMaster/tests/CMakeLists.txt`
Expected: 两者都已在 `add_executable(tst_nrec ...)`（前序任务已加）。若缺则补。

- [ ] **Step 9: 运行测试确认通过**

Run: `cd /d/persional/QtDeployMaster/build && cmake --build . --config Release --target tst_nrec && export PATH="/c/Qt/6.10.1/msvc2022_64/bin:$PATH" && ./tests/Release/tst_nrec.exe -o /tmp/r.txt,txt; cat /tmp/r.txt | grep -E "PASS|FAIL"`
Expected: `multicastRoundTrip`、`udpFileStillLoadsAfterMulticastExtension` + 所有旧用例 PASS。

- [ ] **Step 10: 主构建确认**

Run: `cd /d/persional/QtDeployMaster/build && cmake --build . --config Release 2>&1 | tail -3`
Expected: 0 error（RelayProtocol 加枚举值后，现有 switch 若有 -Werror 可能警告，检查 Backend/Player 现有对 protocol 的分支——本任务只加格式读写，Backend/Player 的 Multicast 分支在 Task 2/3 加；若现有代码有 `switch(protocol)` 无 default 导致编译警告，本步不处理，Task 2/3 覆盖）。

- [ ] **Step 11: Commit**

```bash
cd /d/persional/QtDeployMaster
git add src/tools/NetRelayTool/NetRelayTypes.h src/tools/NetRelayTool/RelayRecorder.* src/tools/NetRelayTool/RelayRecording.* tests/
git commit -m "feat: .nrec 支持组播(protocol=Multicast + reserved 区存组地址/端口) + 单测"
```

---

## Task 2: Backend 组播抓收

**Files:**
- Modify: `src/tools/NetRelayTool/NetRelayBackend.h`, `NetRelayBackend.cpp`

**Interfaces:**
- Consumes: `RelayProtocol::Multicast`, `RelayRecorder::open(...groupAddr, groupPort)`。
- Produces:
  - `void startMulticastCapture(const QString& groupAddr, quint16 port, const QString& ifaceAddr)`
  - 复用现有 `stopRelay()` 停止（需能清理组播 socket）
  - 复用现有 `enableRecording/disableRecording`、`isRunning()`、dataCb/logCb/errorCb

- [ ] **Step 1: 头文件加成员与方法**

`NetRelayBackend.h` 顶部 include 加 `#include <QNetworkInterface>`。公有区加：
```cpp
    // 组播抓收（加入组播组，抄收数据 + 可选录制；对源与现有消费者零影响）
    void startMulticastCapture(const QString& groupAddr, quint16 port, const QString& ifaceAddr);
```
私有区加成员：
```cpp
    QUdpSocket*  m_mcastSocket = nullptr;   // 组播抓收 socket
    QString      m_mcastGroup;              // 当前组播组地址
    quint16      m_mcastPort = 0;           // 当前组播端口
    void onMulticastReadyRead();            // 组播数据到达
```

- [ ] **Step 2: 实现 startMulticastCapture**

`NetRelayBackend.cpp` 顶部确认 `#include <QNetworkInterface>` `#include <QHostAddress>`。加实现：
```cpp
void NetRelayBackend::startMulticastCapture(const QString& groupAddr, quint16 port,
                                            const QString& ifaceAddr)
{
    if (m_running) { reportError("已在运行中，请先停止"); return; }
    if (m_mode == RelayMode::Replaying) { reportError("回放进行中，无法抓收"); return; }

    // 组播地址校验：IPv4 D 类 224.0.0.0 ~ 239.255.255.255
    QHostAddress group(groupAddr);
    quint32 v4 = group.toIPv4Address();
    bool okProto = (group.protocol() == QAbstractSocket::IPv4Protocol);
    quint8 firstOctet = quint8((v4 >> 24) & 0xFF);
    if (!okProto || firstOctet < 224 || firstOctet > 239) {
        reportError("非法组播地址（须 224.0.0.0~239.255.255.255）: " + groupAddr.toStdString());
        return;
    }
    if (port == 0) { reportError("组播端口无效"); return; }

    // 选定网卡（按本地 IP 匹配）
    QNetworkInterface iface;
    if (!ifaceAddr.isEmpty()) {
        for (const QNetworkInterface& itf : QNetworkInterface::allInterfaces()) {
            for (const QNetworkAddressEntry& e : itf.addressEntries()) {
                if (e.ip().toString() == ifaceAddr) { iface = itf; break; }
            }
            if (iface.isValid()) break;
        }
    }

    m_mcastSocket = new QUdpSocket();
    // 关键：AnyIPv4（非 Any）+ ShareAddress|ReuseAddressHint（Windows 只认后者，不抢占现有消费者）
    if (!m_mcastSocket->bind(QHostAddress::AnyIPv4, port,
            QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        reportError("组播端口绑定失败: " + m_mcastSocket->errorString().toStdString());
        delete m_mcastSocket; m_mcastSocket = nullptr;
        return;
    }
    bool joined = iface.isValid()
        ? m_mcastSocket->joinMulticastGroup(group, iface)
        : m_mcastSocket->joinMulticastGroup(group);
    if (!joined) {
        reportError("加入组播组失败: " + m_mcastSocket->errorString().toStdString());
        m_mcastSocket->close(); delete m_mcastSocket; m_mcastSocket = nullptr;
        return;
    }
    QObject::connect(m_mcastSocket, &QUdpSocket::readyRead,
                     [this]() { onMulticastReadyRead(); });

    m_mcastGroup = groupAddr;
    m_mcastPort = port;
    m_running = true;
    m_mode = RelayMode::Relaying;

    // 录制：组播录制把组地址/端口写入 .nrec 头
    if (m_recordEnabled && !m_recordPath.isEmpty()) {
        m_recorder = std::make_unique<RelayRecorder>();
        m_recordElapsed.start();
        qint64 epoch = QDateTime::currentMSecsSinceEpoch();
        if (!m_recorder->open(m_recordPath, RelayProtocol::Multicast, epoch, groupAddr, port)) {
            reportError("录制文件打开失败: " + m_recordPath.toStdString());
            m_recorder.reset();
        } else {
            log("录制已开启: " + m_recordPath.toStdString());
        }
    }
    log("[组播] 已加入 " + groupAddr.toStdString() + ":" + std::to_string(port)
        + "（额外订阅者，对源与现有消费者无影响）");
}

void NetRelayBackend::onMulticastReadyRead()
{
    if (m_cancelled || !m_mcastSocket) return;
    while (m_mcastSocket->hasPendingDatagrams()) {
        QByteArray data;
        data.resize(int(m_mcastSocket->pendingDatagramSize()));
        QHostAddress sender; quint16 senderPort = 0;
        m_mcastSocket->readDatagram(data.data(), data.size(), &sender, &senderPort);
        if (data.isEmpty()) continue;
        QString peer = sender.toString() + ":" + QString::number(senderPort);
        if (m_dataCb) m_dataCb(RelayDirection::Upstream, peer, 1, data);  // 组播单向，方向恒 Upstream，会话号固定 1
        recordData(RelayDirection::Upstream, 1, data);
    }
}
```

- [ ] **Step 3: stopRelay 清理组播 socket**

在 `stopRelay()` 里（清理 TCP/UDP 之后、`m_running=false` 之前）加组播清理：
```cpp
    if (m_mcastSocket) {
        QObject::disconnect(m_mcastSocket, nullptr, nullptr, nullptr);
        if (!m_mcastGroup.isEmpty())
            m_mcastSocket->leaveMulticastGroup(QHostAddress(m_mcastGroup));
        m_mcastSocket->close();
        m_mcastSocket->deleteLater();
        m_mcastSocket = nullptr;
    }
    m_mcastGroup.clear();
    m_mcastPort = 0;
```

- [ ] **Step 4: 构建验证**

Run: `cd /d/persional/QtDeployMaster/build && cmake --build . --config Release 2>&1 | tail -3`
Expected: 0 error。

- [ ] **Step 5: 端到端手动验证（组播录制）**

在 Git Bash 三终端（或 Python 脚本）：
1. 组播发送器：`python -c "import socket,time; s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); [s.sendto(b'radar%d'%i,('239.1.2.3',6000)) or time.sleep(0.1) for i in range(20)]"`
2. 运行 DeviceForge，网络调试 Tab（Task 4 UI 完成后）选 Multicast、组 `239.1.2.3:6000`、勾录制。
> 本步在 Task 4 UI 完成后才能完整点验；本任务仅确认编译通过 + 代码审查，端到端点验推迟到 Task 4。标注为"待 Task 4 UI"。

- [ ] **Step 6: Commit**

```bash
cd /d/persional/QtDeployMaster
git add src/tools/NetRelayTool/NetRelayBackend.*
git commit -m "feat: Backend 组播抓收(加入组+ShareAddress|ReuseAddressHint 零影响+录制)"
```

---

## Task 3: RelayPlayer 组播回灌

**Files:**
- Modify: `src/tools/NetRelayTool/RelayPlayer.h`, `RelayPlayer.cpp`
- Modify: `src/tools/NetRelayTool/NetRelayBackend.cpp`（startReplay 传网卡给 player）

**Interfaces:**
- Consumes: `NrecFile.protocol==Multicast`, `NrecFile.groupAddr/groupPort`。
- Produces:
  - RelayPlayer 支持组播回灌：`start()` 内识别 Multicast，用 `setMulticastInterface` + `writeDatagram(payload, groupAddr, groupPort)`
  - 新增 `void setMulticastInterface(const QString& ifaceAddr)`（回放前由 Backend 设置）
  - 回放目标组：默认取文件头 groupAddr/port，可被 `start()` 传入的 consumerHost/consumerPort 覆盖（非空/非0时）

- [ ] **Step 1: RelayPlayer.h 加网卡设置 + 成员**

include 加 `#include <QNetworkInterface>`。公有加：
```cpp
    void setMulticastInterface(const QString& ifaceAddr) { m_mcastIfaceAddr = ifaceAddr; }
```
私有成员加：
```cpp
    QString m_mcastIfaceAddr;   // 组播回灌网卡本地 IP（空=默认）
```

- [ ] **Step 2: RelayPlayer.cpp start() 加组播分支**

在 `start()` 里，`RelayRecording::load` 成功后、原有 TCP/UDP socket 建立逻辑处，加 Multicast 分支。找到设置 `m_protocol` 与目标地址的部分，改为：
```cpp
    m_protocol = m_file.protocol;

    // 目标地址：组播默认取文件头组地址/端口，consumerHost/Port 非空则覆盖
    if (m_protocol == RelayProtocol::Multicast) {
        QString tgtAddr = consumerHost.isEmpty() ? m_file.groupAddr : consumerHost;
        quint16 tgtPort = (consumerPort == 0) ? m_file.groupPort : consumerPort;
        m_consumerAddr = QHostAddress(tgtAddr);
        m_consumerPort = tgtPort;
    } else {
        m_consumerAddr = QHostAddress(consumerHost);
        m_consumerPort = consumerPort;
    }
```
在建立 socket 的分支（现有有 TCP/UDP 两路）加组播路：
```cpp
    if (m_protocol == RelayProtocol::Multicast) {
        m_udp = new QUdpSocket();
        if (!m_mcastIfaceAddr.isEmpty()) {
            for (const QNetworkInterface& itf : QNetworkInterface::allInterfaces()) {
                for (const QNetworkAddressEntry& e : itf.addressEntries()) {
                    if (e.ip().toString() == m_mcastIfaceAddr) {
                        m_udp->setMulticastInterface(itf); break;
                    }
                }
            }
        }
        m_active = true;
        scheduleNext();   // 组播无连接，直接开调度（复用现有 timer 时序逻辑）
        return true;
    }
```
> 组播回灌走 `m_udp` + `writeDatagram`，与现有 UDP 回放路径一致，仅多一步 `setMulticastInterface`。

- [ ] **Step 3: sendCurrent() 组播发送复用 UDP 路径**

确认 `sendCurrent()` 里 UDP 发送用的是 `m_udp->writeDatagram(rec.payload, m_consumerAddr, m_consumerPort)`——组播地址走同一句即可（组播地址就是目标地址）。若现有 sendCurrent 对 UDP/组播无区分，无需改；确认一下：
Run: `grep -n "writeDatagram\|m_protocol" src/tools/NetRelayTool/RelayPlayer.cpp`
若 `sendCurrent` 里按 `m_protocol==Udp` 分支，改为 `m_protocol==Udp || m_protocol==Multicast` 都走 writeDatagram。

- [ ] **Step 4: Backend startReplay 传网卡给 player**

在 `NetRelayBackend::startReplay` 里，创建 player 后、`m_player->start(...)` 前，若需要网卡则设置。startReplay 需增加一个网卡参数（或复用成员）。最小改动：给 startReplay 加默认参数 `const QString& mcastIfaceAddr = QString()`，在 `.h` 与 `.cpp` 同步，并：
```cpp
    m_player->setMulticastInterface(mcastIfaceAddr);
```
放在其他 setXxxCallback 之后、`m_player->start(...)` 之前。

- [ ] **Step 5: 构建验证**

Run: `cd /d/persional/QtDeployMaster/build && cmake --build . --config Release 2>&1 | tail -3`
Expected: 0 error。

- [ ] **Step 6: 端到端手动验证（组播回灌）**

> 完整点验需 Task 4 UI。本任务确认编译 + 代码审查；端到端（录制→回放→组播接收器收到相同序列）推迟 Task 4 一并做。

- [ ] **Step 7: Commit**

```bash
cd /d/persional/QtDeployMaster
git add src/tools/NetRelayTool/RelayPlayer.* src/tools/NetRelayTool/NetRelayBackend.*
git commit -m "feat: RelayPlayer 组播回灌(setMulticastInterface + 默认原组可改)"
```

---

## Task 4: Widget UI 组播支持

**Files:**
- Modify: `src/tools/NetRelayTool/NetRelayWidget.h`, `NetRelayWidget.cpp`

**Interfaces:**
- Consumes: Backend `startMulticastCapture(groupAddr, port, ifaceAddr)`、`startReplay(nrecPath, consumerHost, consumerPort, speedFactor, mcastIfaceAddr)`。

- [ ] **Step 1: 协议下拉加 Multicast + 网卡下拉成员**

`NetRelayWidget.h` include 加 `#include <QNetworkInterface>`。私有控件加：
```cpp
    QComboBox* m_comboIface = nullptr;   // 网卡选择（组播用）
```
（组地址/端口复用现有 m_editUpstreamHost/m_spinUpstreamPort 或新增；见 Step 2 决策。）

- [ ] **Step 2: setupUi 协议下拉加项 + 网卡下拉填充**

找到协议下拉初始化（现有 `m_comboProtocol->addItem("TCP"); addItem("UDP");`），加：
```cpp
    m_comboProtocol->addItem("Multicast");
```
在配置区加网卡下拉（填充所有含 IPv4 的网卡）：
```cpp
    m_comboIface = new QComboBox(this);
    m_comboIface->addItem("默认网卡", QString());
    for (const QNetworkInterface& itf : QNetworkInterface::allInterfaces()) {
        if (!itf.flags().testFlag(QNetworkInterface::IsUp)) continue;
        for (const QNetworkAddressEntry& e : itf.addressEntries()) {
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol
                && !e.ip().isLoopback()) {
                m_comboIface->addItem(itf.humanReadableName() + " (" + e.ip().toString() + ")",
                                      e.ip().toString());
            }
        }
    }
```
把 `m_comboIface` addWidget 到配置行（网卡标签 + 下拉）。

- [ ] **Step 3: onStartClicked 分派组播**

在 `onStartClicked` 里，读协议下拉，若为 Multicast(index==2) 走组播抓收：
```cpp
    int protoIdx = m_comboProtocol->currentIndex();
    // ... 录制勾选处理（现有逻辑，enableRecording/disableRecording）...
    if (protoIdx == 2) {   // Multicast
        QString group = m_editUpstreamHost->text().trimmed();  // 复用上游地址框作组地址
        quint16 port = quint16(m_spinUpstreamPort->value());   // 复用上游端口框作组端口
        QString iface = m_comboIface->currentData().toString();
        m_backend->startMulticastCapture(group, port, iface);
    } else {
        // 现有 TCP/UDP startRelay 逻辑
        ...
    }
```
> UI 决策：组播复用"上游地址/端口"输入框作为"组地址/端口"（标签在选 Multicast 时可动态改文案，最小改动可不改标签，靠占位提示）。

- [ ] **Step 4: onReplayStart 传网卡 + 组播默认目标**

在 `onReplayStart` 调用 `m_backend->startReplay(...)` 处，追加网卡参数：
```cpp
    QString iface = m_comboIface->currentData().toString();
    m_backend->startReplay(file, host, port, speed, iface);
```
（host/port 用户可留空→Backend/Player 回退到文件头组地址；回放组播时提示"确认真实源已离线避免混叠"，用现有 appendLog 或 QMessageBox。）

- [ ] **Step 5: 构建 + 冒烟**

Run:
```bash
cd /d/persional/QtDeployMaster/build && cmake --build . --config Release 2>&1 | tail -3
cd Release && export PATH="/c/Qt/6.10.1/msvc2022_64/bin:$PATH" && cp -f ../../lib/libcurl-x64.dll libcurl.dll 2>/dev/null; timeout 6 ./DeviceForge.exe >/dev/null 2>&1 & PID=$!; sleep 4; kill -0 $PID 2>/dev/null && echo ALIVE && kill $PID 2>/dev/null || echo EXITED
```
Expected: 0 error + ALIVE。

- [ ] **Step 6: 端到端手动点验（组播录制→回灌全链路）**

```bash
# 组播发送器（模拟雷达）
python -c "import socket,time; s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM,socket.IPPROTO_UDP); s.setsockopt(socket.IPPROTO_IP,socket.IP_MULTICAST_TTL,2); [print('sent',i) or s.sendto(b'radar-%03d'%i,('239.1.2.3',6000)) or time.sleep(0.1) for i in range(30)]"
# 组播接收器（验证零影响 + 回灌）
python -c "import socket,struct; s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM,socket.IPPROTO_UDP); s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); s.bind(('',6000)); mreq=struct.pack('4sl',socket.inet_aton('239.1.2.3'),socket.INADDR_ANY); s.setsockopt(socket.IPPROTO_IP,socket.IP_ADD_MEMBERSHIP,mreq); [print(s.recv(1024)) for _ in range(30)]"
```
1. 起接收器 + 发送器，确认接收器收到 30 包（基线）。
2. 同时开工具录制 → 确认接收器**仍收到全部**（零影响验证，ShareAddress|ReuseAddressHint 生效）+ 工具录到 .nrec。
3. 停发送器与工具，工具回放该 .nrec 到 239.1.2.3:6000 → 接收器再次收到相同序列。

- [ ] **Step 7: Commit**

```bash
cd /d/persional/QtDeployMaster
git add src/tools/NetRelayTool/NetRelayWidget.*
git commit -m "feat: NetRelayWidget 组播 UI(协议下拉+网卡选择+组播抓收/回灌分派)"
```

---

## Task 5: 文档同步

**Files:**
- Modify: `CLAUDE.md`, `README.md`, `docs/architecture.md`, `docs/user-guide.md`

- [ ] **Step 1: CLAUDE.md**

NetRelayTool 描述处补充：支持组播(Multicast)录制 + 回灌原组，网卡选择，`.nrec` protocol=2 存组地址；模块表"网络调试"协议加"/组播"。

- [ ] **Step 2: README.md**

功能模块表"网络调试中继"说明追加"，组播录制与回灌"。

- [ ] **Step 3: docs/architecture.md + user-guide.md**

architecture.md NetRelayTool 节补组播数据流（零影响订阅者 + 回灌）；user-guide.md 网络调试章节加组播使用步骤（选 Multicast、填组地址/端口、选网卡、录制/回放）。

- [ ] **Step 4: Commit**

```bash
cd /d/persional/QtDeployMaster
git add CLAUDE.md README.md docs/
git commit -m "docs: 同步 NetRelayTool 组播录制回灌能力"
```

---

## Self-Review（对照 spec）

- **spec §2.1 组播录制+回灌** → Task 2（抓收）+ Task 3（回灌）。✅
- **spec §2.2 零影响** → Task 2 Step 2 `ShareAddress|ReuseAddressHint`，Global Constraints 明确；Task 4 Step 6 端到端零影响验证。✅
- **spec §2.3 回放默认原组可改** → Task 3 Step 2（consumerHost 覆盖，空则用文件头）。✅
- **spec §2.4 网卡选择** → Task 2（join iface）+ Task 3（setMulticastInterface）+ Task 4（网卡下拉）。✅
- **spec §2.5 .nrec 扩展 protocol=2 + reserved 存组地址** → Task 1（Step 5b 用 u16 port+u32 ipv4 存 reserved，不涨 version）。✅
- **spec §5.1 端口共享命门** → Global Constraints + Task 2 Step 2 双 flag。✅
- **spec §5.2 网卡指定** → Task 2/3/4。✅
- **spec §5.3 IPv4 D 类校验** → Task 2 Step 2 fail-closed 校验。✅
- **spec §5.5 格式兼容** → Task 1 Step 7 + `udpFileStillLoadsAfterMulticastExtension` 回归测试。✅
- **spec §7 测试** → Task 1（单元往返+兼容）+ Task 4 Step 6（端到端零影响+回灌）。✅
- **spec §8 非目标（转另一组/转单播/IPv6）** → 计划未包含，正确。✅
- **占位符扫描**：无 TBD/TODO；每步含可执行命令/完整代码。Task 1 Step 5→5b 是有意的"决策记录+修正"，非占位。✅
- **类型一致性**：`RelayProtocol::Multicast`(T1)、`NrecFile.groupAddr/groupPort`(T1)、`startMulticastCapture(groupAddr,port,ifaceAddr)`(T2)、`setMulticastInterface`(T3)、`startReplay` 加 mcastIfaceAddr 参数(T3→T4 调用一致)。✅
- **reserved 区容量**：Task 1 Step 5b 用 port(2)+ipv4(4)+保留(10)=16 字节,严格等于 reserved 区,IPv4 地址用 u32 存不受点分长度限制。✅
