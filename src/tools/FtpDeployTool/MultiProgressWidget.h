/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: MultiProgressWidget.h
 * Date: 2026-07-25（2026-08-22 v2.8 并行批量部署 Task 4：空壳转正）
 * Author: turnarond
 *
 * Description: 多设备并行部署进度组件 — 顶行总进度条 + 取消按钮，下方每设备
 *              一行（标签 ip:port + QProgressBar + 状态 QLabel）。
 *              行键与 DeploymentRunner 的 deviceProgress 回调键及
 *              DeviceResult.deviceKey 同源（恒为 "ip:port"，端口覆盖后取值）；
 *              聚合总进度经保留键 kOverallKey 分流至 setOverallProgress，
 *              分流在后端完成（见 FtpDeployBackend::startUpload）。
 *
 * 线程契约：所有公有方法仅允许 GUI 线程调用——后端回调从 Runner 池线程触发，
 * 消费侧（FtpDeployWidget::connectBackendSignals）已统一 QueuedConnection 编组。
 */

#pragma once
#include <QWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QMap>
#include <QString>

#include <vector>

class QLabel;
class QVBoxLayout;

class MultiProgressWidget : public QWidget {
    Q_OBJECT
public:
    explicit MultiProgressWidget(QWidget* parent = nullptr);

    // 新一轮批量开始：清空旧行并复位总进度条（重复调用即重开一轮）
    void setDeviceCount(int count);
    // 注册一台设备的行（key = "ip:port"；重复注册防御式忽略）
    void setDeviceInfo(const QString& key);
    // 单台即时进度（0-100）；未知 key 静默忽略（行已随新一轮批量重建的陈旧回调）
    void setDeviceProgress(const QString& key, int pct);
    // 单台终态着色：ok=true「成功」青绿 / false「失败」红
    void setDeviceStatusByKey(const QString& key, bool ok);
    // 单台取消终态：「已取消」+ 中性灰（取消非信号态，不用琴色/青绿/错误红；
    // 未知 key 同样静默忽略）
    void setDeviceCancelled(const QString& key);
    // 聚合总进度（后端自 kOverallKey 哨兵键分流而来）
    void setOverallProgress(int pct);
    // 收尾文案（总进度条格式覆写为「部署完成：x/y 成功」）
    void setFinishedSummary(int done, int total);

signals:
    void cancelRequested();

private:
    // 单设备行的控件集合（行内控件父级为行容器，随整表重建一并销毁）
    struct DeviceRow {
        QProgressBar* bar = nullptr;
        QLabel* status = nullptr;
    };

    // 按 m_keys 注册序整表重建（setDeviceCount 清空重建、setDeviceInfo 追加重建
    // 统一走此入口——「行数变化 rebuildUi」，n 为台数量级，全量重建成本可忽略）
    void rebuildUi();

    int m_deviceCount = 0;
    std::vector<QString> m_keys;      // 行序 = 设备注册序（部署提交序）
    QMap<QString, DeviceRow> m_rows;  // key("ip:port") → 行控件
    QVBoxLayout* m_rowsLayout = nullptr;  // 每设备一行的纵向容器
    QProgressBar* m_overallBar = nullptr;
    QPushButton* m_cancelBtn = nullptr;
};
