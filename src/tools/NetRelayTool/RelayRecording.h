/* RelayRecording.h — 读取并校验 .nrec 文件 */
#pragma once
#include "NetRelayTypes.h"
#include <QString>
#include <QByteArray>
#include <QVector>

struct NrecRecord {
    RelayDirection dir = RelayDirection::Upstream;
    int            sessionId = 0;
    qint64         tsOffsetMs = 0;
    QByteArray     payload;
};

struct NrecFile {
    RelayProtocol       protocol = RelayProtocol::Tcp;
    qint64              startEpochMs = 0;
    QString             groupAddr;          // 组播组地址（点分），非组播为空
    quint16             groupPort = 0;      // 组播端口，非组播为 0
    QVector<NrecRecord> records;
};

class RelayRecording {
public:
    // 成功返回 true 并填充 out；失败返回 false 并写入 error。坏 magic/版本/截断都判失败。
    static bool load(const QString& path, NrecFile& out, QString& error);
};
