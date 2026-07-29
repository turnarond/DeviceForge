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
#include <QVBoxLayout>
#include <QScrollArea>

MultiProgressWidget::MultiProgressWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 4, 0, 4);
    mainLayout->setSpacing(4);

    // 全局进度条 + 取消按钮
    auto* overallRow = new QHBoxLayout();
    m_overallBar = new QProgressBar(this);
    m_overallBar->setRange(0, 100);
    m_overallBar->setValue(0);
    m_overallBar->setTextVisible(true);
    m_overallBar->setFormat("总进度 %p%");
    m_overallBar->setMaximumHeight(20);
    overallRow->addWidget(m_overallBar, 1);

    m_cancelBtn = new QPushButton("取消", this);
    m_cancelBtn->setMaximumWidth(50);
    m_cancelBtn->setMaximumHeight(20);
    connect(m_cancelBtn, &QPushButton::clicked, this, &MultiProgressWidget::cancelRequested);
    overallRow->addWidget(m_cancelBtn);

    mainLayout->addLayout(overallRow);

    // 设备状态区（可滚动，最大高度 68px ≈ 3 行紧凑标签）
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setMaximumHeight(68);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_deviceContainer = new QWidget(this);
    m_deviceLayout = new QVBoxLayout(m_deviceContainer);
    m_deviceLayout->setContentsMargins(0, 0, 0, 0);
    m_deviceLayout->setSpacing(2);

    scrollArea->setWidget(m_deviceContainer);
    mainLayout->addWidget(scrollArea);

    setVisible(false);
}

void MultiProgressWidget::rebuildUi()
{
    // 清除旧标签
    QLayoutItem* item;
    while ((item = m_deviceLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    m_rows.clear();

    // 紧凑设备状态标签：[IP ✓/✗/⏳] 水平排列，自动换行由 scroll 处理
    for (int i = 0; i < m_deviceCount; ++i) {
        DeviceRow row;
        row.statusLabel = new QLabel("⏳ 等待中", this);
        row.statusLabel->setStyleSheet(
            "QLabel {"
            "  background: #1A1F2A;"
            "  border: 1px solid #333B48;"
            "  border-radius: 3px;"
            "  color: #7B8494;"
            "  font-size: 11px;"
            "  padding: 2px 8px;"
            "}"
        );
        m_deviceLayout->addWidget(row.statusLabel);
        m_rows.push_back(row);
    }
    m_deviceLayout->addStretch();
}

void MultiProgressWidget::setDeviceCount(int count)
{
    m_deviceCount = count;
    rebuildUi();
    setVisible(count > 0);
    m_overallBar->setFormat(count > 0
        ? QString("准备部署 %1 台设备...").arg(count)
        : QStringLiteral("总进度 %p%"));
}

void MultiProgressWidget::setDeviceInfo(int deviceIndex, const QString& info)
{
    if (deviceIndex >= 0 && deviceIndex < static_cast<int>(m_rows.size())) {
        m_rows[deviceIndex].statusLabel->setText("⏳ " + info);
        m_rows[deviceIndex].deviceKey = info;
    }
}

void MultiProgressWidget::setDeviceStatusByKey(const QString& key, bool ok)
{
    for (auto& row : m_rows) {
        if (row.deviceKey == key) {
            row.statusLabel->setText((ok ? "✓ " : "✗ ") + key);
            row.statusLabel->setStyleSheet(ok
                ? "QLabel { background: #14261E; border: 1px solid #40C8A0;"
                  "  border-radius: 3px; color: #40C8A0; font-size: 11px; padding: 2px 8px; }"
                : "QLabel { background: #261418; border: 1px solid #E85848;"
                  "  border-radius: 3px; color: #E85848; font-size: 11px; padding: 2px 8px; }");
            return;
        }
    }
}

void MultiProgressWidget::setFinishedSummary(int done, int total)
{
    m_overallBar->setFormat(QString("部署完成 %1/%2 设备").arg(done).arg(total));
}

void MultiProgressWidget::setDeviceProgress(int deviceIndex, int pct)
{
    if (deviceIndex >= 0 && deviceIndex < static_cast<int>(m_rows.size())) {
        m_rows[deviceIndex].statusLabel->setText(
            QString("⏳ %1%").arg(pct));
    }
}

void MultiProgressWidget::setDeviceStatus(int deviceIndex, const QString& status, bool ok)
{
    if (deviceIndex >= 0 && deviceIndex < static_cast<int>(m_rows.size())) {
        auto& row = m_rows[deviceIndex];
        row.statusLabel->setText(ok ? "✓ " + status : "✗ " + status);
        row.statusLabel->setStyleSheet(ok
            ? "QLabel { background: #14261E; border: 1px solid #40C8A0; border-radius: 3px;"
              "  color: #40C8A0; font-size: 11px; padding: 2px 8px; }"
            : "QLabel { background: #261418; border: 1px solid #E85848; border-radius: 3px;"
              "  color: #E85848; font-size: 11px; padding: 2px 8px; }");
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
