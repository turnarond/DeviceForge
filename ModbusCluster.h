#pragma once

#include <QWidget>
#include <QDateTime>
#include <QMap>
#include <QTextEdit>
#include <QStringList>
#include <QTimer>
#include <QModbusTcpClient>
#include <QModbusDataUnit>

namespace Ui {
    class TabModbusCluster;
}

class DeployMaster;

class ModbusCluster : public QWidget
{
    Q_OBJECT

public:
    explicit ModbusCluster(DeployMaster* parentWindow, QWidget* parent = nullptr);
    ~ModbusCluster();

    // 新增：由主窗口设置设备列表（IP:Port 或 IP:Port:SlaveID）
    void setTargetDevices(const QStringList& devices);

private slots:
    void on_btn_readAll_clicked();
    void on_btn_startAutoRefresh_toggled(bool checked);
    void on_refreshTimer_timeout();
    void on_btn_writeSelected_clicked();
    void on_btn_updateDeviceList_clicked();

    void appendLog(const QString& log);

private:
    QStringList getTargetDevices(); // 从内部缓存返回
    QModbusDataUnit::RegisterType getRegisterType() const;
    int getSlaveIdFromDeviceStr(const QString& devStr) const;
    QString extractIpPort(const QString& devStr) const;

    void startReadingAll();
    void readDevice(const QString& devStr, const int port, const int slave);
    void updateTableWithResult(const QString& devStr, const QVector<quint16>& values);
    void clearTable();
    void updateDeviceDurationDisplay();

    QStringList getSelectedWriteDevices() const; // 从 list_selectedDevices 获取选中项
    QVector<quint16> generateWriteValues(int count) const; // 根据 radio 按钮生成值

private:
    Ui::TabModbusCluster* ui;
    DeployMaster* m_mainWindow;
    QTextEdit* txt_globalLog = nullptr;

    QStringList m_targetDevices; // 替代原来的 txt_deviceList

    QMap<QString, QModbusTcpClient*> m_clients; // key: "IP:Port"
    QMap<QString, QVector<quint16>> m_currentValues;
    QMap<QString, qint64> m_readDurations; // 记录每个设备读取耗时（ms）

    QTimer* m_refreshTimer = nullptr;
    int m_pendingReadCount = 0;
};