#pragma once

#include <QObject>
#include "../utils/DeployEvent.h"

class ModbusPresenter : public QObject {
    Q_OBJECT

public:
    explicit ModbusPresenter(QObject* parent = nullptr);
    ~ModbusPresenter();

private slots:
    void onEventPosted(const DeployEvent& event);
};
