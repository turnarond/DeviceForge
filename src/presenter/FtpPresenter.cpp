#include "FtpPresenter.h"
#include "../model/FtpManager.h"
#include "../framework/EventBus.h"
#include "../framework/AppState.h"
#include <QtConcurrent/QtConcurrent>
#include <QPointer>

FtpPresenter::FtpPresenter(QObject* parent) : QObject(parent) {
    m_manager = new FtpManager();
    EventBus::instance()->subscribe(DeployEvent::UploadRequest, this, SLOT(onEventPosted(const DeployEvent&)));
}

FtpPresenter::~FtpPresenter() {
    EventBus::instance()->unsubscribe(this);
    delete m_manager;
    m_manager = nullptr;
}

void FtpPresenter::onEventPosted(const DeployEvent& event) {
    if (event.type != DeployEvent::UploadRequest) return;

    AppState::instance()->setIsBusy(true);
    AppState::instance()->setCurrentTask("FTP 上传");
    AppState::instance()->setTaskProgress(0);

    QVariantMap data = event.data.toMap();
    QStringList items = data["items"].toStringList();
    QStringList targets = data["targets"].toStringList();
    QString user = data["user"].toString();
    QString pass = data["pass"].toString();
    QString remotePath = data["remotePath"].toString();
    bool shouldReboot = data.value("shouldReboot", false).toBool();
    bool shouldClearRemote = data.value("shouldClearRemote", false).toBool();

    m_manager->setCredentials(user, pass);

    // 异步执行上传（含可选清空和重启逻辑）
    // 使用 QPointer 防止异步任务执行期间对象被销毁后的悬垂指针访问
    QPointer<FtpManager> managerGuard(m_manager);
    QPointer<FtpPresenter> selfGuard(this);

    QtConcurrent::run([=]() {
        if (!managerGuard || !selfGuard) return;

        if (shouldClearRemote) {
            for (const QString& target : targets) {
                QString host = target.contains(':') ? target.split(':').first() : target;
                try {
                    FtpManager cleaner(host, 21);
                    cleaner.setCredentials(user, pass);
                    cleaner.clearRemoteDirectory(remotePath);
                    EventBus::instance()->postEvent(
                        DeployEvent(DeployEvent::LogMessage,
                            QString("🧹 已清空 %1:%2").arg(host).arg(remotePath)));
                } catch (const std::exception& ex) {
                    EventBus::instance()->postEvent(
                        DeployEvent(DeployEvent::LogMessage,
                            QString("❌ 清空失败 %1: %2").arg(host, ex.what())));
                }
            }
        }

        if (!managerGuard || !selfGuard) return;

        // 执行上传
        managerGuard->uploadFiles(items, targets, remotePath,
            [selfGuard](int progress) {
                if (!selfGuard) return;
                QMetaObject::invokeMethod(selfGuard, [selfGuard, progress]() {
                    if (!selfGuard) return;
                    selfGuard->onUploadProgress(progress);
                }, Qt::QueuedConnection);
            },
            [selfGuard, shouldReboot, user, pass](bool success, const QStringList& successes, const QStringList& failures) {
                if (!selfGuard) return;
                QMetaObject::invokeMethod(selfGuard, [selfGuard, success, successes, failures, shouldReboot, user, pass]() {
                    if (!selfGuard) return;
                    selfGuard->onUploadFinished(success, successes, failures);

                    // 部署后重启（通过 Telnet 发送 reboot -f 命令）
                    // TODO: 迁移到 TelnetPresenter 后统一管理重启逻辑
                    if (shouldReboot && !successes.isEmpty()) {
                        QString logMsg = QString("⚠️ 部署后重启功能待 TelnetPresenter 迁移后实现, 需重启设备: %1")
                            .arg(successes.join(", "));
                        EventBus::instance()->postEvent(
                            DeployEvent(DeployEvent::LogMessage, logMsg));
                    }
                }, Qt::QueuedConnection);
            });
    });
}

void FtpPresenter::onUploadProgress(int progress) {
    AppState::instance()->setTaskProgress(progress);
    EventBus::instance()->postEvent(DeployEvent(DeployEvent::TaskProgress, progress));
}

void FtpPresenter::onUploadFinished(bool success, const QStringList& successes, const QStringList& failures) {
    AppState::instance()->setIsBusy(false);
    AppState::instance()->setTaskProgress(100);

    QVariantMap result;
    result["success"] = success;
    result["successes"] = successes;
    result["failures"] = failures;

    EventBus::instance()->postEvent(DeployEvent(DeployEvent::TaskFinished, result));

    QString log = QString("FTP上传完成: 成功%1个, 失败%2个").arg(successes.size()).arg(failures.size());
    EventBus::instance()->postEvent(DeployEvent(DeployEvent::LogMessage, log));
}
