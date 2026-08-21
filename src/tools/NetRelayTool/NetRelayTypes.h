/*
 * NetRelayTypes.h — NetRelayTool 共享类型、.nrec 格式常量与地址校验工具
 */
#pragma once
#include <cstdint>
#include <QString>
#include <QHostAddress>
#include <QNetworkInterface>

// 中继协议类型
enum class RelayProtocol { Tcp, Udp, Multicast };

// 中继方向：Upstream = 生产者→消费者；Downstream = 消费者→生产者
enum class RelayDirection { Upstream, Downstream };

// 组播抓收实时转发配置（M→U：单播目标；M→M：组播目标，需 join 目标组才能发送）
struct CaptureForward {
    bool               enabled = false;
    QHostAddress       target;
    quint16            port = 0;
    QNetworkInterface  iface;   // 转发网卡（M→M join 目标组用；M→U 可空）
};

// .nrec 录制文件格式常量（详见 spec §4）
namespace nrec {
    constexpr char       kMagic[4]   = { 'N', 'R', 'E', 'C' };
    constexpr uint16_t   kVersion    = 1;
    constexpr int        kHeaderSize = 32;   // 固定文件头字节数
}

// 完整 IPv4 点分格式校验（4 段、每段 0-255）——拒绝 inet_aton 式简写（如 "239.1.2"）
inline bool isFullIPv4Literal(const QString& addr)
{
    const QStringList parts = addr.split('.');
    if (parts.size() != 4) return false;
    for (const QString& p : parts) {
        if (p.isEmpty() || p.size() > 3) return false;
        bool ok = false;
        const int v = p.toInt(&ok);
        if (!ok || v < 0 || v > 255) return false;
    }
    return true;
}

// 组播地址校验：IPv4 D 类 224.0.0.0 ~ 239.255.255.255（纯逻辑，可单测）
inline bool isValidMulticastAddress(const QString& addr)
{
    if (!isFullIPv4Literal(addr)) return false;
    const QHostAddress group(addr);
    if (group.protocol() != QAbstractSocket::IPv4Protocol) return false;
    const quint32 v4 = group.toIPv4Address();
    const quint8 first = quint8((v4 >> 24) & 0xFF);
    return first >= 224 && first <= 239;
}

// 转发目标校验：有效 IPv4 单播（M→U）或组播 D 类（M→M）；
// 拒绝有限广播 255.255.255.255（广播非本功能目标）。纯逻辑，可单测。
inline bool isValidForwardTarget(const QString& addr)
{
    if (!isFullIPv4Literal(addr)) return false;
    const QHostAddress target(addr);
    if (target.protocol() != QAbstractSocket::IPv4Protocol) return false;
    const quint32 v4 = target.toIPv4Address();
    const quint8 first = quint8((v4 >> 24) & 0xFF);
    if (first >= 224 && first <= 239) return true;   // 组播目标（M→M）
    if (first >= 240) return false;                  // 保留段
    if (v4 == 0xFFFFFFFF) return false;              // 有限广播 255.255.255.255
    return true;                                     // 普通单播（M→U）
}
