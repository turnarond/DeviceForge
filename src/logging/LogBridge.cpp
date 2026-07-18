// src/logging/LogBridge.cpp
#include "LogBridge.h"
#include <QtCore/QtGlobal>
#include <QtCore/QString>
#include <QDebug>
#include "lwlog.h"

namespace {

    void qtMessageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
        // 用 toLocal8Bit() 兼容预编译 lwlog（无 /utf-8，本地代码页内部处理）
        std::string str = msg.toLocal8Bit().toStdString();

        switch (type) {
        case QtDebugMsg:
            LWLOG_D(str);
            break;
        case QtWarningMsg:
            LWLOG_W(str);
            break;
        case QtCriticalMsg:
        case QtFatalMsg:
            LWLOG_E(str);
            break;
        case QtInfoMsg:
            LWLOG_I(str);
            break;
        }

        // stderr 输出用 UTF-8（与 /utf-8 保持一致）
        QByteArray utf8 = msg.toUtf8();
        fprintf(stderr, "[Qt] %s\n", utf8.constData());
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
