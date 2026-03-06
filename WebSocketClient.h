#pragma once

#include <QWidget>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QTextEdit>
#include <QStringList>
#include <QMutex>

namespace Ui {
    class TabWebSocket;
}

class DeployMaster; // 前向声明主窗口

class WebSocketClient : public QWidget
{
    Q_OBJECT

public:
    explicit WebSocketClient(DeployMaster* parentWindow, QWidget* parent = nullptr);
    ~WebSocketClient();

private slots:
    void on_btn_start_clicked();
    void on_btn_stop_clicked();
    void on_radio_server_toggled(bool checked);
    void on_radio_client_toggled(bool checked);
    void on_btn_subscribe_clicked();
    void on_btn_publish_clicked();
    void on_btn_clearLog_clicked();

    // Server 模式相关槽函数
    void onNewConnection();
    void onServerTextMessageReceived(const QString& message);
    void onServerDisconnected();

    // Client 模式相关槽函数
    void onClientConnected();
    void onClientTextMessageReceived(const QString& message);
    void onClientDisconnected();
    void onClientError(QAbstractSocket::SocketError error);

private:
    void appendMessage(const QString& message, bool isCriticalLog = false);
    void startServer();
    void stopServer();
    void startClient();
    void stopClient();
    void sendMessageToAllClients(const QString& message);
    void publishMessage(const QString& topic, const QString& message);
    void subscribeToTopic(const QString& topic);
    void sendMessageToSubscribers(const QString& topic, const QString& message);

private:
    Ui::TabWebSocket* ui;
    DeployMaster* m_mainWindow;

    // Server 模式
    QWebSocketServer* m_server;
    QList<QWebSocket*> m_clients;
    QMutex m_clientsMutex;
    // 订阅映射: 主题 -> 订阅该主题的客户端列表
    QMap<QString, QList<QWebSocket*>> m_topicSubscribers;
    QMutex m_topicMutex;

    // Client 模式
    QWebSocket* m_client;
    // 客户端订阅的主题列表
    QStringList m_subscribedTopics;

    // 状态
    bool m_isRunning = false;
    bool m_isServerMode = true;
};