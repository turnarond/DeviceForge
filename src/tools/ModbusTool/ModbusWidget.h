#pragma once
#include "framework/ToolWidget.h"
#include <QModbusDataUnit>
#include <QTableWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>

class ModbusBackend;

class ModbusWidget : public ToolWidget {
    Q_OBJECT
public:
    explicit ModbusWidget(QWidget* parent = nullptr);
    ~ModbusWidget() override = default;

    QString toolId() const override { return "com.deviceforge.modbus.test"; }
    QString toolName() const override { return "Modbus 测试"; }
    void onToolStart() override;
    void onToolStop() override;

    void setBackend(ModbusBackend* backend);

private slots:
    void onReadClicked();
    void onAutoRefreshToggled(bool checked);
    void onTimerTick();
    void onWriteClicked();

private:
    void setupUi();
    void appendLog(const QString& msg);

    ModbusBackend* m_backend = nullptr;

    QComboBox*     m_regTypeCombo   = nullptr;
    QSpinBox*      m_startAddrSpin  = nullptr;
    QSpinBox*      m_countSpin      = nullptr;
    QSpinBox*      m_slaveIdSpin    = nullptr;
    QSpinBox*      m_intervalSpin   = nullptr;
    QTableWidget*  m_resultTable    = nullptr;
    QPushButton*   m_readBtn        = nullptr;
    QPushButton*   m_writeBtn       = nullptr;
    QPushButton*   m_autoBtn        = nullptr;
    QTextEdit*     m_logView        = nullptr;
    QTimer*        m_timer          = nullptr;
};
