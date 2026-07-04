#pragma once

#include <QObject>
#include <QMap>
#include <QList>
#include <QMutex>
#include "../utils/DeployEvent.h"

class EventBus : public QObject {
    Q_OBJECT

private:
    struct Subscriber {
        QObject* receiver = nullptr;
        QByteArray methodName;

        bool operator==(const Subscriber& other) const {
            return receiver == other.receiver && methodName == other.methodName;
        }
    };

    EventBus(QObject* parent = nullptr) : QObject(parent) {}
    ~EventBus() = default;

    QMap<DeployEvent::Type, QList<Subscriber>> m_subscribers;
    QMutex m_mutex;

public:
    static EventBus* instance() {
        static EventBus instance;
        return &instance;
    }

    // 订阅指定事件类型；slot 须为接收 DeployEvent 的槽函数（如 SLOT(onEventPosted(const DeployEvent&))）
    void subscribe(DeployEvent::Type type, QObject* receiver, const char* slot);
    void unsubscribe(QObject* receiver);

    // 发布事件到所有订阅了该类型的接收者（线程安全）
    void postEvent(const DeployEvent& event);
};
