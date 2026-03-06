#pragma once

#include <QWidget>
#include <QListWidgetItem>
#include <QStandardItemModel>
#include <QTimer>

#include "tinyvsoa/tinyvsoa.h"

namespace Ui {
    class TabDiagnostic;
}

class DeployMaster; // 前向声明主窗口

class DiagnosticClient : public QWidget
{
    Q_OBJECT

public:
    explicit DiagnosticClient(DeployMaster* parentWindow, QWidget* parent = nullptr);
    ~DiagnosticClient();

private slots:
    void on_btn_add_device_clicked();
    void on_btn_remove_device_clicked();
    void on_btn_refresh_status_clicked();
    void on_btn_start_diagnostic_clicked();
    void on_btn_open_log_clicked();
    void on_btn_clean_logs_clicked();

    // 定时器相关槽函数
    void onRefreshStatusTimer();

private:
    void initUI();
    void loadDevices();
    void saveDevices();
    void updateDeviceStatus(const QString& deviceIP, const QString& status);
    void startDiagnosticForDevice(const QString& deviceIP);
    void collectLogs(const QString& deviceIP);
    void downloadLogs(const QString& deviceIP, const QString& fileName, qint64 fileSize);
    void cleanupRemoteFiles(const QString& deviceIP, const QString& fileName);
    void updateLocalLogTree();
    void updateTasksTable();

private:
    Ui::TabDiagnostic* ui;
    DeployMaster* m_mainWindow;
    QTimer* m_refreshTimer;
    QStandardItemModel* m_logTreeModel;

    // 设备信息
    struct DeviceInfo {
        QString ip;
        QString name;
        QString status;
        VsoaClient* vsoaClient;
    };
    QList<DeviceInfo> m_devices;

    // 任务信息
    struct TaskInfo {
        QString deviceIP;
        QString status;
        int progress;
        QString fileName;
        qint64 fileSize;
    };
    QList<TaskInfo> m_tasks;
};