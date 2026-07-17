#include "ModbusCluster.h"
#include "DeployMaster.h"
#include "ui_tab_modbus_cluster_test.h"

#include <QMessageBox>
#include <QHeaderView>
#include <QModbusTcpClient>
#include <QModbusDataUnit>
#include <QTime>
#include <QRandomGenerator>
#include <QListWidgetItem>

ModbusCluster::ModbusCluster(DeployMaster* parentWindow, QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::TabModbusCluster)
    , m_mainWindow(parentWindow)
{
    ui->setupUi(this);
    txt_globalLog = m_mainWindow->getGlobalLogItem();
    m_targetDevices = getTargetDevices();

    // 按钮连接
    connect(ui->btn_readAll, &QPushButton::clicked, this, &ModbusCluster::on_btn_readAll_clicked);
    connect(ui->btn_startAutoRefresh, &QPushButton::toggled, this, &ModbusCluster::on_btn_startAutoRefresh_toggled);
    connect(ui->btn_writeSelected, &QPushButton::clicked, this, &ModbusCluster::on_btn_writeSelected_clicked);
    connect(ui->btn_updateDeviceList, &QPushButton::clicked, this, &ModbusCluster::on_btn_updateDeviceList_clicked);

    // 初始化表格
    clearTable();

    // 定时器
    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &ModbusCluster::on_refreshTimer_timeout);

    // 默认选中“按表格值写入”
    ui->radio_writeSpecificValues->setChecked(true);
}

ModbusCluster::~ModbusCluster()
{
    for (auto client : std::as_const(m_clients)) {
        if (client && client->state() == QModbusClient::ConnectedState)
            client->disconnectDevice();
        client->deleteLater();
    }
    delete ui;
}

// ==================== 外部接口 ====================

void ModbusCluster::setTargetDevices(const QStringList& devices)
{
    m_targetDevices = devices;
    on_btn_updateDeviceList_clicked(); // 自动刷新 UI
}

QStringList ModbusCluster::getTargetDevices()
{
    return m_mainWindow->getTargetIPList();
}

// ==================== 工具函数 ====================

QModbusDataUnit::RegisterType ModbusCluster::getRegisterType() const
{
    return ui->cmb_regType->currentIndex() == 0
        ? QModbusDataUnit::HoldingRegisters
        : QModbusDataUnit::Coils;
}

int ModbusCluster::getSlaveIdFromDeviceStr(const QString& devStr) const
{
    QStringList parts = devStr.split(':');
    if (parts.size() >= 3) {
        bool ok;
        int id = parts[2].toInt(&ok);
        if (ok && id >= 0 && id <= 247) return id;
    }
    return ui->lineEdit_slaveId->text().toInt(); // 使用 UI 中的默认从站 ID
}

QString ModbusCluster::extractIpPort(const QString& devStr) const
{
    QStringList parts = devStr.split(':');
    if (parts.size() >= 2) {
        return parts[0] + ":" + parts[1];
    }
    return devStr;
}

// ==================== UI 控制 ====================

void ModbusCluster::on_btn_updateDeviceList_clicked()
{
    ui->list_selectedDevices->clear();
    for (const QString& dev : m_targetDevices) {
        QListWidgetItem* item = new QListWidgetItem(dev, ui->list_selectedDevices);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked); // 默认全选
    }
}

QStringList ModbusCluster::getSelectedWriteDevices() const
{
    QStringList selected;
    for (int i = 0; i < ui->list_selectedDevices->count(); ++i) {
        QListWidgetItem* item = ui->list_selectedDevices->item(i);
        if (item->checkState() == Qt::Checked) {
            selected << item->text();
        }
    }
    return selected;
}

// ==================== 读取逻辑 ====================

void ModbusCluster::on_btn_readAll_clicked()
{
    startReadingAll();
}

void ModbusCluster::on_btn_startAutoRefresh_toggled(bool checked)
{
    if (checked) {
        int interval = ui->spin_refreshInterval->value() * 1000;
        m_refreshTimer->start(interval);
        ui->btn_startAutoRefresh->setText("🔁 定时刷新（开启）");
        startReadingAll();
    }
    else {
        m_refreshTimer->stop();
        ui->btn_startAutoRefresh->setText("🔁 定时刷新（关闭）");
    }
}

void ModbusCluster::on_refreshTimer_timeout()
{
    startReadingAll();
}

void ModbusCluster::startReadingAll()
{
    if (m_targetDevices.isEmpty()) {
        m_targetDevices = getTargetDevices();
        if (m_targetDevices.isEmpty()) {
            QMessageBox::warning(this, "警告", "未设置目标设备");
            return;
        }
    }

    int length = ui->spin_length->value();
    int startAddr = ui->spin_startAddr->value();
    int port = ui->lineEdit_port->text().toInt();
    int slave = ui->lineEdit_slaveId->text().toInt();

    m_currentValues.clear();
    m_readDurations.clear();
    m_pendingReadCount = m_targetDevices.size();

    // 设置表格：时间 + 地址 + 待写值 + N个设备
    int colCount = 3 + m_targetDevices.size(); // 0:时间, 1:地址, 2:待写值, 3~:设备
    ui->table_registerMatrix->setColumnCount(colCount);

    QStringList headers;
    headers << "读取时间" << "寄存器地址" << "待写值";
    for (const QString& dev : m_targetDevices) {
        headers << dev;
    }
    ui->table_registerMatrix->setHorizontalHeaderLabels(headers);
    ui->table_registerMatrix->setRowCount(length);

    QString currentTime = QTime::currentTime().toString("HH:mm:ss");
    for (int i = 0; i < length; ++i) {
        // 时间列
        auto* timeItem = new QTableWidgetItem(currentTime);
        timeItem->setTextAlignment(Qt::AlignCenter);
        timeItem->setFlags(timeItem->flags() & ~Qt::ItemIsEditable);
        ui->table_registerMatrix->setItem(i, 0, timeItem);

        // 地址列
        auto* addrItem = new QTableWidgetItem(QString::number(startAddr + i));
        addrItem->setFlags(addrItem->flags() & ~Qt::ItemIsEditable);
        ui->table_registerMatrix->setItem(i, 1, addrItem);

        // 待写值列（可编辑）
        auto* writeItem = new QTableWidgetItem("0");
        writeItem->setTextAlignment(Qt::AlignCenter);
        ui->table_registerMatrix->setItem(i, 2, writeItem);
    }

    // 开始读取每个设备
    for (const QString& dev : m_targetDevices) {
        readDevice(dev, port, slave);
    }

    ui->table_registerMatrix->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->table_registerMatrix->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->table_registerMatrix->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
}

void ModbusCluster::readDevice(const QString& devStr, const int port, const int slave)
{
    QString ipPort = extractIpPort(devStr);
    int slaveId = getSlaveIdFromDeviceStr(devStr);

    if (!m_clients.contains(ipPort)) {
        QModbusTcpClient* client = new QModbusTcpClient(this);
        m_clients[ipPort] = client;

        connect(client, &QModbusClient::stateChanged, this, [this, ipPort](QModbusClient::State state) {
            if (state == QModbusClient::UnconnectedState) {
                appendLog(QString("设备 %1 断开").arg(ipPort));
            }
            });
        connect(client, &QModbusClient::errorOccurred, this, [this, ipPort](QModbusDevice::Error error) {
            if (error != QModbusDevice::NoError) {
                appendLog(QString("设备 %1 错误: %2").arg(ipPort).arg(m_clients[ipPort]->errorString()));
            }
            });
    }

    QModbusTcpClient* client = m_clients[ipPort];
    if (client->state() != QModbusClient::ConnectedState) {
        QStringList parts = ipPort.split(':');
        QString ip = parts[0];
        int port = parts.size() > 1 ? parts[1].toInt() : ui->lineEdit_port->text().toInt();
        client->setConnectionParameter(QModbusDevice::NetworkAddressParameter, ip);
        client->setConnectionParameter(QModbusDevice::NetworkPortParameter, port);
        if (!client->connectDevice()) {
            appendLog(QString("无法连接 %1: %2").arg(ipPort).arg(client->errorString()));
            m_pendingReadCount--;
            return;
        }
    }

    QModbusDataUnit::RegisterType regType = getRegisterType();
    int startAddr = ui->spin_startAddr->value();
    int length = ui->spin_length->value();

    // 记录开始时间
    QDateTime startTime = QDateTime::currentDateTime();

    if (auto* reply = client->sendReadRequest(QModbusDataUnit(regType, startAddr, length), slaveId)) {
        if (!reply->isFinished()) {
            connect(reply, &QModbusReply::finished, this, [this, devStr, ipPort, reply, startTime]() {
                qint64 elapsed = startTime.msecsTo(QDateTime::currentDateTime());
                m_readDurations[devStr] = elapsed;

                if (reply->error() == QModbusDevice::NoError) {
                    const QModbusDataUnit result = reply->result();
                    QVector<quint16> values;
                    for (uint i = 0; i < result.valueCount(); ++i) {
                        values.append(result.value(i));
                    }
                    updateTableWithResult(devStr, values);
                    m_currentValues[devStr] = values;
                }
                else {
                    appendLog(QString("读取 %1 失败: %2").arg(devStr).arg(reply->errorString()));
                    updateTableWithResult(devStr, QVector<quint16>(ui->spin_length->value(), 0xFFFF));
                }

                updateDeviceDurationDisplay();
                reply->deleteLater();
                m_pendingReadCount--;
                });
        }
        else {
            reply->deleteLater();
            m_pendingReadCount--;
        }
    }
    else {
        appendLog(QString("发送读请求失败 %1: %2").arg(ipPort).arg(client->errorString()));
        m_pendingReadCount--;
    }
}

void ModbusCluster::updateTableWithResult(const QString& devStr, const QVector<quint16>& values)
{
    int colIndex = m_targetDevices.indexOf(devStr) + 3; // 跳过 0,1,2 列
    if (colIndex < 3) return;

    int rowCount = qMin(values.size(), ui->table_registerMatrix->rowCount());
    for (int row = 0; row < rowCount; ++row) {
        QString text;
        if (getRegisterType() == QModbusDataUnit::Coils) {
            text = values[row] ? "ON" : "OFF";
        }
        else {
            text = QString::number(values[row]);
        }
        auto* item = new QTableWidgetItem(text);
        item->setTextAlignment(Qt::AlignCenter);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        ui->table_registerMatrix->setItem(row, colIndex, item);
    }
}

void ModbusCluster::updateDeviceDurationDisplay()
{
    QString info = "⏱️ 设备操作耗时:\n";
    for (const QString& dev : m_targetDevices) {
        if (m_readDurations.contains(dev)) {
            info += QString("• %1: %2 ms\n").arg(dev).arg(m_readDurations[dev]);
        }
        else {
            info += QString("• %1: --\n").arg(dev);
        }
    }
    ui->label_duration->setText(info);
}

void ModbusCluster::clearTable()
{
    ui->table_registerMatrix->clearContents();
    ui->table_registerMatrix->setRowCount(0);
    ui->table_registerMatrix->setColumnCount(3);
    ui->table_registerMatrix->setHorizontalHeaderLabels({ "读取时间", "寄存器地址", "待写值" });
}

// ==================== 写入逻辑 ====================

QVector<quint16> ModbusCluster::generateWriteValues(int count) const
{
    QVector<quint16> values;
    if (ui->radio_writeSingleValue->isChecked()) {
        quint16 val = ui->spin_singleValue->value();
        values.fill(val, count);
    }
    else if (ui->radio_writeRandomValues->isChecked()) {
        auto gen = QRandomGenerator::global();
        for (int i = 0; i < count; ++i) {
            if (getRegisterType() == QModbusDataUnit::Coils) {
                values.append(gen->bounded(2)); // 0 or 1
            }
            else {
                values.append(static_cast<quint16>(gen->bounded(65536)));
            }
        }
    }
    else { // radio_writeSpecificValues
        values.resize(count);
        for (int i = 0; i < count; ++i) {
            QTableWidgetItem* item = ui->table_registerMatrix->item(i, 2); // 第2列：待写值
            QString text = item ? item->text().trimmed() : "0";
            bool ok = false;
            if (getRegisterType() == QModbusDataUnit::Coils) {
                if (text == "1" || text.toLower() == "on" || text.toLower() == "true") {
                    values[i] = 1;
                }
                else {
                    values[i] = 0;
                }
            }
            else {
                quint16 val = text.toUShort(&ok, 10);
                if (!ok) val = 0;
                values[i] = val;
            }
        }
    }
    return values;
}

void ModbusCluster::on_btn_writeSelected_clicked()
{
    QStringList selectedDevices = getSelectedWriteDevices();
    if (selectedDevices.isEmpty()) {
        QMessageBox::warning(this, "警告", "请至少选择一个设备进行写入");
        return;
    }

    int length = ui->spin_length->value();
    QVector<quint16> writeValues = generateWriteValues(length);
    int startAddr = ui->spin_startAddr->value();
    QModbusDataUnit::RegisterType regType = getRegisterType();

    int pendingWrites = 0;
    for (const QString& devStr : selectedDevices) {
        QString ipPort = extractIpPort(devStr);
        int slaveId = getSlaveIdFromDeviceStr(devStr);

        if (!m_clients.contains(ipPort)) {
            appendLog(QString("跳过未初始化设备: %1").arg(devStr));
            continue;
        }

        QModbusTcpClient* client = m_clients[ipPort];
        if (client->state() != QModbusClient::ConnectedState) {
            appendLog(QString("设备未连接: %1").arg(devStr));
            continue;
        }

        QModbusDataUnit writeUnit(regType, startAddr, writeValues);
        if (auto* reply = client->sendWriteRequest(writeUnit, slaveId)) {
            pendingWrites++;
            connect(reply, &QModbusReply::finished, this, [this, devStr, reply, &pendingWrites]() {
                if (reply->error() == QModbusDevice::NoError) {
                    appendLog(QString("✅ 写入成功 %1").arg(devStr));
                }
                else {
                    appendLog(QString("❌ 写入失败 %1: %2").arg(devStr).arg(reply->errorString()));
                }
                reply->deleteLater();
                pendingWrites--;
                if (pendingWrites == 0) {
                    // 可选：写完后自动刷新
                    // startReadingAll();
                }
                });
        }
        else {
            appendLog(QString("❌ 发送写请求失败 %1: %2").arg(devStr).arg(client->errorString()));
        }
    }
}

// ==================== 日志 ====================

void ModbusCluster::appendLog(const QString& log)
{
    if (txt_globalLog) {
        txt_globalLog->append(QDateTime::currentDateTime().toString("hh:mm:ss") + " " + log);
    }
}