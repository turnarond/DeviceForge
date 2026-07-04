#include "EventBus.h"
#include <QMetaObject>
#include <QMutexLocker>
#include <algorithm>

void EventBus::subscribe(DeployEvent::Type type, QObject* receiver, const char* slot) {
    // 从 SLOT(methodName(Type)) 签名中提取方法名
    QByteArray slotStr(slot);
    int parenIdx = slotStr.indexOf('(');
    if (parenIdx < 0) return; // 无效的槽签名

    QByteArray methodName = slotStr.left(parenIdx).trimmed();
    // SLOT 宏有时会在方法名前加 '1' 前缀（Qt 内部编码），需要去除
    if (!methodName.isEmpty() && methodName[0] == '1') {
        methodName = methodName.mid(1);
    }

    Subscriber sub;
    sub.receiver = receiver;
    sub.methodName = methodName;

    QMutexLocker locker(&m_mutex);
    if (!m_subscribers[type].contains(sub)) {
        m_subscribers[type].append(sub);
    }
}

void EventBus::unsubscribe(QObject* receiver) {
    QMutexLocker locker(&m_mutex);
    for (auto& list : m_subscribers) {
        list.erase(
            std::remove_if(list.begin(), list.end(),
                [receiver](const Subscriber& s) { return s.receiver == receiver; }),
            list.end());
    }
}

void EventBus::postEvent(const DeployEvent& event) {
    QMutexLocker locker(&m_mutex);
    if (!m_subscribers.contains(event.type)) return;

    // 复制订阅者列表，释放锁后再调用 invokeMethod（避免死锁和长时间的锁持有）
    QList<Subscriber> receivers = m_subscribers[event.type];
    locker.unlock();

    for (const Subscriber& sub : receivers) {
        // Qt::QueuedConnection 确保跨线程安全：事件投递到接收者所在线程的事件循环
        QMetaObject::invokeMethod(sub.receiver, sub.methodName.constData(),
            Qt::QueuedConnection, Q_ARG(DeployEvent, event));
    }
}
