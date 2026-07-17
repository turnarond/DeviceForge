#pragma once

#include <QWidget>
#include <QMap>
#include <QTimer>
#include <QTextEdit>
#include <QStringList>

namespace Ui { class TabOpcUaClient; }
class DeployMaster;

/**
 * @brief OPC UA 客户端 Tab（演示模式）
 * 
 * 注意：此类当前为演示模式实现，没有实际的 OPC UA 连接功能。
 * 所有操作（连接、浏览、读写、订阅）都使用硬编码的演示数据。
 * 
 * 要实现真正的 OPC UA 功能，需要：
 * 1. 引入 OPC UA SDK（如 open62541 或 Qt OPC UA 模块）
 * 2. 实现实际的连接、浏览、读写、订阅逻辑
 * 3. 处理 OPC UA 特有的安全模型（证书、签名等）
 * 
 * 当前保留此演示模式用于：
 * - UI 原型展示
 * - 界面交互测试
 * - 未来 OPC UA 功能的界面模板
 */
class OpcUaClientTab : public QWidget {
    Q_OBJECT
public:
    explicit OpcUaClientTab(DeployMaster* parentWindow, QWidget* parent = nullptr);
    ~OpcUaClientTab();

    void setEndpointList(const QStringList& endpoints);

private slots:
    void on_btn_connect_clicked();
    void on_btn_browse_clicked();
    void on_btn_readValue_clicked();
    void on_btn_writeValue_clicked();
    void on_btn_subscribe_clicked();

    void appendLog(const QString& log);

private:
    Ui::TabOpcUaClient* ui;
    DeployMaster* m_mainWindow;
    QTextEdit* m_globalLog = nullptr;

    QStringList m_endpoints;

    // 注意：以下成员变量为演示模式使用，无实际 OPC UA 连接
    bool m_connected = false;
};
