/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: ToolWidget.h
 *
 * Date: 2026-07-04
 *
 * Author: turnarond
 *
 * Description: Tool 前端基类 — 所有 Tool 的 UI 必须继承此类
 *
 * 定义统一的 Tool 前端接口。通过 QWidget 集成到主窗口的 Tab 或切换面板中。
 */

#pragma once
#include <QWidget>
#include <QString>
#include <memory>

// 前向声明：消息队列类型将在后续 Task 中引入 nanopb 后精确定义
// 当前阶段使用 QVariant 作为过渡方案
class QVariant;

// Tool 前端基类 — 所有 Tool 的 UI 必须继承此类
class ToolWidget : public QWidget {
    Q_OBJECT

public:
    explicit ToolWidget(QWidget* parent = nullptr) : QWidget(parent) {}
    virtual ~ToolWidget() = default;

    // --- 身份 ---
    virtual QString toolId() const = 0;
    virtual QString toolName() const = 0;

    // --- 生命周期回调（由 ToolHost 调用） ---
    virtual void onToolStart() = 0;   // Backend 启动后，可开始 UI 交互
    virtual void onToolStop() = 0;    // Backend 停止前，清理 UI 状态

signals:
    // 通知 ToolHost：当前 Tool 状态发生变化（用于状态栏显示）
    void toolStatusChanged(const QString& status);
};
