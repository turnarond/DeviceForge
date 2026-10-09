#pragma once
#include "framework/ToolWidget.h"
#include "WsEventTypes.h"
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTimer>

class QButtonGroup;
class QListWidget;
class QStackedWidget;
class QTableWidget;
class WebSocketBackend;

// v2.12 页面重构：左控制台（模式/配置/状态/操作/发布订阅）+ 右消息事件流。
// QSplitter 横切、右栏为唯一垂直扩展主体，根治旧版线性表单堆叠在拉伸后散架的问题。
// 全局日志仍只记方向+字节数；正文预览仅进本页面事件流（非日志）。
class WebSocketWidget : public ToolWidget {
    Q_OBJECT

public:
    explicit WebSocketWidget(QWidget* parent = nullptr);
    ~WebSocketWidget() override = default;

    // --- ToolWidget 实现 ---
    QString toolId() const override { return "com.deviceforge.websocket.comm"; }
    QString toolName() const override { return "WebSocket 通信"; }
    void onToolStart() override;
    void onToolStop() override;

    // 绑定后端（由 DeviceForge setupXxxTab 调用）
    void setBackend(WebSocketBackend* backend);

    // 事件注入点（后端回调与本页面之外的自动化共用；上限 kMaxEventRows FIFO）
    void pushEvent(const wsproto::WsEvent& event);

    static constexpr int kMaxEventRows = 2000;

private slots:
    void onStartClicked();
    void onStopClicked();
    void onServerModeToggled();
    void onClientModeToggled();
    void onSubscribeClicked();
    void onUnsubscribeClicked();
    void onPublishClicked();
    void onClearEventsClicked();
    void onTokenVisibilityToggled(bool show);
    void onRefreshStatusTick();

private:
    void setupUi();
    void restoreLastEndpoint();
    void sweepLegacyTokenRows();
    void appendLog(const QString& msg);
    void applyRunningUi(bool running);
    void refreshStatusCard();

    WebSocketBackend* m_backend = nullptr;

    // 左控制台
    QPushButton*     m_btnModeServer   = nullptr;
    QPushButton*     m_btnModeClient   = nullptr;
    QButtonGroup*    m_modeGroup       = nullptr;
    QStackedWidget*  m_configStack     = nullptr;
    QLineEdit*       m_editBindAddr    = nullptr;
    QSpinBox*        m_spinPort        = nullptr;
    QCheckBox*       m_chkWss          = nullptr;
    QLineEdit*       m_editToken       = nullptr;
    QPushButton*     m_btnTokenEye     = nullptr;
    QLineEdit*       m_editUrl         = nullptr;
    QCheckBox*       m_chkTrustCert    = nullptr;
    QLabel*          m_statusDot       = nullptr;
    QLabel*          m_statusText      = nullptr;
    QLabel*          m_targetText      = nullptr;
    QListWidget*     m_clientList      = nullptr;
    QListWidget*     m_topicList       = nullptr;
    QPushButton*     m_btnStart        = nullptr;
    QPushButton*     m_btnStop         = nullptr;
    QLineEdit*       m_editTopic       = nullptr;
    QLineEdit*       m_editMessage     = nullptr;
    QPushButton*     m_btnSubscribe    = nullptr;
    QPushButton*     m_btnUnsubscribe  = nullptr;
    QPushButton*     m_btnPublish      = nullptr;

    // 右事件流
    QTableWidget*    m_eventTable      = nullptr;
    QCheckBox*       m_chkAutoScroll   = nullptr;
    QPushButton*     m_btnClearEvents  = nullptr;
    QLabel*          m_droppedLabel    = nullptr;

    QTimer           m_statusTimer;
    int              m_droppedCount    = 0;
};
