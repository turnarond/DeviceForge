#pragma once

#include <QVariant>
#include <QString>
#include <QMetaType>

class DeployEvent {
public:
    enum Type {
        ConnectionStatusChanged,  // 连接状态变化
        TaskStarted,              // 任务开始
        TaskProgress,             // 任务进度
        TaskFinished,             // 任务完成
        ErrorOccurred,            // 错误发生
        LogMessage,               // 日志消息
        UploadRequest,            // 上传请求
        CommandExecute            // 命令执行请求
    };

    DeployEvent(Type type = TaskFinished, const QVariant& data = QVariant(), const QString& senderId = "")
        : type(type), data(data), senderId(senderId) {}

    Type type;
    QVariant data;
    QString senderId;
};

Q_DECLARE_METATYPE(DeployEvent)
Q_DECLARE_METATYPE(DeployEvent::Type)
