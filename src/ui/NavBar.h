/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: NavBar.h
 * Date: 2026-07-25
 * Author: turnarond
 *
 * Description: 左侧竖排图标导航栏 — 56px 固定宽，每项图标+标签，
 *              活跃态琴色左侧竖条 + 图标着色，非活跃态石墨色。
 */

#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <vector>

struct NavItem {
    QString icon;
    QString label;
    QString toolId;
};

class NavBar : public QWidget {
    Q_OBJECT
public:
    explicit NavBar(QWidget* parent = nullptr);

    void addItem(const QString& icon, const QString& label, const QString& toolId);
    void setActiveItem(int index);
    int activeIndex() const { return m_activeIndex; }
    int count() const { return static_cast<int>(m_items.size()); }

signals:
    void itemClicked(int index);

private:
    void rebuildUi();

    std::vector<NavItem> m_items;
    std::vector<QPushButton*> m_buttons;
    int m_activeIndex = -1;
    QVBoxLayout* m_layout = nullptr;
};
