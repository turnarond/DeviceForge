/*
 * Copyright (c) 2026 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * File: DeviceBusWidget.cpp
 *
 * Date: 2026-07-04
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: 设备总线 UI 组件实现
 */

#include "DeviceBusWidget.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QInputDialog>
#include <QMessageBox>
#include <QIcon>
#include <QPixmap>
#include <QPainter>

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

static QIcon s_onlineIcon  = createOnlineIcon();
static QIcon s_offlineIcon = createOfflineIcon();

DeviceBusWidget::DeviceBusWidget(QWidget* parent) : QWidget(parent)
{
    setupUi();
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

    credRow->addWidget(userLabel);
    credRow->addWidget(m_userEdit);
    credRow->addWidget(passLabel);
    credRow->addWidget(m_passEdit);
    credRow->addStretch();

    mainLayout->addLayout(credRow);
}

void DeviceBusWidget::addDevice(const DeviceInfo& device)
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
    item->setIcon(s_offlineIcon);  // 默认离线
    item->setToolTip(QString::fromStdString(
        device.ip + ":" + std::to_string(device.port) + " [" + device.protocol + "]"));
    m_deviceList->addItem(item);
}

void DeviceBusWidget::removeDevice(const QString& ip)
{
    for (int i = 0; i < m_deviceList->count(); ++i) {
        if (m_deviceList->item(i)->data(Qt::UserRole).toString() == ip) {
            delete m_deviceList->takeItem(i);
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
            item->setIcon(online ? s_onlineIcon : s_offlineIcon);
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
        tr("IP:Port（例: 192.168.1.100:21）"),
        QLineEdit::Normal, "", &ok);
    if (ok && !text.isEmpty()) {
        DeviceInfo di;
        if (text.contains(':')) {
            auto parts = text.split(':');
            di.ip   = parts[0].toStdString();
            di.port = parts[1].toInt();
        } else {
            di.ip   = text.toStdString();
            di.port = 21; // 默认 FTP
        }
        // 根据端口猜测协议
        di.protocol = di.port == 23 ? "telnet" :
                      di.port == 502 ? "modbus" : "ftp";
        addDevice(di);
    }
}

void DeviceBusWidget::onRemoveClicked()
{
    auto selected = m_deviceList->selectedItems();
    for (auto* item : selected) {
        delete m_deviceList->takeItem(m_deviceList->row(item));
    }
}
