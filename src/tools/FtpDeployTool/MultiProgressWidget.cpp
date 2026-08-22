/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: MultiProgressWidget.cpp
 * Date: 2026-07-25（2026-08-22 v2.8 并行批量部署 Task 4：per-device 行实装）
 * Author: turnarond
 *
 * Description: MultiProgressWidget 实现 — 总进度 + 每设备行（标签/进度条/状态）。
 */

#include "MultiProgressWidget.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace {
// 终态着色（信号态语义色，配色纪律：青绿=成功/完成信号，红=失败告警；
// 行结构本身不着色）。与连接状态点用色同源。
constexpr const char* kOkColor   = "#40C8A0";
constexpr const char* kFailColor = "#E85848";

// 状态文案：等待中 → 上传中（首个进度事件起）→ 成功/失败（终态）
const QString kStatusPending = QStringLiteral("等待中");
}  // namespace

MultiProgressWidget::MultiProgressWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 2, 0, 2);
    mainLayout->setSpacing(4);

    // 顶行：总进度条 + 取消按钮（沿用 v2.5 布局语义）
    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(6);

    m_overallBar = new QProgressBar(this);
    m_overallBar->setRange(0, 100);
    m_overallBar->setValue(0);
    m_overallBar->setTextVisible(true);
    m_overallBar->setMaximumHeight(24);
    topRow->addWidget(m_overallBar, 1);

    m_cancelBtn = new QPushButton("取消", this);
    m_cancelBtn->setFixedSize(56, 24);
    connect(m_cancelBtn, &QPushButton::clicked, this, &MultiProgressWidget::cancelRequested);
    topRow->addWidget(m_cancelBtn);

    mainLayout->addLayout(topRow);

    // 每设备一行容器（v2.8 Task 4 实装）
    m_rowsLayout = new QVBoxLayout();
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(2);
    mainLayout->addLayout(m_rowsLayout);

    setVisible(false);
}

void MultiProgressWidget::rebuildUi()
{
    // 整表重建：先摘除旧行（deleteLater 异步销毁，避免布局遍历中析构悬垂）
    while (QLayoutItem* item = m_rowsLayout->takeAt(0)) {
        if (auto* w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }
    m_rows.clear();

    // 按注册序逐台重建：标签 ip:port + 进度条 + 状态
    for (const QString& key : m_keys) {
        auto* rowWidget = new QWidget(this);
        auto* row = new QHBoxLayout(rowWidget);
        row->setContentsMargins(12, 0, 0, 0);   // 行相对总进度条缩进，层级可辨
        row->setSpacing(6);

        auto* label = new QLabel(key, rowWidget);
        label->setMinimumWidth(120);

        auto* bar = new QProgressBar(rowWidget);
        bar->setRange(0, 100);
        bar->setValue(0);
        bar->setTextVisible(true);
        bar->setMaximumHeight(18);

        auto* status = new QLabel(kStatusPending, rowWidget);
        status->setFixedWidth(48);

        row->addWidget(label);
        row->addWidget(bar, 1);
        row->addWidget(status);

        m_rowsLayout->addWidget(rowWidget);
        m_rows.insert(key, DeviceRow{bar, status});
    }
}

void MultiProgressWidget::setDeviceCount(int count)
{
    m_deviceCount = count;
    setVisible(count > 0);
    m_keys.clear();
    rebuildUi();
    m_overallBar->setValue(0);
    m_overallBar->setFormat(QString("正在部署 %1 台设备...").arg(count));
}

void MultiProgressWidget::setDeviceInfo(const QString& key)
{
    if (m_rows.contains(key)) {
        return;   // 重复注册防御（同键二次注册不产生第二行）
    }
    m_keys.push_back(key);
    rebuildUi();
}

void MultiProgressWidget::setDeviceProgress(const QString& key, int pct)
{
    const auto it = m_rows.constFind(key);
    if (it == m_rows.constEnd()) {
        return;   // 未注册键（新一轮批量重建后的陈旧回调）：静默忽略
    }
    it->bar->setValue(qBound(0, pct, 100));
    // 首个进度事件把状态从「等待中」推进为「上传中」（终态着色后不再覆盖：
    // 终态文本非 kStatusPending，此分支自然跳过）
    if (it->status->text() == kStatusPending) {
        it->status->setText(QStringLiteral("上传中"));
    }
}

void MultiProgressWidget::setDeviceStatusByKey(const QString& key, bool ok)
{
    const auto it = m_rows.constFind(key);
    if (it == m_rows.constEnd()) {
        return;   // 同 setDeviceProgress：陈旧回调静默忽略
    }
    if (ok) {
        it->bar->setValue(100);
        it->status->setText(QStringLiteral("成功"));
        it->status->setStyleSheet(QStringLiteral("color: %1;").arg(QLatin1String(kOkColor)));
    } else {
        it->status->setText(QStringLiteral("失败"));
        it->status->setStyleSheet(QStringLiteral("color: %1;").arg(QLatin1String(kFailColor)));
    }
}

void MultiProgressWidget::setOverallProgress(int pct)
{
    m_overallBar->setValue(pct);
}

void MultiProgressWidget::setFinishedSummary(int done, int total)
{
    m_overallBar->setFormat(QString("部署完成：%1/%2 成功").arg(done).arg(total));
}
