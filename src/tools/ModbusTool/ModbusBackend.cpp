#include "ModbusBackend.h"
#include <QUrl>
#include <QVariant>
#include <QModbusResponse>
#include <lwlog/lwlog.h>
#include <thread>
#include <chrono>

ModbusBackend::ModbusBackend() {}

ModbusBackend::~ModbusBackend()
{
    for (auto* c : m_clients) { c->disconnectDevice(); delete c; }
    m_clients.clear();
}

int ModbusBackend::svc()
{
    while (ServiceTask::isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
}

void ModbusBackend::bindDevices(const std::vector<DeviceInfo>& devices) { m_devices = devices; }
void ModbusBackend::bindCredentials(const AuthInfo& auth) { m_auth = auth; }
void ModbusBackend::applyConfig(const lwserverbase::config::ConfigValue&) {}

QModbusTcpClient* ModbusBackend::getOrCreateClient(const QString& ip, int port)
{
    QString key = ip + ":" + QString::number(port);
    if (m_clients.contains(key)) return m_clients[key];

    auto* client = new QModbusTcpClient();
    client->setConnectionParameter(QModbusDevice::NetworkAddressParameter, QVariant(ip));
    client->setConnectionParameter(QModbusDevice::NetworkPortParameter, QVariant(port));
    client->setTimeout(3000);
    client->setNumberOfRetries(2);
    m_clients[key] = client;

    // 状态/错误信号只在客户端创建处挂一次，避免每次读取未连接时重复累加 stateChanged
    QObject::connect(client, &QModbusDevice::stateChanged, this,
        [this, key](QModbusDevice::State state) {
            if (state != QModbusDevice::ConnectedState) return;
            auto it = m_pendingActions.find(key);
            if (it == m_pendingActions.end()) return;
            auto entry = it.value();
            m_pendingActions.erase(it);
            entry.action(m_clients.value(key));
        });
    QObject::connect(client, &QModbusDevice::errorOccurred, this,
        [this, key](QModbusDevice::Error error) {
            if (error == QModbusDevice::NoError) return;
            auto it = m_pendingActions.find(key);
            QString err = QStringLiteral("Modbus 连接错误: %1")
                .arg(m_clients.value(key) ? m_clients.value(key)->errorString() : QString());
            if (it != m_pendingActions.end()) {
                // 有挂起动作 → 经结果回调上报失败（不再静默）
                if (m_resultCb) m_resultCb(it.value().ip, {}, 0, err.toStdString());
                m_pendingActions.erase(it);
            }
            if (m_logCb) m_logCb(err.toStdString());
        });

    return client;
}

void ModbusBackend::enqueueAction(const QString& key, const std::string& ip,
                                  const std::function<void(QModbusTcpClient*)>& action)
{
    auto* client = m_clients.value(key);
    if (!client) return;
    if (client->state() == QModbusDevice::ConnectedState) {
        action(client);
        return;
    }
    m_pendingActions[key] = {ip, action};  // 覆盖旧的挂起动作：只执行最近一次请求
    if (client->state() == QModbusDevice::UnconnectedState) {
        client->connectDevice();
    }
}

// 组装 Modbus 异常文本：优先从原始响应提取异常码（0x01-0x0B，最有诊断价值），附 Qt 错误描述
static QString modbusErrorText(const QString& prefix, QModbusReply* reply)
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

void ModbusBackend::readAllRegisters(int slaveId, QModbusDataUnit::RegisterType regType, int startAddr, int count)
{
    if (m_devices.empty()) {
        if (m_logCb) m_logCb("无目标设备");
        return;
    }

    for (const auto& dev : m_devices) {
        int port = dev.port > 0 ? dev.port : 502;
        QString ip = QString::fromStdString(dev.ip);
        QString key = ip + ":" + QString::number(port);
        getOrCreateClient(ip, port);

        auto doRead = [=](QModbusTcpClient* c) {
            auto start = std::chrono::steady_clock::now();
            QModbusDataUnit unit(regType, startAddr, static_cast<quint16>(count));
            auto* reply = c->sendReadRequest(unit, slaveId);
            if (!reply) {
                if (m_resultCb) m_resultCb(dev.ip, {}, 0, "Modbus 读取请求发送失败");
                return;
            }
            QObject::connect(reply, &QModbusReply::finished, [=]() {
                auto end = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                if (reply->error() == QModbusDevice::NoError) {
                    if (m_resultCb) m_resultCb(dev.ip, reply->result().values(), elapsed, "");
                } else {
                    // 透传异常码 + 错误描述（不再把错误伪装成成功的空读取）
                    QString err = modbusErrorText(QStringLiteral("读取失败"), reply);
                    if (m_logCb) m_logCb(err.toStdString());
                    if (m_resultCb) m_resultCb(dev.ip, {}, elapsed, err.toStdString());
                }
                reply->deleteLater();
            });
        };

        enqueueAction(key, dev.ip, doRead);
    }
}

void ModbusBackend::writeRegister(const std::string& device, int slaveId,
                                  QModbusDataUnit::RegisterType regType, int addr, quint16 value)
{
    if (m_devices.empty()) {
        if (m_logCb) m_logCb("无目标设备");
        return;
    }
    // 按 IP 定位设备端口（找不到回退 502），与读取路径共用连接缓存
    int port = 502;
    for (const auto& d : m_devices) {
        if (d.ip == device) { port = d.port > 0 ? d.port : 502; break; }
    }
    QString key = QString::fromStdString(device) + ":" + QString::number(port);
    getOrCreateClient(QString::fromStdString(device), port);

    auto doWrite = [=](QModbusTcpClient* c) {
        QModbusReply* reply = nullptr;
        if (regType == QModbusDataUnit::Coils) {
            // 线圈：非零 → ON (0xFF00)，零 → OFF (0x0000)；函数码 0x05
            reply = c->sendWriteSingleCoil(slaveId, addr, value != 0);
        } else {
            // 寄存器：函数码 0x06
            reply = c->sendWriteSingleRegister(slaveId, addr, value);
        }
        if (!reply) {
            if (m_resultCb) m_resultCb(device, {}, 0, "Modbus 写入请求发送失败");
            return;
        }
        QObject::connect(reply, &QModbusReply::finished, [=]() {
            if (reply->error() != QModbusDevice::NoError) {
                QString err = modbusErrorText(QStringLiteral("写入失败"), reply);
                if (m_logCb) m_logCb(err.toStdString());
                if (m_resultCb) m_resultCb(device, {}, 0, err.toStdString());
            } else {
                if (m_logCb) m_logCb("写入成功: " + QString::fromStdString(device));
                if (m_resultCb) m_resultCb(device, {}, 0, "");
            }
            reply->deleteLater();
        });
    };

    enqueueAction(key, device, doWrite);
}
