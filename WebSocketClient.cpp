#include "WebSocketClient.h"
#include "ui_tab_websocket.h"
#include "DeployMaster.h"

#include <QMessageBox>
#include <QDateTime>
#include <QMetaObject>

WebSocketClient::WebSocketClient(DeployMaster* parentWindow, QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::TabWebSocket)
    , m_mainWindow(parentWindow)
    , m_server(nullptr)
    , m_client(nullptr)
    , m_isRunning(false)
    , m_isServerMode(true)
{
    ui->setupUi(this);

    // 连接信号槽
    connect(ui->btn_start, &QPushButton::clicked, this, &WebSocketClient::on_btn_start_clicked);
    connect(ui->btn_stop, &QPushButton::clicked, this, &WebSocketClient::on_btn_stop_clicked);
    connect(ui->radio_server, &QRadioButton::toggled, this, &WebSocketClient::on_radio_server_toggled);
    connect(ui->radio_client, &QRadioButton::toggled, this, &WebSocketClient::on_radio_client_toggled);
    connect(ui->btn_subscribe, &QPushButton::clicked, this, &WebSocketClient::on_btn_subscribe_clicked);
    connect(ui->btn_publish, &QPushButton::clicked, this, &WebSocketClient::on_btn_publish_clicked);
    connect(ui->btn_clearLog, &QPushButton::clicked, this, &WebSocketClient::on_btn_clearLog_clicked);

    // 初始状态
    ui->btn_stop->setEnabled(false);

    // 初始模式
    on_radio_server_toggled(true);
}

WebSocketClient::~WebSocketClient()
{
    if (m_isRunning) {
        if (m_isServerMode) {
            stopServer();
        } else {
            stopClient();
        }
    }
    delete ui;
}

void WebSocketClient::on_radio_server_toggled(bool checked)
{
    if (checked) {
        m_isServerMode = true;
        ui->groupBox_server->setEnabled(true);
        ui->groupBox_client->setEnabled(false);
    }
}

void WebSocketClient::on_radio_client_toggled(bool checked)
{
    if (checked) {
        m_isServerMode = false;
        ui->groupBox_server->setEnabled(false);
        ui->groupBox_client->setEnabled(true);
    }
}

void WebSocketClient::on_btn_start_clicked()
{
    if (m_isRunning) {
        QMessageBox::warning(this, "警告", "WebSocket 服务已经在运行中");
        return;
    }

    // 先设置按钮状态为禁用，避免重复点击
    ui->btn_start->setEnabled(false);
    ui->btn_stop->setEnabled(true);
    ui->radio_server->setEnabled(false);
    ui->radio_client->setEnabled(false);

    if (m_isServerMode) {
        startServer();
    } else {
        startClient();
    }

    // 注意：m_isRunning的设置在startServer和startClient中处理
}

void WebSocketClient::on_btn_stop_clicked()
{
    if (!m_isRunning) {
        QMessageBox::warning(this, "警告", "WebSocket 服务未运行");
        return;
    }

    if (m_isServerMode) {
        stopServer();
    } else {
        stopClient();
    }

    m_isRunning = false;
    ui->btn_start->setEnabled(true);
    ui->btn_stop->setEnabled(false);
    ui->radio_server->setEnabled(true);
    ui->radio_client->setEnabled(true);
}

void WebSocketClient::startServer()
{
    int port = ui->spin_serverPort->value();
    bool secureMode = ui->chk_secureMode->isChecked();
    
    QWebSocketServer::SslMode sslMode = secureMode ? QWebSocketServer::SecureMode : QWebSocketServer::NonSecureMode;
    m_server = new QWebSocketServer("DeployMaster WebSocket Server", sslMode, this);

    if (secureMode) {
        // 配置SSL证书
        QSslConfiguration sslConfig;
        // 注意：在实际使用中，需要设置正确的SSL证书和私钥
        // 这里仅作为示例，实际部署时需要替换为真实的证书
        m_server->setSslConfiguration(sslConfig);
    }

    if (!m_server->listen(QHostAddress::Any, port)) {
        appendMessage(QString("[Server] 启动失败: %1").arg(m_server->errorString()), true);
        delete m_server;
        m_server = nullptr;
        m_isRunning = false;
        ui->btn_start->setEnabled(true);
        ui->btn_stop->setEnabled(false);
        ui->radio_server->setEnabled(true);
        ui->radio_client->setEnabled(true);
        return;
    }

    connect(m_server, &QWebSocketServer::newConnection, this, &WebSocketClient::onNewConnection);
    m_isRunning = true;
    QString mode = secureMode ? "WSS" : "WS";
    appendMessage(QString("[Server] 启动成功，%1 模式，监听端口: %2").arg(mode).arg(port), true);
}

void WebSocketClient::stopServer()
{
    if (m_server) {
        // 关闭所有客户端连接
        m_clientsMutex.lock();
        for (QWebSocket* client : m_clients) {
            client->close();
            delete client;
        }
        m_clients.clear();
        m_clientsMutex.unlock();

        // 关闭服务器
        m_server->close();
        delete m_server;
        m_server = nullptr;
        appendMessage("[Server] 已停止", true);
    }
}

void WebSocketClient::startClient()
{
    QString serverUrl = ui->lineEdit_serverUrl->text().trimmed();
    if (serverUrl.isEmpty()) {
        QMessageBox::warning(this, "警告", "请输入服务器 URL");
        m_isRunning = false;
        ui->btn_start->setEnabled(true);
        ui->btn_stop->setEnabled(false);
        ui->radio_server->setEnabled(true);
        ui->radio_client->setEnabled(true);
        return;
    }

    m_client = new QWebSocket();
    connect(m_client, &QWebSocket::connected, this, &WebSocketClient::onClientConnected);
    connect(m_client, &QWebSocket::disconnected, this, &WebSocketClient::onClientDisconnected);
    connect(m_client, &QWebSocket::textMessageReceived, this, &WebSocketClient::onClientTextMessageReceived);
    connect(m_client, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::errorOccurred), this, &WebSocketClient::onClientError);

    // 支持安全模式（WSS）
    if (serverUrl.startsWith("wss://")) {
        // 配置SSL设置
        QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
        // 注意：在实际使用中，可能需要设置自定义的SSL证书验证
        m_client->setSslConfiguration(sslConfig);
        appendMessage(QString("[Client] 正在以 WSS 模式连接到: %1").arg(serverUrl), false);
    } else {
        appendMessage(QString("[Client] 正在以 WS 模式连接到: %1").arg(serverUrl), false);
    }

    m_client->open(QUrl(serverUrl));
}

void WebSocketClient::stopClient()
{
    if (m_client) {
        m_client->close();
        delete m_client;
        m_client = nullptr;
        appendMessage("[Client] 已断开连接", true);
    }
}

void WebSocketClient::onNewConnection()
{
    QWebSocket* client = m_server->nextPendingConnection();
    connect(client, &QWebSocket::textMessageReceived, this, &WebSocketClient::onServerTextMessageReceived);
    connect(client, &QWebSocket::disconnected, this, &WebSocketClient::onServerDisconnected);

    m_clientsMutex.lock();
    m_clients.append(client);
    m_clientsMutex.unlock();

    appendMessage(QString("[Server] 新客户端连接: %1").arg(client->peerAddress().toString()), false);
}

void WebSocketClient::onServerTextMessageReceived(const QString& message)
{
    QWebSocket* client = qobject_cast<QWebSocket*>(sender());
    if (client) {
        if (message.startsWith("SUBSCRIBE:")) {
            // 处理订阅请求
            QString topic = message.mid(10); // 移除 "SUBSCRIBE:" 前缀
            subscribeToTopic(topic);
        } else if (message.startsWith("PUBLISH:")) {
            // 处理发布请求
            QStringList parts = message.split(":");
            if (parts.size() >= 3) {
                QString topic = parts[1];
                QString publishMessage = parts.mid(2).join(":");
                sendMessageToSubscribers(topic, publishMessage);
                appendMessage(QString("[Server] 客户端 %1 发布消息到主题 %2: %3").arg(client->peerAddress().toString()).arg(topic).arg(publishMessage), false);
            }
        } else {
            appendMessage(QString("[Server] 收到来自 %1 的消息: %2").arg(client->peerAddress().toString()).arg(message), false);
        }
    }
}

void WebSocketClient::onServerDisconnected()
{
    QWebSocket* client = qobject_cast<QWebSocket*>(sender());
    if (client) {
        m_clientsMutex.lock();
        m_clients.removeAll(client);
        m_clientsMutex.unlock();

        // 从所有主题的订阅列表中移除该客户端
        m_topicMutex.lock();
        for (auto it = m_topicSubscribers.begin(); it != m_topicSubscribers.end(); ++it) {
            it.value().removeAll(client);
        }
        m_topicMutex.unlock();

        appendMessage(QString("[Server] 客户端断开连接: %1").arg(client->peerAddress().toString()), false);
        client->deleteLater();
    }
}

void WebSocketClient::onClientConnected()
{
    m_isRunning = true;
    appendMessage("[Client] 连接成功", true);
}

void WebSocketClient::onClientTextMessageReceived(const QString& message)
{
    if (message.startsWith("TOPIC:")) {
        // 处理主题消息
        QStringList parts = message.split(":");
        if (parts.size() >= 3) {
            QString topic = parts[1];
            QString topicMessage = parts.mid(2).join(":");
            appendMessage(QString("[Client] 收到主题 %1 的消息: %2").arg(topic).arg(topicMessage), false);
        }
    } else {
        appendMessage(QString("[Client] 收到消息: %1").arg(message), false);
    }
}

void WebSocketClient::onClientDisconnected()
{
    appendMessage("[Client] 连接断开", true);
    // 客户端断开连接，恢复按钮状态
    m_isRunning = false;
    ui->btn_start->setEnabled(true);
    ui->btn_stop->setEnabled(false);
    ui->radio_server->setEnabled(true);
    ui->radio_client->setEnabled(true);
}

void WebSocketClient::onClientError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    if (m_client) {
        appendMessage(QString("[Client] 错误: %1").arg(m_client->errorString()), true);
    }
    // 连接失败，恢复按钮状态
    m_isRunning = false;
    ui->btn_start->setEnabled(true);
    ui->btn_stop->setEnabled(false);
    ui->radio_server->setEnabled(true);
    ui->radio_client->setEnabled(true);
}

void WebSocketClient::on_btn_subscribe_clicked()
{
    QString topic = ui->lineEdit_topic->text().trimmed();
    if (topic.isEmpty()) {
        QMessageBox::warning(this, "警告", "请输入主题 URL");
        return;
    }

    if (!m_isRunning) {
        QMessageBox::warning(this, "警告", "请先启动 WebSocket 服务");
        return;
    }

    if (m_isServerMode) {
        QMessageBox::warning(this, "警告", "Server 模式不支持订阅操作");
        return;
    }

    if (m_client && m_client->state() == QAbstractSocket::ConnectedState) {
        // 向服务器发送订阅请求
        QString subscribeMessage = QString("SUBSCRIBE:%1").arg(topic);
        m_client->sendTextMessage(subscribeMessage);
        m_subscribedTopics.append(topic);
        appendMessage(QString("[Client] 订阅主题: %1").arg(topic), false);
    } else {
        QMessageBox::warning(this, "警告", "客户端未连接");
    }
}

void WebSocketClient::on_btn_publish_clicked()
{
    QString topic = ui->lineEdit_topic->text().trimmed();
    QString message = ui->lineEdit_pubMessage->text().trimmed();
    
    if (topic.isEmpty()) {
        QMessageBox::warning(this, "警告", "请输入主题 URL");
        return;
    }
    
    if (message.isEmpty()) {
        QMessageBox::warning(this, "警告", "请输入要发布的消息");
        return;
    }

    if (!m_isRunning) {
        QMessageBox::warning(this, "警告", "请先启动 WebSocket 服务");
        return;
    }

    publishMessage(topic, message);
}

void WebSocketClient::on_btn_clearLog_clicked()
{
    ui->txt_messages->clear();
    appendMessage("[System] 日志已清理", false);
}

void WebSocketClient::sendMessageToAllClients(const QString& message)
{
    m_clientsMutex.lock();
    for (QWebSocket* client : m_clients) {
        client->sendTextMessage(message);
    }
    m_clientsMutex.unlock();
}

void WebSocketClient::publishMessage(const QString& topic, const QString& message)
{
    if (m_isServerMode) {
        // Server 模式：直接向订阅该主题的客户端发送消息
        sendMessageToSubscribers(topic, message);
        appendMessage(QString("[Server] 发布消息到主题 %1: %2").arg(topic).arg(message), false);
    } else {
        // Client 模式：向服务器发送发布请求
        if (m_client && m_client->state() == QAbstractSocket::ConnectedState) {
            QString publishMessage = QString("PUBLISH:%1:%2").arg(topic).arg(message);
            m_client->sendTextMessage(publishMessage);
            appendMessage(QString("[Client] 发布消息到主题 %1: %2").arg(topic).arg(message), false);
        } else {
            QMessageBox::warning(this, "警告", "客户端未连接");
        }
    }
}

void WebSocketClient::subscribeToTopic(const QString& topic)
{
    QWebSocket* client = qobject_cast<QWebSocket*>(sender());
    if (!client) return;

    m_topicMutex.lock();
    if (!m_topicSubscribers.contains(topic)) {
        m_topicSubscribers[topic] = QList<QWebSocket*>();
    }
    if (!m_topicSubscribers[topic].contains(client)) {
        m_topicSubscribers[topic].append(client);
        appendMessage(QString("[Server] 客户端 %1 订阅主题 %2").arg(client->peerAddress().toString()).arg(topic));
    }
    m_topicMutex.unlock();
}

void WebSocketClient::sendMessageToSubscribers(const QString& topic, const QString& message)
{
    m_topicMutex.lock();
    if (m_topicSubscribers.contains(topic)) {
        QList<QWebSocket*> subscribers = m_topicSubscribers[topic];
        for (QWebSocket* client : subscribers) {
            QString formattedMessage = QString("TOPIC:%1:%2").arg(topic).arg(message);
            client->sendTextMessage(formattedMessage);
        }
    }
    m_topicMutex.unlock();
}

void WebSocketClient::appendMessage(const QString& message, bool isCriticalLog)
{
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    QString logMessage = QString("[%1] %2").arg(timestamp).arg(message);
    ui->txt_messages->append(logMessage);

    // 只将关键日志输出到全局日志
    if (isCriticalLog && m_mainWindow) {
        QTextEdit* globalLog = m_mainWindow->getGlobalLogItem();
        if (globalLog) {
            QMetaObject::invokeMethod(globalLog, "append", Qt::QueuedConnection, Q_ARG(QString, logMessage));
        }
    }
}