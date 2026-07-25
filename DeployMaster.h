#pragma once

#include <QtWidgets/QMainWindow>
#include <QStackedWidget>
#include "ui_DeployMaster.h"
#include "src/framework/AppState.h"
#include "src/ui/NavBar.h"
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

public:
    ToolHost* toolHost() const { return m_toolHost; }
    DeviceBusWidget* deviceBusWidget() const { return m_deviceBusWidget; }

    void initToolTabs();

public slots:
    void appendGlobalLog(const QString& log);

private slots:
    void onClearLogClicked();

private:
    void setupFtpDeployTab();
    void setupTelnetDeployTab();
    void setupModbusClusterTab();
    void setupOpcUaClientTab(); // new
    void setupWebSocketClientTab(); // new
    void setupNetRelayTab(); // 网络调试中继 Tool

    // 在线更新集成（Task 5）
    void setupUpdateChecker();
    void onCheckUpdateTriggered(bool isAuto = false);
    void onVersionLabelClicked();
    void onUpdateStateChanged(UpdateState state);
    
private:
    Ui::DeployMaster ui;
    NavBar* m_navBar = nullptr;
    QStackedWidget* m_toolStack = nullptr;
    ToolHost* m_toolHost = nullptr;
    DeviceBusWidget* m_deviceBusWidget = nullptr;
    std::shared_ptr<class FtpDeployBackend> m_ftpBackend;
    std::shared_ptr<class TelnetBackend> m_telnetBackend;
    std::shared_ptr<class WebSocketBackend> m_webSocketBackend;
    class FtpDeployWidget* m_ftpDeployTab = nullptr;
    TelnetWidget* m_telnetDeployTab = nullptr;
    std::shared_ptr<class ModbusBackend> m_modbusBackend;
    class ModbusWidget* m_modbusWidget = nullptr;
    OpcUaClientTab* m_opcUaTab = nullptr; // 旧演示 Tab（已废弃，保留声明避免破坏其它引用）
    std::shared_ptr<class OpcUaClientBackend> m_opcUaClientBackend;
    class OpcUaClientWidget* m_opcUaClientWidget = nullptr;
    WebSocketWidget* m_webSocketWidget = nullptr; // migrated to Tool architecture
    std::shared_ptr<class NetRelayBackend> m_netRelayBackend;
    NetRelayWidget* m_netRelayWidget = nullptr;

    // 底部日志折叠（Task 4）
    QWidget* m_logCollapseBar = nullptr;  // 日志折叠指示条
    bool     m_logExpanded    = true;     // 日志展开状态

    // 在线更新（Task 5）
    std::shared_ptr<UpdateChecker> m_updateChecker; // 在线更新 ServiceTask
    UpdateDialog*  m_updateDialog     = nullptr;    // 非模态更新对话框
    QAction*       m_checkUpdateAction = nullptr;   // 帮助菜单:检查更新
    QLabel*        m_versionLabel     = nullptr;    // 状态栏:当前版本标签(可点击)
    bool           m_autoCheck        = false;      // 自动检查标志(静默失败)
};

