// src/logging/LogBridge.cpp
#include "LogBridge.h"
#include <QtCore/QtGlobal>
#include <QtCore/QString>
#include <QDebug>
#include "lwlog.h"

namespace {

    void qtMessageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
        std::string str = msg.toStdString();

        switch (type) {
        case QtDebugMsg:
            LWLOG_D(str.c_str());
            break;
        case QtWarningMsg:
            LWLOG_W(str.c_str());
            break;
        case QtCriticalMsg:
        case QtFatalMsg:
            LWLOG_E(str.c_str());
            break;
        case QtInfoMsg:
            LWLOG_I(str.c_str());
            break;
        }

        // 同时输出到控制台（调试用）
        fprintf(stderr, "[Qt] %s\n", str.c_str());
        if (type == QtFatalMsg) {
            abort();
        }
    }

} // anonymous namespace

void LogBridge::install() {
    qInstallMessageHandler(qtMessageHandler);
    LWLOG_I("LogBridge installed - qDebug/qWarning/qCritical -> lwlog");
}

void LogBridge::uninstall() {
    qInstallMessageHandler(nullptr);
    LWLOG_I("LogBridge uninstalled");
}
