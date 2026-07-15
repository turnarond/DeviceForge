#pragma once

#include <QtWidgets/QMainWindow>
#include <QComboBox>
#include <QPushButton>
#include <QLineEdit>
#include <QStandardItemModel>
#include <QStandardItem>
#include "ui_DeployMaster.h"
#include "src/framework/AppState.h"
#include "src/model/FtpManager.h"
#include "src/updater/UpdateTypes.h" // Task 5: UpdateState 枚举(用于 onUpdateStateChanged 签名)

class ToolHost;
class DeviceBusWidget;
class TelnetWidget;
class OpcUaClientTab; // forward declaration
class WebSocketWidget; // forward declaration (migrated to Tool architecture)
class NetRelayWidget; // forward declaration (网络调试中继 Tool)
class UpdateChecker;  // 在线更新检查服务（Task 3）
class UpdateDialog;   // 在线更新对话框（Task 4）

class DeployMaster : public QMainWindow
{
    Q_OBJECT

public:
    DeployMaster(QWidget* parent = nullptr);
    ~DeployMaster();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

public:
    QStringList getTargetIPList() const;
    QTextEdit* getGlobalLogItem() const {
        return ui.txt_globalLog;
    }
    QString getFtpUser() const;
    QString getFtpPass() const;


private:
    QString lastUsedDirectory; // 记录上次使用的目录
    // 旧 deploy 列表已清理
    
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

private slots:
    void buildRemoteFileTree(const QList<FtpFileInfo>& files);

private:
    void setupFtpDeployTab();
    void setupTelnetDeployTab();
    void setupModbusClusterTab();
    void setupOpcUaClientTab(); // new
    void setupWebSocketClientTab(); // new
    void setupNetRelayTab(); // 网络调试中继 Tool
    void setupRemotePreview(); // 初始化远端预览功能

    // 在线更新集成（Task 5）
    void setupUpdateChecker();
    void onCheckUpdateTriggered();
    void onVersionLabelClicked();
    void onUpdateStateChanged(UpdateState state);
    
    // 远端预览相关方法
    void refreshRemoteFiles(); // 刷新远程文件列表
    void refreshDeviceCombo();
    void onIPSelectionChanged();
    void onRemoteFileDoubleClicked(const QModelIndex& index);
    void onDownloadRemoteFile();

private:
    Ui::DeployMaster ui;
    ToolHost* m_toolHost = nullptr;
    DeviceBusWidget* m_deviceBusWidget = nullptr;
    QLineEdit* m_remotePathEdit = nullptr;  // 远端预览路径（替代旧 ui.txt_remotePath）
    QComboBox* m_protocolCombo = nullptr;   // 协议选择（FTP / SCP）
    QPushButton* m_refreshBtn = nullptr;    // 刷新按钮（替代旧 ui.btn_refreshRemote）
    std::shared_ptr<class FtpDeployBackend> m_ftpBackend;
    std::shared_ptr<class TelnetBackend> m_telnetBackend;
    std::shared_ptr<class WebSocketBackend> m_webSocketBackend;
    class FtpDeployWidget* m_ftpDeployTab = nullptr;
    TelnetWidget* m_telnetDeployTab = nullptr;
    std::shared_ptr<class ModbusBackend> m_modbusBackend;
    class ModbusWidget* m_modbusWidget = nullptr;
    OpcUaClientTab* m_opcUaTab = nullptr; // new
    WebSocketWidget* m_webSocketWidget = nullptr; // migrated to Tool architecture
    std::shared_ptr<class NetRelayBackend> m_netRelayBackend;
    NetRelayWidget* m_netRelayWidget = nullptr;

    // 在线更新（Task 5）
    std::shared_ptr<UpdateChecker> m_updateChecker; // 在线更新 ServiceTask
    UpdateDialog*  m_updateDialog     = nullptr;    // 非模态更新对话框
    QAction*       m_checkUpdateAction = nullptr;   // 帮助菜单:检查更新
    QLabel*        m_versionLabel     = nullptr;    // 状态栏:当前版本标签(可点击)
};

