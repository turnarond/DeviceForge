#pragma once

#include <QtWidgets/QMainWindow>
#include <QStandardItemModel>
#include <QStandardItem>
#include "ui_DeployMaster.h"
#include "FtpRemoteItem.h"
#include "FtpManager.h"
#include "LogQueryTab.h"
#include "TelnetDeploy.h"
#include "ModbusCluster.h"

class OpcUaClientTab; // forward declaration
class WebSocketClient; // forward declaration
class DiagnosticClient; // forward declaration

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
    QString getFtpUser() const {
        return ui.txt_user->text().trimmed();
    }
    QString getFtpPass() const {
        return ui.txt_pass->text().trimmed();
    }


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

public slots:
    void appendFtpLog(const QString& log);

private slots:
    // Add your custom slots here
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
    void buildRemoteFileTree(const QList<FtpFileInfo>& files); // 构建远程文件树

private:
    void setupLogQueryTab();
    void setupTelnetDeployTab();
    void setupModbusClusterTab();
    void setupOpcUaClientTab(); // new
    void setupWebSocketClientTab(); // new
    void setupDiagnosticClientTab(); // new
    void setupRemotePreview(); // 初始化远端预览功能
    
    // 远端预览相关方法
    void refreshRemoteFiles(); // 刷新远程文件列表
    void onIPSelectionChanged(); // IP选择变化处理
    void onRemoteFileDoubleClicked(const QModelIndex& index); // 双击文件/文件夹处理

private:
    Ui::DeployMaster ui;
    LogQueryTab* m_logQueryTab = nullptr;
    TelnetDeploy* m_telnetDeployTab = nullptr;
    ModbusCluster* m_modbusCluster = nullptr;
    OpcUaClientTab* m_opcUaTab = nullptr; // new
    WebSocketClient* m_webSocketClient = nullptr; // new
    //DiagnosticClient* m_diagnosticClient = nullptr; // new
};

