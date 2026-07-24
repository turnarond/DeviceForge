/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: OpcUaClientBackend.h
 *
 * Date: 2026-07-10
 *
 * Author: turnarond
 *
 * Description: OPC UA 客户端 Tool 后端 — 继承 ToolBackend，通过 OpcUaAdapter
 *              连接 OPC UA 服务器，提供节点的读写、浏览和订阅功能。
 *
 * 回调约定（与 ModbusBackend 一致）：
 *   - LogCallback:        操作日志 → Widget 日志面板
 *   - ConnectionCallback: 连接状态变更 → Widget 连接指示灯
 *   - DataChangeCallback: 数据变更（订阅模式下由 open62541 MonitoredItem
 *                         回调触发，经 svc() 的 runIterate 驱动派发）
 */

#pragma once
#include "framework/ToolBackend.h"
#include "adapter/OpcUaAdapter.h"
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <QVariantList>
#include <QMap>
#include <QFuture>
#include <memory>
#include <atomic>

class OpcUaClientBackend : public ToolBackend {
public:
    OpcUaClientBackend();
    ~OpcUaClientBackend() override;

    int svc() override;

    // --- ToolBackend 身份 ---
    std::string toolId() const override { return "com.deviceforge.opcua.client"; }
    std::string toolName() const override { return "OPC UA 客户端"; }
    std::string toolVersion() const override { return "2.2.0"; }
    std::string toolCategory() const override { return "test"; }
    std::string toolIcon() const override { return "opcua_client"; }

    void bindDevices(const std::vector<DeviceInfo>& devices) override;
    void bindCredentials(const AuthInfo& auth) override;
    void applyConfig(const lwserverbase::config::ConfigValue&) override {}

    // --- 对外 API ---
    void connectToServer(const QString& endpoint);
    void disconnectFromServer();
    void readNodes(const QStringList& nodeIds);
    void writeNodes(const QStringList& nodeIds, const QVariantList& values);
    void subscribeNodes(const QStringList& nodeIds);
    void unsubscribeAll();
    QString browseAddressSpace();

    // --- 回调设置 ---
    using LogCallback = std::function<void(const std::string&)>;
    using ConnectionCallback = std::function<void(bool connected)>;
    using DataChangeCallback = std::function<void(const QString& nodeId, const QVariant& value, quint64 timestamp, const QString& quality)>;

    void setLogCallback(LogCallback cb) { m_logCb = std::move(cb); }
    void setConnectionCallback(ConnectionCallback cb) { m_connCb = std::move(cb); }
    void setDataChangeCallback(DataChangeCallback cb) { m_dataCb = std::move(cb); }

private:
    std::shared_ptr<OpcUaAdapter> m_adapter;
    std::atomic<bool> m_subscribed{false};
    std::atomic<bool> m_running{false};
    QFuture<void> m_subscribeFuture;   // QtConcurrent::run 返回值，析构时等待

    // 回调在 setXxxCallback 中由 GUI 线程一次性设置，之后仅 svc 线程读取；
    // setDataChangeCallback 必须在 subscribeNodes 之前调用，之后不再修改。
    LogCallback        m_logCb;
    ConnectionCallback m_connCb;
    DataChangeCallback m_dataCb;
};
