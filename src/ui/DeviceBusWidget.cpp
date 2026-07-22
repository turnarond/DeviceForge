/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: DeviceBusWidget.cpp
 *
 * Date: 2026-07-04
 *
 * Author: turnarond
 *
 * Description: 设备总线 UI 组件实现
 */

#include "DeviceBusWidget.h"
#include "config/ConfigStore.h"
#include "config/DpapiCrypto.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QInputDialog>
#include <QMessageBox>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QDateTime>
#include <QDebug>

// 创建在线指示灯图标
static QIcon createOnlineIcon()
{
    QPixmap pix(12, 12);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor("#40C8A0"));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(1, 1, 10, 10);
    painter.end();
    return QIcon(pix);
}

// 创建离线指示灯图标
static QIcon createOfflineIcon()
{
    QPixmap pix(12, 12);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor("#E85848"));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(1, 1, 10, 10);
    painter.end();
    return QIcon(pix);
}

// 延迟初始化：首次调用时 QApplication 已就绪，避免静态初始化期创建 QPixmap 崩溃
static QIcon onlineIcon()  { static QIcon icon = createOnlineIcon();  return icon; }
static QIcon offlineIcon() { static QIcon icon = createOfflineIcon(); return icon; }

DeviceBusWidget::DeviceBusWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName("deviceBusContainer");
    setupUi();

    // 启动时加载历史设备到 UI 胶囊列表（ConfigStore 须已 open）
    m_recentDevices = ConfigStore::instance().list(QStringLiteral("device.list"), 20);
    for (const QVariantMap& row : m_recentDevices) {
        DeviceInfo di;
        di.ip   = row.value(QStringLiteral("ip")).toString().toStdString();
        di.port = row.value(QStringLiteral("port")).toInt();
        const QString display = row.value(QStringLiteral("displayName")).toString();
        if (!display.isEmpty() && display != row.value(QStringLiteral("ip")).toString())
            di.alias = display.toStdString();
        di.note = row.value(QStringLiteral("note")).toString().toStdString();
        if (di.ip.empty())
            continue;
        addDevice(di, /*persist=*/false);
    }

    // 恢复最近一条 FTP/设备总线凭证（密码字段为 DPAPI base64 密文）
    const auto creds = ConfigStore::instance().list(QStringLiteral("ftp.credential"), 1);
    if (!creds.isEmpty()) {
        const QVariantMap& c = creds.first();
        if (m_userEdit)
            m_userEdit->setText(c.value(QStringLiteral("username")).toString());
        if (m_passEdit) {
            const QString cipher = c.value(QStringLiteral("password")).toString();
            if (!cipher.isEmpty()) {
                const QString plain = DpapiCrypto::unprotect(cipher);
                if (!plain.isEmpty() || cipher.isEmpty())
                    m_passEdit->setText(plain);
            }
        }
    }
}

void DeviceBusWidget::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 4, 8, 4);
    mainLayout->setSpacing(4);

    // ==================== 设备列表行 ====================
    auto* deviceRow = new QHBoxLayout();
    deviceRow->setSpacing(6);

    m_deviceList = new QListWidget(this);
    m_deviceList->setFlow(QListWidget::LeftToRight);       // 水平排列
    m_deviceList->setViewMode(QListWidget::IconMode);      // 图标模式
    m_deviceList->setIconSize(QSize(12, 12));
    m_deviceList->setSpacing(4);
    m_deviceList->setMaximumHeight(48);
    m_deviceList->setSelectionMode(QAbstractItemView::MultiSelection);
    m_deviceList->setObjectName("deviceBus");
    m_deviceList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_deviceList, &QListWidget::itemSelectionChanged,
            this, &DeviceBusWidget::deviceSelectionChanged);

    m_addButton = new QPushButton("+", this);
    m_addButton->setFixedSize(28, 28);
    m_addButton->setObjectName("btnDeviceAdd");
    m_addButton->setToolTip(tr("添加设备"));
    connect(m_addButton, &QPushButton::clicked, this, &DeviceBusWidget::onAddClicked);

    m_removeButton = new QPushButton(QString::fromWCharArray(L"−"), this);
    m_removeButton->setFixedSize(28, 28);
    m_removeButton->setObjectName("btnDeviceRemove");
    m_removeButton->setToolTip(tr("删除选中设备"));
    connect(m_removeButton, &QPushButton::clicked, this, &DeviceBusWidget::onRemoveClicked);

    deviceRow->addWidget(m_deviceList, 1);
    deviceRow->addWidget(m_addButton);
    deviceRow->addWidget(m_removeButton);

    mainLayout->addLayout(deviceRow);

    // ==================== 凭证行 ====================
    auto* credRow = new QHBoxLayout();
    credRow->setSpacing(4);

    auto* userLabel = new QLabel(tr("用户:"), this);
    m_userEdit = new QLineEdit(this);
    m_userEdit->setPlaceholderText(tr("用户名"));
    m_userEdit->setMaximumWidth(120);
    m_userEdit->setObjectName("deviceBusUser");

    auto* passLabel = new QLabel(tr("密码:"), this);
    m_passEdit = new QLineEdit(this);
    m_passEdit->setPlaceholderText(tr("密码"));
    m_passEdit->setEchoMode(QLineEdit::Password);
    m_passEdit->setMaximumWidth(120);
    m_passEdit->setObjectName("deviceBusPass");

    // 凭证变更信号
    auto emitCredChanged = [this]() {
        emit credentialsChanged(m_userEdit->text(), m_passEdit->text());
    };
    connect(m_userEdit, &QLineEdit::textChanged, this, emitCredChanged);
    connect(m_passEdit, &QLineEdit::textChanged, this, emitCredChanged);

    // 失焦时隐式保存凭证（密码经 DPAPI 加密后入库）
    auto saveCreds = [this]() {
        const QString user = m_userEdit ? m_userEdit->text().trimmed() : QString();
        const QString pass = m_passEdit ? m_passEdit->text() : QString();
        if (user.isEmpty() && pass.isEmpty())
            return;
        const QString cipher = DpapiCrypto::protect(pass);
        if (!pass.isEmpty() && cipher.isEmpty()) {
            qWarning("DeviceBus: DPAPI 加密密码失败，跳过凭证保存以免清空已存密文");
            return;
        }
        QVariantMap cred;
        cred.insert(QStringLiteral("username"), user);
        cred.insert(QStringLiteral("password"), cipher);
        cred.insert(QStringLiteral("updated_at"), QDateTime::currentMSecsSinceEpoch());
        // 若当前有选中设备，附带 host/port 方便回填与区分
        const auto selected = selectedDevices();
        QString key = user.isEmpty() ? QStringLiteral("_default") : user;
        if (!selected.empty()) {
            const auto& d = selected.front();
            const QString host = QString::fromStdString(d.ip);
            const int port = d.port > 0 ? d.port : 21;
            cred.insert(QStringLiteral("host"), host);
            cred.insert(QStringLiteral("port"), port);
            key = QStringLiteral("%1@%2:%3").arg(user.isEmpty() ? QStringLiteral("anon") : user,
                                                 host,
                                                 QString::number(port));
        }
        if (!ConfigStore::instance().save(QStringLiteral("ftp.credential"), key, cred))
            qWarning("DeviceBus: 保存 ftp.credential 失败 key=%s", qPrintable(key));
    };
    connect(m_userEdit, &QLineEdit::editingFinished, this, saveCreds);
    connect(m_passEdit, &QLineEdit::editingFinished, this, saveCreds);

    credRow->addWidget(userLabel);
    credRow->addWidget(m_userEdit);
    credRow->addWidget(passLabel);
    credRow->addWidget(m_passEdit);
    credRow->addStretch();

    mainLayout->addLayout(credRow);
}

void DeviceBusWidget::addDevice(const DeviceInfo& device, bool persist)
{
    // 防止重复添加（按 IP 去重）
    QString ipStr = QString::fromStdString(device.ip);
    for (int i = 0; i < m_deviceList->count(); ++i) {
        if (m_deviceList->item(i)->data(Qt::UserRole).toString() == ipStr) {
            return; // 已存在
        }
    }

    auto* item = new QListWidgetItem();
    QString label = device.alias.empty()
        ? ipStr
        : QString::fromStdString(device.alias) + " (" + ipStr + ")";
    item->setText(label);
    item->setData(Qt::UserRole,     ipStr);
    item->setData(Qt::UserRole + 1, QString::fromStdString(device.protocol));
    item->setData(Qt::UserRole + 2, device.port);
    item->setIcon(offlineIcon());  // 默认离线
    item->setToolTip(QString::fromStdString(
        device.ip + ":" + std::to_string(device.port) + " [" + device.protocol + "]"));
    m_deviceList->addItem(item);

    if (persist) {
        // 持久化到 ConfigStore（key = "ip:port"，覆盖式更新）
        QVariantMap dev;
        dev.insert(QStringLiteral("ip"), ipStr);
        dev.insert(QStringLiteral("port"), device.port);
        dev.insert(QStringLiteral("displayName"),
                   device.alias.empty() ? ipStr : QString::fromStdString(device.alias));
        dev.insert(QStringLiteral("note"), QString::fromStdString(device.note));
        dev.insert(QStringLiteral("updated_at"), QDateTime::currentMSecsSinceEpoch());
        const QString key = QStringLiteral("%1:%2").arg(ipStr).arg(device.port);
        if (!ConfigStore::instance().save(QStringLiteral("device.list"), key, dev))
            qWarning("DeviceBus: 保存 device.list 失败 key=%s", qPrintable(key));
    }

    emit deviceSelectionChanged();
}

void DeviceBusWidget::removeDevice(const QString& ip)
{
    int port = 0;
    for (int i = 0; i < m_deviceList->count(); ++i) {
        if (m_deviceList->item(i)->data(Qt::UserRole).toString() == ip) {
            port = m_deviceList->item(i)->data(Qt::UserRole + 2).toInt();
            delete m_deviceList->takeItem(i);
            // 同步删除持久化记录
            ConfigStore::instance().remove(
                QStringLiteral("device.list"),
                QStringLiteral("%1:%2").arg(ip).arg(port));
            emit deviceSelectionChanged();
            break;
        }
    }
}

std::vector<DeviceInfo> DeviceBusWidget::selectedDevices() const
{
    std::vector<DeviceInfo> devices;
    for (auto* item : m_deviceList->selectedItems()) {
        DeviceInfo di;
        di.ip       = item->data(Qt::UserRole).toString().toStdString();
        di.protocol = item->data(Qt::UserRole + 1).toString().toStdString();
        di.port     = item->data(Qt::UserRole + 2).toInt();
        devices.push_back(di);
    }
    return devices;
}

std::vector<DeviceInfo> DeviceBusWidget::allDevices() const
{
    std::vector<DeviceInfo> devices;
    for (int i = 0; i < m_deviceList->count(); ++i) {
        auto* item = m_deviceList->item(i);
        DeviceInfo di;
        di.ip       = item->data(Qt::UserRole).toString().toStdString();
        di.protocol = item->data(Qt::UserRole + 1).toString().toStdString();
        di.port     = item->data(Qt::UserRole + 2).toInt();
        devices.push_back(di);
    }
    return devices;
}

void DeviceBusWidget::setOnlineStatus(const QString& ip, bool online)
{
    for (int i = 0; i < m_deviceList->count(); ++i) {
        auto* item = m_deviceList->item(i);
        if (item->data(Qt::UserRole).toString() == ip) {
            item->setIcon(online ? onlineIcon() : offlineIcon());
            // 存储在线状态供 QSS 使用
            item->setData(Qt::UserRole + 3, online);
            return;
        }
    }
}

void DeviceBusWidget::onAddClicked()
{
    bool ok;
    QString text = QInputDialog::getText(this, tr("添加设备"),
        tr("设备 IP 地址（例: 192.168.1.100）"),
        QLineEdit::Normal, "", &ok);
    if (ok && !text.isEmpty()) {
        DeviceInfo di;
        di.ip   = text.trimmed().toStdString();
        di.port = 0; // 端口由各 Tool 自行配置
        addDevice(di);
    }
}

void DeviceBusWidget::onRemoveClicked()
{
    const auto selected = m_deviceList->selectedItems();
    // 先收集 IP，避免 takeItem 过程中迭代失效
    QStringList ips;
    for (auto* item : selected)
        ips.append(item->data(Qt::UserRole).toString());
    for (const QString& ip : ips)
        removeDevice(ip);
}

QString DeviceBusWidget::user() const
{
    return m_userEdit ? m_userEdit->text().trimmed() : QString();
}

QString DeviceBusWidget::password() const
{
    return m_passEdit ? m_passEdit->text().trimmed() : QString();
}
