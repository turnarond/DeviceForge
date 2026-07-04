#include "DeployMaster.h"
#include <QtWidgets/QApplication>
#include <QFile>
#include <QTextStream>
#include "src/logging/LogBridge.h"
#include "src/adapter/ProtocolRegistry.h"
#include "src/adapter/FtpAdapter.h"
#include "src/adapter/TelnetAdapter.h"
#include "src/framework/ToolHost.h"
#include "src/framework/ToolRegistry.h"
#include "src/tools/FtpDeployTool/FtpDeployBackend.h"
#include "src/tools/FtpDeployTool/FtpDeployWidget.h"
#include "src/tools/TelnetTool/TelnetBackend.h"
#include "src/tools/TelnetTool/TelnetWidget.h"
// #include "src/presenter/FtpPresenter.h" — 临时禁用，待 Task 13

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    LogBridge::install();  // Qt -> lwlog 日志桥接

    // 注册内置协议适配器
    ProtocolRegistry::instance()->registerFactory("ftp",
        []() -> std::shared_ptr<IProtocolAdapter> {
            return std::make_shared<FtpAdapter>();
        });
    ProtocolRegistry::instance()->registerFactory("telnet",
        []() -> std::shared_ptr<IProtocolAdapter> {
            return std::make_shared<TelnetAdapter>();
        });

    // 加载深色主题样式表
    QFile styleFile(":/styles/darkstyle.qss");
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        QTextStream stream(&styleFile);
        QString styleSheet = stream.readAll();
        app.setStyleSheet(styleSheet);
        styleFile.close();
    }

    DeployMaster window;

    // 注册 Tool 到注册表（元数据，用于导航显示）
    ToolRegistry::instance()->registerBuiltin(
        "com.deploymaster.ftp.deploy", "文件部署", "deploy",
        "ftp_deploy", "2.0.0", "通过 FTP 协议批量上传文件/文件夹到目标设备");
    ToolRegistry::instance()->registerBuiltin(
        "com.deploymaster.telnet.command", "批量命令", "command",
        "telnet_command", "2.0.0", "通过 Telnet 协议批量执行 Shell 命令");

    // 注册 Tool 工厂到 ToolHost（创建 Backend + Widget 实例）
    // ToolHost 由 DeployMaster 内部创建和管理
    window.toolHost()->registerBuiltinFactory("com.deploymaster.ftp.deploy",
        []() -> std::shared_ptr<ToolBackend> {
            return std::make_shared<FtpDeployBackend>();
        },
        [](QWidget* parent) -> ToolWidget* {
            return new FtpDeployWidget(parent);
        });
    window.toolHost()->registerBuiltinFactory("com.deploymaster.telnet.command",
        []() -> std::shared_ptr<ToolBackend> {
            return std::make_shared<TelnetBackend>();
        },
        [](QWidget* parent) -> ToolWidget* {
            return new TelnetWidget(parent);
        });

    // 创建 FtpPresenter 实例（订阅 EventBus 事件，连接部署管道）
    // 临时禁用: FtpPresenter.cpp 存在 QPointer 类型兼容性问题，待 Task 13 重构
    // auto* ftpPresenter = new FtpPresenter(&window);
    // Q_UNUSED(ftpPresenter); // 生命周期由 parent 管理

    window.show();
    return app.exec();
}
