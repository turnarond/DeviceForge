/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: tst_deploy_toolbar_layout.cpp
 *
 * Date: 2026-10-20
 *
 * Author: turnarond
 *
 * Description: v2.15 波3 任务13（方案 C）部署页工具栏两行化布局回归。
 *              断言 deployToolbar 容器内是 QVBoxLayout，恰好两个 QHBoxLayout 行：
 *              第 1 行=连接组 + 主操作（刷新/部署/保存为模板），
 *              第 2 行=部署选项（清空/重启）+ 结果操作（导出报告/重试失败设备）。
 *              控件按 objectName 或可见文本定位，不新增 objectName（硬约束）。
 */

#include <QtTest>

#include <QApplication>
#include <QCheckBox>
#include <QPushButton>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDir>
#include <QFile>
#include <QUuid>
#include <QList>

#include "config/ConfigStore.h"
#include "tools/FtpDeployTool/FtpDeployWidget.h"

namespace {

// 递归收集某布局（含子布局）直接持有的控件
QList<QWidget*> collectRowWidgets(QLayout* layout)
{
    QList<QWidget*> out;
    if (!layout)
        return out;
    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem* item = layout->itemAt(i);
        if (!item)
            continue;
        if (QWidget* w = item->widget())
            out.append(w);
        else if (QLayout* sub = item->layout())
            out.append(collectRowWidgets(sub));
    }
    return out;
}

bool rowContains(QLayout* row, QWidget* target)
{
    return collectRowWidgets(row).contains(target);
}

} // namespace

class TstDeployToolbarLayout : public QObject {
    Q_OBJECT

private:
    QString m_workDir;
    QString m_dbPath;

    void freshDb()
    {
        ConfigStore::instance().close();
        QFile::remove(m_dbPath);
        QFile::remove(m_dbPath + QStringLiteral("-wal"));
        QFile::remove(m_dbPath + QStringLiteral("-shm"));
        QVERIFY2(ConfigStore::instance().open(m_dbPath),
                 qPrintable(QStringLiteral("open failed path=%1").arg(m_dbPath)));
    }

private slots:
    void initTestCase()
    {
        m_workDir = QDir::temp().filePath(
            QStringLiteral("tst_deploy_toolbar_layout_%1")
                .arg(QUuid::createUuid().toString(QUuid::Id128)));
        QVERIFY(QDir().mkpath(m_workDir));
        m_dbPath = QDir(m_workDir).filePath(QStringLiteral("config.db"));
    }

    void cleanupTestCase()
    {
        ConfigStore::instance().close();
        QDir(m_workDir).removeRecursively();
    }

    void init() { freshDb(); }
    void cleanup() { ConfigStore::instance().close(); }

    void toolbar_containerExists()
    {
        FtpDeployWidget w;
        auto* bar = w.findChild<QWidget*>(QStringLiteral("deployToolbar"));
        QVERIFY2(bar, "未找到 objectName=deployToolbar 的工具栏容器");
    }

    void toolbar_isVBoxLayoutWithTwoHBoxRows()
    {
        FtpDeployWidget w;
        auto* bar = w.findChild<QWidget*>(QStringLiteral("deployToolbar"));
        QVERIFY(bar);
        auto* v = qobject_cast<QVBoxLayout*>(bar->layout());
        QVERIFY2(v, "deployToolbar 的 layout 不是 QVBoxLayout");
        QCOMPARE(v->count(), 2);
        auto* row1 = qobject_cast<QHBoxLayout*>(v->itemAt(0)->layout());
        auto* row2 = qobject_cast<QHBoxLayout*>(v->itemAt(1)->layout());
        QVERIFY2(row1, "第 1 行不是 QHBoxLayout");
        QVERIFY2(row2, "第 2 行不是 QHBoxLayout");
    }

    void row1_hasConnectionAndPrimaryActions()
    {
        FtpDeployWidget w;
        auto* bar = w.findChild<QWidget*>(QStringLiteral("deployToolbar"));
        QVERIFY(bar);
        auto* v = qobject_cast<QVBoxLayout*>(bar->layout());
        QVERIFY(v);
        QCOMPARE(v->count(), 2);
        auto* row1 = qobject_cast<QHBoxLayout*>(v->itemAt(0)->layout());
        QVERIFY(row1);

        auto* deploy = w.findChild<QWidget*>(QStringLiteral("btnPrimary"));
        auto* refresh = w.findChild<QWidget*>(QStringLiteral("refreshBtn"));
        auto* saveTemplate = w.findChild<QWidget*>(QStringLiteral("deploySaveTemplateBtn"));
        QVERIFY(deploy);
        QVERIFY(refresh);
        QVERIFY(saveTemplate);

        QVERIFY2(rowContains(row1, refresh), "⟳ 刷新 不在第 1 行");
        QVERIFY2(rowContains(row1, deploy), "▶ 部署（btnPrimary）不在第 1 行");
        QVERIFY2(rowContains(row1, saveTemplate), "保存为模板 不在第 1 行");
    }

    void row2_hasDeployOptionsAndResultActions()
    {
        FtpDeployWidget w;
        auto* bar = w.findChild<QWidget*>(QStringLiteral("deployToolbar"));
        QVERIFY(bar);
        auto* v = qobject_cast<QVBoxLayout*>(bar->layout());
        QVERIFY(v);
        QCOMPARE(v->count(), 2);
        auto* row2 = qobject_cast<QHBoxLayout*>(v->itemAt(1)->layout());
        QVERIFY(row2);

        const QList<QWidget*> row2Widgets = collectRowWidgets(row2);

        // 复选框按可见文本定位（不依赖新 objectName）
        QCheckBox* clearCheck = nullptr;
        QCheckBox* rebootCheck = nullptr;
        const auto boxes = w.findChildren<QCheckBox*>();
        for (auto* box : boxes) {
            if (box->text() == QStringLiteral("部署前清空远程"))
                clearCheck = box;
            else if (box->text() == QStringLiteral("部署后重启"))
                rebootCheck = box;
        }
        QVERIFY2(clearCheck, "未找到『部署前清空远程』复选框");
        QVERIFY2(rebootCheck, "未找到『部署后重启』复选框");
        QVERIFY2(row2Widgets.contains(clearCheck), "『部署前清空远程』不在第 2 行");
        QVERIFY2(row2Widgets.contains(rebootCheck), "『部署后重启』不在第 2 行");

        // 按钮按可见文本定位
        QPushButton* exportBtn = nullptr;
        QPushButton* retryBtn = nullptr;
        const auto buttons = w.findChildren<QPushButton*>();
        for (auto* btn : buttons) {
            if (btn->text().contains(QStringLiteral("导出报告")))
                exportBtn = btn;
            else if (btn->text().contains(QStringLiteral("重试失败设备")))
                retryBtn = btn;
        }
        QVERIFY2(exportBtn, "未找到『导出报告』按钮");
        QVERIFY2(retryBtn, "未找到『重试失败设备』按钮");
        QVERIFY2(row2Widgets.contains(exportBtn), "『导出报告』不在第 2 行");
        QVERIFY2(row2Widgets.contains(retryBtn), "『重试失败设备』不在第 2 行");
    }
};

QTEST_MAIN(TstDeployToolbarLayout)
#include "tst_deploy_toolbar_layout.moc"
