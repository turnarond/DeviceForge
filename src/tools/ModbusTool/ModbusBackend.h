#pragma once
#include "framework/ToolBackend.h"
#include <QModbusTcpClient>
#include <QModbusDataUnit>
#include <QTimer>
#include <QMap>
#include <functional>

// 界面下拉索引 → QModbusDataUnit 寄存器类型映射
// （与 ModbusWidget 下拉顺序一致：Holding/Input/Coils/Discrete Inputs）
inline QModbusDataUnit::RegisterType registerTypeFromComboIndex(int idx)
{
    switch (idx) {
    case 0:  return QModbusDataUnit::HoldingRegisters;
    case 1:  return QModbusDataUnit::InputRegisters;
    case 2:  return QModbusDataUnit::Coils;
    default: return QModbusDataUnit::DiscreteInputs;
    }
}

// 单次请求最大数量（Modbus 协议上限）：
// Holding/Input 寄存器 125 点；Coils/Discrete Inputs 允许 2000 点
inline int maxReadCountForType(QModbusDataUnit::RegisterType type)
{
    switch (type) {
    case QModbusDataUnit::Coils:
    case QModbusDataUnit::DiscreteInputs:
        return 2000;
    default:
        return 125;
    }
}

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

    // 回调（errorMsg 非空 = 读/写失败或连接失败，含 Modbus 异常码文本）
    using LogCallback = std::function<void(const std::string&)>;
    using ResultCallback = std::function<void(const std::string& device, const QVector<quint16>& values, qint64 elapsedMs, const std::string& errorMsg)>;
    void setLogCallback(LogCallback cb) { m_logCb = std::move(cb); }
    void setResultCallback(ResultCallback cb) { m_resultCb = std::move(cb); }

    void readAllRegisters(int slaveId, QModbusDataUnit::RegisterType regType, int startAddr, int count);
    void writeRegister(const std::string& device, int slaveId, QModbusDataUnit::RegisterType regType, int addr, quint16 value);

    // 当前绑定的设备列表（写入对话框选设备用）
    const std::vector<DeviceInfo>& devices() const { return m_devices; }

private:
    QModbusTcpClient* getOrCreateClient(const QString& ip, int port);
    // 挂起动作：连接未就绪时暂存，ConnectedState 后执行；连接失败经结果回调上报
    void enqueueAction(const QString& key, const std::string& ip,
                       const std::function<void(QModbusTcpClient*)>& action);

    struct PendingAction {
        std::string ip;
        std::function<void(QModbusTcpClient*)> action;
    };

    std::vector<DeviceInfo> m_devices;
    AuthInfo m_auth;

    QMap<QString, QModbusTcpClient*> m_clients;
    QMap<QString, PendingAction> m_pendingActions;  // 每客户端最多一个挂起动作（新动作覆盖旧动作）
    LogCallback m_logCb;
    ResultCallback m_resultCb;
};
