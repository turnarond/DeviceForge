// src/logging/LogBridge.h
#pragma once

namespace LogBridge {
    // 安装 Qt -> lwlog 桥接。调用后 qDebug/qWarning/qCritical 的输出
    // 将通过 lwlog 管道分发，同时保留原有控制台输出。
    // 应在 QApplication 创建之后、任何 Qt 日志产生之前调用。
    void install();

    // 卸载桥接，恢复默认 Qt 消息处理
    void uninstall();
}
