/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: MultiProgressWidget.h
 * Date: 2026-07-25
 * Author: turnarond
 *
 * Description: 多设备并行部署进度组件 — 显示每台设备的独立进度条 + 状态，
 *              以及全局总进度条 + 取消按钮。
 */

#pragma once
#include <QWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <vector>

class MultiProgressWidget : public QWidget {
    Q_OBJECT
public:
    explicit MultiProgressWidget(QWidget* parent = nullptr);

    void setDeviceCount(int count);
    void setDeviceProgress(int deviceIndex, int pct);
    void setDeviceStatus(int deviceIndex, const QString& status, bool ok);
    void setOverallProgress(int pct);
    void reset();

signals:
    void cancelRequested();

private:
    void rebuildUi();

    int m_deviceCount = 0;
    QProgressBar* m_overallBar = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QVBoxLayout* m_deviceLayout = nullptr;
    QWidget* m_deviceContainer = nullptr;

    struct DeviceRow {
        QLabel* ipLabel = nullptr;
        QProgressBar* bar = nullptr;
        QLabel* statusLabel = nullptr;
    };
    std::vector<DeviceRow> m_rows;
};
