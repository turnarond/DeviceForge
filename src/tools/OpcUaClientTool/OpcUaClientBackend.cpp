/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: OpcUaClientBackend.cpp
 *
 * Date: 2026-07-10
 *
 * Author: turnarond
 *
 * Description: OPC UA 客户端 Tool 后端实现 — 通过 OpcUaAdapter 连接 OPC UA 服务器，
 *              提供节点的读/写/浏览/订阅功能。
 *
 * 线程模型：open62541 client 在 UA_MULTITHREADING=0 下非线程安全。
 * readNodes/writeNodes 由 GUI 线程同步调用 adapter；订阅建立后，svc() 线程周期性调用
 * adapter->runIterate(100) 驱动 DataChange 回调。adapter 内部以 recursive_mutex 串行化
 * 一切对 UA_Client 的访问，保证 GUI 线程与 svc 线程不并发触碰同一客户端。
 */

#include "OpcUaClientBackend.h"
#include <QUrl>
#include <QDateTime>
#include <thread>
#include <chrono>

// ============================================================
// 构造 / 析构
// ============================================================

OpcUaClientBackend::OpcUaClientBackend()
    : m_adapter(std::make_shared<OpcUaAdapter>())
{
}

OpcUaClientBackend::~OpcUaClientBackend()
{
    // 关键：svc() 循环体解引用派生类成员(m_adapter/m_subscribed)，而基类
    // ~ServiceTask 才 join 线程——此时派生成员已析构，会造成 UAF。故必须在
    // 派生析构中先停并 join svc 线程，再让成员销毁。
    m_running = false;
    requestShutdown();  // 置基类 running_ = false，使 svc 循环退出
    wait();             // join svc 线程(lwserverbase wait 守卫 joinable，可重入)
    if (m_adapter && m_adapter->isConnected()) {
        m_adapter->disconnect();
    }
}

// ============================================================
// ServiceTask — svc() 后台线程
// ============================================================

int OpcUaClientBackend::svc()
{
    m_running = true;
    while (m_running && ServiceTask::isRunning()) {
        // 已订阅且已连接时，驱动 open62541 事件循环派发 DataChange 回调；
        // runIterate(100) 单次最长阻塞 ~100ms，保持约 100ms 轮询节奏。
        // 未订阅时退化为保活休眠（readNodes/writeNodes 为同步调用，无需轮询）。
        if (m_subscribed.load() && m_adapter && m_adapter->isConnected()) {
            m_adapter->runIterate(100);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    m_running = false;
    return 0;
}

// ============================================================
// ToolBackend — 设备/凭证绑定
// ============================================================

void OpcUaClientBackend::bindDevices(const std::vector<DeviceInfo>& /*devices*/)
{
    // OPC UA 为点对点连接，设备由 connectToServer 的 endpoint 参数指定，
    // bindDevices 在此保留为接口兼容，不做实际操作。
}

void OpcUaClientBackend::bindCredentials(const AuthInfo& /*auth*/)
{
    // 首期仅支持匿名认证，bindCredentials 保留为接口兼容。
}

// ============================================================
// 对外 API
// ============================================================

void OpcUaClientBackend::connectToServer(const QString& endpoint)
{
    if (m_logCb) {
        m_logCb(std::string("正在连接 OPC UA 服务器: ") + endpoint.toStdString());
    }

    // 解析 endpoint URL 提取端口号（默认 4840）
    int port = 4840;
    QUrl parsedUrl(endpoint);
    if (parsedUrl.port() > 0) {
        port = parsedUrl.port();
    }

    // 将完整 endpoint URL 存入 DeviceInfo.ip（适配器自动检测 opc.tcp:// 前缀）
    DeviceInfo device;
    device.ip   = endpoint.toStdString();
    device.port = port;
    device.protocol = "opcua";

    // 首期使用匿名认证
    AuthInfo auth;

    bool ok = m_adapter->connect(device, auth);
    if (ok) {
        if (m_logCb) m_logCb("OPC UA 服务器连接成功");
    } else {
        std::string err = m_adapter->lastError();
        if (!err.empty()) {
            if (m_logCb) m_logCb("OPC UA 服务器连接失败: " + err);
        } else {
            if (m_logCb) m_logCb("OPC UA 服务器连接失败");
        }
    }

    if (m_connCb) m_connCb(ok);
}

void OpcUaClientBackend::disconnectFromServer()
{
    if (m_logCb) m_logCb("正在断开 OPC UA 服务器连接...");
    m_subscribed = false;  // 停止 svc 轮询泵，与 unsubscribeAll 对称
    m_adapter->disconnect();
    if (m_logCb) m_logCb("OPC UA 服务器已断开");
    if (m_connCb) m_connCb(false);
}

void OpcUaClientBackend::readNodes(const QStringList& nodeIds)
{
    if (!m_adapter->isConnected()) {
        if (m_logCb) m_logCb("OPC UA 未连接，无法读取节点");
        return;
    }

    QVariantMap results = m_adapter->readNodes(nodeIds);
    if (m_logCb) {
        m_logCb(std::string("批量读取 ") + std::to_string(nodeIds.size())
                + " 个节点完成");
    }

    // 通过 DataChangeCallback 将结果推送给 Widget（模拟订阅式数据推送）
    if (m_dataCb) {
        quint64 ts = QDateTime::currentMSecsSinceEpoch();
        for (const auto& nid : nodeIds) {
            QVariant val = results.value(nid);
            QString quality = val.isValid() ? QStringLiteral("Good")
                                            : QStringLiteral("Bad");
            m_dataCb(nid, val, ts, quality);
        }
    }
}

void OpcUaClientBackend::writeNodes(const QStringList& nodeIds, const QVariantList& values)
{
    if (!m_adapter->isConnected()) {
        if (m_logCb) m_logCb("OPC UA 未连接，无法写入节点");
        return;
    }

    QMap<QString, QString> statuses = m_adapter->writeNodes(nodeIds, values);

    if (m_logCb) {
        m_logCb(std::string("批量写入 ") + std::to_string(nodeIds.size())
                + " 个节点完成");
    }

    // 将写入结果推送至日志
    if (m_logCb) {
        for (const auto& nid : nodeIds) {
            QString st = statuses.value(nid, QStringLiteral("未知"));
            m_logCb(std::string("写入 ") + nid.toStdString()
                    + " → " + st.toStdString());
        }
    }
}

void OpcUaClientBackend::subscribeNodes(const QStringList& nodeIds)
{
    if (!m_adapter->isConnected()) {
        if (m_logCb) m_logCb("OPC UA 未连接，无法订阅节点");
        return;
    }

    // 通过适配器建立 DataChange 订阅；回调在 svc() 的 runIterate 线程触发，
    // 经 m_dataCb 转发到 Widget（Widget 侧已用 QueuedConnection 编组到 GUI 线程）。
    quint32 subId = m_adapter->subscribeDataChange(nodeIds,
        [this](const QString& nodeId, const QVariant& value,
               quint64 ts, const QString& quality) {
            if (m_dataCb) m_dataCb(nodeId, value, ts, quality);
        });

    if (subId != 0) {
        m_subscribed = true;   // 置位后 svc() 开始驱动 runIterate
        if (m_logCb) {
            m_logCb(std::string("已订阅 ") + std::to_string(nodeIds.size())
                    + " 个节点 (subscriptionId=" + std::to_string(subId) + ")");
        }
    } else {
        m_subscribed = false;
        std::string err = m_adapter->lastError();
        if (m_logCb) {
            m_logCb(err.empty() ? std::string("订阅失败")
                                : std::string("订阅失败: ") + err);
        }
    }
}

void OpcUaClientBackend::unsubscribeAll()
{
    m_subscribed = false;   // 先停轮询，再删除订阅
    m_adapter->unsubscribeAll();
    if (m_logCb) m_logCb("已取消所有订阅");
}

QString OpcUaClientBackend::browseAddressSpace()
{
    if (!m_adapter->isConnected()) {
        if (m_logCb) m_logCb("OPC UA 未连接，无法浏览地址空间");
        return QString();
    }

    QString json = m_adapter->browseRoot();
    if (m_logCb) m_logCb("地址空间浏览完成");
    return json;
}
