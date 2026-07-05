#pragma once

#include <QtWidgets/QMainWindow>
#include <QStandardItemModel>
#include <QStandardItem>
#include "ui_DeployMaster.h"
#include "FtpRemoteItem.h"
#include "LogQueryTab.h"
#include "ModbusCluster.h"
#include "src/framework/AppState.h"
#include "src/model/FtpManager.h"

class ToolHost;
class DeviceBusWidget;
class TelnetWidget;
class OpcUaClientTab; // forward declaration
class WebSocketWidget; // forward declaration (migrated to Tool architecture)

class DeployMaster : public QMainWindow
{
    Q_OBJECT

public:
    DeployMaster(QWidget* parent = nullptr);
    ~DeployMaster();

public:
    QStringList getTargetIPList() const;
    QTextEdit* getGlobalLogItem() const {
        return ui.txt_globalLog;
    }
    QString getFtpUser() const;
    QString getFtpPass() const;


private:
    QString lastUsedDirectory; // 记录上次使用的目录
    QList<UploadItem> uploadItems;
    
    // 远端预览相关
    QString currentRemoteIP; // 当前选中的远程设备IP
    QString currentRemotePath; // 当前浏览的远程路径
    QStandardItemModel* remoteFileModel; // 远程文件树模型

private:
    void addFileToList(const QString& filePath); // 添加文件到列表的函数
    void addFolderToList(const QString& folderPath); // 添加文件夹到列表的函数    
    QStringList getTargetIPs();

public:
    ToolHost* toolHost() const { return m_toolHost; }
    DeviceBusWidget* deviceBusWidget() const { return m_deviceBusWidget; }

    void initToolTabs();

public slots:
    void appendGlobalLog(const QString& log);

private slots:
    void onAddFilesClicked();
    void onAddFolderClicked();
    void onFilesDropped(const QStringList& filePaths);
    void onFileItemCleanClicked();
    void onDeployClicked();
    void onClearLogClicked();

    void onFtpUploadFinished(const QStringList& deploySuccesses,
        const QStringList& deployFailures,
        bool shouldReboot,
        const QString& user,
        const QString& pass);

private slots:
    void buildRemoteFileTree(const QList<FtpFileInfo>& files);

private:
    void setupLogQueryTab();
    void setupFtpDeployTab();
    void setupTelnetDeployTab();
    void setupModbusClusterTab();
    void setupOpcUaClientTab(); // new
    void setupWebSocketClientTab(); // new
    void setupRemotePreview(); // 初始化远端预览功能
    
    // 远端预览相关方法
    void refreshRemoteFiles(); // 刷新远程文件列表
    void refreshDeviceCombo(); // 根据 DeviceBusWidget 刷新设备下拉框
    void onIPSelectionChanged(); // IP选择变化处理
    void onRemoteFileDoubleClicked(const QModelIndex& index); // 双击文件/文件夹处理

private:
    Ui::DeployMaster ui;
    ToolHost* m_toolHost = nullptr;
    DeviceBusWidget* m_deviceBusWidget = nullptr;
    QLineEdit* m_remotePathEdit = nullptr;  // 远端预览路径（替代旧 ui.txt_remotePath）
    QComboBox* m_protocolCombo = nullptr;   // 协议选择（FTP / SCP）
    QPushButton* m_refreshBtn = nullptr;    // 刷新按钮（替代旧 ui.btn_refreshRemote）
    LogQueryTab* m_logQueryTab = nullptr;
    std::shared_ptr<class FtpDeployBackend> m_ftpBackend;
    std::shared_ptr<class TelnetBackend> m_telnetBackend;
    std::shared_ptr<class WebSocketBackend> m_webSocketBackend;
    class FtpDeployWidget* m_ftpDeployTab = nullptr;
    TelnetWidget* m_telnetDeployTab = nullptr;
    ModbusCluster* m_modbusCluster = nullptr;
    OpcUaClientTab* m_opcUaTab = nullptr; // new
    WebSocketWidget* m_webSocketWidget = nullptr; // migrated to Tool architecture
};

