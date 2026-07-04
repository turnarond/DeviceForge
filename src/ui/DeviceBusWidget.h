/*
 * Copyright (c) 2026 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * File: DeviceBusWidget.h
 *
 * Date: 2026-07-04
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: 设备总线 UI 组件 — 水平胶囊形设备列表，支持添加/删除/选中，
 *              在线状态指示灯和凭证输入。工业仪表盘风格。
 */

#pragma once
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLineEdit>
#include <vector>
#include "framework/DeviceInfo.h"

class DeviceBusWidget : public QWidget {
    Q_OBJECT

public:
    explicit DeviceBusWidget(QWidget* parent = nullptr);

    // 设备列表操作
    void addDevice(const DeviceInfo& device);
    void removeDevice(const QString& ip);
    std::vector<DeviceInfo> selectedDevices() const;
    std::vector<DeviceInfo> allDevices() const;
    void setOnlineStatus(const QString& ip, bool online);

    // 凭证访问
    QString user() const;
    QString password() const;

signals:
    void deviceSelectionChanged();
    void credentialsChanged(const QString& user, const QString& password);

private slots:
    void onAddClicked();
    void onRemoveClicked();

private:
    void setupUi();
    void updateDeviceItem(QListWidgetItem* item, const DeviceInfo& device);

    QListWidget* m_deviceList   = nullptr;   // 水平设备列表
    QPushButton* m_addButton    = nullptr;
    QPushButton* m_removeButton = nullptr;
    QLineEdit*   m_userEdit     = nullptr;
    QLineEdit*   m_passEdit     = nullptr;
};
