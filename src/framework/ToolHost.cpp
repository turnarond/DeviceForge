/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: ToolHost.cpp
 *
 * Date: 2026-07-04
 *
 * Author: turnarond
 *
 * Description: ToolHost 桥接层实现
 *
 * 管理 Tool 的完整生命周期：
 * - 创建流程：Backend 工厂 → Widget 工厂 → Backend.OnStart() → Widget.onToolStart()
 * - 销毁流程：Widget.onToolStop() → Backend.OnStop() → Widget.deleteLater() → Backend.reset()
 * - 同一时间只有一个活跃 Tool（切换时自动销毁上一个）
 */

#include "ToolHost.h"
#include "ToolBackend.h"
#include "ToolWidget.h"
#include "lwserverbase/core/ServiceManager.h"
#include "lwlog/lwlog.h"

ToolHost::ToolHost(QObject* parent) : QObject(parent) {}

ToolHost::~ToolHost()
{
    if (m_activeTool) {
        destroyTool(m_activeTool);
    }
}

void ToolHost::registerBuiltinFactory(const std::string& toolId,
                                       BackendFactory bf, WidgetFactory wf)
{
    m_factories[toolId] = {bf, wf};
    LWLOG_I(("ToolHost: 注册内置 Tool 工厂: " + toolId).c_str());
}

ToolInstance* ToolHost::createTool(const std::string& toolId, QWidget* parentWidget)
{
    // 如果已有活跃 Tool，先销毁
    if (m_activeTool) {
        LWLOG_I(("ToolHost: 切换 Tool，先销毁: " + m_activeTool->toolId).c_str());
        destroyTool(m_activeTool);
    }

    // 查找工厂
    auto it = m_factories.find(toolId);
    if (it == m_factories.end()) {
        std::string err = "未找到 Tool 工厂: " + toolId;
        LWLOG_E(err.c_str());
        emit toolError(QString::fromStdString(toolId),
                       QString::fromStdString(err));
        return nullptr;
    }

    // 创建实例
    auto* instance = new ToolInstance;
    instance->toolId = toolId;
    instance->backend = it->second.backendFactory();
    instance->widget = it->second.widgetFactory(parentWidget);

    if (!instance->backend || !instance->widget) {
        LWLOG_E(("ToolHost: Tool 创建失败（工厂返回空）: " + toolId).c_str());
        delete instance;
        emit toolError(QString::fromStdString(toolId),
                       QString::fromStdString("工厂返回空指针"));
        return nullptr;
    }

    // 启动 Backend（通过 ServiceTask::OnStart 启动线程管理）
    int rc = instance->backend->OnStart(0, nullptr);
    if (rc != 0) {
        LWLOG_E(("ToolHost: Tool Backend 启动失败: " + toolId
                  + " (rc=" + std::to_string(rc) + ")").c_str());
        delete instance;
        emit toolError(QString::fromStdString(toolId),
                       QString::fromStdString("Backend OnStart 返回 " + std::to_string(rc)));
        return nullptr;
    }

    // 通知 Widget：Backend 已就绪
    instance->widget->onToolStart();

    m_activeTool = instance;
    emit toolCreated(QString::fromStdString(toolId));
    LWLOG_I(("ToolHost: Tool 已创建: " + toolId).c_str());

    return instance;
}

void ToolHost::destroyTool(ToolInstance* instance)
{
    if (!instance) return;

    // 1. 通知 Widget 停止 UI 交互
    if (instance->widget) {
        instance->widget->onToolStop();
        // Qt parent 管理 Widget 生命周期，这里只隐藏并标记删除
        instance->widget->hide();
        instance->widget->deleteLater();
    }

    // 2. 停止 Backend（通过 ServiceTask::OnStop 停止线程）
    if (instance->backend) {
        instance->backend->OnStop();
        instance->backend.reset();
    }

    // 3. 通知外部
    emit toolDestroyed(QString::fromStdString(instance->toolId));
    LWLOG_I(("ToolHost: Tool 已销毁: " + instance->toolId).c_str());

    // 4. 清理活跃指针
    if (m_activeTool == instance) {
        m_activeTool = nullptr;
    }

    delete instance;
}
