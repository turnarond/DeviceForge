/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: DeviceBusWidget.cpp
 *
 * Date: 2026-07-25
 *
 * Author: turnarond
 *
 * Description: 设备总线 UI 组件实现 — 胶囊式紧凑布局
 */

#include "DeviceBusWidget.h"
#include "config/ConfigStore.h"
#include "config/DpapiCrypto.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QInputDialog>
#include <QDateTime>
#include <QStyle>
#include <QDebug>

DeviceBusWidget::DeviceBusWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName("deviceBusContainer");
    setupUi();

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
    mainLayout->setContentsMargins(0, 2, 0, 2);
    mainLayout->setSpacing(4);

    // ── 第一行：设备胶囊 + 添加按钮 ──
    auto* deviceRow = new QHBoxLayout();
    deviceRow->setSpacing(4);

    auto* deviceIcon = new QLabel(QStringLiteral("\xF0\x9F\x93\xA1"), this);
    deviceIcon->setStyleSheet(QStringLiteral("font-size: 14px;"));
    deviceRow->addWidget(deviceIcon);

    // 设备胶囊容器（水平流式布局）
    m_pillContainer = new QWidget(this);
    m_pillLayout = new QHBoxLayout(m_pillContainer);
    m_pillLayout->setContentsMargins(0, 0, 0, 0);
    m_pillLayout->setSpacing(4);
    m_pillLayout->addStretch(); // 胶囊左对齐
    deviceRow->addWidget(m_pillContainer, 1);

    // + 添加按钮
    m_addButton = new QPushButton(QStringLiteral("+ \xE6\xB7\xBB\xE5\x8A\xA0"), this);
    m_addButton->setFixedHeight(26);
    m_addButton->setCursor(Qt::PointingHandCursor);
    m_addButton->setStyleSheet(
        "QPushButton {"
        "  background: transparent;"
        "  border: 1px dashed #333B48;"
        "  border-radius: 4px;"
        "  color: #7B8494;"
        "  font-size: 11px;"
        "  padding: 2px 10px;"
        "}"
        "QPushButton:hover {"
        "  border-color: #7B8494;"
        "  color: #C8CCD4;"
        "}"
    );
    connect(m_addButton, &QPushButton::clicked, this, &DeviceBusWidget::onAddClicked);
    deviceRow->addWidget(m_addButton);

    mainLayout->addLayout(deviceRow);

    // ── 第二行：凭证输入 ──
    auto* authRow = new QHBoxLayout();
    authRow->setSpacing(6);

    auto* userIcon = new QLabel(QStringLiteral("\xF0\x9F\x91\xA4"), this);
    authRow->addWidget(userIcon);

    m_userEdit = new QLineEdit(this);
    m_userEdit->setPlaceholderText(QStringLiteral("\xE7\x94\xA8\xE6\x88\xB7\xE5\x90\x8D"));
    m_userEdit->setFixedWidth(120);
    m_userEdit->setFixedHeight(24);
    m_userEdit->setStyleSheet(
        "QLineEdit {"
        "  background: #0E1219;"
        "  border: 1px solid #333B48;"
        "  border-radius: 3px;"
        "  color: #C8CCD4;"
        "  padding: 2px 6px;"
        "  font-size: 12px;"
        "}"
        "QLineEdit:focus { border-color: #F0A030; }"
    );
    authRow->addWidget(m_userEdit);

    auto* passIcon = new QLabel(QStringLiteral("\xF0\x9F\x94\x91"), this);
    authRow->addWidget(passIcon);

    m_passEdit = new QLineEdit(this);
    m_passEdit->setPlaceholderText(QStringLiteral("\xE5\xAF\x86\xE7\xA0\x81"));
    m_passEdit->setEchoMode(QLineEdit::Password);
    m_passEdit->setFixedWidth(140);
    m_passEdit->setFixedHeight(24);
    m_passEdit->setStyleSheet(m_userEdit->styleSheet());
    authRow->addWidget(m_passEdit);

    authRow->addStretch();

    mainLayout->addLayout(authRow);

    // 连接凭证变更信号
    connect(m_userEdit, &QLineEdit::textChanged, this, [this]() {
        emit credentialsChanged(m_userEdit->text(), m_passEdit->text());
    });
    connect(m_passEdit, &QLineEdit::textChanged, this, [this]() {
        emit credentialsChanged(m_userEdit->text(), m_passEdit->text());
    });

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

    // ── 从 ConfigStore 恢复历史设备 ──
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
        addDevice(di, false);
    }
}

void DeviceBusWidget::addDevice(const DeviceInfo& device, bool persist)
{
    // 检查重复
    for (const auto& d : m_devices) {
        if (d.ip == device.ip) return;
    }
    m_devices.push_back(device);

    // 创建胶囊标签
    auto* pill = new QPushButton(this);
    QString ipStr = QString::fromStdString(device.ip);
    QString label = ipStr;
    if (device.port > 0 && device.port != 21) {
        label += QStringLiteral(":") + QString::number(device.port);
    }
    pill->setText(label + QStringLiteral("  \xC3\x97"));
    pill->setFixedHeight(26);
    pill->setCursor(Qt::PointingHandCursor);
    pill->setCheckable(true);
    pill->setProperty("deviceIp", ipStr);
    pill->setProperty("selected", false);

    pill->setStyleSheet(
        "QPushButton {"
        "  background: #232A36;"
        "  border: 1px solid #333B48;"
        "  border-radius: 4px;"
        "  color: #C8CCD4;"
        "  font-size: 12px;"
        "  padding: 2px 10px;"
        "}"
        "QPushButton:hover {"
        "  border-color: #7B8494;"
        "}"
        "QPushButton[selected=\"true\"] {"
        "  border-color: #F0A030;"
        "  background: #2A2518;"
        "}"
    );

    // 点击切换选中
    connect(pill, &QPushButton::clicked, this, [this, pill]() {
        bool sel = !pill->property("selected").toBool();
        pill->setProperty("selected", sel);
        pill->style()->unpolish(pill);
        pill->style()->polish(pill);
        emit deviceSelectionChanged();
    });

    // 插入到 stretch 之前
    m_pillLayout->insertWidget(m_pillLayout->count() - 1, pill);
    m_pills.push_back(pill);

    if (persist) {
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
    for (size_t i = 0; i < m_devices.size(); ++i) {
        if (m_devices[i].ip == ip.toStdString()) {
            port = m_devices[i].port;
            m_devices.erase(m_devices.begin() + static_cast<ptrdiff_t>(i));
            break;
        }
    }
    for (size_t i = 0; i < m_pills.size(); ++i) {
        if (m_pills[i]->property("deviceIp").toString() == ip) {
            m_pillLayout->removeWidget(m_pills[i]);
            delete m_pills[i];
            m_pills.erase(m_pills.begin() + static_cast<ptrdiff_t>(i));
            break;
        }
    }
    // 同步删除持久化记录
    ConfigStore::instance().remove(
        QStringLiteral("device.list"),
        QStringLiteral("%1:%2").arg(ip).arg(port));
    emit deviceSelectionChanged();
}

std::vector<DeviceInfo> DeviceBusWidget::selectedDevices() const
{
    std::vector<DeviceInfo> result;
    size_t n = std::min(m_devices.size(), m_pills.size());
    for (size_t i = 0; i < n; ++i) {
        if (m_pills[i]->property("selected").toBool()) {
            result.push_back(m_devices[i]);
        }
    }
    return result;
}

std::vector<DeviceInfo> DeviceBusWidget::allDevices() const
{
    return m_devices;
}

void DeviceBusWidget::setOnlineStatus(const QString& ip, bool online)
{
    for (auto* pill : m_pills) {
        if (pill->property("deviceIp").toString() == ip) {
            pill->setProperty("online", online);
            pill->style()->unpolish(pill);
            pill->style()->polish(pill);
            return;
        }
    }
}

void DeviceBusWidget::onAddClicked()
{
    bool ok;
    QString text = QInputDialog::getText(this, tr("添加设备"),
        tr("设备 IP 地址（例: 192.168.1.100）"),
        QLineEdit::Normal, QString(), &ok);
    if (ok && !text.isEmpty()) {
        DeviceInfo di;
        di.ip   = text.trimmed().toStdString();
        di.port = 0;
        addDevice(di);
    }
}

QString DeviceBusWidget::user() const
{
    return m_userEdit ? m_userEdit->text().trimmed() : QString();
}

QString DeviceBusWidget::password() const
{
    return m_passEdit ? m_passEdit->text().trimmed() : QString();
}
