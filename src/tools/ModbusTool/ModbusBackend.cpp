// v2.12 MODBUS 重构 Task 4：调度器驱动的传输执行器实现（旧"全局广播读 +
// 每客户端 1 挂起槽 + 单写广播"路径整体废除；调度/队列/退避语义见 ModbusScheduler.h）。
// 线程口径：本类全部 IO 与回调在 GUI 线程（规格 §1 规模 B）；ServiceTask 壳仅维持
// 生命周期一致性，svc() 不触碰任何 Qt 对象。
#include "ModbusBackend.h"
#include "ModbusDataStore.h"
#include "ModbusTargetStore.h"

#include <QDebug>
#include <QLatin1Char>
#include <QModbusReply>
#include <QModbusResponse>
#include <QTimer>
#include <QUrl>
#include <QVariant>

#include <chrono>
#include <thread>

namespace {

// 组装 Modbus 异常文本：优先从原始响应提取异常码（0x01-0x0B，最有诊断价值），附 Qt 错误描述
QString modbusErrorText(const QString& prefix, QModbusReply* reply)
{
    QString hex;
    QString name;
    const QModbusResponse raw = reply->rawResult();
    if (raw.isException()) {
        hex = QStringLiteral("0x%1").arg(raw.exceptionCode(), 2, 16, QChar('0'));
        switch (raw.exceptionCode()) {
        case 0x01: name = QStringLiteral("非法功能"); break;
        case 0x02: name = QStringLiteral("非法数据地址"); break;
        case 0x03: name = QStringLiteral("非法数据值"); break;
        case 0x04: name = QStringLiteral("从站设备故障"); break;
        case 0x05: name = QStringLiteral("确认"); break;
        case 0x06: name = QStringLiteral("从站设备忙"); break;
        case 0x0A: name = QStringLiteral("网关路径不可用"); break;
        case 0x0B: name = QStringLiteral("网关目标设备无响应"); break;
        default:   name = QStringLiteral("未知异常"); break;
        }
    }
    return hex.isEmpty()
        ? QStringLiteral("%1: %2").arg(prefix, reply->errorString())
        : QStringLiteral("%1: %2 %3: %4").arg(prefix, hex, name, reply->errorString());
}

// Qt 设备错误 → 调度器结果分类：TimeoutError 单独成类，其余通讯类共走退避通道
modbus::Outcome outcomeForDeviceError(QModbusDevice::Error error)
{
    return error == QModbusDevice::TimeoutError ? modbus::Outcome::Timeout
                                                : modbus::Outcome::CommError;
}

} // namespace

ModbusBackend::ModbusBackend() {}

ModbusBackend::~ModbusBackend()
{
    stopPolling();   // 先断信号后关（沿用既有死锁规避口径）
}

int ModbusBackend::svc()
{
    // ServiceTask 生命周期壳：真实工作全在 GUI 线程（QTimer/QModbusTcpClient 事件驱动）
    while (ServiceTask::isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
}

void ModbusBackend::bindDevices(const std::vector<DeviceInfo>& /*devices*/)
{
    // 接口兼容保留：v2.12 起目标集 = startPolling 注入的 PollTarget（含 connKey/slaveId），
    // 旧"设备列表广播"数据源已废除（规格 §3"废全局段广播"）
}
void ModbusBackend::bindCredentials(const AuthInfo& /*auth*/) {}   // Modbus TCP 无认证面（规格 §2.2）
void ModbusBackend::applyConfig(const lwserverbase::config::ConfigValue& /*config*/) {}

// ── 轮询生命周期 ────────────────────────────────────────────────────

void ModbusBackend::startPolling(const QVector<modbus::PollTarget>& targets,
                                 modbus::ModbusTargetStore* store,
                                 modbus::ModbusDataStore* data)
{
    stopPolling();   // 重复调用 = 重配目标集（调度器 reset 语义：在途/队列/退避清零）
    m_store = store;
    m_data = data;

    m_pollById.clear();
    QVector<modbus::PollTarget> valid;
    valid.reserve(targets.size());
    for (const modbus::PollTarget& t : targets) {
        if (t.targetId.isEmpty() || t.connKey.isEmpty()) {
            if (m_logCb)
                m_logCb(QStringLiteral("跳过无效目标（targetId/connKey 为空）: %1").arg(t.targetId)
                            .toStdString());
            continue;
        }
        // 裁决 2 契约兜底：targetId 全集唯一（makeTargetId 天然保证），重复项拒绝入组
        if (m_pollById.contains(t.targetId)) {
            qWarning() << "ModbusBackend: 重复 targetId 被剔除" << t.targetId;
            continue;
        }
        m_pollById.insert(t.targetId, t);
        valid.push_back(t);
    }

    m_clock.start();
    m_scheduler.reset(valid, m_clock.elapsed());
    setSchedulerCallbacks();
    m_polling = true;
    m_driver->start();
}

void ModbusBackend::stopPolling()
{
    // 顺序（控制者裁决绑定）：停 tick → 清队列/在途 → 逐 client 先断信号后关
    if (m_driver) {
        m_driver->stop();
        QObject::disconnect(m_driver, nullptr, nullptr, nullptr);
        delete m_driver;
        m_driver = nullptr;
    }
    m_readOnce.clear();
    m_pendingSend.clear();
    m_active.clear();
    for (auto* client : m_clients) {
        QObject::disconnect(client, nullptr, nullptr, nullptr);
        client->disconnectDevice();
        delete client;
    }
    m_clients.clear();
    m_polling = false;
}

void ModbusBackend::setSchedulerCallbacks()
{
    if (m_driver) return;
    // 驱动 timer 固定 250ms（kTickMs）：nextWakeMs 动态唤醒为可选优化——规模 B
    // （5~20 设备）下空 tick 仅数次整数比较，固定节拍更简单可靠；
    // 到期过滤在调度器 onTick 内完成，无提前唤醒诉求（简报步骤 3 口径）。
    m_driver = new QTimer();
    m_driver->setInterval(modbus::ModbusScheduler::kTickMs);
    // backend 非 QObject：以 driver 自身为 context，析构/stopPolling 断连双保险
    QObject::connect(m_driver, &QTimer::timeout, m_driver, [this]() { onTick(); });
}

// ── 派发执行 ────────────────────────────────────────────────────────

void ModbusBackend::onTick()
{
    const qint64 now = m_clock.elapsed();
    const QVector<modbus::Dispatch> dispatched = m_scheduler.onTick(now);
    for (const modbus::Dispatch& d : dispatched)
        dispatchScheduled(d);

    // 立即读取挂起项：调度器派发之后处理；连接忙（在途/待发）则下一 tick 补发（≤250ms）
    if (m_readOnce.isEmpty()) return;
    QSet<QString> done;
    for (const QString& targetId : m_readOnce) {
        const auto it = m_pollById.constFind(targetId);
        if (it == m_pollById.constEnd()) {
            done.insert(targetId);   // 目标已被移除：丢弃挂起
            continue;
        }
        const QString& connKey = it->connKey;
        if (m_active.contains(connKey) || m_pendingSend.contains(connKey))
            continue;
        // 轮2 审查 I-2：手工派发同样必须进调度器在途记账——旧缺陷是不置 inFlight，
        // 下一到期 tick 的周期派发经 sendDispatch 顶掉 m_active 槽位，手工结果被
        // 代际守卫静默丢弃。onResult（handleReply/handleDeviceError/failDispatch
        // 三路）对称清除；轮询态写路径经 enqueueWrite 弹队首时调度器自动置位，无旁路。
        m_scheduler.onDispatched(connKey);
        dispatchScheduled(makePollDispatch(*it));
        done.insert(targetId);
    }
    m_readOnce.subtract(done);
}

modbus::Dispatch ModbusBackend::makePollDispatch(const modbus::PollTarget& t)
{
    modbus::Dispatch d;
    d.targetId = t.targetId;
    d.connKey = t.connKey;
    d.slaveId = t.slaveId;
    d.kind = modbus::Kind::Poll;
    d.poll = t.spec;
    return d;
}

void ModbusBackend::dispatchScheduled(const modbus::Dispatch& d)
{
    QModbusTcpClient* client = getOrCreateClient(d.connKey);
    if (!client) {
        failDispatch(d, QStringLiteral("Modbus 端点无法解析: %1").arg(d.connKey));
        return;
    }
    if (client->state() != QModbusDevice::ConnectedState) {
        // 单连接一在途保证调度器侧已成立：每连接同一时刻仅一条派发，此处暂存槽不覆盖
        m_pendingSend.insert(d.connKey, d);
        if (client->state() == QModbusDevice::UnconnectedState)
            client->connectDevice();
        return;
    }
    sendDispatch(d);
}

void ModbusBackend::sendDispatch(const modbus::Dispatch& d)
{
    QModbusTcpClient* client = m_clients.value(d.connKey);
    if (!client) {
        failDispatch(d, QStringLiteral("Modbus 连接缺失: %1").arg(d.connKey));
        return;
    }
    QModbusReply* reply = nullptr;
    QModbusDataUnit unit = [&d]() {
        if (d.kind == modbus::Kind::Poll)
            return QModbusDataUnit(d.poll.regType, d.poll.startAddr, d.poll.count);
        // 写：必须显式 QVector——花括号 {value} 会被 QModbusDataUnit(RegisterType, int,
        // int size) 重载吞掉（value 当 size，环回测试实测复现，v2.11 旧写法同缺陷）
        if (d.poll.regType == QModbusDataUnit::Coils) {
            // 线圈：非零 → ON (0xFF00)，零 → OFF (0x0000)；Qt 按单点自动用函数码 0x05
            const QVector<quint16> values{ static_cast<quint16>(d.writeValue != 0 ? 0xFF00 : 0x0000) };
            return QModbusDataUnit(QModbusDataUnit::Coils, d.writeAddr, values);
        }
        // 寄存器：函数码 0x06（写单寄存器）
        const QVector<quint16> values{ d.writeValue };
        return QModbusDataUnit(QModbusDataUnit::HoldingRegisters, d.writeAddr, values);
    }();
    reply = d.kind == modbus::Kind::Poll
        ? client->sendReadRequest(unit, d.slaveId)
        : client->sendWriteRequest(unit, d.slaveId);
    if (!reply) {
        failDispatch(d, QStringLiteral("%1请求发送失败: %2")
                            .arg(d.kind == modbus::Kind::Poll ? QStringLiteral("读")
                                                              : QStringLiteral("写"),
                                 client->errorString()));
        return;
    }
    m_active.insert(d.connKey,
                    ActiveRequest{ d.targetId, d.kind, reply, m_clock.elapsed() });
    QObject::connect(reply, &QModbusReply::finished, reply,
                     [this, connKey = d.connKey, reply]() { handleReply(connKey, reply); });
}

// 组装写派发（用户写上下文：regType 取自目标 spec，线圈 0x05/寄存器 0x06 由 send 侧分派）
static modbus::Dispatch makeWriteDispatch(const modbus::PollTarget& t, int addr, quint16 value)
{
    modbus::Dispatch d;
    d.targetId = t.targetId;
    d.connKey = t.connKey;
    d.slaveId = t.slaveId;
    d.kind = modbus::Kind::Write;
    d.poll = t.spec;
    d.writeAddr = addr;
    d.writeValue = value;
    return d;
}

void ModbusBackend::requestReadOnce(const QString& targetId)
{
    const auto it = m_pollById.constFind(targetId);
    if (it == m_pollById.constEnd()) {
        if (m_logCb)
            m_logCb(QStringLiteral("立即读取忽略：未知目标 %1").arg(targetId).toStdString());
        return;
    }
    if (m_polling) {
        m_readOnce.insert(targetId);   // 下一 tick 统一走派发缝（连接忙自动顺延）
        return;
    }
    // 已停/未起轮询：一次性直发（调度器无该连接记账，onResult 侧幂等无害）
    if (m_active.contains(it->connKey) || m_pendingSend.contains(it->connKey)) {
        if (m_logCb)
            m_logCb(QStringLiteral("立即读取忽略：连接忙碌 %1").arg(targetId).toStdString());
        return;
    }
    dispatchScheduled(makePollDispatch(*it));
}

void ModbusBackend::enqueueWriteTarget(const QString& targetId, int addr, quint16 value)
{
    if (m_polling) {
        m_scheduler.enqueueWrite(targetId, addr, value);   // 插队首永不丢弃（规格 §3）；
        return;                                            // 未知目标由调度器忽略
    }
    const auto it = m_pollById.constFind(targetId);
    if (it == m_pollById.constEnd()) {
        if (m_logCb)
            m_logCb(QStringLiteral("写入丢弃：未知目标 %1（未启动轮询）").arg(targetId).toStdString());
        return;
    }
    if (m_active.contains(it->connKey) || m_pendingSend.contains(it->connKey)) {
        if (m_logCb)
            m_logCb(QStringLiteral("写入丢弃：连接忙碌 %1").arg(targetId).toStdString());
        return;
    }
    dispatchScheduled(makeWriteDispatch(*it, addr, value));
}

bool ModbusBackend::inTargetBackoff(const QString& targetId) const
{
    if (!m_polling || !m_clock.isValid()) return false;
    const auto it = m_pollById.constFind(targetId);
    if (it == m_pollById.constEnd()) return false;
    return m_scheduler.connInBackoff(it->connKey, m_clock.elapsed());
}

// ── 响应归位 ────────────────────────────────────────────────────────

void ModbusBackend::handleReply(const QString& connKey, QModbusReply* reply)
{
    const auto it = m_active.find(connKey);
    if (it == m_active.end() || it->reply != reply) {
        // 代际守卫：已被 handleDeviceError/stopPolling 归位或目标已停——本次残响静默丢弃
        reply->deleteLater();
        return;
    }
    const ActiveRequest active = it.value();
    m_active.erase(it);

    const qint64 now = m_clock.elapsed();
    const qint64 elapsed = now - active.startedMs;
    const bool isPoll = active.kind == modbus::Kind::Poll;

    if (reply->error() == QModbusDevice::NoError) {
        if (isPoll) {
            // 轮2 审查 I-1：后端只回调不写 store——单写者=Widget 侧 handleBackendResult
            // （先读 prev 再 updatePoll，↑/↓ 判定依赖旧值；旧双写在回调前预写 store，
            // 队列后到时 prev==cur，高亮静默失效）。store 语义不变：过滤期隐藏行也经
            // 回调入 store。
            const QVector<quint16> values = reply->result().values();
            if (m_resultCb) m_resultCb(active.targetId, values, elapsed, QString());
        } else {
            // 写成功：不改动数值档案（下轮轮询自然刷新），仅回执 + 复位退避
            if (m_resultCb) m_resultCb(active.targetId, {}, elapsed, QString());
            if (m_logCb)
                m_logCb(QStringLiteral("写入成功: %1").arg(active.targetId).toStdString());
        }
        m_scheduler.onResult(connKey, modbus::Outcome::Ok, now);
    } else {
        const QString err = modbusErrorText(isPoll ? QStringLiteral("读取失败")
                                                   : QStringLiteral("写入失败"), reply);
        if (m_resultCb) m_resultCb(active.targetId, {}, elapsed, err);   // store 由单写者落（I-1）
        if (m_logCb) m_logCb(err.toStdString());
        m_scheduler.onResult(connKey, outcomeForDeviceError(reply->error()), now);
    }
    reply->deleteLater();
}

void ModbusBackend::handleDeviceError(const QString& connKey, QModbusDevice::Error error)
{
    if (error == QModbusDevice::NoError) return;
    QModbusTcpClient* client = m_clients.value(connKey);
    if (!client) return;

    const auto ait = m_active.find(connKey);
    if (ait != m_active.end()) {
        // 在途请求被设备级错误击落（超时/断连双路归位之一）：代际守卫防 finished 残响重复
        const ActiveRequest active = ait.value();
        m_active.erase(ait);
        const qint64 now = m_clock.elapsed();
        const QString err = QStringLiteral("Modbus 通讯错误: %1").arg(client->errorString());
        if (m_resultCb) m_resultCb(active.targetId, {}, now - active.startedMs, err);   // store 由单写者落（I-1）
        if (m_logCb) m_logCb(err.toStdString());
        m_scheduler.onResult(connKey, outcomeForDeviceError(error), now);
        return;
    }
    const auto pit = m_pendingSend.find(connKey);
    if (pit != m_pendingSend.end()) {
        // 等待连接就绪的派发：连接失败即归位失败（退避由调度器门限统一节制重试节奏）
        const modbus::Dispatch d = pit.value();
        m_pendingSend.erase(pit);
        failDispatch(d, QStringLiteral("Modbus 连接失败: %1").arg(client->errorString()));
    }
}

void ModbusBackend::failDispatch(const modbus::Dispatch& d, const QString& errText)
{
    const qint64 now = m_clock.isValid() ? m_clock.elapsed() : 0;
    if (m_resultCb) m_resultCb(d.targetId, {}, 0, errText);   // store 由单写者落（I-1）
    if (m_logCb) m_logCb(errText.toStdString());
    // 调度器侧：弹队首已置在途 → onResult 清在途并推进退避；未知连接为无害幂等
    m_scheduler.onResult(d.connKey, modbus::Outcome::CommError, now);
}

// ── 连接池 ──────────────────────────────────────────────────────────

QModbusTcpClient* ModbusBackend::getOrCreateClient(const QString& connKey)
{
    const auto existing = m_clients.constFind(connKey);
    if (existing != m_clients.constEnd()) return existing.value();

    // connKey 约定 "ip:port"（端点列/档案 modbus 端点同源；末段冒号分割容忍 IPv4）
    const int sep = connKey.lastIndexOf(QLatin1Char(':'));
    if (sep <= 0 || sep == connKey.size() - 1) return nullptr;
    bool ok = false;
    const int port = connKey.mid(sep + 1).toInt(&ok);
    if (!ok || port < 1 || port > 65535) return nullptr;
    const QString ip = connKey.left(sep);

    auto* client = new QModbusTcpClient();
    client->setConnectionParameter(QModbusDevice::NetworkAddressParameter, ip);
    client->setConnectionParameter(QModbusDevice::NetworkPortParameter, port);
    client->setTimeout(3000);
    client->setNumberOfRetries(2);
    m_clients[connKey] = client;

    // 客户端信号只在创建处挂一次；backend 非 QObject → 以 client 为 context（3 参），
    // client 析构自动断连（stopPolling"先断信号后关"口径的第二重保险）
    QObject::connect(client, &QModbusDevice::stateChanged, client,
        [this, connKey](QModbusDevice::State state) {
            if (state != QModbusDevice::ConnectedState) return;
            const auto pit = m_pendingSend.find(connKey);
            if (pit == m_pendingSend.end()) return;
            const modbus::Dispatch d = pit.value();
            m_pendingSend.erase(pit);
            sendDispatch(d);
        });
    QObject::connect(client, &QModbusDevice::errorOccurred, client,
        [this, connKey](QModbusDevice::Error error) {
            handleDeviceError(connKey, error);
        });
    return client;
}
