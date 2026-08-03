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
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 2, 0, 2);
    mainLayout->setSpacing(6);

    // 总进度条
    m_overallBar = new QProgressBar(this);
    m_overallBar->setRange(0, 100);
    m_overallBar->setValue(0);
    m_overallBar->setTextVisible(true);
    m_overallBar->setMaximumHeight(24);
    mainLayout->addWidget(m_overallBar, 1);

    // 取消按钮
    m_cancelBtn = new QPushButton("取消", this);
    m_cancelBtn->setFixedSize(56, 24);
    connect(m_cancelBtn, &QPushButton::clicked, this, &MultiProgressWidget::cancelRequested);
    mainLayout->addWidget(m_cancelBtn);

    setVisible(false);
}

void MultiProgressWidget::setDeviceCount(int count)
{
    m_deviceCount = count;
    setVisible(count > 0);
    m_overallBar->setFormat(QString("正在部署 %1 台设备...").arg(count));
}

void MultiProgressWidget::setOverallProgress(int pct)
{
    m_overallBar->setValue(pct);
}

void MultiProgressWidget::setFinishedSummary(int done, int total)
{
    m_overallBar->setFormat(QString("部署完成：%1/%2 成功").arg(done).arg(total));
}

void MultiProgressWidget::reset()
{
    m_overallBar->setValue(0);
    m_deviceCount = 0;
    setVisible(false);
}

