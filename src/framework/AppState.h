#pragma once

#include <QObject>
#include <QStringList>
#include <QString>
#include <QMutex>
#include <QMutexLocker>

class AppState : public QObject {
    Q_OBJECT

private:
    AppState(QObject* parent = nullptr) : QObject(parent), m_taskProgress(0), m_isBusy(false) {}
    ~AppState() = default;

    QStringList m_selectedDevices;
    QString m_currentTask;
    int m_taskProgress;
    bool m_isBusy;
    QString m_lastError;
    mutable QMutex m_mutex;

public:
    static AppState* instance() {
        static AppState instance;
        return &instance;
    }

    QStringList selectedDevices() const {
        QMutexLocker locker(&m_mutex);
        return m_selectedDevices;
    }
    void setSelectedDevices(const QStringList& devices);

    QString currentTask() const {
        QMutexLocker locker(&m_mutex);
        return m_currentTask;
    }
    void setCurrentTask(const QString& task);

    int taskProgress() const {
        QMutexLocker locker(&m_mutex);
        return m_taskProgress;
    }
    void setTaskProgress(int progress);

    bool isBusy() const {
        QMutexLocker locker(&m_mutex);
        return m_isBusy;
    }
    void setIsBusy(bool busy);

    QString lastError() const {
        QMutexLocker locker(&m_mutex);
        return m_lastError;
    }
    void setLastError(const QString& error);

signals:
    void selectedDevicesChanged(const QStringList& devices);
    void currentTaskChanged(const QString& task);
    void taskProgressChanged(int progress);
    void isBusyChanged(bool busy);
    void lastErrorChanged(const QString& error);
};
