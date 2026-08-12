#include "DeviceForge.h"
#include "ThemeUtils.h"
#include <QtWidgets/QApplication>
#include <QNetworkProxy>
#include <QFile>
#include <QTextStream>
#include <QIcon>
#include <libssh2/libssh2.h>
#include "src/logging/LogBridge.h"
#include "src/adapter/ProtocolRegistry.h"
#include "src/adapter/FtpAdapter.h"
#include "src/adapter/TelnetAdapter.h"
#include "src/adapter/SshAdapter.h"
#include "src/adapter/OpcUaAdapter.h"
#include "src/framework/ToolHost.h"
#include "src/framework/ToolRegistry.h"
#include "src/config/ConfigStore.h"
#include "src/tools/FtpDeployTool/FtpDeployBackend.h"
#include "src/tools/FtpDeployTool/FtpDeployWidget.h"
#include "src/tools/TelnetTool/TelnetBackend.h"
#include "src/tools/TelnetTool/TelnetWidget.h"
#include "src/tools/WebSocketTool/WebSocketBackend.h"
#include "src/tools/WebSocketTool/WebSocketWidget.h"
#include <curl/curl.h>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    // 工业局域网工具：所有 Qt socket（SFTP/WebSocket）直连，忽略系统代理
    // （Windows 系统代理如 Clash 会劫持局域网连接，QTcpSocket 报 ProxyTypeError）
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    app.setWindowIcon(QIcon(":/icons/app.ico"));  // 应用级窗口/任务栏图标
    LogBridge::install();  // Qt -> lwlog 日志桥接

    // 打开 ConfigStore（单例；用于 device.list / SettingsDialog 等持久化）
    // SettingsDialog 内部有防御性 open，但 DeviceBusWidget 启动期需要它已 open
    ConfigStore::instance().open();

    // libcurl 全局初始化(UpdateChecker 跨线程使用)
    // 进程退出由 OS 回收，无需显式 curl_global_cleanup
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // libssh2 进程级初始化（仅一次）— SshAdapter 不再各自 init/exit，
    // 避免每实例反复拆建全局状态。进程退出由 OS 回收，无需显式 libssh2_exit()
    libssh2_init(0);

    // 注册内置协议适配器
    ProtocolRegistry::instance()->registerFactory("ftp",
        []() -> std::shared_ptr<IProtocolAdapter> {
            return std::make_shared<FtpAdapter>();
        });
    ProtocolRegistry::instance()->registerFactory("telnet",
        []() -> std::shared_ptr<IProtocolAdapter> {
            return std::make_shared<TelnetAdapter>();
        });
    ProtocolRegistry::instance()->registerFactory("ssh",
        []() -> std::shared_ptr<IProtocolAdapter> {
            return std::make_shared<SshAdapter>();
        });
    ProtocolRegistry::instance()->registerFactory("opcua",
        []() -> std::shared_ptr<IProtocolAdapter> {
            return std::make_shared<OpcUaAdapter>();
        });

    // 加载主题样式表（按 ConfigStore 配置：appearance.theme = "dark"/"light"）
    const QString theme = ConfigStore::instance()
        .load(QStringLiteral("appearance"), QStringLiteral("theme"))
        .value(QStringLiteral("theme")).toString();
    QFile styleFile(themeQssPath(theme));
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        QTextStream stream(&styleFile);
        QString styleSheet = stream.readAll();
        app.setStyleSheet(styleSheet);
        styleFile.close();
    } else {
        qWarning() << "主题样式表加载失败（回退无样式）: " << styleFile.fileName();
    }

    DeviceForge window;
    window.setWindowIcon(QIcon(":/icons/app.ico"));  // 主窗口图标

    // 注册 Tool 到注册表（元数据，用于导航显示）
    ToolRegistry::instance()->registerBuiltin(
        "com.deviceforge.ftp.deploy", "文件部署", "deploy",
        "ftp_deploy", "2.0.0", "通过 FTP 协议批量上传文件/文件夹到目标设备");
    ToolRegistry::instance()->registerBuiltin(
        "com.deviceforge.telnet.command", "批量命令", "command",
        "telnet_command", "2.0.0", "通过 Telnet 协议批量执行 Shell 命令");
    ToolRegistry::instance()->registerBuiltin(
        "com.deviceforge.websocket.comm", "WebSocket 通信", "communication",
        "websocket_comm", "2.0.0", "WebSocket Server/Client 通信，支持订阅/发布主题");

    // 注册 Tool 工厂到 ToolHost（预留：当前 Tool 通过 DeviceForge 直接创建，
    // 待 ToolHost 支持多 Tool 并发后切换为 createTool() 方式）
    window.toolHost()->registerBuiltinFactory("com.deviceforge.ftp.deploy",
        []() -> std::shared_ptr<ToolBackend> {
            return std::make_shared<FtpDeployBackend>();
        },
        [](QWidget* parent) -> ToolWidget* {
            return new FtpDeployWidget(parent);
        });
    window.toolHost()->registerBuiltinFactory("com.deviceforge.telnet.command",
        []() -> std::shared_ptr<ToolBackend> {
            return std::make_shared<TelnetBackend>();
        },
        [](QWidget* parent) -> ToolWidget* {
            return new TelnetWidget(parent);
        });
    window.toolHost()->registerBuiltinFactory("com.deviceforge.websocket.comm",
        []() -> std::shared_ptr<ToolBackend> {
            return std::make_shared<WebSocketBackend>();
        },
        [](QWidget* parent) -> ToolWidget* {
            return new WebSocketWidget(parent);
        });

    // window.initToolTabs(); — 所有 Tool 已在构造函数中按导航栏顺序创建

    window.show();
    const int rc = app.exec();

    // 关闭 ConfigStore（释放 QSql 连接；进程退出前）
    ConfigStore::instance().close();
    return rc;
}
