/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: NavBar.cpp
 * Date: 2026-07-25
 * Author: turnarond
 *
 * Description: NavBar 实现。
 */

#include "NavBar.h"
#include <QStyle>

NavBar::NavBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("navBar");
    setFixedWidth(56);
    setStyleSheet(
        "NavBar { background: #0E1219; border-right: 1px solid #252A33; }"
    );

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 8, 0, 8);
    m_layout->setSpacing(2);
    m_layout->addStretch(); // 工具项在上，设置按钮在底部
}

void NavBar::addItem(const QString& icon, const QString& label, const QString& toolId)
{
    m_items.push_back({icon, label, toolId});
    rebuildUi();
}

void NavBar::setActiveItem(int index)
{
    if (index >= 0 && index < static_cast<int>(m_buttons.size())) {
        // 清除旧活跃态
        if (m_activeIndex >= 0 && m_activeIndex < static_cast<int>(m_buttons.size())) {
            m_buttons[m_activeIndex]->setProperty("active", false);
            m_buttons[m_activeIndex]->style()->unpolish(m_buttons[m_activeIndex]);
            m_buttons[m_activeIndex]->style()->polish(m_buttons[m_activeIndex]);
        }
        // 设置新活跃态
        m_activeIndex = index;
        m_buttons[index]->setProperty("active", true);
        m_buttons[index]->style()->unpolish(m_buttons[index]);
        m_buttons[index]->style()->polish(m_buttons[index]);
    }
}

void NavBar::rebuildUi()
{
    // 清除旧按钮（保留 stretch 在末尾）
    for (auto* btn : m_buttons) {
        m_layout->removeWidget(btn);
        delete btn;
    }
    m_buttons.clear();

    // 重新构建
    for (size_t i = 0; i < m_items.size(); ++i) {
        auto* btn = new QPushButton(this);
        // 图标在上，文字在下
        btn->setText(m_items[i].icon + "\n" + m_items[i].label);
        btn->setFixedSize(56, 48);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setProperty("navIndex", static_cast<int>(i));
        btn->setProperty("active", false);

        // 样式：默认石墨色
        btn->setStyleSheet(
            "QPushButton {"
            "  background: transparent;"
            "  border: none;"
            "  border-left: 2px solid transparent;"
            "  color: #7B8494;"
            "  font-size: 10px;"
            "  font-family: 'Microsoft YaHei';"
            "  padding: 4px 2px;"
            "  text-align: center;"
            "}"
            "QPushButton:hover {"
            "  background: #1A1F2A;"
            "  color: #C8CCD4;"
            "}"
            "QPushButton[active=\"true\"] {"
            "  color: #F0A030;"
            "  background: #141820;"
            "  border-left: 2px solid #F0A030;"
            "}"
        );

        connect(btn, &QPushButton::clicked, [this, i]() {
            setActiveItem(static_cast<int>(i));
            emit itemClicked(static_cast<int>(i));
        });

        // 插入到 stretch 之前
        m_layout->insertWidget(static_cast<int>(i), btn);
        m_buttons.push_back(btn);
    }
}
