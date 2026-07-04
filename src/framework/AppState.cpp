#include "AppState.h"
#include <QMutexLocker>

void AppState::setSelectedDevices(const QStringList& devices) {
    QMutexLocker locker(&m_mutex);
    if (m_selectedDevices != devices) {
        m_selectedDevices = devices;
        emit selectedDevicesChanged(m_selectedDevices);
    }
}

void AppState::setCurrentTask(const QString& task) {
    QMutexLocker locker(&m_mutex);
    if (m_currentTask != task) {
        m_currentTask = task;
        emit currentTaskChanged(m_currentTask);
    }
}

void AppState::setTaskProgress(int progress) {
    QMutexLocker locker(&m_mutex);
    int clamped = qMax(0, qMin(100, progress));
    if (m_taskProgress != clamped) {
        m_taskProgress = clamped;
        emit taskProgressChanged(m_taskProgress);
    }
}

void AppState::setIsBusy(bool busy) {
    QMutexLocker locker(&m_mutex);
    if (m_isBusy != busy) {
        m_isBusy = busy;
        emit isBusyChanged(m_isBusy);
    }
}

void AppState::setLastError(const QString& error) {
    QMutexLocker locker(&m_mutex);
    if (m_lastError != error) {
        m_lastError = error;
        emit lastErrorChanged(m_lastError);
    }
}
