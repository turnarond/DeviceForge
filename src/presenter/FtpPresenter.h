#pragma once

#include <QObject>
#include "../utils/DeployEvent.h"

class FtpManager;

class FtpPresenter : public QObject {
    Q_OBJECT

public:
    explicit FtpPresenter(QObject* parent = nullptr);
    ~FtpPresenter();

private slots:
    void onEventPosted(const DeployEvent& event);
    void onUploadProgress(int progress);
    void onUploadFinished(bool success, const QStringList& successes, const QStringList& failures);

private:
    FtpManager* m_manager;
};
