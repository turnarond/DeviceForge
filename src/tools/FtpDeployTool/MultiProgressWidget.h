/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: MultiProgressWidget.h
 * Date: 2026-07-25
 * Author: turnarond
 *
 * Description: 多设备并行部署进度组件 — 顶部总进度条 + 紧凑设备状态标签行
 *              （每个设备一个胶囊标签，水平排列，超出后滚动）
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
    void setDeviceInfo(int deviceIndex, const QString& info);
    void setDeviceProgress(int deviceIndex, int pct);
    void setDeviceStatus(int deviceIndex, const QString& status, bool ok);
    void setDeviceStatusByKey(const QString& key, bool ok);
    void setOverallProgress(int pct);
    void setFinishedSummary(int done, int total);
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
        QLabel* statusLabel = nullptr;
        QString deviceKey;
    };
    std::vector<DeviceRow> m_rows;
};
