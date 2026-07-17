#include "ModbusBackend.h"
#include <QUrl>
#include <QVariant>
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
    return client;
}

static QModbusDataUnit::RegisterType mapRegType(int idx)
{
    switch (idx) {
    case 0: return QModbusDataUnit::HoldingRegisters;
    case 1: return QModbusDataUnit::InputRegisters;
    case 2: return QModbusDataUnit::Coils;
    default: return QModbusDataUnit::DiscreteInputs;
    }
}

void ModbusBackend::readAllRegisters(int slaveId, QModbusDataUnit::RegisterType regType, int startAddr, int count)
{
    if (m_devices.empty()) {
        if (m_logCb) m_logCb("无目标设备");
        return;
    }
    m_pendingReads = static_cast<int>(m_devices.size());

    for (const auto& dev : m_devices) {
        int port = dev.port > 0 ? dev.port : 502;
        QString ip = QString::fromStdString(dev.ip);
        auto* client = getOrCreateClient(ip, port);

        auto doRead = [=](QModbusTcpClient* c) {
            auto start = std::chrono::steady_clock::now();
            QModbusDataUnit unit(regType, startAddr, static_cast<quint16>(count));
            auto* reply = c->sendReadRequest(unit, slaveId);
            if (!reply) { m_pendingReads--; return; }
            QObject::connect(reply, &QModbusReply::finished, [=]() {
                auto end = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                if (reply->error() == QModbusDevice::NoError) {
                    if (m_resultCb) m_resultCb(dev.ip, reply->result().values(), elapsed);
                } else {
                    if (m_resultCb) m_resultCb(dev.ip, {}, elapsed);
                }
                m_pendingReads--;
                reply->deleteLater();
            });
        };

        if (client->state() != QModbusDevice::ConnectedState) {
            QObject::connect(client, &QModbusTcpClient::stateChanged,
                [=](QModbusDevice::State state) {
                    if (state == QModbusDevice::ConnectedState) doRead(client);
                });
            client->connectDevice();
        } else {
            doRead(client);
        }
    }
}

void ModbusBackend::writeRegister(const std::string& device, int slaveId, QModbusDataUnit::RegisterType regType, int addr, quint16 value)
{
    auto* client = getOrCreateClient(QString::fromStdString(device), 502);
    QModbusDataUnit unit(regType, addr, 1);
    unit.setValue(0, value);

    auto doWrite = [=](QModbusTcpClient* c) {
        auto* reply = c->sendWriteRequest(unit, slaveId);
        if (reply) QObject::connect(reply, &QModbusReply::finished, reply, &QObject::deleteLater);
    };

    if (client->state() != QModbusDevice::ConnectedState) {
        QObject::connect(client, &QModbusTcpClient::stateChanged,
            [=](QModbusDevice::State state) {
                if (state == QModbusDevice::ConnectedState) doWrite(client);
            });
        client->connectDevice();
    } else {
        doWrite(client);
    }
}
