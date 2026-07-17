/*
 * OpcUaAdapter.h — OPC UA 协议适配器（基于 open62541 v1.5.5）
 *
 * 实现 IProtocolAdapter 统一接口，protocolId() → "opcua"。
 * 首期能力：None 安全策略 + 匿名认证的同步客户端；
 *   - 批量读节点   readNodes()
 *   - 批量写节点   writeNodes()
 *   - 浏览根节点树 browseRoot()（返回 JSON 字符串）
 *   - request()    读单个节点（请求-响应模式）
 *
 * open62541 client 在 UA_MULTITHREADING=0 下非线程安全，
 * 调用方需保证同一实例的方法串行调用（放入单一后台线程）。
 */
#pragma once
#include "IProtocolAdapter.h"
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QVariantList>
#include <QStringList>
#include <QMap>
#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

// 前向声明 open62541 类型，避免在头文件暴露 open62541.h（庞大且为 C 头）
// 注意：UA_Client 是具名结构体（typedef struct UA_Client UA_Client），可前向声明；
//       UA_Variant 是匿名结构体 typedef，无法前向声明，故仅在 .cpp 中经 open62541.h 使用。
struct UA_Client;
// 单个 MonitoredItem 的回调上下文（定义在 .cpp，头文件仅前向声明并以指针持有）
struct OpcUaMonContext;

class OpcUaAdapter : public IProtocolAdapter {
public:
    OpcUaAdapter();
    ~OpcUaAdapter() override;

    // --- IProtocolAdapter 实现 ---
    std::string protocolId() const override { return "opcua"; }
    bool connect(const DeviceInfo& device, const AuthInfo& auth) override;
    void disconnect() override;
    bool isConnected() const override;
    std::string lastError() const override;
    std::future<Response> request(const Request& req) override;
    void subscribe(const Request& req, StreamCallback onData) override;
    void unsubscribe() override;
    ProtocolCapability capability() const override;

    // --- OPC UA 扩展接口 ---
    // 批量读节点：key = nodeId 字符串（如 "ns=2;i=2255"），value = 读到的值（失败为无效 QVariant）
    QVariantMap readNodes(const QStringList& nodeIds);
    // 批量写节点：nodeIds 与 values 一一对应，返回 key=nodeId，value=状态字符串（"Good" 或错误名）
    QMap<QString, QString> writeNodes(const QStringList& nodeIds, const QVariantList& values);
    // 浏览根节点（Objects Folder）的直接子节点，返回 JSON 数组字符串
    QString browseRoot();

    // --- DataChange 订阅（Task 6）---
    // DataChange 回调签名：nodeId / 值 / 时间戳(unix 毫秒) / 质量("Good"/"Bad")
    using DataChangeCb = std::function<void(const QString& nodeId,
                                            const QVariant& value,
                                            quint64 timestampMs,
                                            const QString& quality)>;
    // 为 nodeIds 创建一个 Subscription + 每节点一个 MonitoredItem，值变化时经 cb 回调。
    // 返回 subscriptionId（0 表示失败）。需外部循环调用 runIterate() 驱动回调派发。
    quint32 subscribeDataChange(const QStringList& nodeIds, DataChangeCb cb);
    // 删除当前 Subscription 并释放所有 MonitoredItem 上下文（幂等）。
    void unsubscribeAll();
    // 驱动 open62541 客户端事件循环（订阅回调在此期间同线程触发）。
    // timeoutMs 为单次最长阻塞（建议 100ms），null/未连接时安全返回。
    void runIterate(int timeoutMs);

private:
    UA_Client*        m_client = nullptr;
    std::atomic<bool> m_connected{false};
    QString           m_lastError;
    StreamCallback    m_streamCb;

    // 线程安全：open62541 在 UA_MULTITHREADING=0 下非线程安全，UA_Client 必须串行访问。
    // readNodes/writeNodes/request/browseRoot 可能在 GUI 线程或 std::async 后台线程调用，
    // 而 runIterate 在 ServiceTask(svc) 线程调用——两者都触碰 m_client。
    // 规则：所有触碰 m_client 的方法体（含 request 的 async lambda 内部）必须持有 m_clientMutex。
    // 采用 recursive_mutex，因 connect() 内部会调用 disconnect()（二者均需锁）。
    // DataChange 静态回调在 runIterate 期间同线程触发，仅调用用户 std::function（不回调本类
    // 任何加锁方法），故不依赖递归，但递归锁也使其无害。
    std::recursive_mutex          m_clientMutex;
    quint32                       m_subscriptionId = 0;
    std::vector<OpcUaMonContext*> m_monContexts;   // 已分配的回调上下文，析构/取消时释放

    void setError(const QString& err);
    static QString uaStatusToString(quint32 statusCode);
};
