#pragma once

#include <QObject>
#include <QMap>
#include <QString>
#include <QMutex>

class QObject;

class ConnectionPool : public QObject {
    Q_OBJECT

public:
    enum ConnectionType {
        Ftp,
        Telnet,
        Modbus,
        OpcUa,
        WebSocket
    };

    enum ConnectionStatus {
        Disconnected,
        Connecting,
        Connected,
        Error
    };

private:
    ConnectionPool(QObject* parent = nullptr) : QObject(parent) {}
    ~ConnectionPool() = default;

    QMap<QString, QObject*> m_connections;
    QString m_user;
    QString m_pass;
    mutable QMutex m_mutex;

public:
    static ConnectionPool* instance() {
        static ConnectionPool instance;
        return &instance;
    }

    QObject* getConnection(const QString& ip, int port, ConnectionType type);
    void releaseConnection(QObject* connection);
    void setCredentials(const QString& user, const QString& pass);
    ConnectionStatus getStatus(const QString& ip) const;

signals:
    void connectionStatusChanged(const QString& ip, ConnectionStatus status);
};
