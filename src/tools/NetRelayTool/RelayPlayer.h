/* RelayPlayer.h — 加载 .nrec 并按原始时序把上行记录重放给消费者 */
#pragma once
#include "NetRelayTypes.h"
#include "RelayRecording.h"
#include <QString>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QHostAddress>
#include <QTimer>
#include <functional>
#include <string>

class RelayPlayer {
public:
    using LogCallback      = std::function<void(const std::string&)>;
    using ErrorCallback    = std::function<void(const std::string&)>;
    using ProgressCallback = std::function<void(int played, int total, qint64 tsOffsetMs)>;
    using FinishedCallback = std::function<void()>;

    RelayPlayer() = default;
    ~RelayPlayer();

    bool start(const QString& nrecPath, const QString& consumerHost,
               quint16 consumerPort, double speedFactor);
    void pause();
    void resume();
    void stop();
    bool isActive() const { return m_active; }

    void setLogCallback(LogCallback cb)           { m_logCb = std::move(cb); }
    void setErrorCallback(ErrorCallback cb)       { m_errorCb = std::move(cb); }
    void setProgressCallback(ProgressCallback cb) { m_progressCb = std::move(cb); }
    void setFinishedCallback(FinishedCallback cb) { m_finishedCb = std::move(cb); }

private:
    void scheduleNext();     // 安排下一条上行记录
    void sendCurrent();      // 发送 m_upstream[m_idx] 并递进
    void finishPlayback();   // 触发 finished 回调 + cleanup（带去重保护）
    void cleanup();
    void log(const std::string& s)   { if (m_logCb) m_logCb(s); }
    void fail(const std::string& s);

    NrecFile              m_file;
    QVector<int>          m_upstreamIdx;   // 指向 m_file.records 中 Upstream 记录的下标
    int                   m_idx = 0;        // 当前进度（在 m_upstreamIdx 内）
    double                m_speed = 1.0;

    RelayProtocol         m_protocol = RelayProtocol::Tcp;
    QTcpSocket*           m_tcp = nullptr;
    QUdpSocket*           m_udp = nullptr;
    QHostAddress          m_consumerAddr;
    quint16               m_consumerPort = 0;

    QTimer*               m_timer = nullptr;  // 单发定时器，按间隔调度
    bool                  m_active = false;
    bool                  m_paused = false;
    bool                  m_connected = false;
    bool                  m_finishing = false;  // 防止收尾流程被重复触发

    LogCallback      m_logCb;
    ErrorCallback    m_errorCb;
    ProgressCallback m_progressCb;
    FinishedCallback m_finishedCb;
};
