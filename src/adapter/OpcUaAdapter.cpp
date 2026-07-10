/*
 * OpcUaAdapter.cpp — OPC UA 协议适配器实现（open62541 v1.5.5）
 *
 * 说明：open62541.h 中大量声明由 UA_ENABLE_* 编译宏门控。CMake 构建中这些宏
 * 由 open62541 目标以 PUBLIC 方式传递到本文件；VS/vcxproj 构建下本文件不经过
 * open62541_impl.cpp，故此处在包含头文件前用 #ifndef 兜底定义，保证两套构建对等。
 */

// --- open62541 编译开关兜底（与 src/thirdparty/open62541/CMakeLists.txt 保持一致）---
#ifndef UA_ENABLE_CLIENT
#define UA_ENABLE_CLIENT 1
#endif
#ifndef UA_ENABLE_SUBSCRIPTIONS
#define UA_ENABLE_SUBSCRIPTIONS 1
#endif
#ifndef UA_ENABLE_NODELIST
#define UA_ENABLE_NODELIST 1
#endif
#ifndef UA_ENABLE_METHODCALLS
#define UA_ENABLE_METHODCALLS 0
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
    m_connected = false;
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

ProtocolCapability OpcUaAdapter::capability() const
{
    ProtocolCapability c;
    c.requestResponse  = true;
    c.streaming        = false;   // 订阅（流）暂未实现
    c.broadcast        = false;
    c.publishSubscribe = false;   // UA_ENABLE_SUBSCRIPTIONS 可用，待后续接入
    c.maxConnections   = 1;
    return c;
}

// ============================================================
// OPC UA 扩展接口
// ============================================================

QVariantMap OpcUaAdapter::readNodes(const QStringList& nodeIds)
{
    QVariantMap results;
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

QString OpcUaAdapter::browseRoot()
{
    if (!m_client || !m_connected) {
        setError(QStringLiteral("OPC UA 未连接"));
        return QStringLiteral("[]");
    }

    // 浏览 Objects Folder (ns=0;i=85) 的正向子节点
    UA_BrowseRequest bReq;
    UA_BrowseRequest_init(&bReq);
    bReq.requestedMaxReferencesPerNode = 0;  // 0 = 不限制
    bReq.nodesToBrowse = UA_BrowseDescription_new();
    bReq.nodesToBrowseSize = 1;
    bReq.nodesToBrowse[0].nodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    bReq.nodesToBrowse[0].browseDirection = UA_BROWSEDIRECTION_FORWARD;
    bReq.nodesToBrowse[0].resultMask = UA_BROWSERESULTMASK_ALL;
    bReq.nodesToBrowse[0].includeSubtypes = true;

    UA_BrowseResponse bResp = UA_Client_Service_browse(m_client, bReq);

    QJsonArray arr;
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
                arr.append(node);
            }
        }
    } else {
        setError(uaStatusToString(bResp.responseHeader.serviceResult));
    }

    // bReq.nodesToBrowse 由 UA_BrowseDescription_new 分配，随 request clear 释放
    UA_BrowseRequest_clear(&bReq);
    UA_BrowseResponse_clear(&bResp);

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
