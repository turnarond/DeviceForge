# NetRelayTool 录制回放 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为已完成的 NetRelayTool 透明中继补齐「录制到自定义 `.nrec` 格式 + 按原始时序回放上行数据给消费者」的核心能力。

**Architecture:** 在现有 `NetRelayBackend`(中继引擎) 旁新增两个内聚、可独立测试的单元：`RelayRecorder`(顺序写 `.nrec`) 与 `RelayPlayer`(读 `.nrec` + QTimer 按时序重放)。共享的枚举/记录结构抽到 `NetRelayTypes.h`。Backend 用 `RelayMode{Idle,Relaying,Replaying}` 状态机保证中继与回放互斥；录制在中继数据发射点顺带落盘；回放由 Backend 持有 Player 并转发回调。UI 增加录制勾选与回放面板。

**Tech Stack:** C++17、Qt 6.10.1 (Core/Network/Widgets/Test)、CMake、QDataStream(LittleEndian) 做二进制序列化。

## Global Constraints

- 语言：注释/日志/文档中文优先，专业术语除外（照 CLAUDE.md）。
- 命名：类 PascalCase、方法 camelCase、成员 `m_camelCase`、常量 `kXxx` / UPPER_SNAKE。
- 非 QObject 约定：Recorder/Reader/Player 与 Backend 一致，均为**纯 C++ 类**（非 QObject），Qt 对象用裸指针 + `new`，`QObject::connect` 用无 context 的 lambda 重载。
- 跨线程：所有 socket/timer 在主线程事件循环驱动；Backend→Widget 回调经 `QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection)`。
- 字节序：`.nrec` 全部字段 **小端**（`QDataStream::LittleEndian`），payload 用 `writeRawData/readRawData`（不带长度前缀）。
- 双构建：新增 `.cpp/.h` 必须同时登记到 `CMakeLists.txt` 与 `DeployMaster.vcxproj`（含 `<QtMoc>` 仅用于含 `Q_OBJECT` 的类——本计划新增类均非 QObject，故只进 `<ClCompile>`/`<ClInclude>`）。
- 测试目标仅走 CMake（`find_package(Qt6 ... Test)` + CTest），不进 vcxproj，不影响 VS 构建 DeviceForge。
- 回放语义：**只重放 direction=Upstream 的记录**发给消费者；下行记录保留在文件中但首期回放忽略。
- 设计依据：`docs/superpowers/specs/2026-07-08-net-relay-design.md`。

---

## 文件结构

| 文件 | 职责 | 状态 |
|------|------|------|
| `src/tools/NetRelayTool/NetRelayTypes.h` | 共享枚举 `RelayProtocol`/`RelayDirection` + `.nrec` 格式常量 | 新建 |
| `src/tools/NetRelayTool/RelayRecorder.h/.cpp` | 顺序写 `.nrec`（header + records） | 新建 |
| `src/tools/NetRelayTool/RelayRecording.h/.cpp` | 读 `.nrec` 并校验（供 Player + 测试用） | 新建 |
| `src/tools/NetRelayTool/RelayPlayer.h/.cpp` | 加载 + 按时序重放上行到消费者 | 新建 |
| `src/tools/NetRelayTool/NetRelayBackend.h/.cpp` | 加录制钩子 + 回放委托 + 模式状态机 | 修改 |
| `src/tools/NetRelayTool/NetRelayWidget.h/.cpp` | 录制勾选/路径 + 回放面板 | 修改 |
| `tests/NetRelayTool/tst_nrec.cpp` | Recorder/Reader/Player 单元测试(QtTest) | 新建 |
| `tests/CMakeLists.txt` | 测试目标定义 | 新建 |
| `CMakeLists.txt` / `DeployMaster.vcxproj` | 登记新源文件 + enable_testing | 修改 |
| `CLAUDE.md` / `README.md` | 文档同步 | 修改 |

---

## Task 1: 抽取共享类型头 + `.nrec` 格式常量

**Files:**
- Create: `src/tools/NetRelayTool/NetRelayTypes.h`
- Modify: `src/tools/NetRelayTool/NetRelayBackend.h`（删除本地枚举定义，改为 include 新头）

**Interfaces:**
- Produces: `enum class RelayProtocol { Tcp, Udp };`、`enum class RelayDirection { Upstream, Downstream };`、命名空间常量 `nrec::kMagic`(char[4]="NREC")、`nrec::kVersion`(quint16=1)、`nrec::kHeaderSize`(=32)。

- [ ] **Step 1: 创建共享类型头**

`src/tools/NetRelayTool/NetRelayTypes.h`:
```cpp
/*
 * NetRelayTypes.h — NetRelayTool 共享类型与 .nrec 格式常量
 */
#pragma once
#include <cstdint>

// 中继协议类型
enum class RelayProtocol { Tcp, Udp };

// 中继方向：Upstream = 生产者→消费者；Downstream = 消费者→生产者
enum class RelayDirection { Upstream, Downstream };

// .nrec 录制文件格式常量（详见 spec §4）
namespace nrec {
    constexpr char       kMagic[4]   = { 'N', 'R', 'E', 'C' };
    constexpr uint16_t   kVersion    = 1;
    constexpr int        kHeaderSize = 32;   // 固定文件头字节数
}
```

- [ ] **Step 2: 修改 NetRelayBackend.h 引用共享头**

删除 `NetRelayBackend.h:33-37` 处的两个 `enum class` 定义，替换为：
```cpp
#include "NetRelayTypes.h"
```
（放在 `#include "framework/ToolBackend.h"` 之后。`RelaySession` 结构体保留在 NetRelayBackend.h 不动。）

- [ ] **Step 3: 构建验证（确保无回归）**

Run: `cd build && cmake --build . --config Release`
Expected: 编译成功，0 error（枚举定义迁移后中继原功能不变）。

- [ ] **Step 4: 同步 vcxproj 登记新头**

在 `DeployMaster.vcxproj` 的 `<ClInclude>` 组内（`NetRelayBackend.h` 一行旁）追加：
```xml
    <ClInclude Include="src\tools\NetRelayTool\NetRelayTypes.h" />
```

- [ ] **Step 5: Commit**

```bash
git add src/tools/NetRelayTool/NetRelayTypes.h src/tools/NetRelayTool/NetRelayBackend.h DeployMaster.vcxproj
git commit -m "refactor: 抽取 NetRelayTypes.h 共享枚举 + .nrec 格式常量"
```

---

## Task 2: 测试脚手架（QtTest 目标）

**Files:**
- Create: `tests/CMakeLists.txt`
- Create: `tests/NetRelayTool/tst_nrec.cpp`（先放空壳，一个恒真测试）
- Modify: `CMakeLists.txt`（末尾加 `enable_testing()` + `add_subdirectory(tests)`）

**Interfaces:**
- Produces: CMake 测试目标 `tst_nrec`，可用 `ctest` 运行。后续 Task 3/4/5 往 `tst_nrec.cpp` 追加用例。

- [ ] **Step 1: 写测试目标 CMake**

`tests/CMakeLists.txt`:
```cmake
find_package(Qt6 REQUIRED COMPONENTS Core Network Test)

set(NETRELAY_DIR ${CMAKE_SOURCE_DIR}/src/tools/NetRelayTool)

add_executable(tst_nrec
    NetRelayTool/tst_nrec.cpp
    ${NETRELAY_DIR}/RelayRecorder.cpp
    ${NETRELAY_DIR}/RelayRecording.cpp
    ${NETRELAY_DIR}/RelayPlayer.cpp
)
target_include_directories(tst_nrec PRIVATE
    ${NETRELAY_DIR}
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_nrec PRIVATE Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME tst_nrec COMMAND tst_nrec)
```

> 注意：Recorder/Recording/Player 的 `.cpp` 在 Task 3/4/6 才创建。本任务先建空壳测试，暂时**只编译 tst_nrec.cpp**——先把上面 `add_executable` 里未创建的三个 `.cpp` 行注释掉，Task 3/4/6 完成对应文件时再逐一取消注释。

修正本步的 `add_executable` 为（仅测试文件）：
```cmake
add_executable(tst_nrec
    NetRelayTool/tst_nrec.cpp
)
```

- [ ] **Step 2: 写空壳测试**

`tests/NetRelayTool/tst_nrec.cpp`:
```cpp
#include <QtTest/QtTest>

class TstNrec : public QObject {
    Q_OBJECT
private slots:
    void sanity() { QVERIFY(true); }
};

QTEST_MAIN(TstNrec)
#include "tst_nrec.moc"
```

- [ ] **Step 3: 挂到根 CMake**

在 `CMakeLists.txt` 末尾追加：
```cmake
enable_testing()
add_subdirectory(tests)
```

- [ ] **Step 4: 配置并运行空壳测试**

Run:
```bash
cd build && cmake .. -DCMAKE_PREFIX_PATH="C:/Qt/6.10.1/msvc2022_64" && cmake --build . --config Release --target tst_nrec && ctest -C Release -R tst_nrec --output-on-failure
```
Expected: `tst_nrec` 编译并 `1 test passed`（sanity）。

- [ ] **Step 5: Commit**

```bash
git add tests/ CMakeLists.txt
git commit -m "test: 建立 NetRelayTool QtTest 目标脚手架"
```

---

## Task 3: RelayRecorder（写 `.nrec`）+ RelayRecording（读 `.nrec`）

**Files:**
- Create: `src/tools/NetRelayTool/RelayRecorder.h`, `RelayRecorder.cpp`
- Create: `src/tools/NetRelayTool/RelayRecording.h`, `RelayRecording.cpp`
- Modify: `tests/NetRelayTool/tst_nrec.cpp`（加往返测试）, `tests/CMakeLists.txt`（取消注释这两个 `.cpp`）

**Interfaces:**
- Produces (RelayRecorder):
  - `bool open(const QString& path, RelayProtocol proto, qint64 startEpochMs);`
  - `void append(RelayDirection dir, int sessionId, qint64 tsOffsetMs, const QByteArray& data);`
  - `void close();`
  - `bool isOpen() const;`
- Produces (RelayRecording):
  - `struct NrecRecord { RelayDirection dir; int sessionId; qint64 tsOffsetMs; QByteArray payload; };`
  - `struct NrecFile { RelayProtocol protocol; qint64 startEpochMs; QVector<NrecRecord> records; };`
  - `static bool RelayRecording::load(const QString& path, NrecFile& out, QString& error);`

- [ ] **Step 1: 写往返失败测试**

在 `tests/NetRelayTool/tst_nrec.cpp` 的 `TstNrec` 类中新增 `#include` 与用例：
```cpp
#include "RelayRecorder.h"
#include "RelayRecording.h"
#include <QTemporaryFile>
```
```cpp
    void recorderRoundTrip() {
        QString path = QDir::temp().filePath("tst_roundtrip.nrec");
        RelayRecorder rec;
        QVERIFY(rec.open(path, RelayProtocol::Tcp, 1000));
        rec.append(RelayDirection::Upstream,   1, 0,   QByteArray("hello"));
        rec.append(RelayDirection::Downstream, 1, 15,  QByteArray("\x00\x01\x02", 3));
        rec.append(RelayDirection::Upstream,   1, 40,  QByteArray("world"));
        rec.close();

        NrecFile f; QString err;
        QVERIFY2(RelayRecording::load(path, f, err), qPrintable(err));
        QCOMPARE(f.protocol, RelayProtocol::Tcp);
        QCOMPARE(f.startEpochMs, qint64(1000));
        QCOMPARE(f.records.size(), 3);
        QCOMPARE(f.records[0].dir, RelayDirection::Upstream);
        QCOMPARE(f.records[0].tsOffsetMs, qint64(0));
        QCOMPARE(f.records[0].payload, QByteArray("hello"));
        QCOMPARE(f.records[1].payload, QByteArray("\x00\x01\x02", 3));
        QCOMPARE(f.records[2].tsOffsetMs, qint64(40));
        QFile::remove(path);
    }

    void loadRejectsBadMagic() {
        QString path = QDir::temp().filePath("tst_bad.nrec");
        QFile bad(path); QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("XXXX garbage"); bad.close();
        NrecFile f; QString err;
        QVERIFY(!RelayRecording::load(path, f, err));   // 坏 magic 必须被拒
        QVERIFY(!err.isEmpty());
        QFile::remove(path);
    }
```
补 `#include <QDir>` 到文件顶部。

- [ ] **Step 2: 运行确认失败（未实现）**

先在 `tests/CMakeLists.txt` 取消注释 `RelayRecorder.cpp` 与 `RelayRecording.cpp` 两行。
Run: `cd build && cmake --build . --config Release --target tst_nrec`
Expected: 编译失败（`RelayRecorder.h` 不存在）。

- [ ] **Step 3: 写 RelayRecorder.h**

```cpp
/* RelayRecorder.h — 顺序写 .nrec 录制文件 */
#pragma once
#include "NetRelayTypes.h"
#include <QString>
#include <QByteArray>
#include <QFile>
#include <QDataStream>
#include <memory>

class RelayRecorder {
public:
    RelayRecorder() = default;
    ~RelayRecorder();

    bool open(const QString& path, RelayProtocol proto, qint64 startEpochMs);
    void append(RelayDirection dir, int sessionId, qint64 tsOffsetMs, const QByteArray& data);
    void close();
    bool isOpen() const { return m_file && m_file->isOpen(); }

private:
    std::unique_ptr<QFile>       m_file;
    std::unique_ptr<QDataStream> m_stream;
};
```

- [ ] **Step 4: 写 RelayRecorder.cpp**

```cpp
/* RelayRecorder.cpp */
#include "RelayRecorder.h"

RelayRecorder::~RelayRecorder() { close(); }

bool RelayRecorder::open(const QString& path, RelayProtocol proto, qint64 startEpochMs)
{
    close();
    m_file = std::make_unique<QFile>(path);
    if (!m_file->open(QIODevice::WriteOnly)) { m_file.reset(); return false; }
    m_file->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    m_stream = std::make_unique<QDataStream>(m_file.get());
    m_stream->setByteOrder(QDataStream::LittleEndian);

    // --- 文件头（32 字节，详见 spec §4.1）---
    m_stream->writeRawData(nrec::kMagic, 4);                 // 0: magic
    *m_stream << quint16(nrec::kVersion);                    // 4: version
    *m_stream << quint8(proto == RelayProtocol::Tcp ? 0 : 1);// 6: protocol
    *m_stream << quint8(0);                                  // 7: reserved
    *m_stream << qint64(startEpochMs);                       // 8: start_epoch_ms
    for (int i = 0; i < 16; ++i) *m_stream << quint8(0);     // 16: reserved[16]
    return true;
}

void RelayRecorder::append(RelayDirection dir, int sessionId, qint64 tsOffsetMs, const QByteArray& data)
{
    if (!isOpen() || !m_stream) return;
    *m_stream << quint8(dir == RelayDirection::Upstream ? 0 : 1); // direction
    *m_stream << quint8(0);                                       // reserved
    *m_stream << quint32(sessionId);                             // session_id
    *m_stream << qint64(tsOffsetMs);                            // ts_offset_ms
    *m_stream << quint32(data.size());                          // length
    if (!data.isEmpty()) m_stream->writeRawData(data.constData(), data.size());
}

void RelayRecorder::close()
{
    m_stream.reset();
    if (m_file) { m_file->close(); m_file.reset(); }
}
```

- [ ] **Step 5: 写 RelayRecording.h**

```cpp
/* RelayRecording.h — 读取并校验 .nrec 文件 */
#pragma once
#include "NetRelayTypes.h"
#include <QString>
#include <QByteArray>
#include <QVector>

struct NrecRecord {
    RelayDirection dir = RelayDirection::Upstream;
    int            sessionId = 0;
    qint64         tsOffsetMs = 0;
    QByteArray     payload;
};

struct NrecFile {
    RelayProtocol       protocol = RelayProtocol::Tcp;
    qint64              startEpochMs = 0;
    QVector<NrecRecord> records;
};

class RelayRecording {
public:
    // 成功返回 true 并填充 out；失败返回 false 并写入 error。坏 magic/版本/截断都判失败。
    static bool load(const QString& path, NrecFile& out, QString& error);
};
```

- [ ] **Step 6: 写 RelayRecording.cpp**

```cpp
/* RelayRecording.cpp */
#include "RelayRecording.h"
#include <QFile>
#include <QDataStream>

bool RelayRecording::load(const QString& path, NrecFile& out, QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = "无法打开文件: " + file.errorString(); return false; }
    if (file.size() < nrec::kHeaderSize) { error = "文件过小，非法 .nrec"; return false; }

    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);

    char magic[4];
    if (in.readRawData(magic, 4) != 4 ||
        magic[0]!='N'||magic[1]!='R'||magic[2]!='E'||magic[3]!='C') {
        error = "magic 校验失败，非 .nrec 文件"; return false;
    }
    quint16 version; quint8 proto, r0;
    in >> version >> proto >> r0;
    if (version != nrec::kVersion) { error = QString("不支持的版本: %1").arg(version); return false; }
    qint64 startEpoch; in >> startEpoch;
    for (int i = 0; i < 16; ++i) { quint8 x; in >> x; }  // reserved[16]

    out.protocol = (proto == 0) ? RelayProtocol::Tcp : RelayProtocol::Udp;
    out.startEpochMs = startEpoch;
    out.records.clear();

    while (!in.atEnd()) {
        quint8 dir, rr; quint32 sid; qint64 ts; quint32 len;
        in >> dir >> rr >> sid >> ts >> len;
        if (in.status() != QDataStream::Ok) { error = "记录头截断"; return false; }
        QByteArray payload;
        if (len > 0) {
            payload.resize(int(len));
            if (in.readRawData(payload.data(), int(len)) != int(len)) {
                error = "payload 截断"; return false;
            }
        }
        NrecRecord rec;
        rec.dir = (dir == 0) ? RelayDirection::Upstream : RelayDirection::Downstream;
        rec.sessionId = int(sid);
        rec.tsOffsetMs = ts;
        rec.payload = payload;
        out.records.append(rec);
    }
    return true;
}
```

- [ ] **Step 7: 运行测试确认通过**

Run: `cd build && cmake --build . --config Release --target tst_nrec && ctest -C Release -R tst_nrec --output-on-failure`
Expected: `recorderRoundTrip`、`loadRejectsBadMagic`、`sanity` 全部 PASS。

- [ ] **Step 8: 登记到主构建 + Commit**

在 `CMakeLists.txt` 的 `SOURCES` NetRelayTool 段追加 `RelayRecorder.cpp`、`RelayRecording.cpp`；在 `DeployMaster.vcxproj` `<ClCompile>` 加两 `.cpp`、`<ClInclude>` 加两 `.h`。
```bash
git add src/tools/NetRelayTool/RelayRecorder.* src/tools/NetRelayTool/RelayRecording.* tests/ CMakeLists.txt DeployMaster.vcxproj
git commit -m "feat: RelayRecorder/RelayRecording — .nrec 录制读写 + 单测"
```

---

## Task 4: RelayPlayer（按原始时序回放上行到消费者）

**Files:**
- Create: `src/tools/NetRelayTool/RelayPlayer.h`, `RelayPlayer.cpp`
- Modify: `tests/NetRelayTool/tst_nrec.cpp`（加回放测试）, `tests/CMakeLists.txt`（取消注释 `RelayPlayer.cpp`）

**Interfaces:**
- Consumes: `RelayRecording::load`, `NrecFile`/`NrecRecord`, `RelayProtocol`。
- Produces:
  - callbacks (typedef): `LogCallback=std::function<void(const std::string&)>`、`ErrorCallback=std::function<void(const std::string&)>`、`ProgressCallback=std::function<void(int played,int total,qint64 tsOffsetMs)>`、`FinishedCallback=std::function<void()>`。
  - `bool start(const QString& nrecPath, const QString& consumerHost, quint16 consumerPort, double speedFactor);`
  - `void pause(); void resume(); void stop(); bool isActive() const;`
  - setters: `setLogCallback/setErrorCallback/setProgressCallback/setFinishedCallback`。

- [ ] **Step 1: 写回放失败测试（本地 TCP 收集器作消费者）**

在 `tst_nrec.cpp` 加 `#include "RelayPlayer.h"`、`#include <QTcpServer>`、`#include <QTcpSocket>`、`#include <QSignalSpy>`、`#include <QElapsedTimer>`，新增用例：
```cpp
    void playerReplaysUpstreamOnly() {
        // 造一个含上/下行混合的 .nrec：上行 "A","B"，下行 "x"
        QString path = QDir::temp().filePath("tst_replay.nrec");
        { RelayRecorder rec;
          QVERIFY(rec.open(path, RelayProtocol::Tcp, 0));
          rec.append(RelayDirection::Upstream,   1, 0,  QByteArray("A"));
          rec.append(RelayDirection::Downstream, 1, 5,  QByteArray("x")); // 应被回放忽略
          rec.append(RelayDirection::Upstream,   1, 20, QByteArray("B"));
          rec.close(); }

        // 本地消费者：QTcpServer 收字节
        QByteArray received;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        quint16 port = server.serverPort();
        QObject::connect(&server, &QTcpServer::newConnection, [&]() {
            QTcpSocket* c = server.nextPendingConnection();
            QObject::connect(c, &QTcpSocket::readyRead, [&, c]() { received += c->readAll(); });
        });

        RelayPlayer player;
        bool finished = false;
        player.setFinishedCallback([&]() { finished = true; });
        QVERIFY(player.start(path, "127.0.0.1", port, 1.0));

        // 事件循环推进直到 finished 或超时
        QElapsedTimer t; t.start();
        while (!finished && t.elapsed() < 3000) { QCoreApplication::processEvents(); QTest::qWait(5); }
        QVERIFY(finished);
        QCOMPARE(received, QByteArray("AB"));   // 只上行、按序，忽略下行 "x"
        QFile::remove(path);
    }
```

- [ ] **Step 2: 运行确认失败**

在 `tests/CMakeLists.txt` 取消注释 `RelayPlayer.cpp`。
Run: `cd build && cmake --build . --config Release --target tst_nrec`
Expected: 编译失败（`RelayPlayer.h` 不存在）。

- [ ] **Step 3: 写 RelayPlayer.h**

```cpp
/* RelayPlayer.h — 加载 .nrec 并按原始时序把上行记录重放给消费者 */
#pragma once
#include "NetRelayTypes.h"
#include "RelayRecording.h"
#include <QString>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QHostAddress>
#include <QTimer>
#include <functional>
#include <string>

class RelayPlayer {
public:
    using LogCallback      = std::function<void(const std::string&)>;
    using ErrorCallback    = std::function<void(const std::string&)>;
    using ProgressCallback = std::function<void(int played, int total, qint64 tsOffsetMs)>;
    using FinishedCallback = std::function<void()>;

    RelayPlayer() = default;
    ~RelayPlayer();

    bool start(const QString& nrecPath, const QString& consumerHost,
               quint16 consumerPort, double speedFactor);
    void pause();
    void resume();
    void stop();
    bool isActive() const { return m_active; }

    void setLogCallback(LogCallback cb)           { m_logCb = std::move(cb); }
    void setErrorCallback(ErrorCallback cb)       { m_errorCb = std::move(cb); }
    void setProgressCallback(ProgressCallback cb) { m_progressCb = std::move(cb); }
    void setFinishedCallback(FinishedCallback cb) { m_finishedCb = std::move(cb); }

private:
    void scheduleNext();     // 安排下一条上行记录
    void sendCurrent();      // 发送 m_upstream[m_idx] 并递进
    void cleanup();
    void log(const std::string& s)   { if (m_logCb) m_logCb(s); }
    void fail(const std::string& s);

    NrecFile              m_file;
    QVector<int>          m_upstreamIdx;   // 指向 m_file.records 中 Upstream 记录的下标
    int                   m_idx = 0;        // 当前进度（在 m_upstreamIdx 内）
    double                m_speed = 1.0;

    RelayProtocol         m_protocol = RelayProtocol::Tcp;
    QTcpSocket*           m_tcp = nullptr;
    QUdpSocket*           m_udp = nullptr;
    QHostAddress          m_consumerAddr;
    quint16               m_consumerPort = 0;

    QTimer*               m_timer = nullptr;  // 单发定时器，按间隔调度
    bool                  m_active = false;
    bool                  m_paused = false;
    bool                  m_connected = false;

    LogCallback      m_logCb;
    ErrorCallback    m_errorCb;
    ProgressCallback m_progressCb;
    FinishedCallback m_finishedCb;
};
```

- [ ] **Step 4: 写 RelayPlayer.cpp**

```cpp
/* RelayPlayer.cpp */
#include "RelayPlayer.h"
#include <QHostInfo>

RelayPlayer::~RelayPlayer() { cleanup(); }

void RelayPlayer::fail(const std::string& s)
{
    if (m_errorCb) m_errorCb(s);
    cleanup();
}

bool RelayPlayer::start(const QString& nrecPath, const QString& consumerHost,
                        quint16 consumerPort, double speedFactor)
{
    if (m_active) return false;

    QString err;
    if (!RelayRecording::load(nrecPath, m_file, err)) { if (m_errorCb) m_errorCb(err.toStdString()); return false; }

    // 建立"上行记录"索引
    m_upstreamIdx.clear();
    for (int i = 0; i < m_file.records.size(); ++i)
        if (m_file.records[i].dir == RelayDirection::Upstream) m_upstreamIdx.append(i);
    if (m_upstreamIdx.isEmpty()) { if (m_errorCb) m_errorCb("录制中无上行记录，无可回放数据"); return false; }

    m_protocol = m_file.protocol;
    m_speed = (speedFactor > 0.0) ? speedFactor : 1.0;
    m_idx = 0;
    m_paused = false;

    QHostAddress addr;
    if (!addr.setAddress(consumerHost)) {
        if (consumerHost.compare("localhost", Qt::CaseInsensitive) == 0) addr = QHostAddress::LocalHost;
        else { QHostInfo hi = QHostInfo::fromName(consumerHost);   // 回放为用户主动操作，允许同步解析
               if (!hi.addresses().isEmpty()) addr = hi.addresses().first(); }
    }
    if (addr.isNull()) { if (m_errorCb) m_errorCb("消费者地址无效: " + consumerHost.toStdString()); return false; }
    m_consumerAddr = addr;
    m_consumerPort = consumerPort;

    m_timer = new QTimer();
    m_timer->setSingleShot(true);
    QObject::connect(m_timer, &QTimer::timeout, [this]() { sendCurrent(); });

    m_active = true;

    if (m_protocol == RelayProtocol::Tcp) {
        m_tcp = new QTcpSocket();
        QObject::connect(m_tcp, &QTcpSocket::connected, [this]() {
            m_connected = true;
            log("回放: 已连接消费者，开始重放");
            scheduleNext();
        });
        QObject::connect(m_tcp, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            [this](QAbstractSocket::SocketError) {
                if (m_tcp) fail("回放连接失败: " + m_tcp->errorString().toStdString());
            });
        m_tcp->connectToHost(m_consumerAddr, m_consumerPort);
    } else {
        m_udp = new QUdpSocket();
        m_connected = true;            // UDP 无需连接
        log("回放: UDP 模式，开始重放");
        scheduleNext();
    }
    return true;
}

void RelayPlayer::scheduleNext()
{
    if (!m_active || m_paused) return;
    if (m_idx >= m_upstreamIdx.size()) {          // 全部发完
        log("回放完成");
        if (m_finishedCb) m_finishedCb();
        cleanup();
        return;
    }
    qint64 delay = 0;
    if (m_idx > 0) {
        qint64 prev = m_file.records[m_upstreamIdx[m_idx - 1]].tsOffsetMs;
        qint64 cur  = m_file.records[m_upstreamIdx[m_idx]].tsOffsetMs;
        delay = qMax<qint64>(0, cur - prev);
        if (m_speed > 0.0) delay = qint64(delay / m_speed);
    }
    m_timer->start(int(qMin<qint64>(delay, 60000)));  // 首条 delay=0 立即；单发上限 60s 防异常
}

void RelayPlayer::sendCurrent()
{
    if (!m_active || m_paused) return;
    const NrecRecord& rec = m_file.records[m_upstreamIdx[m_idx]];
    if (m_protocol == RelayProtocol::Tcp) {
        if (m_tcp && m_tcp->state() == QAbstractSocket::ConnectedState) m_tcp->write(rec.payload);
    } else {
        if (m_udp) m_udp->writeDatagram(rec.payload, m_consumerAddr, m_consumerPort);
    }
    ++m_idx;
    if (m_progressCb) m_progressCb(m_idx, m_upstreamIdx.size(), rec.tsOffsetMs);
    scheduleNext();
}

void RelayPlayer::pause()
{
    if (!m_active || m_paused) return;
    m_paused = true;
    if (m_timer) m_timer->stop();
    log("回放已暂停");
}

void RelayPlayer::resume()
{
    if (!m_active || !m_paused) return;
    m_paused = false;
    log("回放已继续");
    scheduleNext();
}

void RelayPlayer::stop()
{
    if (!m_active) return;
    log("回放已停止");
    cleanup();
}

void RelayPlayer::cleanup()
{
    m_active = false;
    m_paused = false;
    m_connected = false;
    if (m_timer) { m_timer->stop(); QObject::disconnect(m_timer, nullptr, nullptr, nullptr);
                   m_timer->deleteLater(); m_timer = nullptr; }
    if (m_tcp)   { QObject::disconnect(m_tcp, nullptr, nullptr, nullptr);
                   m_tcp->close(); m_tcp->deleteLater(); m_tcp = nullptr; }
    if (m_udp)   { m_udp->close(); m_udp->deleteLater(); m_udp = nullptr; }
}
```

- [ ] **Step 5: 运行测试确认通过**

Run: `cd build && cmake --build . --config Release --target tst_nrec && ctest -C Release -R tst_nrec --output-on-failure`
Expected: `playerReplaysUpstreamOnly` PASS（消费者收到 `"AB"`），其余用例仍 PASS。

- [ ] **Step 6: 登记 + Commit**

`CMakeLists.txt` SOURCES 加 `RelayPlayer.cpp`；`DeployMaster.vcxproj` 加 `RelayPlayer.cpp`(`<ClCompile>`) + `RelayPlayer.h`(`<ClInclude>`)。
```bash
git add src/tools/NetRelayTool/RelayPlayer.* tests/ CMakeLists.txt DeployMaster.vcxproj
git commit -m "feat: RelayPlayer — 按原始时序回放上行到消费者 + 单测"
```

---

## Task 5: Backend 集成（录制钩子 + 回放委托 + 模式状态机）

**Files:**
- Modify: `src/tools/NetRelayTool/NetRelayBackend.h`, `NetRelayBackend.cpp`

**Interfaces:**
- Consumes: `RelayRecorder`, `RelayPlayer`, `RelayRecording`。
- Produces (Backend 新增公有 API):
  - `void enableRecording(const QString& path); void disableRecording();`
  - `void startReplay(const QString& nrecPath, const QString& consumerHost, quint16 consumerPort, double speedFactor);`
  - `void pauseReplay(); void resumeReplay(); void stopReplay(); bool isReplaying() const;`
  - replay 回调 setters：`setReplayProgressCallback(std::function<void(int,int,qint64)>)`、`setReplayFinishedCallback(std::function<void()>)`（log/error 复用现有）。

- [ ] **Step 1: 头文件加成员与 API**

在 `NetRelayBackend.h` 顶部 include 段追加：
```cpp
#include "RelayRecorder.h"
#include "RelayPlayer.h"
#include <memory>
```
在 `// --- 中继控制 ---` 段后新增声明：
```cpp
    // --- 录制 ---
    void enableRecording(const QString& path);   // startRelay 前调用；启动时打开录制
    void disableRecording();

    // --- 回放（与中继互斥）---
    void startReplay(const QString& nrecPath, const QString& consumerHost,
                     quint16 consumerPort, double speedFactor);
    void pauseReplay();
    void resumeReplay();
    void stopReplay();
    bool isReplaying() const { return m_mode == RelayMode::Replaying; }

    using ReplayProgressCallback = std::function<void(int played, int total, qint64 tsOffsetMs)>;
    using ReplayFinishedCallback = std::function<void()>;
    void setReplayProgressCallback(ReplayProgressCallback cb) { m_replayProgressCb = std::move(cb); }
    void setReplayFinishedCallback(ReplayFinishedCallback cb) { m_replayFinishedCb = std::move(cb); }
```
在私有成员段追加：
```cpp
    // 模式状态机（中继/回放互斥，spec §6）
    enum class RelayMode { Idle, Relaying, Replaying };
    RelayMode m_mode = RelayMode::Idle;

    // 录制
    bool                           m_recordEnabled = false;
    QString                        m_recordPath;
    std::unique_ptr<RelayRecorder> m_recorder;
    QElapsedTimer                  m_recordElapsed;

    // 回放
    std::unique_ptr<RelayPlayer>   m_player;
    ReplayProgressCallback         m_replayProgressCb;
    ReplayFinishedCallback         m_replayFinishedCb;

    void recordData(RelayDirection dir, int sessionId, const QByteArray& data); // 录制辅助
```

- [ ] **Step 2: 实现录制辅助 + 在四个数据发射点调用**

在 `NetRelayBackend.cpp` 加实现：
```cpp
void NetRelayBackend::enableRecording(const QString& path) { m_recordEnabled = true;  m_recordPath = path; }
void NetRelayBackend::disableRecording()                   { m_recordEnabled = false; m_recordPath.clear(); }

void NetRelayBackend::recordData(RelayDirection dir, int sessionId, const QByteArray& data)
{
    if (m_recorder && m_recorder->isOpen())
        m_recorder->append(dir, sessionId, m_recordElapsed.elapsed(), data);
}
```
在下列四处 **紧邻现有 `if (m_dataCb) m_dataCb(...)` 调用之后** 各加一行 `recordData(...)`：
- `onTcpClientReadyRead`：`recordData(RelayDirection::Upstream, pair->sessionId, data);`
- `onTcpUpstreamReadyRead`：`recordData(RelayDirection::Downstream, pair->sessionId, data);`
- `onUdpListenReadyRead`：`recordData(RelayDirection::Upstream, s->sessionId, data);`
- `onUdpUpstreamReadyRead`：`recordData(RelayDirection::Downstream, session->sessionId, data);`

- [ ] **Step 3: startRelay/stopRelay 接入模式机与录制开关**

在 `startRelay` 开头的 `if (m_running)` 检查后追加互斥判断：
```cpp
    if (m_mode == RelayMode::Replaying) { reportError("回放进行中，无法启动中继"); return; }
```
在 `startRelay` **成功设 `m_running = true` 的每个分支之后**（TCP 分支末、UDP `startUdpListen` 末），置模式并开录制：
```cpp
    m_mode = RelayMode::Relaying;
    if (m_recordEnabled && !m_recordPath.isEmpty()) {
        m_recorder = std::make_unique<RelayRecorder>();
        m_recordElapsed.start();
        qint64 epoch = QDateTime::currentMSecsSinceEpoch();
        if (!m_recorder->open(m_recordPath, m_protocol, epoch)) {
            reportError("录制文件打开失败: " + m_recordPath.toStdString());
            m_recorder.reset();
        } else {
            log("录制已开启: " + m_recordPath.toStdString());
        }
    }
```
（`startUdpListen` 分支同理——为避免重复，可把上述录制块提炼为私有 `void beginRecordingIfEnabled();` 在两分支各调一次。）在文件顶部确保 `#include <QDateTime>`。

在 `stopRelay()` 末尾（`m_running = false;` 之后）追加：
```cpp
    if (m_recorder) { m_recorder->close(); m_recorder.reset(); log("录制已保存"); }
    if (m_mode == RelayMode::Relaying) m_mode = RelayMode::Idle;
```

- [ ] **Step 4: 实现回放委托**

```cpp
void NetRelayBackend::startReplay(const QString& nrecPath, const QString& consumerHost,
                                  quint16 consumerPort, double speedFactor)
{
    if (m_mode == RelayMode::Relaying) { reportError("中继进行中，无法回放"); return; }
    if (m_mode == RelayMode::Replaying) { reportError("回放已在进行中"); return; }

    m_player = std::make_unique<RelayPlayer>();
    m_player->setLogCallback([this](const std::string& s){ log(s); });
    m_player->setErrorCallback([this](const std::string& s){
        reportError(s);
        m_mode = RelayMode::Idle;             // 失败回到 Idle
    });
    m_player->setProgressCallback([this](int p, int t, qint64 ts){
        if (m_replayProgressCb) m_replayProgressCb(p, t, ts);
    });
    m_player->setFinishedCallback([this](){
        if (m_replayFinishedCb) m_replayFinishedCb();
        m_mode = RelayMode::Idle;
    });

    if (m_player->start(nrecPath, consumerHost, consumerPort, speedFactor)) {
        m_mode = RelayMode::Replaying;
    } else {
        m_player.reset();
        m_mode = RelayMode::Idle;
    }
}

void NetRelayBackend::pauseReplay()  { if (m_player) m_player->pause(); }
void NetRelayBackend::resumeReplay() { if (m_player) m_player->resume(); }
void NetRelayBackend::stopReplay()
{
    if (m_player) { m_player->stop(); m_player.reset(); }
    if (m_mode == RelayMode::Replaying) m_mode = RelayMode::Idle;
}
```

- [ ] **Step 5: 析构安全**

在 `~NetRelayBackend()` 里、`clearCallbacks()` 之后追加：
```cpp
    if (m_player)   { m_player->stop(); m_player.reset(); }
    if (m_recorder) { m_recorder->close(); m_recorder.reset(); }
```

- [ ] **Step 6: 构建验证**

Run: `cd build && cmake --build . --config Release`
Expected: 0 error。中继原行为不变；录制/回放 API 就绪。

- [ ] **Step 7: Commit**

```bash
git add src/tools/NetRelayTool/NetRelayBackend.*
git commit -m "feat: Backend 集成录制钩子 + 回放委托 + 中继/回放互斥状态机"
```

---

## Task 6: Widget UI（录制勾选 + 回放面板）

**Files:**
- Modify: `src/tools/NetRelayTool/NetRelayWidget.h`, `NetRelayWidget.cpp`

**Interfaces:**
- Consumes: Backend 的 `enableRecording/disableRecording/startReplay/pauseReplay/resumeReplay/stopReplay/isReplaying/isRunning`、`setReplayProgressCallback/setReplayFinishedCallback`。

- [ ] **Step 1: 头文件加控件与槽**

`NetRelayWidget.h` 私有控件区追加：
```cpp
    // 录制
    QCheckBox*   m_chkRecord    = nullptr;
    QLineEdit*   m_editRecPath  = nullptr;
    QPushButton* m_btnRecBrowse = nullptr;
    // 回放
    QLineEdit*   m_editReplayFile  = nullptr;
    QPushButton* m_btnReplayBrowse = nullptr;
    QLineEdit*   m_editReplayHost  = nullptr;
    QSpinBox*    m_spinReplayPort  = nullptr;
    QComboBox*   m_comboSpeed      = nullptr;
    QPushButton* m_btnReplayStart  = nullptr;
    QPushButton* m_btnReplayPause  = nullptr;
    QPushButton* m_btnReplayStop   = nullptr;
    QProgressBar* m_replayProgress = nullptr;
```
私有槽区追加：
```cpp
    void onRecordBrowse();
    void onReplayBrowse();
    void onReplayStart();
    void onReplayPause();
    void onReplayStop();
    void setRelayControlsEnabled(bool enabled);   // 回放时禁用中继控件，反之亦然
```
顶部 include 追加 `#include <QProgressBar>`。

- [ ] **Step 2: setupUi 增录制勾选行 + 回放分区**

在中继控制按钮行之后、主分割区之前，插入录制勾选行：
```cpp
    auto* recRow = new QHBoxLayout();
    m_chkRecord = new QCheckBox("录制到文件", this);
    m_editRecPath = new QLineEdit(this);
    m_editRecPath->setPlaceholderText("录制保存路径 (.nrec)");
    m_btnRecBrowse = new QPushButton("浏览", this);
    connect(m_btnRecBrowse, &QPushButton::clicked, this, &NetRelayWidget::onRecordBrowse);
    recRow->addWidget(m_chkRecord);
    recRow->addWidget(m_editRecPath, 1);
    recRow->addWidget(m_btnRecBrowse);
    configLayout->addLayout(recRow);
```
在主分割区之后新增回放 GroupBox：
```cpp
    auto* replayGroup = new QGroupBox("回放", this);
    auto* rl = new QVBoxLayout(replayGroup);
    auto* rr1 = new QHBoxLayout();
    rr1->addWidget(new QLabel("录制文件:", this));
    m_editReplayFile = new QLineEdit(this);
    m_btnReplayBrowse = new QPushButton("选择", this);
    connect(m_btnReplayBrowse, &QPushButton::clicked, this, &NetRelayWidget::onReplayBrowse);
    rr1->addWidget(m_editReplayFile, 1); rr1->addWidget(m_btnReplayBrowse);
    rr1->addWidget(new QLabel("消费者:", this));
    m_editReplayHost = new QLineEdit("127.0.0.1", this); m_editReplayHost->setFixedWidth(120);
    m_spinReplayPort = new QSpinBox(this); m_spinReplayPort->setRange(1, 65535); m_spinReplayPort->setValue(9001);
    rr1->addWidget(m_editReplayHost); rr1->addWidget(m_spinReplayPort);
    rr1->addWidget(new QLabel("速率:", this));
    m_comboSpeed = new QComboBox(this); m_comboSpeed->addItems({"原速", "尽快"});
    rr1->addWidget(m_comboSpeed);
    rl->addLayout(rr1);
    auto* rr2 = new QHBoxLayout();
    m_btnReplayStart = new QPushButton("▶ 回放", this);
    m_btnReplayPause = new QPushButton("⏸ 暂停", this); m_btnReplayPause->setEnabled(false);
    m_btnReplayStop  = new QPushButton("■ 停止", this);  m_btnReplayStop->setEnabled(false);
    m_replayProgress = new QProgressBar(this); m_replayProgress->setRange(0, 100); m_replayProgress->setValue(0);
    connect(m_btnReplayStart, &QPushButton::clicked, this, &NetRelayWidget::onReplayStart);
    connect(m_btnReplayPause, &QPushButton::clicked, this, &NetRelayWidget::onReplayPause);
    connect(m_btnReplayStop,  &QPushButton::clicked, this, &NetRelayWidget::onReplayStop);
    rr2->addWidget(m_btnReplayStart); rr2->addWidget(m_btnReplayPause); rr2->addWidget(m_btnReplayStop);
    rr2->addWidget(m_replayProgress, 1);
    rl->addLayout(rr2);
    mainLayout->addWidget(replayGroup);
```

- [ ] **Step 3: setBackend 绑定回放回调**

在 `setBackend` 末尾追加：
```cpp
    m_backend->setReplayProgressCallback([this](int p, int t, qint64) {
        QMetaObject::invokeMethod(this, [this, p, t]() {
            m_replayProgress->setRange(0, t);
            m_replayProgress->setValue(p);
        }, Qt::QueuedConnection);
    });
    m_backend->setReplayFinishedCallback([this]() {
        QMetaObject::invokeMethod(this, [this]() {
            appendLog("回放完成");
            m_btnReplayStart->setEnabled(true);
            m_btnReplayPause->setEnabled(false);
            m_btnReplayStop->setEnabled(false);
            setRelayControlsEnabled(true);
        }, Qt::QueuedConnection);
    });
```

- [ ] **Step 4: 实现槽 + 录制接入 onStartClicked**

```cpp
void NetRelayWidget::onRecordBrowse() {
    QString f = QFileDialog::getSaveFileName(this, "录制保存为",
        "relay_" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".nrec",
        "NRec 录制 (*.nrec);;所有文件 (*.*)");
    if (!f.isEmpty()) m_editRecPath->setText(f);
}
void NetRelayWidget::onReplayBrowse() {
    QString f = QFileDialog::getOpenFileName(this, "选择录制文件", QString(),
        "NRec 录制 (*.nrec);;所有文件 (*.*)");
    if (!f.isEmpty()) m_editReplayFile->setText(f);
}
void NetRelayWidget::onReplayStart() {
    if (!m_backend) return;
    if (m_backend->isRunning()) { QMessageBox::warning(this, "警告", "请先停止中继再回放"); return; }
    QString file = m_editReplayFile->text().trimmed();
    if (file.isEmpty()) { QMessageBox::warning(this, "警告", "请选择录制文件"); return; }
    double speed = (m_comboSpeed->currentIndex() == 0) ? 1.0 : 1e9;  // 尽快=极大倍率
    m_backend->startReplay(file, m_editReplayHost->text().trimmed(),
                           quint16(m_spinReplayPort->value()), speed);
    m_btnReplayStart->setEnabled(false);
    m_btnReplayPause->setEnabled(true);
    m_btnReplayStop->setEnabled(true);
    setRelayControlsEnabled(false);
    appendLog("回放开始: " + file);
}
void NetRelayWidget::onReplayPause() {
    if (!m_backend) return;
    if (m_btnReplayPause->text().startsWith("⏸")) {
        m_backend->pauseReplay(); m_btnReplayPause->setText("▶ 继续");
    } else {
        m_backend->resumeReplay(); m_btnReplayPause->setText("⏸ 暂停");
    }
}
void NetRelayWidget::onReplayStop() {
    if (!m_backend) return;
    m_backend->stopReplay();
    m_btnReplayStart->setEnabled(true);
    m_btnReplayPause->setEnabled(false); m_btnReplayPause->setText("⏸ 暂停");
    m_btnReplayStop->setEnabled(false);
    setRelayControlsEnabled(true);
    appendLog("回放已停止");
}
void NetRelayWidget::setRelayControlsEnabled(bool enabled) {
    m_btnStart->setEnabled(enabled);
    m_comboProtocol->setEnabled(enabled);
    m_editListenAddr->setEnabled(enabled);
    m_spinListenPort->setEnabled(enabled);
    m_editUpstreamHost->setEnabled(enabled);
    m_spinUpstreamPort->setEnabled(enabled);
    m_chkRecord->setEnabled(enabled);
    m_editRecPath->setEnabled(enabled);
    m_btnRecBrowse->setEnabled(enabled);
}
```
在 `onStartClicked` 中、调用 `m_backend->startRelay(...)` **之前** 接入录制并禁用回放控件：
```cpp
    if (m_chkRecord->isChecked()) {
        QString rp = m_editRecPath->text().trimmed();
        if (rp.isEmpty()) { QMessageBox::warning(this, "警告", "已勾选录制，请指定录制文件路径"); return; }
        m_backend->enableRecording(rp);
    } else {
        m_backend->disableRecording();
    }
```
在 `onStartClicked` 末尾禁用回放起始按钮：`m_btnReplayStart->setEnabled(false);`；在 `onStopClicked` 末尾恢复：`m_btnReplayStart->setEnabled(true);`。
顶部确保 `#include <QFileDialog>`、`#include <QDateTime>`、`#include <QMessageBox>` 已在（现有文件已包含）。

- [ ] **Step 5: 构建 + 手动验证（端到端）**

Run: `cd build && cmake --build . --config Release`
Expected: 0 error。
手动验证脚本（Git Bash 三终端或顺序执行）：
1. 启动本地 echo 消费者：`ncat -l 9000 --keep-open --exec "/bin/cat"`（或任意回显服务，端口 9000）。
2. 运行 DeviceForge，网络调试 Tab：协议 TCP、监听 `127.0.0.1:9500`、消费者 `127.0.0.1:9000`、勾选录制→选路径 `d:/tmp/a.nrec`→启动。
3. 生产者发数据：`printf 'hello\nworld\n' | ncat 127.0.0.1 9500`。确认 Hex 视图出现上下行、停止后提示"录制已保存"。
4. 回放：停止中继→回放区选 `d:/tmp/a.nrec`、消费者 `127.0.0.1:9000`、原速→回放。确认进度条走完、消费者端再次收到 `hello/world`、日志"回放完成"。

- [ ] **Step 6: Commit**

```bash
git add src/tools/NetRelayTool/NetRelayWidget.*
git commit -m "feat: NetRelayWidget 录制勾选 + 回放面板 + 中继/回放互斥 UI"
```

---

## Task 7: 文档同步

**Files:**
- Modify: `CLAUDE.md`, `README.md`

- [ ] **Step 1: 更新 CLAUDE.md**

在 NetRelayTool 描述处（Tool 清单 + 模块表 + 源码清单）补充录制/回放能力与新增文件（`NetRelayTypes.h` / `RelayRecorder` / `RelayRecording` / `RelayPlayer`）；在"尚无测试"注意事项处更新为"NetRelayTool 已建 QtTest 目标 `tst_nrec`（CMake/CTest）"。

- [ ] **Step 2: 更新 README.md**

功能模块表"网络调试中继"行说明追加"支持流量录制(.nrec)与按原始时序回放"。

- [ ] **Step 3: Commit**

```bash
git add CLAUDE.md README.md
git commit -m "docs: 同步 NetRelayTool 录制回放能力与测试目标"
```

---

## Self-Review（对照 spec 检查）

- **Spec §2 需求 1-5（中继/协议/实时视图）** → 已由现有实现覆盖，本计划保留不动。✅
- **§2 需求 6 录制** → Task 3 (RelayRecorder) + Task 5 (Backend 钩子)。✅
- **§2 需求 7 回放上行给消费者** → Task 4 (RelayPlayer 只重放 Upstream) + Task 5 (startReplay)。✅
- **§2 需求 8 保留原始时序** → Task 4 `scheduleNext` 用相邻 `tsOffsetMs` 差值调度；UI 速率下拉（原速/尽快）Task 6。✅
- **§2 需求 9 自定义格式** → Task 1 常量 + Task 3 读写。✅
- **§4 .nrec 格式** → Task 1 常量 + Task 3 header/record 序列化，字段与偏移逐一对应。✅
- **§6 状态机（互斥）** → Task 5 `RelayMode` + startRelay/startReplay 互斥检查。✅
- **§7 UI** → Task 6 录制勾选 + 回放面板 + 互斥禁用。✅
- **§10 测试** → Task 2 脚手架 + Task 3 往返/坏 magic + Task 4 回放端到端。✅
- **类型一致性**：`RelayProtocol`/`RelayDirection`(Task1)、`NrecRecord`/`NrecFile`+`RelayRecording::load`(Task3)、Player 回调签名(Task4)、Backend `RelayMode`+replay API(Task5) 前后引用一致。✅
- **占位符扫描**：无 TBD/TODO；每个代码步含完整代码。✅
