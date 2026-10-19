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
 * Description: v2.15 波3 任务13（方案 C）部署页工具栏两行化回归。
 *              ① 形状：deployToolbar 容器 = QVBoxLayout + 恰好两个 QHBoxLayout 行；
 *              ② 归属：第 1 行=连接组（协议/设备/端口/FTPS/状态点）+ 主操作（刷新/部署/保存为模板），
 *                 第 2 行=部署选项（清空/重启 + 协议/命令/超时/重试四参）+ 结果操作（导出报告/重试失败设备）；
 *              ③ 目的：工具栏最小宽落进 1366 窄屏可用宽之内（阈值推导见 toolbarFitsNarrowScreen）。
 *              既有控件一律不新增 objectName（按可见文本/类型/尺寸特征定位）；
 *              容器对象名 deployToolbar 是本改动新增的唯一对象名。
 */

#include <QtTest>

#include <functional>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QPushButton>
#include <QSpinBox>
#include <QStyleFactory>
#include <QVBoxLayout>
#include <QWidget>
#include <QDir>
#include <QFile>
#include <QUuid>

#include "config/ConfigStore.h"
#include "tools/FtpDeployTool/FtpDeployWidget.h"

namespace {

// 递归收集某布局（含嵌套子布局）直接持有的控件
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

template <class T>
QList<T*> rowOfClass(QLayout* row)
{
    QList<T*> out;
    const QList<QWidget*> ws = collectRowWidgets(row);
    for (QWidget* w : ws) {
        if (T* cast = qobject_cast<T*>(w))
            out.append(cast);
    }
    return out;
}

// 按特征查找（不加对象名）：返回第一个满足 pred 的同类控件
template <class T>
T* findInRow(QLayout* row, std::function<bool(T*)> pred)
{
    const QList<T*> list = rowOfClass<T>(row);
    for (T* item : list) {
        if (pred(item))
            return item;
    }
    return nullptr;
}

QFrame* separatorIn(QLayout* row)
{
    const QList<QWidget*> ws = collectRowWidgets(row);
    for (QWidget* w : ws) {
        auto* frame = qobject_cast<QFrame*>(w);
        if (frame && frame->objectName() == QStringLiteral("toolbarSeparator"))
            return frame;
    }
    return nullptr;
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

    QWidget* toolbar(QWidget* host)
    {
        return host->findChild<QWidget*>(QStringLiteral("deployToolbar"));
    }

    // 取第 index 行；容器缺失、行数不是 2、行类型不是 QHBoxLayout 时返回 nullptr
    // （QVERIFY 系宏只能在测试槽函数里 return，故判空留给调用方）
    QLayout* row(QWidget* host, int index)
    {
        QWidget* bar = toolbar(host);
        if (!bar)
            return nullptr;
        auto* v = qobject_cast<QVBoxLayout*>(bar->layout());
        if (!v || v->count() != 2)
            return nullptr;
        return qobject_cast<QHBoxLayout*>(v->itemAt(index)->layout());
    }

private slots:
    void initTestCase()
    {
        m_workDir = QDir::temp().filePath(
            QStringLiteral("tst_deploy_toolbar_layout_%1")
                .arg(QUuid::createUuid().toString(QUuid::Id128)));
        QVERIFY(QDir().mkpath(m_workDir));
        m_dbPath = QDir(m_workDir).filePath(QStringLiteral("config.db"));

        // 与生产一致的两件事（顺序也一致：先 Fusion 样式，再 QSS）：
        // DeviceForge.cpp:191 QApplication::setStyle(QStyleFactory::create("Fusion"))
        // DeviceForge.cpp:73  …加载 :/styles/darkstyle.qss…
        // 不这么做的后果实测过：无生产样式表时（默认 windowsvista 风格）量得工具栏最小宽 834px，
        // 比 Fusion+QSS 的 933px 小一档——那将是"宽度过关"的假绿。
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        QFile qss(QStringLiteral(":/styles/darkstyle.qss"));
        QVERIFY2(qss.open(QIODevice::ReadOnly),
                 "读不到 :/styles/darkstyle.qss（DeviceForge.qrc 未编入本目标）");
        const QString qssText = QString::fromUtf8(qss.readAll());
        QVERIFY2(qssText.contains(QStringLiteral("min-width: 85px"))
                 || qssText.contains(QStringLiteral("min-width:85px")),
                 "darkstyle.qss 里找不到 QPushButton min-width:85px——样式表来源可疑");
        qApp->setStyleSheet(qssText);
        QVERIFY2(!qApp->styleSheet().isEmpty(), "样式表未真正生效（qApp->styleSheet() 为空）");
    }

    void cleanupTestCase()
    {
        qApp->setStyleSheet(QString());
        ConfigStore::instance().close();
        QDir(m_workDir).removeRecursively();
    }

    void init() { freshDb(); }
    void cleanup() { ConfigStore::instance().close(); }

    // 生效性守卫：QSS 的 min-width 兜底确实在计算里（否则宽度那格是假绿）
    void qssFloorIsInEffect()
    {
        QPushButton probe(QStringLiteral("⟳ 刷新"));
        const int hint = probe.sizeHint().width();
        QVERIFY2(hint >= 105,
                 qPrintable(QStringLiteral("短标签按钮 sizeHint 仅 %1px，QSS min-width:85px 未生效"
                                            "（预期 ≥105 = 85 内容 + 左右 padding 20）").arg(hint)));
    }

    void toolbar_containerExists()
    {
        FtpDeployWidget w;
        auto* bar = w.findChild<QWidget*>(QStringLiteral("deployToolbar"));
        QVERIFY2(bar, "未找到 objectName=deployToolbar 的工具栏容器");
    }

    void toolbar_isVBoxLayoutWithTwoHBoxRows()
    {
        FtpDeployWidget w;
        auto* bar = toolbar(&w);
        QVERIFY2(bar, "未找到 objectName=deployToolbar 的工具栏容器");
        auto* v = qobject_cast<QVBoxLayout*>(bar->layout());
        QVERIFY2(v, "deployToolbar 的 layout 不是 QVBoxLayout");
        QCOMPARE(v->count(), 2);
        QVERIFY2(qobject_cast<QHBoxLayout*>(v->itemAt(0)->layout()), "第 1 行不是 QHBoxLayout");
        QVERIFY2(qobject_cast<QHBoxLayout*>(v->itemAt(1)->layout()), "第 2 行不是 QHBoxLayout");
    }

    void row1_hasConnectionAndPrimaryActions()
    {
        FtpDeployWidget w;
        QLayout* row1 = row(&w, 0);
        QVERIFY2(row1, "第 1 行不可得（容器缺失/行数不是 2/不是 QHBoxLayout）");

        auto* deploy = w.findChild<QWidget*>(QStringLiteral("btnPrimary"));
        auto* refresh = w.findChild<QWidget*>(QStringLiteral("refreshBtn"));
        auto* saveTemplate = w.findChild<QWidget*>(QStringLiteral("deploySaveTemplateBtn"));
        auto* statusDot = w.findChild<QWidget*>(QStringLiteral("connStatusDot"));
        QVERIFY(deploy);
        QVERIFY(refresh);
        QVERIFY(saveTemplate);
        QVERIFY(statusDot);
        QVERIFY2(rowContains(row1, refresh), "⟳ 刷新 不在第 1 行");
        QVERIFY2(rowContains(row1, deploy), "▶ 部署（btnPrimary）不在第 1 行");
        QVERIFY2(rowContains(row1, saveTemplate), "保存为模板 不在第 1 行");
        QVERIFY2(rowContains(row1, statusDot), "连接状态点不在第 1 行");

        // 连接组三件套按特征定位：协议下拉含 FTP/SFTP；设备下拉取 min 160；端口 spin 上限 65535
        auto* protoCombo = findInRow<QComboBox>(row1, [](QComboBox* c) {
            return c->itemText(0) == QStringLiteral("FTP");
        });
        QVERIFY2(protoCombo, "协议下拉（首项 FTP）不在第 1 行");
        auto* deviceCombo = findInRow<QComboBox>(row1, [](QComboBox* c) {
            return c->minimumWidth() >= 160;
        });
        QVERIFY2(deviceCombo, "设备下拉（min 160）不在第 1 行");
        auto* portSpin = findInRow<QSpinBox>(row1, [](QSpinBox* s) {
            return s->maximum() == 65535;
        });
        QVERIFY2(portSpin, "端口 spin（上限 65535）不在第 1 行");
        auto* ftps = findInRow<QCheckBox>(row1, [](QCheckBox* b) {
            return b->text() == QStringLiteral("FTPS 加密");
        });
        QVERIFY2(ftps, "『FTPS 加密』不在第 1 行");
        QVERIFY2(separatorIn(row1), "第 1 行缺少组间分隔条 toolbarSeparator");
        // 反向：第 1 行不该有部署选项与结果操作（否则"两行"只是名义上的）
        QVERIFY2(!findInRow<QCheckBox>(row1, [](QCheckBox* b) {
                     return b->text() == QStringLiteral("部署后重启");
                 }),
                 "『部署后重启』竟出现在第 1 行");
    }

    void row2_hasDeployOptionsAndResultActions()
    {
        FtpDeployWidget w;
        QLayout* row2 = row(&w, 1);
        QVERIFY2(row2, "第 2 行不可得（容器缺失/行数不是 2/不是 QHBoxLayout）");

        auto* clearCheck = findInRow<QCheckBox>(row2, [](QCheckBox* b) {
            return b->text() == QStringLiteral("部署前清空远程");
        });
        auto* rebootCheck = findInRow<QCheckBox>(row2, [](QCheckBox* b) {
            return b->text() == QStringLiteral("部署后重启");
        });
        QVERIFY2(clearCheck, "『部署前清空远程』不在第 2 行");
        QVERIFY2(rebootCheck, "『部署后重启』不在第 2 行");

        // 重启四参：协议(Telnet/SSH)、命令(唯一 QLineEdit)、超时(上限 120)、重试(上限 3)
        auto* rebootCombo = findInRow<QComboBox>(row2, [](QComboBox* c) {
            return c->itemText(0) == QStringLiteral("Telnet");
        });
        QVERIFY2(rebootCombo, "重启协议下拉（首项 Telnet）不在第 2 行");
        auto* cmdEdit = findInRow<QLineEdit>(row2, [](QLineEdit* e) { return true; });
        QVERIFY2(cmdEdit, "重启命令输入框不在第 2 行");
        QVERIFY2(findInRow<QSpinBox>(row2, [](QSpinBox* s) { return s->maximum() == 120; }),
                 "超时 spin（上限 120）不在第 2 行");
        QVERIFY2(findInRow<QSpinBox>(row2, [](QSpinBox* s) { return s->maximum() == 3; }),
                 "重试 spin（上限 3）不在第 2 行");
        QVERIFY2(rowOfClass<QComboBox>(row2).size() == 1, "第 2 行的下拉应只有重启协议一个");
        QVERIFY2(rowOfClass<QSpinBox>(row2).size() == 2, "第 2 行的 spin 应只有超时/重试两个");

        auto* exportBtn = findInRow<QPushButton>(row2, [](QPushButton* b) {
            return b->text().contains(QStringLiteral("导出报告"));
        });
        auto* retryBtn = findInRow<QPushButton>(row2, [](QPushButton* b) {
            return b->text().contains(QStringLiteral("重试失败设备"));
        });
        QVERIFY2(exportBtn, "『导出报告』不在第 2 行");
        QVERIFY2(retryBtn, "『重试失败设备』不在第 2 行");
        QVERIFY2(separatorIn(row2), "第 2 行缺少组间分隔条 toolbarSeparator");
        // 反向：主操作不该下沉
        QVERIFY2(!rowContains(row2, w.findChild<QWidget*>(QStringLiteral("btnPrimary"))),
                 "▶ 部署 竟出现在第 2 行");
    }

    // 目的钉桩（不只钉形状）：两行的意义是让工具栏最小宽落进常见窄屏的可用宽之内。
    // 阈值推导：1366（常见窄屏）− 导航栏 56（src/ui/NavBar.cpp:20）
    //           − 右侧垂直布局左右 margin 8+8（src/app/DeviceForge.cpp:82）
    //           − 部署页布局左右 margin 12+12（FtpDeployWidget.cpp:130）= **1270**。
    // 变异取证（同一测法：Fusion 样式 + 生产 darkstyle.qss + 默认内容）：
    //   单行 totalMinimumSize=1866px → 两行 933px；无样式表时 834px（故设 qssFloorIsInEffect 守卫防降级假绿）。
    // 顺带实测否证了一个直觉担忧：把主按钮文案换成「▶ 部署到 24 台设备」后 totalMinimumSize 仍是 933px——
    //   QSS 的 min-width 决定最小宽，长文案只涨 sizeHint，不会把窗口顶宽，因此不为"现场台数"另设一格。
    void toolbarFitsNarrowScreen()
    {
        FtpDeployWidget w;
        auto* bar = toolbar(&w);
        QVERIFY2(bar, "未找到 deployToolbar 容器");
        QVERIFY(bar->layout());
        const int minW = bar->layout()->totalMinimumSize().width();
        QVERIFY2(minW > 0, "工具栏最小宽算出 0，布局未参与计算（用例失效信号）");
        QVERIFY2(minW <= 1270,
                 qPrintable(QStringLiteral("工具栏最小宽 %1px 仍超过 1366 屏可用宽 1270px").arg(minW)));
        qInfo().noquote() << QStringLiteral("toolbar totalMinimumSize=%1px (预算 1270)").arg(minW);

        auto* deploy = w.findChild<QWidget*>(QStringLiteral("btnPrimary"));
        QVERIFY(deploy);
        static_cast<QPushButton*>(deploy)->setText(QStringLiteral("▶ 部署到 24 台设备"));
        const int minWLabel = bar->layout()->totalMinimumSize().width();
        QVERIFY2(minWLabel <= 1270,
                 qPrintable(QStringLiteral("主按钮带台数文案后工具栏最小宽 %1px 超过 1270px").arg(minWLabel)));
    }
};

QTEST_MAIN(TstDeployToolbarLayout)
#include "tst_deploy_toolbar_layout.moc"
