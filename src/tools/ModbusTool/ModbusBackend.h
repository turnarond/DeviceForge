#pragma once
#include "framework/ToolBackend.h"
#include <QModbusTcpClient>
#include <QModbusDataUnit>
#include <QTimer>
#include <QMap>
#include <functional>
#include <atomic>

class ModbusBackend : public ToolBackend {
public:
    ModbusBackend();
    ~ModbusBackend() override;

    int svc() override;

    std::string toolId() const override { return "com.deviceforge.modbus.test"; }
    std::string toolName() const override { return "Modbus 测试"; }
    std::string toolVersion() const override { return "2.1.0"; }
    std::string toolCategory() const override { return "test"; }
    std::string toolIcon() const override { return "modbus_test"; }

    void bindDevices(const std::vector<DeviceInfo>& devices) override;
    void bindCredentials(const AuthInfo& auth) override;
    void applyConfig(const lwserverbase::config::ConfigValue& config) override;

    // 回调
    using LogCallback = std::function<void(const std::string&)>;
    using ResultCallback = std::function<void(const std::string& device, const QVector<quint16>& values, qint64 elapsedMs)>;
    void setLogCallback(LogCallback cb) { m_logCb = std::move(cb); }
    void setResultCallback(ResultCallback cb) { m_resultCb = std::move(cb); }

    void readAllRegisters(int slaveId, QModbusDataUnit::RegisterType regType, int startAddr, int count);
    void writeRegister(const std::string& device, int slaveId, QModbusDataUnit::RegisterType regType, int addr, quint16 value);

private:
    QModbusTcpClient* getOrCreateClient(const QString& ip, int port);

    std::vector<DeviceInfo> m_devices;
    AuthInfo m_auth;
    std::atomic<bool> m_cancelled{false};

    QMap<QString, QModbusTcpClient*> m_clients;
    LogCallback m_logCb;
    ResultCallback m_resultCb;
    int m_pendingReads = 0;
};
