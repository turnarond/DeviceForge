/* RelayRecorder.h — 顺序写 .nrec 录制文件 */
#pragma once
#include "NetRelayTypes.h"
#include <QString>
#include <QByteArray>
#include <QFile>
#include <QDataStream>
#include <memory>

class RelayRecorder {
public:
    RelayRecorder() = default;
    ~RelayRecorder();

    bool open(const QString& path, RelayProtocol proto, qint64 startEpochMs,
              const QString& groupAddr = QString(), quint16 groupPort = 0);
    void append(RelayDirection dir, int sessionId, qint64 tsOffsetMs, const QByteArray& data);
    void close();
    bool isOpen() const { return m_file && m_file->isOpen(); }

private:
    std::unique_ptr<QFile>       m_file;
    std::unique_ptr<QDataStream> m_stream;
};
