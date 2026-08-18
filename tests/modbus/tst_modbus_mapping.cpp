#include <QtTest/QtTest>
#include "tools/ModbusTool/ModbusBackend.h"

// registerTypeFromComboIndex + maxReadCountForType 纯逻辑测试（不连设备）：
// 覆盖 #1 下拉索引 → QModbusDataUnit 寄存器类型映射（Holding/Input/Coils/Discrete）
// 与 #2 按类型区分的单次请求数量上限（寄存器 125 / 线圈与离散量 2000）
class TstModbusMapping : public QObject {
    Q_OBJECT
private slots:
    void comboIndexMapping() {
        // 与 ModbusWidget 下拉顺序一致：Holding Register / Input Register / Coils / Discrete Inputs
        QCOMPARE(registerTypeFromComboIndex(0), QModbusDataUnit::HoldingRegisters);
        QCOMPARE(registerTypeFromComboIndex(1), QModbusDataUnit::InputRegisters);
        QCOMPARE(registerTypeFromComboIndex(2), QModbusDataUnit::Coils);
        QCOMPARE(registerTypeFromComboIndex(3), QModbusDataUnit::DiscreteInputs);
    }
    void countCapByType() {
        // 寄存器：125；线圈/离散量：2000（Modbus 协议上限）
        QCOMPARE(maxReadCountForType(QModbusDataUnit::HoldingRegisters), 125);
        QCOMPARE(maxReadCountForType(QModbusDataUnit::InputRegisters), 125);
        QCOMPARE(maxReadCountForType(QModbusDataUnit::Coils), 2000);
        QCOMPARE(maxReadCountForType(QModbusDataUnit::DiscreteInputs), 2000);
    }
};
QTEST_MAIN(TstModbusMapping)
#include "tst_modbus_mapping.moc"
