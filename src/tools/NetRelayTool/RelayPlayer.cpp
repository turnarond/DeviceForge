/* RelayPlayer.cpp */
#include "RelayPlayer.h"
#include <QHostInfo>

RelayPlayer::~RelayPlayer() { cleanup(); }

void RelayPlayer::fail(const std::string& s)
{
    if (m_errorCb) m_errorCb(s);
    cleanup();
}

bool RelayPlayer::start(const QString& nrecPath, const QString& consumerHost,
                        quint16 consumerPort, double speedFactor)
{
    if (m_active) return false;

    QString err;
    if (!RelayRecording::load(nrecPath, m_file, err)) { if (m_errorCb) m_errorCb(err.toStdString()); return false; }

    // 建立"上行记录"索引
    m_upstreamIdx.clear();
    for (int i = 0; i < m_file.records.size(); ++i)
        if (m_file.records[i].dir == RelayDirection::Upstream) m_upstreamIdx.append(i);
    if (m_upstreamIdx.isEmpty()) { if (m_errorCb) m_errorCb("录制中无上行记录，无可回放数据"); return false; }

    m_protocol = m_file.protocol;
    m_speed = (speedFactor > 0.0) ? speedFactor : 1.0;
    m_idx = 0;
    m_paused = false;

    if (m_protocol == RelayProtocol::Multicast) {
        // 组播回灌：默认目标取文件头组地址/端口，consumerHost/Port 非空则覆盖
        QString tgtAddr = consumerHost.isEmpty() ? m_file.groupAddr : consumerHost;
        quint16 tgtPort = (consumerPort == 0) ? m_file.groupPort : consumerPort;
        QHostAddress gaddr(tgtAddr);
        if (gaddr.isNull()) { if (m_errorCb) m_errorCb("组播目标地址无效: " + tgtAddr.toStdString()); return false; }
        m_consumerAddr = gaddr;
        m_consumerPort = tgtPort;
    } else {
        QHostAddress addr;
        if (!addr.setAddress(consumerHost)) {
            if (consumerHost.compare("localhost", Qt::CaseInsensitive) == 0) addr = QHostAddress::LocalHost;
            else { QHostInfo hi = QHostInfo::fromName(consumerHost);   // 回放为用户主动操作，允许同步解析
                   if (!hi.addresses().isEmpty()) addr = hi.addresses().first(); }
        }
        if (addr.isNull()) { if (m_errorCb) m_errorCb("消费者地址无效: " + consumerHost.toStdString()); return false; }
        m_consumerAddr = addr;
        m_consumerPort = consumerPort;
    }

    m_timer = new QTimer();
    m_timer->setSingleShot(true);
    QObject::connect(m_timer, &QTimer::timeout, [this]() { sendCurrent(); });

    m_active = true;

    if (m_protocol == RelayProtocol::Tcp) {
        m_tcp = new QTcpSocket();
        QObject::connect(m_tcp, &QTcpSocket::connected, [this]() {
            m_connected = true;
            log("回放: 已连接消费者，开始重放");
            scheduleNext();
        });
        QObject::connect(m_tcp, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            [this](QAbstractSocket::SocketError) {
                if (m_tcp) fail("回放连接失败: " + m_tcp->errorString().toStdString());
            });
        m_tcp->connectToHost(m_consumerAddr, m_consumerPort);
    } else if (m_protocol == RelayProtocol::Multicast) {
        m_udp = new QUdpSocket();
        if (!m_mcastIfaceAddr.isEmpty()) {
            for (const QNetworkInterface& itf : QNetworkInterface::allInterfaces()) {
                for (const QNetworkAddressEntry& e : itf.addressEntries()) {
                    if (e.ip().toString() == m_mcastIfaceAddr) {
                        m_udp->setMulticastInterface(itf); break;
                    }
                }
            }
        }
        m_connected = true;            // 组播无连接，直接开调度
        log("回放: 组播回灌模式，开始重放");
        scheduleNext();
    } else {
        m_udp = new QUdpSocket();
        m_connected = true;            // UDP 无需连接
        log("回放: UDP 模式，开始重放");
        scheduleNext();
    }
    return true;
}

void RelayPlayer::scheduleNext()
{
    if (!m_active || m_paused) return;
    if (m_idx >= m_upstreamIdx.size()) {          // 全部发完
        // TCP: write() 异步，字节可能仍滞留在写缓冲。必须等最后一条真正发出
        // 后再收尾，否则 close() 会丢弃末条记录。
        if (m_protocol == RelayProtocol::Tcp && m_tcp && m_tcp->bytesToWrite() > 0) {
            m_finishing = true;
            m_tcp->flush();
            if (m_tcp->bytesToWrite() > 0) {
                // 仍有未刷出的字节：等待 bytesWritten 排空后再收尾（一次性）
                QObject::connect(m_tcp, &QTcpSocket::bytesWritten, [this]() {
                    if (m_tcp && m_tcp->bytesToWrite() == 0) finishPlayback();
                });
                return;
            }
        }
        finishPlayback();
        return;
    }
    qint64 delay = 0;
    if (m_idx > 0) {
        qint64 prev = m_file.records[m_upstreamIdx[m_idx - 1]].tsOffsetMs;
        qint64 cur  = m_file.records[m_upstreamIdx[m_idx]].tsOffsetMs;
        delay = qMax<qint64>(0, cur - prev);
        if (m_speed > 0.0) delay = qint64(delay / m_speed);
    }
    m_timer->start(int(qMin<qint64>(delay, 60000)));  // 首条 delay=0 立即；单发上限 60s 防异常
}

void RelayPlayer::finishPlayback()
{
    if (!m_active) return;              // 已收尾，避免重复触发
    log("回放完成");
    if (m_finishedCb) m_finishedCb();
    cleanup();
}

void RelayPlayer::sendCurrent()
{
    if (!m_active || m_paused) return;
    const NrecRecord& rec = m_file.records[m_upstreamIdx[m_idx]];
    if (m_protocol == RelayProtocol::Tcp) {
        if (m_tcp && m_tcp->state() == QAbstractSocket::ConnectedState) m_tcp->write(rec.payload);
    } else {
        if (m_udp) m_udp->writeDatagram(rec.payload, m_consumerAddr, m_consumerPort);
    }
    ++m_idx;
    if (m_progressCb) m_progressCb(m_idx, m_upstreamIdx.size(), rec.tsOffsetMs);
    scheduleNext();
}

void RelayPlayer::pause()
{
    if (!m_active || m_paused) return;
    m_paused = true;
    if (m_timer) m_timer->stop();
    log("回放已暂停");
}

void RelayPlayer::resume()
{
    if (!m_active || !m_paused) return;
    m_paused = false;
    log("回放已继续");
    scheduleNext();
}

void RelayPlayer::stop()
{
    if (!m_active) return;
    log("回放已停止");
    cleanup();
}

void RelayPlayer::cleanup()
{
    m_active = false;
    m_paused = false;
    m_connected = false;
    m_finishing = false;
    if (m_timer) { m_timer->stop(); QObject::disconnect(m_timer, nullptr, nullptr, nullptr);
                   m_timer->deleteLater(); m_timer = nullptr; }
    if (m_tcp)   { QObject::disconnect(m_tcp, nullptr, nullptr, nullptr);
                   m_tcp->close(); m_tcp->deleteLater(); m_tcp = nullptr; }
    if (m_udp)   { m_udp->close(); m_udp->deleteLater(); m_udp = nullptr; }
}
