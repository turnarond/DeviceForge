/* RelayRecording.cpp */
#include "RelayRecording.h"
#include "NetRelayTypes.h"
#include <QFile>
#include <QDataStream>
#include <QHostAddress>
#include <cstring>

bool RelayRecording::load(const QString& path, NrecFile& out, QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = "无法打开文件: " + file.errorString(); return false; }
    if (file.size() < nrec::kHeaderSize) { error = "文件过小，非法 .nrec"; return false; }

    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);

    char magic[4];
    if (in.readRawData(magic, 4) != 4 ||
        memcmp(magic, nrec::kMagic, 4) != 0) {
        error = "magic 校验失败，非 .nrec 文件"; return false;
    }
    quint16 version; quint8 proto, r0;
    in >> version >> proto >> r0;
    if (version != nrec::kVersion) { error = QString("不支持的版本: %1").arg(version); return false; }
    qint64 startEpoch; in >> startEpoch;
    // reserved 区（16 字节）：组播时含 port(u16)+ipv4(u32)
    quint16 groupPort; quint32 groupIpv4;
    in >> groupPort >> groupIpv4;
    for (int i = 0; i < 10; ++i) { quint8 x; in >> x; }  // 剩余 reserved

    out.protocol = (proto == 0) ? RelayProtocol::Tcp
                 : (proto == 1) ? RelayProtocol::Udp
                 : RelayProtocol::Multicast;
    out.startEpochMs = startEpoch;
    if (out.protocol == RelayProtocol::Multicast) {
        out.groupPort = groupPort;
        out.groupAddr = QHostAddress(groupIpv4).toString();
    } else {
        out.groupPort = 0;
        out.groupAddr.clear();
    }
    out.records.clear();

    while (!in.atEnd()) {
        quint8 dir, rr; quint32 sid; qint64 ts; quint32 len;
        in >> dir >> rr >> sid >> ts >> len;
        if (in.status() != QDataStream::Ok) { error = "记录头截断"; return false; }
        QByteArray payload;
        if (len > 0) {
            // 防止损坏/恶意文件声明超大 len 导致巨额分配或 int 溢出（len>=0x80000000 时 int(len) 为负→UB）
            const qint64 remaining = file.size() - file.pos();
            if (qint64(len) > remaining) {
                error = "payload 长度超出文件剩余字节，文件损坏"; return false;
            }
            payload.resize(int(len));
            if (in.readRawData(payload.data(), int(len)) != int(len)) {
                error = "payload 截断"; return false;
            }
        }
        NrecRecord rec;
        rec.dir = (dir == 0) ? RelayDirection::Upstream : RelayDirection::Downstream;
        rec.sessionId = int(sid);
        rec.tsOffsetMs = ts;
        rec.payload = payload;
        out.records.append(rec);
    }
    return true;
}
