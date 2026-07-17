/* RelayRecorder.cpp */
#include "RelayRecorder.h"
#include <QHostAddress>

RelayRecorder::~RelayRecorder() { close(); }

bool RelayRecorder::open(const QString& path, RelayProtocol proto, qint64 startEpochMs,
                         const QString& groupAddr, quint16 groupPort)
{
    close();
    m_file = std::make_unique<QFile>(path);
    if (!m_file->open(QIODevice::WriteOnly)) { m_file.reset(); return false; }
    m_file->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    m_stream = std::make_unique<QDataStream>(m_file.get());
    m_stream->setByteOrder(QDataStream::LittleEndian);

    // --- 文件头（32 字节，详见 spec §4.1）---
    m_stream->writeRawData(nrec::kMagic, 4);                 // 0: magic
    *m_stream << quint16(nrec::kVersion);                    // 4: version
    quint8 protoByte = (proto == RelayProtocol::Tcp) ? 0
                     : (proto == RelayProtocol::Udp) ? 1 : 2; // 6: protocol (2=Multicast)
    *m_stream << protoByte;
    *m_stream << quint8(0);                                  // 7: reserved
    *m_stream << qint64(startEpochMs);                       // 8: start_epoch_ms
    // 16..31: reserved 区 → 组播: group_port(u16) + group_ipv4(u32) + 剩余保留
    *m_stream << quint16(groupPort);                         // 16: group_port
    quint32 ipv4 = 0;
    if (!groupAddr.isEmpty()) {
        QHostAddress a(groupAddr);
        ipv4 = a.toIPv4Address();                            // 点分→u32；非法为 0
    }
    *m_stream << quint32(ipv4);                              // 18: group_ipv4
    for (int i = 0; i < 10; ++i) *m_stream << quint8(0);     // 22..31: reserved (2+4+10=16)
    return true;
}

void RelayRecorder::append(RelayDirection dir, int sessionId, qint64 tsOffsetMs, const QByteArray& data)
{
    if (!isOpen() || !m_stream) return;
    *m_stream << quint8(dir == RelayDirection::Upstream ? 0 : 1); // direction
    *m_stream << quint8(0);                                       // reserved
    *m_stream << quint32(sessionId);                             // session_id
    *m_stream << qint64(tsOffsetMs);                            // ts_offset_ms
    *m_stream << quint32(data.size());                          // length
    if (!data.isEmpty()) m_stream->writeRawData(data.constData(), data.size());
}

void RelayRecorder::close()
{
    m_stream.reset();
    if (m_file) { m_file->close(); m_file.reset(); }
}
