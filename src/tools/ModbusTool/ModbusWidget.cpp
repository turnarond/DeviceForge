#include "ModbusWidget.h"
#include "ModbusBackend.h"
#include "config/ConfigStore.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QHeaderView>
#include <QDateTime>
#include <QDialog>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QColor>
#include <QBrush>

// 双主题状态色：QSS 无法按行设置 QTableWidgetItem 前景色（item 非 QWidget，无 QSS 选择器），
// 故按当前主题从 ConfigStore 取色；色值须与两套 QSS 常量一致（暗 #40C8A0/#E85848，亮 #2FA88A/#D6453A）
static QColor themeStatusColor(bool success)
{
    const QString theme = ConfigStore::instance()
        .load(QStringLiteral("appearance"), QStringLiteral("theme"))
        .value(QStringLiteral("theme")).toString();
    const bool dark = (theme != QLatin1String("light"));
    return success ? QColor(dark ? QStringLiteral("#40C8A0") : QStringLiteral("#2FA88A"))
                   : QColor(dark ? QStringLiteral("#E85848") : QStringLiteral("#D6453A"));
}

// 最小写入对话框：目标设备 + 寄存器类型 + 地址 + 值（单点写入，0x06/0x05）
class WriteDialog : public QDialog {
public:
    explicit WriteDialog(const std::vector<DeviceInfo>& devices, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("Modbus 写入"));
        auto* lay = new QVBoxLayout(this);
        auto* form = new QFormLayout();

        m_devCombo = new QComboBox(this);
        for (const auto& d : devices) {
            QString label = QString::fromStdString(d.ip);
            if (d.port > 0) label += QStringLiteral(":") + QString::number(d.port);
            m_devCombo->addItem(label, QString::fromStdString(d.ip));
        }
        form->addRow(QStringLiteral("设备:"), m_devCombo);

        m_typeCombo = new QComboBox(this);
        m_typeCombo->addItems({QStringLiteral("寄存器 (0x06)"), QStringLiteral("线圈 (0x05)")});
        form->addRow(QStringLiteral("类型:"), m_typeCombo);

        m_addrSpin = new QSpinBox(this);
        m_addrSpin->setRange(0, 65535);
        form->addRow(QStringLiteral("地址:"), m_addrSpin);

        m_valueSpin = new QSpinBox(this);
        m_valueSpin->setRange(0, 65535);
        m_valueSpin->setToolTip(QStringLiteral("线圈：0 = 断开，非 0 = 闭合"));
        form->addRow(QStringLiteral("值:"), m_valueSpin);

        lay->addLayout(form);
        auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        btns->button(QDialogButtonBox::Ok)->setText(QStringLiteral("写入"));
        btns->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
        connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
        lay->addWidget(btns);
    }

    QString deviceIp() const { return m_devCombo->currentData().toString(); }
    QModbusDataUnit::RegisterType regType() const
    { return m_typeCombo->currentIndex() == 0 ? QModbusDataUnit::HoldingRegisters
                                              : QModbusDataUnit::Coils; }
    int addr() const { return m_addrSpin->value(); }
    quint16 value() const { return static_cast<quint16>(m_valueSpin->value()); }

private:
    QComboBox* m_devCombo = nullptr;
    QComboBox* m_typeCombo = nullptr;
    QSpinBox*  m_addrSpin = nullptr;
    QSpinBox*  m_valueSpin = nullptr;
};

ModbusWidget::ModbusWidget(QWidget* parent) : ToolWidget(parent)
{
    setupUi();

    // 恢复最近一条 modbus.slave 配置
    const auto hist = ConfigStore::instance().list(QStringLiteral("modbus.slave"), 1);
    if (!hist.isEmpty()) {
        const QVariantMap& h = hist.first();
        if (m_regTypeCombo)
            m_regTypeCombo->setCurrentIndex(h.value(QStringLiteral("regType")).toInt());
        if (m_startAddrSpin)
            m_startAddrSpin->setValue(h.value(QStringLiteral("startAddr")).toInt());
        if (m_countSpin)
            m_countSpin->setValue(h.value(QStringLiteral("count")).toInt());
        if (m_slaveIdSpin)
            m_slaveIdSpin->setValue(h.value(QStringLiteral("slaveId")).toInt());
        if (m_intervalSpin)
            m_intervalSpin->setValue(h.value(QStringLiteral("intervalMs")).toInt());
    }
    // 恢复后按寄存器类型收紧数量上限（如旧配置保存了超过当前类型上限的数量）
    updateCountMax();
}

void ModbusWidget::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    // 配置区
    auto* cfg = new QGroupBox("Modbus 设置", this);
    auto* cfgLayout = new QHBoxLayout(cfg);
    cfgLayout->addWidget(new QLabel("寄存器类型:", this));
    m_regTypeCombo = new QComboBox(this);
    m_regTypeCombo->addItems({"Holding Register", "Input Register", "Coils", "Discrete Inputs"});
    cfgLayout->addWidget(m_regTypeCombo);
    cfgLayout->addWidget(new QLabel("起始地址:", this));
    m_startAddrSpin = new QSpinBox(this); m_startAddrSpin->setRange(0, 65535); m_startAddrSpin->setValue(0);
    cfgLayout->addWidget(m_startAddrSpin);
    cfgLayout->addWidget(new QLabel("数量:", this));
    m_countSpin = new QSpinBox(this); m_countSpin->setRange(1, 125); m_countSpin->setValue(10);
    // 数量上限随寄存器类型切换：Holding/Input 125，Coils/Discrete 2000（Modbus 协议上限）
    connect(m_regTypeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ModbusWidget::updateCountMax);
    cfgLayout->addWidget(m_countSpin);
    cfgLayout->addWidget(new QLabel("从站ID:", this));
    m_slaveIdSpin = new QSpinBox(this); m_slaveIdSpin->setRange(1, 247); m_slaveIdSpin->setValue(1);
    cfgLayout->addWidget(m_slaveIdSpin);
    cfgLayout->addWidget(new QLabel("间隔(ms):", this));
    m_intervalSpin = new QSpinBox(this); m_intervalSpin->setRange(100, 60000); m_intervalSpin->setValue(1000);
    cfgLayout->addWidget(m_intervalSpin);
    mainLayout->addWidget(cfg);

    // 操作区
    auto* act = new QHBoxLayout();
    m_readBtn = new QPushButton("读取", this);
    m_autoBtn = new QPushButton("自动刷新", this); m_autoBtn->setCheckable(true);
    m_writeBtn = new QPushButton("写入", this);
    act->addWidget(m_readBtn); act->addWidget(m_autoBtn); act->addWidget(m_writeBtn);
    act->addStretch();
    mainLayout->addLayout(act);

    // 结果表
    m_resultTable = new QTableWidget(0, 2, this);
    m_resultTable->setHorizontalHeaderLabels({"设备", "寄存器值"});
    m_resultTable->horizontalHeader()->setStretchLastSection(true);
    m_resultTable->setAlternatingRowColors(true);
    mainLayout->addWidget(m_resultTable, 1);

    // 定时器
    m_timer = new QTimer(this);
    connect(m_readBtn, &QPushButton::clicked, this, &ModbusWidget::onReadClicked);
    connect(m_autoBtn, &QPushButton::toggled, this, &ModbusWidget::onAutoRefreshToggled);
    connect(m_writeBtn, &QPushButton::clicked, this, &ModbusWidget::onWriteClicked);
    connect(m_timer, &QTimer::timeout, this, &ModbusWidget::onTimerTick);
}

void ModbusWidget::setBackend(ModbusBackend* backend)
{
    m_backend = backend;
    if (!m_backend) return;
    m_backend->setLogCallback([this](const std::string& msg) {
        QMetaObject::invokeMethod(this, [this, msg]() { appendLog(QString::fromStdString(msg)); }, Qt::QueuedConnection);
    });
    m_backend->setResultCallback([this](const std::string& device, const QVector<quint16>& values,
                                        qint64 elapsedMs, const std::string& errorMsg) {
        QMetaObject::invokeMethod(this, [this, device, values, elapsedMs, errorMsg]() {
            QString ip = QString::fromStdString(device);
            auto items = m_resultTable->findItems(ip, Qt::MatchExactly);
            QTableWidgetItem* item;
            if (items.isEmpty()) {
                int row = m_resultTable->rowCount();
                m_resultTable->insertRow(row);
                item = new QTableWidgetItem(ip);
                m_resultTable->setItem(row, 0, item);
                m_resultTable->setItem(row, 1, new QTableWidgetItem());
            } else {
                item = items.first();
            }
            auto* valCell = m_resultTable->item(item->row(), 1);
            QString valStr;
            if (!errorMsg.empty()) {
                // 错误行：异常码 + 描述（红色，双主题取色）
                valStr = QString::fromStdString(errorMsg) + " (" + QString::number(elapsedMs) + "ms)";
                valCell->setForeground(themeStatusColor(false));
            } else if (values.isEmpty()) {
                // 空值 + 无错误 = 写入成功
                valStr = QStringLiteral("写入成功 (") + QString::number(elapsedMs) + "ms)";
                valCell->setForeground(themeStatusColor(true));
            } else {
                for (auto v : values) valStr += QString::number(v) + " ";
                valStr = valStr.trimmed() + " (" + QString::number(elapsedMs) + "ms)";
                valCell->setForeground(QBrush());  // 恢复默认前景色
            }
            valCell->setText(valStr);
        }, Qt::QueuedConnection);
    });
}

void ModbusWidget::onToolStart() { appendLog("Modbus 测试工具已就绪"); }
void ModbusWidget::onToolStop() { appendLog("Modbus 测试工具已停止"); }

void ModbusWidget::onReadClicked()
{
    if (!m_backend) return;
    // 设备总线未对 ModbusBackend 调 bindDevices，读/写前从 ConfigStore 同源同步设备列表
    syncDevicesToBackend();
    // 保存当前从站配置
    {
        QVariantMap v{
            {QStringLiteral("regType"), m_regTypeCombo ? m_regTypeCombo->currentIndex() : 0},
            {QStringLiteral("startAddr"), m_startAddrSpin ? m_startAddrSpin->value() : 0},
            {QStringLiteral("count"), m_countSpin ? m_countSpin->value() : 10},
            {QStringLiteral("slaveId"), m_slaveIdSpin ? m_slaveIdSpin->value() : 1},
            {QStringLiteral("intervalMs"), m_intervalSpin ? m_intervalSpin->value() : 1000},
            {QStringLiteral("updated_at"), QDateTime::currentMSecsSinceEpoch()}
        };
        const int sid = m_slaveIdSpin ? m_slaveIdSpin->value() : 1;
        ConfigStore::instance().save(QStringLiteral("modbus.slave"),
                                     QStringLiteral("slave:%1").arg(sid), v);
    }
    QModbusDataUnit::RegisterType t = registerTypeFromComboIndex(m_regTypeCombo->currentIndex());
    m_resultTable->setRowCount(0);
    appendLog("开始读取...");
    m_backend->readAllRegisters(m_slaveIdSpin->value(), t, m_startAddrSpin->value(), m_countSpin->value());
}

void ModbusWidget::onAutoRefreshToggled(bool checked)
{
    if (checked) {
        m_timer->start(m_intervalSpin->value());
        m_autoBtn->setText("停止刷新");
        appendLog("自动刷新已开启");
    } else {
        m_timer->stop();
        m_autoBtn->setText("自动刷新");
        appendLog("自动刷新已停止");
    }
}

void ModbusWidget::onTimerTick()
{
    if (m_autoBtn->isChecked()) onReadClicked();
}

void ModbusWidget::onWriteClicked()
{
    if (!m_backend) return;
    syncDevicesToBackend();
    const auto& devices = m_backend->devices();
    if (devices.empty()) {
        appendLog("无目标设备，请在设备总线中添加设备");
        return;
    }
    // 最小写入：单点写入（寄存器 0x06 / 线圈 0x05），不做多写
    WriteDialog dlg(devices, this);
    if (dlg.exec() != QDialog::Accepted) return;
    m_backend->writeRegister(dlg.deviceIp().toStdString(), m_slaveIdSpin->value(),
                             dlg.regType(), dlg.addr(), dlg.value());
    appendLog(QStringLiteral("已发起写入请求: %1 地址=%2 值=%3")
        .arg(dlg.deviceIp()).arg(dlg.addr()).arg(dlg.value()));
}

void ModbusWidget::updateCountMax()
{
    const int max = maxReadCountForType(registerTypeFromComboIndex(m_regTypeCombo->currentIndex()));
    m_countSpin->setMaximum(max);  // setMaximum 会自动夹紧当前值
    if (m_countSpin->value() > max) m_countSpin->setValue(max);
}

void ModbusWidget::syncDevicesToBackend()
{
    if (!m_backend) return;
    std::vector<DeviceInfo> devices;
    // device.list 与 DeviceBusWidget 持久化同源（主窗口未对 ModbusBackend 调 bindDevices）
    const auto rows = ConfigStore::instance().list(QStringLiteral("device.list"), 20);
    for (const auto& row : rows) {
        DeviceInfo di;
        di.ip = row.value(QStringLiteral("ip")).toString().toStdString();
        di.port = row.value(QStringLiteral("port")).toInt();
        di.note = row.value(QStringLiteral("note")).toString().toStdString();
        if (!di.ip.empty()) devices.push_back(di);
    }
    m_backend->bindDevices(devices);
}

void ModbusWidget::appendLog(const QString& msg)
{
    if (m_globalLogCb) {
        QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");
        m_globalLogCb("[" + ts + "] " + msg);
    }
}
