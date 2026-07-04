#include "ConnectionPool.h"
#include <QMutexLocker>

QObject* ConnectionPool::getConnection(const QString& ip, int port, ConnectionType type) {
    QMutexLocker locker(&m_mutex);
    QString key = QString("%1:%2:%3").arg(ip).arg(port).arg(static_cast<int>(type));

    if (m_connections.contains(key)) {
        return m_connections[key];
    }

    return nullptr;
}

void ConnectionPool::releaseConnection(QObject* connection) {
    QMutexLocker locker(&m_mutex);
    QString key = m_connections.key(connection);
    if (!key.isEmpty()) {
        m_connections.remove(key);
        connection->deleteLater();
    }
}

void ConnectionPool::setCredentials(const QString& user, const QString& pass) {
    QMutexLocker locker(&m_mutex);
    m_user = user;
    m_pass = pass;
}

ConnectionPool::ConnectionStatus ConnectionPool::getStatus(const QString& ip) const {
    QMutexLocker locker(&m_mutex);
    for (const QString& key : m_connections.keys()) {
        if (key.startsWith(ip)) {
            return Connected;
        }
    }
    return Disconnected;
}
