#include "ModbusPresenter.h"
#include "../framework/EventBus.h"
#include "../framework/AppState.h"

ModbusPresenter::ModbusPresenter(QObject* parent) : QObject(parent) {
    // 订阅相关事件
}

ModbusPresenter::~ModbusPresenter() {
    EventBus::instance()->unsubscribe(this);
}

void ModbusPresenter::onEventPosted(const DeployEvent& event) {
    // 处理 Modbus 相关事件
    Q_UNUSED(event);
}
