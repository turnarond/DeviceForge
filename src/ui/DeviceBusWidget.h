/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: DeviceBusWidget.h
 *
 * Date: 2026-07-25（2026-10 v2.11 Task 6：设备档案名称优先展示 + 档案编辑对话框）
 *
 * Author: turnarond
 *
 * Description: 设备总线 UI 组件 — 胶囊式紧凑设备栏，支持添加/删除/选中，
 *              凭证输入。工业仪表盘风格。
 *              v2.11：接入 DeviceRegistry 后胶囊名称优先（地址为次级信息），
 *              悬浮显示协议端点与备注，双击打开档案编辑对话框（保存经
 *              registry->saveReplacing 以端点替换语义持久化并发射
 *              deviceProfileEdited）；未接入注册表时保持旧 ip:port 行为与
 *              device.list 回退写入。
 */

#pragma once
#include <QWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QVariantMap>
#include <QList>
#include <QStringList>
#include <vector>
#include "device/DeviceProfile.h"
#include "framework/DeviceInfo.h"

// v2.11.1 设备编辑器可用性：协议枚举与凭据持久化对测试与内联新建对话框复用
namespace devicebus {

// 端点编辑允许的协议集合（小写；对齐引擎路由：ftp/ftps/sftp/ssh/telnet/modbus）
QStringList editableEndpointProtocols();

// 凭据入库：密码经 DPAPI 加密后写 ftp.credential；成功返回 key，失败返回空串。
// 密码明文只存在于调用栈内（写前即加密，失败不覆盖已存密文）。
QString storeFtpCredential(const QString& key, const QString& user, const QString& pass,
                           const QString& host, int port);

} // namespace devicebus

class QHBoxLayout;
class DeviceRegistry;

class DeviceBusWidget : public QWidget {
    Q_OBJECT

public:
    explicit DeviceBusWidget(QWidget* parent = nullptr);

    // 设备列表操作
    // persist=false：从 ConfigStore 恢复时只更新 UI，避免启动时反复写库
    void addDevice(const DeviceInfo& device, bool persist = true);
    void removeDevice(const QString& ip);
    std::vector<DeviceInfo> selectedDevices() const;
    std::vector<DeviceInfo> allDevices() const;
    void setOnlineStatus(const QString& ip, bool online);

    // v2.11 Task 6：设备档案注册表（可选）。设置后立即以档案重建胶囊栏；
    // 添加/编辑/删除均经 registry 持久化，同时保留旧 device.list ip:port 行写入
    //（供回退读取的旧路径）。
    void setRegistry(DeviceRegistry* registry);
    // 选中胶囊对应的设备档案：有注册表时按稳定 deviceId 回查；无注册表时
    // 由旧 DeviceInfo 合成（端点身份即 ip:port）。
    std::vector<DeviceProfile> selectedDeviceProfiles() const;

    // 凭证访问
    QString user() const;
    QString password() const;

    // 历史设备列表（ConfigStore device.list，按 updated_at DESC；启动时缓存）
    QList<QVariantMap> historyDevices() const { return m_recentDevices; }

signals:
    void deviceSelectionChanged();
    void credentialsChanged(const QString& user, const QString& password);
    // 档案新增/编辑保存成功后发射（宿主 Tool 可即时刷新设备视图）
    void deviceProfileEdited(const DeviceProfile& profile);

protected:
    // 双击胶囊打开档案编辑对话框（仅接入注册表后启用）
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onAddClicked();

private:
    void setupUi();
    void refreshFromRegistry();
    // 创建一枚胶囊（name 为空时纯地址展示；toolTip 承载端点详情与备注）
    QPushButton* createPill(const QString& ipPart, const QString& name,
                            const QString& deviceId, const QString& toolTip);
    // 双击编辑入口：打开档案对话框，保存经 registry 持久化
    void editProfileFor(QPushButton* pill);
    // 旧 device.list ip:port 行写入/删除（回退兼容，registry 路径同样保留）
    void persistLegacyRow(const DeviceInfo& device);

    // 胶囊式设备栏
    QWidget*      m_pillContainer = nullptr;
    QHBoxLayout*  m_pillLayout    = nullptr;
    std::vector<QPushButton*> m_pills;

    QPushButton* m_addButton    = nullptr;
    QLineEdit*   m_userEdit     = nullptr;
    QLineEdit*   m_passEdit     = nullptr;
    QList<QVariantMap> m_recentDevices;       // ConfigStore device.list 启动缓存
    std::vector<DeviceInfo> m_devices;        // 设备列表
    DeviceRegistry* m_registry = nullptr;     // v2.11 设备档案注册表（可空=旧行为）
};
