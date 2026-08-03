/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: MultiProgressWidget.h
 * Date: 2026-07-25
 * Author: turnarond
 *
 * Description: 多设备并行部署进度组件 — 单行总进度条 + 取消按钮。
 *              部署结果通过日志展示，不显示每设备进度。
 */

#pragma once
#include <QWidget>
#include <QProgressBar>
#include <QPushButton>

class MultiProgressWidget : public QWidget {
    Q_OBJECT
public:
    explicit MultiProgressWidget(QWidget* parent = nullptr);

    void setDeviceCount(int count);
    void setDeviceInfo(int, const QString&) {}
    void setDeviceProgress(int, int) {}
    void setDeviceStatus(int, const QString&, bool) {}
    void setDeviceStatusByKey(const QString&, bool) {}
    void setOverallProgress(int pct);
    void setFinishedSummary(int done, int total);
    void reset();

signals:
    void cancelRequested();

private:
    void rebuildUi() {}

    int m_deviceCount = 0;
    QProgressBar* m_overallBar = nullptr;
    QPushButton* m_cancelBtn = nullptr;
};
