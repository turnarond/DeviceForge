/*
 * OpcUaAdapter.cpp — OPC UA 协议适配器实现（open62541 v1.5.5）
 *
 * 说明：open62541 单文件分发版的功能开关由 open62541.h 顶部 #define 块直接控制，
 * 是唯一真源（两套构建均不传 -DUA_ENABLE_*，见 src/thirdparty/open62541/CMakeLists.txt）。
 * 下方仅保留正向开关(=1)的 #ifndef 兜底，用于头文件未定义时的保守启用；
 * 不要为任何开关兜底 "0"——open62541 用 #ifdef/defined() 判断，定义为 0 反而会激活
 * 对应代码路径（如 UA_ENABLE_METHODCALLS 定义为 0 会启用 Method 调用，与非目标相悖）。
 */

// --- open62541 正向开关兜底（仅当头文件未定义时保守启用；关闭类开关一律不兜底）---
#ifndef UA_ENABLE_CLIENT
#define UA_ENABLE_CLIENT 1
#endif
#ifndef UA_ENABLE_SUBSCRIPTIONS
#define UA_ENABLE_SUBSCRIPTIONS 1
#endif
#ifndef UA_ENABLE_NODELIST
#define UA_ENABLE_NODELIST 1
#endif

#include "OpcUaAdapter.h"
#include <open62541.h>

#include <QDateTime>
#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

// ============================================================
// 内部辅助：类型映射
// ============================================================
namespace {

// UA_String → QString（非 null 结尾，需按 length 截取）
QString uaStringToQString(const UA_String& s)
{
    if (!s.data || s.length == 0)
        return QString();
    return QString::fromUtf8(reinterpret_cast<const char*>(s.data),
                             static_cast<int>(s.length));
}

// UA_NodeId → 可读字符串（如 "ns=2;i=2255"）
QString uaNodeIdToQString(const UA_NodeId& id)
{
    UA_String out = UA_STRING_NULL;
    if (UA_NodeId_print(&id, &out) != UA_STATUSCODE_GOOD)
        return QString();
    QString result = uaStringToQString(out);
    UA_String_clear(&out);
    return result;
}

// UA_DateTime（自 1601 起的 100ns 计数）→ unix 毫秒
quint64 uaDateTimeToUnixMs(UA_DateTime dt)
{
    if (dt <= UA_DATETIME_UNIX_EPOCH)
        return 0;
    return static_cast<quint64>((dt - UA_DATETIME_UNIX_EPOCH) / UA_DATETIME_MSEC);
}

// UA_Variant（标量）→ QVariant，覆盖任务要求的常见类型
QVariant uaVariantToQtVariant(const UA_Variant& val)
{
    if (!val.type || !val.data)
        return QVariant();
    // 仅处理标量；数组暂不展开，返回无效值占位
    if (!UA_Variant_isScalar(&val))
        return QVariant();

    if (val.type == &UA_TYPES[UA_TYPES_BOOLEAN])
        return QVariant(static_cast<bool>(*static_cast<UA_Boolean*>(val.data)));
    if (val.type == &UA_TYPES[UA_TYPES_SBYTE])
        return QVariant(static_cast<int>(*static_cast<UA_SByte*>(val.data)));
    if (val.type == &UA_TYPES[UA_TYPES_BYTE])
        return QVariant(static_cast<uint>(*static_cast<UA_Byte*>(val.data)));
    if (val.type == &UA_TYPES[UA_TYPES_INT16])
        return QVariant(static_cast<int>(*static_cast<UA_Int16*>(val.data)));
    if (val.type == &UA_TYPES[UA_TYPES_UINT16])
        return QVariant(static_cast<uint>(*static_cast<UA_UInt16*>(val.data)));
    if (val.type == &UA_TYPES[UA_TYPES_INT32])
        return QVariant(static_cast<int>(*static_cast<UA_Int32*>(val.data)));
    if (val.type == &UA_TYPES[UA_TYPES_UINT32])
        return QVariant(static_cast<uint>(*static_cast<UA_UInt32*>(val.data)));
    if (val.type == &UA_TYPES[UA_TYPES_INT64])
        return QVariant(static_cast<qlonglong>(*static_cast<UA_Int64*>(val.data)));
    if (val.type == &UA_TYPES[UA_TYPES_UINT64])
        return QVariant(static_cast<qulonglong>(*static_cast<UA_UInt64*>(val.data)));
    if (val.type == &UA_TYPES[UA_TYPES_FLOAT])
        return QVariant(static_cast<double>(*static_cast<UA_Float*>(val.data)));
    if (val.type == &UA_TYPES[UA_TYPES_DOUBLE])
        return QVariant(*static_cast<UA_Double*>(val.data));
    if (val.type == &UA_TYPES[UA_TYPES_STRING])
        return QVariant(uaStringToQString(*static_cast<UA_String*>(val.data)));
    if (val.type == &UA_TYPES[UA_TYPES_DATETIME]) {
        UA_DateTime dt = *static_cast<UA_DateTime*>(val.data);
        qint64 unixSec = UA_DateTime_toUnixTime(dt);
        return QVariant(QDateTime::fromSecsSinceEpoch(unixSec));
    }
    if (val.type == &UA_TYPES[UA_TYPES_BYTESTRING]) {
        UA_ByteString bs = *static_cast<UA_ByteString*>(val.data);
        return QVariant(QByteArray(reinterpret_cast<const char*>(bs.data),
                                   static_cast<int>(bs.length)));
    }
    // 未映射类型：返回无效值
    return QVariant();
}

// QVariant → UA_Variant（标量，setScalarCopy 深拷贝，调用方负责 UA_Variant_clear）
// 返回 true 表示成功构造，false 表示类型不支持
bool qtVariantToUaVariant(const QVariant& v, UA_Variant& out)
{
    UA_Variant_init(&out);
    switch (v.typeId()) {
    case QMetaType::Bool: {
        UA_Boolean b = v.toBool();
        return UA_Variant_setScalarCopy(&out, &b, &UA_TYPES[UA_TYPES_BOOLEAN])
               == UA_STATUSCODE_GOOD;
    }
    case QMetaType::Int: {
        UA_Int32 i = v.toInt();
        return UA_Variant_setScalarCopy(&out, &i, &UA_TYPES[UA_TYPES_INT32])
               == UA_STATUSCODE_GOOD;
    }
    case QMetaType::UInt: {
        UA_UInt32 u = v.toUInt();
        return UA_Variant_setScalarCopy(&out, &u, &UA_TYPES[UA_TYPES_UINT32])
               == UA_STATUSCODE_GOOD;
    }
    case QMetaType::LongLong: {
        UA_Int64 i = v.toLongLong();
        return UA_Variant_setScalarCopy(&out, &i, &UA_TYPES[UA_TYPES_INT64])
               == UA_STATUSCODE_GOOD;
    }
    case QMetaType::ULongLong: {
        UA_UInt64 u = v.toULongLong();
        return UA_Variant_setScalarCopy(&out, &u, &UA_TYPES[UA_TYPES_UINT64])
               == UA_STATUSCODE_GOOD;
    }
    case QMetaType::Float:
    case QMetaType::Double: {
        UA_Double d = v.toDouble();
        return UA_Variant_setScalarCopy(&out, &d, &UA_TYPES[UA_TYPES_DOUBLE])
               == UA_STATUSCODE_GOOD;
    }
    default: {
        // 其余一律按字符串写入
        QByteArray utf8 = v.toString().toUtf8();
        UA_String s = UA_STRING_NULL;
        s.length = static_cast<size_t>(utf8.size());
        s.data = reinterpret_cast<UA_Byte*>(const_cast<char*>(utf8.constData()));
        return UA_Variant_setScalarCopy(&out, &s, &UA_TYPES[UA_TYPES_STRING])
               == UA_STATUSCODE_GOOD;
    }
    }
}

} // namespace

// 单个 MonitoredItem 的回调上下文：持有用户回调 + 该监控项对应的 nodeId 字符串。
// 由 subscribeDataChange 在堆上分配，作为 monContext 传入 open62541；
// unsubscribeAll/disconnect 负责 delete，无泄漏。
struct OpcUaMonContext {
    OpcUaAdapter::DataChangeCb cb;
    QString                    nodeId;
};

// ============================================================
// 构造 / 析构
// ============================================================

OpcUaAdapter::OpcUaAdapter() = default;

OpcUaAdapter::~OpcUaAdapter()
{
    disconnect();
}

// ============================================================
// IProtocolAdapter — 连接生命周期
// ============================================================

bool OpcUaAdapter::connect(const DeviceInfo& device, const AuthInfo& /*auth*/)
{
    // 串行化对 m_client 的访问；connect 内部调用 disconnect() 亦需锁（recursive）。
    std::lock_guard<std::recursive_mutex> lk(m_clientMutex);
    disconnect();

    m_client = UA_Client_new();
    if (!m_client) {
        setError(QStringLiteral("OPC UA 客户端创建失败"));
        return false;
    }
    // None 安全策略 + 匿名认证（默认配置即匿名）
    UA_ClientConfig* cc = UA_Client_getConfig(m_client);
    UA_ClientConfig_setDefault(cc);

    // 组装 endpoint URL：允许 device.ip 直接为完整 "opc.tcp://..." 或仅 host
    QString url = QString::fromStdString(device.ip);
    if (!url.startsWith(QStringLiteral("opc.tcp://"), Qt::CaseInsensitive)) {
        int port = device.port > 0 ? device.port : 4840;
        url = QStringLiteral("opc.tcp://%1:%2").arg(url).arg(port);
    }

    UA_StatusCode ret = UA_Client_connect(m_client, url.toUtf8().constData());
    if (ret != UA_STATUSCODE_GOOD) {
        setError(uaStatusToString(ret));
        UA_Client_delete(m_client);
        m_client = nullptr;
        return false;
    }

    m_connected = true;
    m_lastError.clear();
    return true;
}

void OpcUaAdapter::disconnect()
{
    std::lock_guard<std::recursive_mutex> lk(m_clientMutex);
    m_connected = false;

    // 释放订阅簿记：UA_Client_delete 会释放客户端侧订阅，我们仅需释放堆上回调上下文。
    m_subscriptionId = 0;
    for (auto* ctx : m_monContexts)
        delete ctx;
    m_monContexts.clear();

    if (m_client) {
        UA_Client_disconnect(m_client);
        UA_Client_delete(m_client);
        m_client = nullptr;
    }
}

bool OpcUaAdapter::isConnected() const
{
    return m_connected.load();
}

std::string OpcUaAdapter::lastError() const
{
    return m_lastError.toStdString();
}

// ============================================================
// IProtocolAdapter — 传输模式
// ============================================================

// request：读取 req.path 指定的单个节点，值以字符串返回
std::future<Response> OpcUaAdapter::request(const Request& req)
{
    return std::async(std::launch::async, [this, req]() -> Response {
        Response r;
        // 后台线程访问 m_client：在 lambda 体内加锁（而非包住 async 派发），
        // 锁需覆盖对 m_client 的判空读取。
        std::lock_guard<std::recursive_mutex> lk(m_clientMutex);
        if (!m_client || !m_connected) {
            r.success = false;
            r.errorMessage = "OPC UA 未连接";
            return r;
        }

        UA_NodeId nodeId;
        UA_NodeId_init(&nodeId);
        UA_String nidStr = UA_STRING(const_cast<char*>(req.path.c_str()));
        UA_StatusCode ret = UA_NodeId_parse(&nodeId, nidStr);
        if (ret != UA_STATUSCODE_GOOD) {
            r.success = false;
            r.errorMessage = "无效 NodeId: " + req.path;
            UA_NodeId_clear(&nodeId);
            return r;
        }

        UA_Variant val;
        UA_Variant_init(&val);
        ret = UA_Client_readValueAttribute(m_client, nodeId, &val);
        if (ret == UA_STATUSCODE_GOOD) {
            r.success = true;
            r.data = uaVariantToQtVariant(val).toString().toStdString();
        } else {
            r.success = false;
            r.errorMessage = uaStatusToString(ret).toStdString();
        }
        r.statusCode = static_cast<int>(ret);
        UA_Variant_clear(&val);
        UA_NodeId_clear(&nodeId);
        return r;
    });
}

// subscribe：OPC UA 数据变更订阅。open62541 client 在 UA_MULTITHREADING=0 下非线程安全，
// 需与 read/write 串行调度并由外部循环驱动 UA_Client_run_iterate，暂未纳入首期骨架。
void OpcUaAdapter::subscribe(const Request& /*req*/, StreamCallback onData)
{
    m_streamCb = onData;
    setError(QStringLiteral("OPC UA subscribe 模式暂未实现（首期骨架）"));
}

void OpcUaAdapter::unsubscribe()
{
    m_streamCb = nullptr;
}

// ============================================================
// DataChange 订阅（Task 6）
// ============================================================

// open62541 DataChange C 回调（文件内静态）。在 runIterate 期间同线程触发，此时
// m_clientMutex 已被 runIterate 持有——本回调不调用适配器任何加锁方法，仅执行用户
// std::function，该 std::function 转发到 Backend::m_dataCb（Widget 侧再经 QueuedConnection
// 编组到 GUI 线程）。定义在 .cpp 内以便使用完整的 UA_DataValue 定义。
static void opcuaDataChangeCallback(UA_Client* /*client*/, UA_UInt32 /*subId*/,
                                    void* /*subContext*/, UA_UInt32 /*monId*/,
                                    void* monContext, UA_DataValue* value)
{
    auto* ctx = static_cast<OpcUaMonContext*>(monContext);
    if (!ctx || !ctx->cb || !value)
        return;

    QVariant qv;
    if (value->hasValue)
        qv = uaVariantToQtVariant(value->value);

    quint64 ts;
    if (value->hasSourceTimestamp)
        ts = uaDateTimeToUnixMs(value->sourceTimestamp);
    else if (value->hasServerTimestamp)
        ts = uaDateTimeToUnixMs(value->serverTimestamp);
    else
        ts = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch());

    const bool good = !value->hasStatus || value->status == UA_STATUSCODE_GOOD;
    ctx->cb(ctx->nodeId, qv, ts, good ? QStringLiteral("Good") : QStringLiteral("Bad"));
}

quint32 OpcUaAdapter::subscribeDataChange(const QStringList& nodeIds, DataChangeCb cb)
{
    std::lock_guard<std::recursive_mutex> lk(m_clientMutex);
    if (!m_client || !m_connected) {
        setError(QStringLiteral("OPC UA 未连接"));
        return 0;
    }

    // 复用单一 Subscription：若已存在则先清理，保证 subscribe 幂等。
    if (m_subscriptionId != 0) {
        UA_Client_Subscriptions_deleteSingle(m_client, m_subscriptionId);
        m_subscriptionId = 0;
        for (auto* c : m_monContexts)
            delete c;
        m_monContexts.clear();
    }

    UA_CreateSubscriptionRequest subReq = UA_CreateSubscriptionRequest_default();
    UA_CreateSubscriptionResponse subResp =
        UA_Client_Subscriptions_create(m_client, subReq, nullptr, nullptr, nullptr);
    if (subResp.responseHeader.serviceResult != UA_STATUSCODE_GOOD) {
        setError(uaStatusToString(subResp.responseHeader.serviceResult));
        return 0;
    }
    m_subscriptionId = subResp.subscriptionId;

    for (const QString& nid : nodeIds) {
        UA_NodeId nodeId;
        UA_NodeId_init(&nodeId);
        QByteArray nidUtf8 = nid.toUtf8();  // 持有生命周期，避免 UA_String 悬垂
        UA_String nidStr = UA_STRING(const_cast<char*>(nidUtf8.constData()));
        if (UA_NodeId_parse(&nodeId, nidStr) != UA_STATUSCODE_GOOD) {
            UA_NodeId_clear(&nodeId);
            continue;
        }

        // _default 中 itemToMonitor.nodeId 浅引用 nodeId；createDataChange 同步发送并内部拷贝，
        // 返回后统一 UA_NodeId_clear(&nodeId) 释放一次。
        UA_MonitoredItemCreateRequest monReq = UA_MonitoredItemCreateRequest_default(nodeId);
        auto* ctx = new OpcUaMonContext{cb, nid};
        UA_MonitoredItemCreateResult monRes =
            UA_Client_MonitoredItems_createDataChange(
                m_client, m_subscriptionId, UA_TIMESTAMPSTORETURN_BOTH,
                monReq, ctx, &opcuaDataChangeCallback, nullptr);
        if (monRes.statusCode == UA_STATUSCODE_GOOD) {
            m_monContexts.push_back(ctx);
        } else {
            delete ctx;   // 创建失败：立即回收上下文，避免泄漏
        }
        UA_MonitoredItemCreateResult_clear(&monRes);
        UA_NodeId_clear(&nodeId);
    }

    m_lastError.clear();
    return m_subscriptionId;
}

void OpcUaAdapter::unsubscribeAll()
{
    std::lock_guard<std::recursive_mutex> lk(m_clientMutex);
    if (m_client && m_subscriptionId != 0)
        UA_Client_Subscriptions_deleteSingle(m_client, m_subscriptionId);
    m_subscriptionId = 0;
    for (auto* ctx : m_monContexts)
        delete ctx;
    m_monContexts.clear();
}

void OpcUaAdapter::runIterate(int timeoutMs)
{
    std::lock_guard<std::recursive_mutex> lk(m_clientMutex);
    if (!m_client || !m_connected)
        return;
    // 单次最长阻塞 timeoutMs（建议 100ms），返回及时，故持锁期间不会长时间独占。
    UA_Client_run_iterate(m_client, static_cast<UA_UInt32>(timeoutMs < 0 ? 0 : timeoutMs));
}

ProtocolCapability OpcUaAdapter::capability() const
{
    ProtocolCapability c;
    c.requestResponse  = true;
    c.streaming        = true;   // DataChange 订阅（Task 6）已实现
    c.broadcast        = false;
    c.publishSubscribe = true;   // UA_ENABLE_SUBSCRIPTIONS 已接入
    c.maxConnections   = 1;
    return c;
}

// ============================================================
// OPC UA 扩展接口
// ============================================================

QVariantMap OpcUaAdapter::readNodes(const QStringList& nodeIds)
{
    QVariantMap results;
    std::lock_guard<std::recursive_mutex> lk(m_clientMutex);
    if (!m_client || !m_connected) {
        setError(QStringLiteral("OPC UA 未连接"));
        return results;
    }

    for (const QString& nid : nodeIds) {
        UA_NodeId nodeId;
        UA_NodeId_init(&nodeId);
        QByteArray nidUtf8 = nid.toUtf8();  // 持有生命周期，避免 UA_String 悬垂
        UA_String nidStr = UA_STRING(const_cast<char*>(nidUtf8.constData()));
        if (UA_NodeId_parse(&nodeId, nidStr) != UA_STATUSCODE_GOOD) {
            results[nid] = QVariant();
            UA_NodeId_clear(&nodeId);
            continue;
        }

        UA_Variant val;
        UA_Variant_init(&val);
        UA_StatusCode ret = UA_Client_readValueAttribute(m_client, nodeId, &val);
        if (ret == UA_STATUSCODE_GOOD) {
            results[nid] = uaVariantToQtVariant(val);
        } else {
            results[nid] = QVariant();  // 读失败返回无效值
        }
        UA_Variant_clear(&val);
        UA_NodeId_clear(&nodeId);
    }
    return results;
}

QMap<QString, QString> OpcUaAdapter::writeNodes(const QStringList& nodeIds,
                                                const QVariantList& values)
{
    QMap<QString, QString> results;
    std::lock_guard<std::recursive_mutex> lk(m_clientMutex);
    if (!m_client || !m_connected) {
        setError(QStringLiteral("OPC UA 未连接"));
        return results;
    }
    if (nodeIds.size() != values.size()) {
        setError(QStringLiteral("writeNodes: nodeIds 与 values 数量不一致"));
        return results;
    }

    for (int i = 0; i < nodeIds.size(); ++i) {
        const QString& nid = nodeIds.at(i);

        UA_NodeId nodeId;
        UA_NodeId_init(&nodeId);
        QByteArray nidUtf8 = nid.toUtf8();  // 持有生命周期，避免 UA_String 悬垂
        UA_String nidStr = UA_STRING(const_cast<char*>(nidUtf8.constData()));
        if (UA_NodeId_parse(&nodeId, nidStr) != UA_STATUSCODE_GOOD) {
            results[nid] = QStringLiteral("无效 NodeId");
            UA_NodeId_clear(&nodeId);
            continue;
        }

        UA_Variant val;
        if (!qtVariantToUaVariant(values.at(i), val)) {
            results[nid] = QStringLiteral("值类型不支持");
            UA_NodeId_clear(&nodeId);
            continue;
        }

        UA_StatusCode ret = UA_Client_writeValueAttribute(m_client, nodeId, &val);
        results[nid] = uaStatusToString(ret);

        UA_Variant_clear(&val);
        UA_NodeId_clear(&nodeId);
    }
    return results;
}

// 常见 OPC UA 内置 DataType 节点 → 友好名映射
static QString dataTypeIdToName(const QString& nodeId)
{
    if (nodeId.startsWith("ns=0;i=")) {
        bool ok = false;
        const int id = nodeId.mid(6).toInt(&ok);
        if (!ok) return nodeId;
        switch (id) {
        case 1:  return "Boolean";
        case 2:  return "SByte";
        case 3:  return "Byte";
        case 4:  return "Int16";
        case 5:  return "UInt16";
        case 6:  return "Int32";
        case 7:  return "UInt32";
        case 8:  return "Int64";
        case 9:  return "UInt64";
        case 10: return "Float";
        case 11: return "Double";
        case 12: return "String";
        case 13: return "DateTime";
        case 15: return "ByteString";
        default:  break;
        }
    }
    return nodeId;
}

QString OpcUaAdapter::browseRoot()
{
    std::lock_guard<std::recursive_mutex> lk(m_clientMutex);
    if (!m_client || !m_connected) {
        setError(QStringLiteral("OPC UA 未连接"));
        return QStringLiteral("[]");
    }

    // 浏览 Objects Folder (ns=0;i=85) 的正向子节点
    UA_BrowseRequest bReq;
    UA_BrowseRequest_init(&bReq);
    bReq.requestedMaxReferencesPerNode = 0;
    bReq.nodesToBrowse = UA_BrowseDescription_new();
    bReq.nodesToBrowseSize = 1;
    bReq.nodesToBrowse[0].nodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    bReq.nodesToBrowse[0].browseDirection = UA_BROWSEDIRECTION_FORWARD;
    bReq.nodesToBrowse[0].resultMask = UA_BROWSERESULTMASK_ALL;
    bReq.nodesToBrowse[0].includeSubtypes = true;

    UA_BrowseResponse bResp = UA_Client_Service_browse(m_client, bReq);
    UA_BrowseRequest_clear(&bReq);

    // 第一遍：构建 JSON 数组，同时收集 Variable 节点用于批量读 DataType
    QJsonArray arr;
    QVector<UA_NodeId> varNodeIds;
    QVector<int> varIndices;

    if (bResp.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
        for (size_t i = 0; i < bResp.resultsSize; ++i) {
            const UA_BrowseResult& br = bResp.results[i];
            for (size_t j = 0; j < br.referencesSize; ++j) {
                const UA_ReferenceDescription& ref = br.references[j];
                QJsonObject node;
                node["nodeId"] = uaNodeIdToQString(ref.nodeId.nodeId);
                node["browseName"] = uaStringToQString(ref.browseName.name);
                node["displayName"] = uaStringToQString(ref.displayName.text);
                node["nodeClass"] = static_cast<int>(ref.nodeClass);

                if (ref.nodeClass == UA_NODECLASS_VARIABLE) {
                    node["dataType"] = ""; // 占位
                    varNodeIds.append(ref.nodeId.nodeId);
                    varIndices.append(arr.size());
                } else {
                    node["dataType"] = "";
                }
                arr.append(node);
            }
        }
    } else {
        setError(uaStatusToString(bResp.responseHeader.serviceResult));
    }
    UA_BrowseResponse_clear(&bResp);

    // 第二遍：批量读 Variable 节点的 DataType 属性（一次网络往返）
    if (!varNodeIds.isEmpty()) {
        QVector<UA_ReadValueId> rvids(varNodeIds.size());
        for (int i = 0; i < varNodeIds.size(); ++i) {
            UA_ReadValueId_init(&rvids[i]);
            rvids[i].attributeId = UA_ATTRIBUTEID_DATATYPE;
            rvids[i].nodeId = varNodeIds[i];
        }

        UA_ReadRequest rdReq;
        UA_ReadRequest_init(&rdReq);
        rdReq.nodesToRead = rvids.data();
        rdReq.nodesToReadSize = static_cast<size_t>(rvids.size());

        UA_ReadResponse rdResp = UA_Client_Service_read(m_client, rdReq);
        if (rdResp.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
            for (int i = 0; i < varIndices.size(); ++i) {
                if (i >= static_cast<int>(rdResp.resultsSize)) continue;
                const UA_DataValue& dv = rdResp.results[i];
                if (dv.hasValue && UA_Variant_isScalar(&dv.value) &&
                    dv.value.type == &UA_TYPES[UA_TYPES_NODEID]) {
                    UA_NodeId typeId = *static_cast<UA_NodeId*>(dv.value.data);
                    QString typeStr = dataTypeIdToName(uaNodeIdToQString(typeId));
                    QJsonObject obj = arr[varIndices[i]].toObject();
                    obj["dataType"] = typeStr;
                    arr[varIndices[i]] = obj;
                }
            }
        }
        for (int i = 0; i < rvids.size(); ++i) {
            UA_NodeId_clear(&rvids[i].nodeId);
        }
        UA_ReadResponse_clear(&rdResp);
    }

    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

// ============================================================
// 辅助方法
// ============================================================

void OpcUaAdapter::setError(const QString& err)
{
    m_lastError = err;
}

QString OpcUaAdapter::uaStatusToString(quint32 statusCode)
{
    return QString::fromUtf8(UA_StatusCode_name(statusCode));
}
