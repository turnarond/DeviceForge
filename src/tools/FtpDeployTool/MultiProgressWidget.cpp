/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: MultiProgressWidget.cpp
 * Date: 2026-07-25
 * Author: turnarond
 *
 * Description: MultiProgressWidget 实现。
 */

#include "MultiProgressWidget.h"
#include <QHBoxLayout>

MultiProgressWidget::MultiProgressWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 4, 0, 4);
    mainLayout->setSpacing(4);

    // 全局进度条
    auto* overallRow = new QHBoxLayout();
    m_overallBar = new QProgressBar(this);
    m_overallBar->setRange(0, 100);
    m_overallBar->setValue(0);
    m_overallBar->setTextVisible(true);
    m_overallBar->setFormat("总进度 %p%");
    m_overallBar->setMaximumHeight(24);
    overallRow->addWidget(m_overallBar, 1);

    m_cancelBtn = new QPushButton("取消", this);
    m_cancelBtn->setMaximumWidth(60);
    connect(m_cancelBtn, &QPushButton::clicked, this, &MultiProgressWidget::cancelRequested);
    overallRow->addWidget(m_cancelBtn);

    mainLayout->addLayout(overallRow);

    // 每设备进度行容器
    m_deviceContainer = new QWidget(this);
    m_deviceLayout = new QVBoxLayout(m_deviceContainer);
    m_deviceLayout->setContentsMargins(0, 0, 0, 0);
    m_deviceLayout->setSpacing(2);
    mainLayout->addWidget(m_deviceContainer);

    setVisible(false);
}

void MultiProgressWidget::rebuildUi()
{
    // 清除旧行
    for (auto& row : m_rows) {
        m_deviceLayout->removeWidget(row.ipLabel);
        m_deviceLayout->removeWidget(row.bar);
        m_deviceLayout->removeWidget(row.statusLabel);
        delete row.ipLabel;
        delete row.bar;
        delete row.statusLabel;
    }
    m_rows.clear();

    // 构建新行
    for (int i = 0; i < m_deviceCount; ++i) {
        DeviceRow row;
        auto* rowLayout = new QHBoxLayout();

        row.ipLabel = new QLabel("—", this);
        row.ipLabel->setMinimumWidth(140);
        rowLayout->addWidget(row.ipLabel);

        row.bar = new QProgressBar(this);
        row.bar->setRange(0, 100);
        row.bar->setValue(0);
        row.bar->setTextVisible(false);
        row.bar->setMaximumHeight(16);
        rowLayout->addWidget(row.bar, 1);

        row.statusLabel = new QLabel("等待中", this);
        row.statusLabel->setMinimumWidth(80);
        rowLayout->addWidget(row.statusLabel);

        m_deviceLayout->addLayout(rowLayout);
        m_rows.push_back(row);
    }
}

void MultiProgressWidget::setDeviceCount(int count)
{
    m_deviceCount = count;
    rebuildUi();
    setVisible(count > 0);
}

void MultiProgressWidget::setDeviceProgress(int deviceIndex, int pct)
{
    if (deviceIndex >= 0 && deviceIndex < static_cast<int>(m_rows.size())) {
        m_rows[deviceIndex].bar->setValue(pct);
    }
}

void MultiProgressWidget::setDeviceStatus(int deviceIndex, const QString& status, bool ok)
{
    if (deviceIndex >= 0 && deviceIndex < static_cast<int>(m_rows.size())) {
        auto& row = m_rows[deviceIndex];
        row.statusLabel->setText(status);
        row.statusLabel->setStyleSheet(
            ok ? "color: #40C8A0;" : "color: #E85848;"
        );
        row.bar->setValue(ok ? 100 : 0);
    }
}

void MultiProgressWidget::setOverallProgress(int pct)
{
    m_overallBar->setValue(pct);
}

void MultiProgressWidget::reset()
{
    m_overallBar->setValue(0);
    m_deviceCount = 0;
    rebuildUi();
    setVisible(false);
}
