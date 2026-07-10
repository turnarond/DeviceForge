/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: ToolHost.h
 *
 * Date: 2026-07-04
 *
 * Author: turnarond
 *
 * Description: ToolHost 桥接层 — 负责 Tool 的创建、配对、生命周期管理
 *
 * ToolHost 是 Backend（ServiceTask）与 Widget（QWidget）之间的桥接层。
 * 负责：
 * - 注册内置 Tool 的 Backend/Widget 工厂函数
 * - 创建 Tool 实例（Backend 启动 → Widget.onToolStart()）
 * - 销毁 Tool 实例（Widget.onToolStop() → Backend 停止 → 清理）
 * - 同一时间只有一个活跃 Tool 实例（切换时自动销毁上一个）
 */

#pragma once
#include <QObject>
#include <QWidget>
#include <memory>
#include <string>
#include <unordered_map>

class ToolBackend;
class ToolWidget;

// Tool 实例的完整句柄 — Backend + Widget 配对
struct ToolInstance {
    std::shared_ptr<ToolBackend> backend;
    ToolWidget* widget = nullptr; // QWidget，生命周期由 Qt parent 管理
    std::string toolId;
};

// ToolHost 桥接层 — 负责 Tool 的创建、配对、生命周期管理
class ToolHost : public QObject {
    Q_OBJECT

public:
    explicit ToolHost(QObject* parent = nullptr);
    ~ToolHost() override;

    // 工厂函数类型
    using BackendFactory = std::shared_ptr<ToolBackend>(*)();
    using WidgetFactory = ToolWidget*(*)(QWidget* parent);

    // 注册内置 Tool 的工厂函数（编译时绑定）
    void registerBuiltinFactory(const std::string& toolId,
                                 BackendFactory bf, WidgetFactory wf);

    // 创建 Tool 实例（通过已注册的工厂）
    // 如果已有活跃 Tool，先自动销毁
    ToolInstance* createTool(const std::string& toolId, QWidget* parentWidget);

    // 销毁 Tool 实例
    void destroyTool(ToolInstance* instance);

    // 当前活跃的 Tool 实例
    ToolInstance* activeTool() const { return m_activeTool; }

signals:
    void toolCreated(const QString& toolId);
    void toolDestroyed(const QString& toolId);
    void toolError(const QString& toolId, const QString& error);

private:
    struct BuiltinFactory {
        BackendFactory backendFactory;
        WidgetFactory  widgetFactory;
    };
    std::unordered_map<std::string, BuiltinFactory> m_factories;
    ToolInstance* m_activeTool = nullptr;
};
