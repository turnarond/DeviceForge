#include "ModbusWidget.h"
#include "ModbusBackend.h"
#include "config/ConfigStore.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QHeaderView>
#include <QDateTime>

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

    // 日志
    m_logView = new QTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setMaximumHeight(100);
    mainLayout->addWidget(m_logView);

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
    m_backend->setResultCallback([this](const std::string& device, const QVector<quint16>& values, qint64 elapsedMs) {
        QMetaObject::invokeMethod(this, [this, device, values, elapsedMs]() {
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
            QString valStr;
            for (auto v : values) valStr += QString::number(v) + " ";
            m_resultTable->item(item->row(), 1)->setText(valStr.trimmed() + " (" + QString::number(elapsedMs) + "ms)");
        }, Qt::QueuedConnection);
    });
}

void ModbusWidget::onToolStart() { appendLog("Modbus 测试工具已就绪"); }
void ModbusWidget::onToolStop() { appendLog("Modbus 测试工具已停止"); }

void ModbusWidget::onReadClicked()
{
    if (!m_backend) return;
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
    QModbusDataUnit::RegisterType t = static_cast<QModbusDataUnit::RegisterType>(m_regTypeCombo->currentIndex() + 1);
    if (m_regTypeCombo->currentIndex() > 1) t = static_cast<QModbusDataUnit::RegisterType>(m_regTypeCombo->currentIndex());
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
    // 简化写入：向第一个选中设备写入起始地址
    appendLog("写入功能请通过 Modbus 批量测试界面操作");
}

void ModbusWidget::appendLog(const QString& msg)
{
    QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");
    m_logView->append("[" + ts + "] " + msg);
}
