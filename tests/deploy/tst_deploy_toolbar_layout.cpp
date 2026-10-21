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
 *              v2.16-B 任务4b 追加：未选择设备＝没有目标（行为变更钉桩）——
 *              空选中点部署被拒绝（日志「未选择目标设备…」+ 按钮文案「▶ 部署（未选择设备）」），
 *              对照组（恰勾 1 台）走「Backend 未就绪」分支，证明两条分支各自可达。
 *              v2.16-B 任务7 追加：设备栏布局守卫（行最小宽 >0、2 台→80 台行高不换行，
 *              屏宽无关）+ 视觉稿/规格 §7 实测数字量测步 +「×」命中区在生产 QSS 下的
 *              像素级覆盖钉桩——本目标 Fusion + 生产 darkstyle.qss 口径，
 *              地板复用 qssFloorIsInEffect（无 QSS 时量到的都是默认风格，非用户所见）。
 */

#include <QtTest>

#include <functional>

#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMenu>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QStringList>
#include <QStyleFactory>
#include <QStyleOptionButton>
#include <QFontMetricsF>
#include <QPixmap>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>
#include <QDir>
#include <QFile>
#include <QUuid>
#include <cmath>
#include <climits>

#include "config/ConfigStore.h"
#include "tools/FtpDeployTool/FtpDeployWidget.h"
#include "ui/DeviceBusWidget.h"

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

// v2.16-B 任务4b 夹具：旧行设备（无注册表；选中键 = "ip:<地址>"，见
// DeviceBusWidget::makeSelectionKey）。ip 取互不前缀重叠的三段，保证过滤
// 「10.77.0.2」时子串匹配只命中一台（matchesFilter 为大小写不敏感子串式）。
DeviceInfo makeBusDevice(const QString& ip)
{
    DeviceInfo d;
    d.ip = ip.toStdString();
    d.port = 21;
    d.protocol = "ftp";
    return d;
}

// 含 needle 的日志行数（appendLog 每行带 [hh:mm:ss] 前缀，按子串判定）
int loggedCount(const QStringList& logs, const QString& needle)
{
    int n = 0;
    for (const QString& line : logs)
        if (line.contains(needle))
            ++n;
    return n;
}

// ---- v2.16-B 任务7 布局守卫夹具 ----
// n 台旧行设备（persist=false 不落库；alias 空 → chips 回落纯地址），
// ip=10.77.1.<101..180>：整数互不为子串（101..180 区间），过滤词取整条 ip 时
// matchesFilter 恰命中一台——"勾 2 台"用两条过滤词各勾一台（集合是并集）。
void fillBusWithLegacyDevices(DeviceBusWidget& bus, int n)
{
    for (int i = 1; i <= n; ++i)
        bus.addDevice(makeBusDevice(QStringLiteral("10.77.1.%1").arg(100 + i)), false);
}

// 经 deviceSearchEdit 过滤词勾选指定 ip（公开选择缝，同 oneSelectedDeviceReachesBackendGuard）
void selectDeviceByFilter(DeviceBusWidget& bus, const QString& ip)
{
    auto* search = bus.findChild<QLineEdit*>(QStringLiteral("deviceSearchEdit"));
    QVERIFY(search);
    search->setText(ip);
    bus.selectAllVisible();
}

// chips 宿主里第一枚未显式隐藏的胶囊（chips 形态下可见 == 已选）
QPushButton* firstVisibleChip(DeviceBusWidget& bus)
{
    const auto pills = bus.findChildren<QPushButton*>(QStringLiteral("devicePill"));
    for (QPushButton* pill : pills) {
        if (!pill->isHidden())
            return pill;
    }
    return nullptr;
}

// 胶囊离屏渲染后的**尾枚字形**水平包络 [left, right]（px，胶囊自身坐标）。
// 判据：与 #devicePill 暗底 #232A36 的 RGB 距离 >150 记为"墨像素"——文字
// #C8CCD4（距离 ~581）计入，石墨边框 #333B48（距离 ~51）与圆角外黑（~131）排除。
// 尾标 "  ×" 的两个前导空格无墨，从最右墨列向左回走、空列 >3 即停 ⇒ 得到
// 的是渲染后「×」字形（而非整个 caption）的左缘。这是"用户看得见的 ×"的
// 唯一机械化取法——QStyleSheetStyle 解析出的 font-size/padding 不流经任何
// QWidget 公共 API（任务5报告 §F3 口径限制），像素即事实。
bool scanCloseMarkEnvelope(QPushButton& pill, int* left, int* right)
{
    QPixmap pix(pill.size());
    pix.fill(Qt::transparent);
    pill.render(&pix);
    const QImage img = pix.toImage();
    const QColor bg(QStringLiteral("#232A36"));
    QVector<bool> inkColumn(img.width(), false);
    for (int x = 3; x < img.width() - 3; ++x) {          // 两侧留 3px 躲边框/圆角 AA
        for (int y = 4; y < img.height() - 4; ++y) {
            const QColor c = img.pixelColor(x, y);
            const int dist = qAbs(c.red() - bg.red()) + qAbs(c.green() - bg.green())
                             + qAbs(c.blue() - bg.blue());
            if (dist > 150) {
                inkColumn[x] = true;
                break;
            }
        }
    }
    int r = -1;
    for (int x = img.width() - 4; x >= 0; --x) {
        if (inkColumn[x]) { r = x; break; }
    }
    if (r < 0)
        return false;
    int l = r;
    int gap = 0;
    for (int x = r; x >= 0; --x) {
        if (inkColumn[x]) {
            l = x;
            gap = 0;
        } else if (++gap > 3) {
            break;
        }
    }
    *left = l;
    *right = r;
    return true;
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

    // ---- v2.16-B 任务4b：未选择设备＝没有目标（行为变更钉桩）----
    // 夹具要点（均为定死口径，缺一用例就假绿）：
    // ① 必须注入 DeviceBusWidget——m_deviceBus 默认空指针，不注入则点部署恒走
    //    「设备总线未关联」分支，与空选中拒绝无关；
    // ② 空选中拒绝检查排在 !m_backend 之前（onDeployClicked 定死顺序），
    //    所以本 UI-only 目标（m_backend 恒空）能走到拒绝分支；
    // ③ 对照组恰勾 1 台——若拒绝文案其实来自 backend 空指针分支，对照组
    //    的「Backend 未就绪」断言与实验组的「未出现 Backend 未就绪」断言同时失效。
    // 日志经 setGlobalLogCallback 采集（appendLog 唯一出口，无回调即静默）。

    // 实验组：3 台设备、一台不勾 → 点部署被拒，拒绝文案出自空选中分支；
    // 按钮文案为「▶ 部署（未选择设备）」（旧行为：空选中回退全量，文案显示 3 台）
    void emptySelectionRefusesDeployAndSaysSo()
    {
        DeviceBusWidget bus;
        FtpDeployWidget w;
        w.setDeviceBusWidget(&bus);              // m_deviceBus 注入（须在 addDevice 前，桩件走同一信号链）
        QStringList logs;
        w.setGlobalLogCallback([&logs](const QString& line) { logs << line; });

        // addDevice(persist=false) 不落库；新设备默认未选中（"新设备未选 → 先收起"）
        bus.addDevice(makeBusDevice(QStringLiteral("10.77.0.1")), false);
        bus.addDevice(makeBusDevice(QStringLiteral("10.77.0.2")), false);
        bus.addDevice(makeBusDevice(QStringLiteral("10.77.0.3")), false);
        QVERIFY2(bus.selectedDevices().empty(), "夹具失效：新设备本应未选中");
        // 硬线守卫：allDevices() 恒为全集（本任务只废"空选中回退全集"，不动全集本身）
        QCOMPARE(bus.allDevices().size(), std::size_t{3});

        auto* btn = w.findChild<QPushButton*>(QStringLiteral("btnPrimary"));
        QVERIFY(btn);
        QCOMPARE(btn->text(), QStringLiteral("▶ 部署（未选择设备）"));

        btn->click();

        QVERIFY2(loggedCount(logs, QStringLiteral("未选择目标设备，请在设备栏选择至少一台")) == 1,
                 qPrintable(QStringLiteral("空选中未见拒绝日志（或重复出现），日志=%1")
                                .arg(logs.join(QStringLiteral(" | ")))));
        // 拒绝必须出自空选中分支而不是 backend 空指针分支
        QCOMPARE(loggedCount(logs, QStringLiteral("Backend 未就绪")), 0);
        QCOMPARE(loggedCount(logs, QStringLiteral("设备总线未关联")), 0);
        // 被拒不落传输：后续执行路径日志一条都不该出现
        QCOMPARE(loggedCount(logs, QStringLiteral("请先在左侧本地面板中选中要部署的文件")), 0);
    }

    // 对照组：3 台设备、恰好勾 1 台（过滤只显该台 → selectAllVisible → 撤过滤）
    // → 点部署走「Backend 未就绪」（选中检查放行、后端判空兜底可达），
    // 按钮文案「▶ 部署到 1 台设备」（非空选中下新旧文案同值，本格钉台数口径，
    // 行为变更的判别力在日志断言与实验组）
    void oneSelectedDeviceReachesBackendGuard()
    {
        DeviceBusWidget bus;
        FtpDeployWidget w;
        w.setDeviceBusWidget(&bus);
        QStringList logs;
        w.setGlobalLogCallback([&logs](const QString& line) { logs << line; });

        bus.addDevice(makeBusDevice(QStringLiteral("10.77.0.1")), false);
        bus.addDevice(makeBusDevice(QStringLiteral("10.77.0.2")), false);
        bus.addDevice(makeBusDevice(QStringLiteral("10.77.0.3")), false);

        // 精确勾一台：隐藏 chips 不可 mouseClick，走公开选择缝——
        // deviceSearchEdit 文本直连 m_filterText（cpp:497-498），过滤到唯一命中后全选可见
        auto* search = bus.findChild<QLineEdit*>(QStringLiteral("deviceSearchEdit"));
        QVERIFY(search);
        search->setText(QStringLiteral("10.77.0.2"));
        bus.selectAllVisible();
        search->setText(QString());               // 撤过滤：只动可见集，不动已选
        QCOMPARE(bus.selectedIds().size(), 1);
        QCOMPARE(bus.selectedDevices().size(), std::size_t{1});

        auto* btn = w.findChild<QPushButton*>(QStringLiteral("btnPrimary"));
        QVERIFY(btn);
        QCOMPARE(btn->text(), QStringLiteral("▶ 部署到 1 台设备"));

        btn->click();

        QVERIFY2(loggedCount(logs, QStringLiteral("Backend 未就绪")) == 1,
                 qPrintable(QStringLiteral("勾 1 台后未落到 backend 判空分支，日志=%1")
                                .arg(logs.join(QStringLiteral(" | ")))));
        // 非空选中不得触发空选中拒绝（两条分支互斥即顺序正确的证明）
        QCOMPARE(loggedCount(logs, QStringLiteral("未选择目标设备，请在设备栏选择至少一台")), 0);
        QCOMPARE(loggedCount(logs, QStringLiteral("设备总线未关联")), 0);
    }

    // ---- v2.16-B 任务6：部署页 selectDeviceByIp（「打开这台设备」编排落在部署页的一步）----
    // 部署页设备下拉以纯 ip 为条目键（setDeviceBusWidget 里 addItem(d.ip)），选中键与
    // deviceopen::Route.ip 同源。本用例走与用户同一条 currentTextChanged 路径（实现
    // 不 blockSignals），命中→切到该 ip 并返回 true，未命中→不动并返回 false。
    void selectDeviceByIpSwitchesCombo()
    {
        DeviceBusWidget bus;
        FtpDeployWidget w;
        w.setDeviceBusWidget(&bus);
        QStringList logs;
        w.setGlobalLogCallback([&logs](const QString& line) { logs << line; });

        bus.addDevice(makeBusDevice(QStringLiteral("10.77.0.1")), false);
        bus.addDevice(makeBusDevice(QStringLiteral("10.77.0.2")), false);

        // 设备下拉按特征定位（min width 160，见 row1 用例同款判据），不加 objectName
        QComboBox* deviceCombo = nullptr;
        const auto combos = w.findChildren<QComboBox*>();
        for (QComboBox* c : combos) {
            if (c->minimumWidth() >= 160) { deviceCombo = c; break; }
        }
        QVERIFY2(deviceCombo, "未找到设备下拉（min width 160）");
        // 下拉以纯 ip 填充，条目键与 Route.ip 同源
        QCOMPARE(deviceCombo->count(), 2);
        QCOMPARE(deviceCombo->currentText(), QStringLiteral("10.77.0.1"));   // 首台

        // ---- 修复轮 1 Important 1：把「走用户路径发射 currentTextChanged」钉成载荷断言 ----
        // 判别力：blockSignals 变异（setCurrentIndex 前后屏蔽信号）下本 spy 恒 0 计数 → 必红；
        // 既有 currentText/返回值断言对该变异不可分辨（等价变异 M3 的补位）。填充期
        // blockSignals 发生在 spy 创建之前，故初始 0。
        QSignalSpy signalSpy(deviceCombo, &QComboBox::currentTextChanged);
        QVERIFY(signalSpy.isValid());
        QCOMPARE(signalSpy.count(), 0);

        // 右面板设备选择器 = 全页唯一可见的 panelDeviceCombo：左面板默认 local 源，
        // 其设备选择器随 setSourceProto("local") 隐藏（FileBrowserPanel.cpp:268），
        // 可见性判据是语义配对（右面板=部署目标浏览视图），不依赖 findChildren 顺序。
        // 它是 currentTextChanged lambda（FtpDeployWidget.cpp:619-622）第一跳的观测点：
        // 只换个字（blockSignals 形态）不会动它。
        QComboBox* rightPanelDeviceCombo = nullptr;
        const auto panelCombos = w.findChildren<QComboBox*>();
        for (QComboBox* c : panelCombos) {
            if (c->objectName() == QStringLiteral("panelDeviceCombo") && !c->isHidden())
                rightPanelDeviceCombo = c;
        }
        QVERIFY2(rightPanelDeviceCombo,
                 "未找到唯一可见的 panelDeviceCombo（右面板设备选择器；夹具失效信号）");
        QCOMPARE(rightPanelDeviceCombo->currentText(), QStringLiteral("10.77.0.1"));

        // 命中第二台 → 返回 true 且 currentText 切到该 ip
        QVERIFY(w.selectDeviceByIp(QStringLiteral("10.77.0.2")));
        QCOMPARE(deviceCombo->currentText(), QStringLiteral("10.77.0.2"));
        // 切到**不同**设备 → 用户路径必须恰发射一次 currentTextChanged（==0 即 blockSignals 降级）
        QCOMPARE(signalSpy.count(), 1);
        // 信号路径的下游副作用：右面板源设备真正跟随（「打开即就绪」的可观测半边）
        QCOMPARE(rightPanelDeviceCombo->currentText(), QStringLiteral("10.77.0.2"));

        // 未命中 → 返回 false 且 currentText 不变（不得回落到首台或清空）
        QVERIFY(!w.selectDeviceByIp(QStringLiteral("10.77.9.9")));
        QCOMPARE(deviceCombo->currentText(), QStringLiteral("10.77.0.2"));
        // 未命中不得发射（也不得动右面板）
        QCOMPARE(signalSpy.count(), 1);
        QCOMPARE(rightPanelDeviceCombo->currentText(), QStringLiteral("10.77.0.2"));
    }

    // ---- v2.16-B 任务7：设备栏布局守卫（屏宽无关，规格 §3.7；地板复用上方 qssFloorIsInEffect）----
    // 两条判据都与屏宽/折算枚数无关（tests/AGENTS.md 约定 6）：
    // ① 设备行 totalMinimumSize().width() > 0——量出 0 == 布局从未参与计算，是本用例
    //    自身的失效信号（同 toolbarFitsNarrowScreen 的口径）；
    // ② 已选 2 台 → 全选 80 台，设备栏 sizeHint().height() 一字不动——溢出走「+M 台」
    //    折算、**不换行**（规格 §2.3 单行 QHBoxLayout + §3.7）。可见枚数随宽度折叠是
    //    合法形态，故只断高度不变与"布局参与"，不断具体枚数（0 台/2 台/80 台三态都量）。
    //    ②′（修复轮1 Minor 5 补）同三态并判 mainV->totalMinimumSize().height()——
    //    sizeHint 不吃 children 的 setMinimumHeight（M-G2 已证盲），布局地板吃，两口径
    //    合起来才封住"长高"的两条通路。
    // bus.resize(1200,·) 是给 applyPillOverflow 一个确定的人造宽度让折叠发生；
    // 两条断言都不读该宽度——换任何宽度结论不变（枚数会变，但没人断枚数）。
    void deviceBarRowGuardMinWidthPositiveAndNoSecondRow()
    {
        DeviceBusWidget bus;
        bus.resize(1200, 160);
        fillBusWithLegacyDevices(bus, 80);
        QCOMPARE(bus.allDevices().size(), std::size_t{80});   // 全集硬线顺带再过一遍

        auto* mainV = qobject_cast<QVBoxLayout*>(bus.layout());
        QVERIFY2(mainV && mainV->count() == 2,
                 "DeviceBusWidget 主布局不再是『设备行 + 凭证行』两行结构");
        auto* deviceRow = qobject_cast<QHBoxLayout*>(mainV->itemAt(0)->layout());
        QVERIFY2(deviceRow, "设备栏第 1 行不是 QHBoxLayout——溢出折算依赖单行形态（规格 §2.3）");

        // 空态（0 台已选，提示可见）
        // 量测协议（修复轮 1 定）：每态读数前显式 invalidate+activate——children 的
        // setVisible/setMinimumHeight 只发 LayoutRequest（未 show 的 offscreen 件不投递），
        // totalMinimumSize 是带缓存的：初轮不激活连读三态得到的"恒 396"实为 setup 时
        // 缓存值（勘误，见修复报告）；激活后才是运行期事件循环将兑现的真地板。
        const auto activateLayout = [mainV]() {
            mainV->invalidate();
            mainV->activate();
        };
        activateLayout();
        const int minW0 = deviceRow->totalMinimumSize().width();
        QVERIFY2(minW0 > 0, "设备行最小宽算出 0，布局未参与计算（用例失效信号）");
        const int h0 = bus.sizeHint().height();
        QVERIFY2(h0 > 0, "设备栏 sizeHint 高度算出 0，布局未参与计算（用例失效信号）");
        // 判据 B′（修复轮 1，评审 Minor 5）：布局地板高度——sizeHint 不读 minimum
        // （变异 M-G2 用容器 setMinimumHeight(60) 证明过它盲），totalMinimumSize 会把
        // children 的 setMinimumHeight 计入行高地板 ⇒ 两口径并判才封住「长高不换行」
        // 的两条通路（生产折叠只动可见性，两个高度在 0/2/80 台三态都必须一字不动）。
        const int floorH0 = mainV->totalMinimumSize().height();
        QVERIFY2(floorH0 > 0, "设备栏布局最小高度算出 0，布局未参与计算（用例失效信号）");

        // 勾 2 台（两条过滤词各勾一台，集合为并集；撤过滤不动已选）
        selectDeviceByFilter(bus, QStringLiteral("10.77.1.101"));
        selectDeviceByFilter(bus, QStringLiteral("10.77.1.102"));
        auto* search = bus.findChild<QLineEdit*>(QStringLiteral("deviceSearchEdit"));
        QVERIFY(search);
        search->setText(QString());
        QCOMPARE(bus.selectedIds().size(), 2);
        activateLayout();
        const int minW2 = deviceRow->totalMinimumSize().width();
        QVERIFY2(minW2 > 0, "设备行最小宽算出 0，布局未参与计算（用例失效信号）");
        const int h2 = bus.sizeHint().height();
        QCOMPARE(h2, h0);
        const int floorH2 = mainV->totalMinimumSize().height();
        QVERIFY2(floorH2 == floorH0,
                 qPrintable(QStringLiteral("2 台已选让设备栏布局最小高度从 %1px 变到 %2px——"
                                            "尺寸地板不得随已选台数增长（规格 §2.3/§3.7）")
                                .arg(floorH0).arg(floorH2)));

        // 全选 80 台：行高不得长第二行
        bus.clearSelection();
        bus.selectAllVisible();
        QCOMPARE(bus.selectedIds().size(), 80);
        activateLayout();
        const int minW80 = deviceRow->totalMinimumSize().width();
        QVERIFY2(minW80 > 0, "设备行最小宽算出 0，布局未参与计算（用例失效信号）");
        const int h80 = bus.sizeHint().height();
        QVERIFY2(h80 == h2,
                 qPrintable(QStringLiteral("80 台全选的已选让设备栏长高了 %1px——溢出必须折进「+M 台」，"
                                            "不得换行（规格 §2.3/§3.7）").arg(h80 - h2)));
        const int floorH80 = mainV->totalMinimumSize().height();
        QVERIFY2(floorH80 == floorH2,
                 qPrintable(QStringLiteral("80 台全选（折叠生效）让设备栏布局最小高度从 %1px 变到 %2px——"
                                            "折叠只得改可见性，不得撑尺寸地板（M-G2 形态，规格 §2.3/§3.7）")
                                .arg(floorH2).arg(floorH80)));
        // 行宽随提示控件可见性换档（空态提示在、已选态收起）与「+M 台」按钮进/出行
        // 是**设计语义**（视觉稿状态①/②/④），非行数增长；修复轮 1 激活口径真值：
        // 空态 396（含提示 ~196 宽）、2 台 200、80 台 311（折叠生效 +M 台按钮进地板，
        // 通用节 min-width:85）——chips 本身依旧不进地板（min-width:0 可收缩，
        // §9.1a"逐个 setVisible 试到达标"的结构前提仍在：地板由固定控件决定）。
        qInfo().noquote() << QStringLiteral(
            "deviceBar guard measured (artificial width 1200, Fusion+darkstyle.qss, activated layout): "
            "rowTotalMinimumSize 0sel=%1px 2sel=%2px 80sel=%3px | bus sizeHint height=%4px/%5px/%6px "
            "| layout floor height=%7px/%8px/%9px")
            .arg(minW0).arg(minW2).arg(minW80).arg(h0).arg(h2).arg(h80)
            .arg(floorH0).arg(floorH2).arg(floorH80);
    }

    // ---- v2.16-B 任务7：「×」命中区在生产 QSS 形态下的钉桩（任务5报告 §F6.3 的"届时复核"）----
    // 任务5 移交的偏差口径是"QSS padding 8px vs PM_ButtonMargin 6px ⇒ 区左缘漂约 2px"。
    // 生产 QSS 加载后实测修正该口径（数字见本槽 qInfo 留证）：#devicePill 有 QSS padding 时
    // QStyleSheetStyle 把 PM_ButtonMargin 解析为 **0**（padding 接管了样式层边距），
    // 区宽 = ceil(尾标字宽 16) + 0 = 16px（默认样式下是 16+6=22，任务5 量的是那套）；
    // 画出的 × 包络 [W-17, W-9]（QSS padding 8 + border 1 把文字推离右缘），区 [W-16, W]
    // ⇒ 字形最左 1 列（× 左尖端）差 1px 出区，方向安全——落该区外的 release 是主体单击
    // ＝不改选择，不存在"误删"面。功能契约钉两条：① 区必须盖过 × 中心列（左半都不到
    // 就是真缺陷，先报告再动码）；② 真行为——× 中心 release 移出选择、主体 release 不移出。
    // 区宽按头注释登记的同款公式（胶囊 font() + PM_ButtonMargin）在测试侧独立复算
    // （pillCloseZoneWidth 是 private，不为此扩 API），行为部分走的是生产分派本体。
    void chipCloseZoneCoversDrawnMarkUnderProductionQss()
    {
        DeviceBusWidget bus;
        bus.resize(1000, 160);
        DeviceInfo d = makeBusDevice(QStringLiteral("10.77.9.77"));
        d.alias = "S7-1500-01";        // 有名档案形态：caption=名称，chip 文案 "S7-1500-01  ×"
        bus.addDevice(d, false);
        selectDeviceByFilter(bus, QStringLiteral("10.77.9.77"));
        QPushButton* pill = firstVisibleChip(bus);
        QVERIFY2(pill, "勾了 1 台却没有可见 chip（chips 只显已选形态失效）");
        pill->resize(pill->sizeHint());
        QVERIFY2(pill->width() >= 60,
                 qPrintable(QStringLiteral("chip 仅 %1px 宽，主体/×区没有可分辨的注入点（夹具失效信号）")
                                .arg(pill->width())));

        QStyleOptionButton opt;
        opt.initFrom(pill);
        const int pm = pill->style()->pixelMetric(QStyle::PM_ButtonMargin, &opt, pill);
        const int markAdv = int(std::ceil(
            QFontMetricsF(pill->font()).horizontalAdvance(QStringLiteral("  \xC3\x97"))));
        const int zone = markAdv + pm;

        int markLeft = 0;
        int markRight = 0;
        QVERIFY2(scanCloseMarkEnvelope(*pill, &markLeft, &markRight),
                 "离屏渲染没扫到任何墨像素——× 不可见？（渲染降级/样式未生效，取证失败而非通过）");
        const int markCenter = (markLeft + markRight) / 2;
        qInfo().noquote() << QStringLiteral(
            "close-zone under Fusion+darkstyle.qss: markAdvance=%1px PM_ButtonMargin=%2px zone=%3px "
            "| drawn mark x=[%4, %5] center=%6 pillWidth=%7 zoneLeftEdge=%8 | pillFont=%9")
            .arg(markAdv).arg(pm).arg(zone).arg(markLeft).arg(markRight)
            .arg(markCenter).arg(pill->width()).arg(pill->width() - zone)
            .arg(pill->font().toString());
        // 不变式（修复轮1 Minor/裁决④配套格）：区宽必须至少覆盖画出的 × 墨包络**宽度**
        // （markRight-markLeft+1 列）——这是对"区公式 ≥ 字形"的结构关系判定，与中心列
        // 落点无关；下方 tripwire 判"中心列在区内"（位置关系，余量仅 1px，字体/DPI
        // 变化敏感）。两格各管一类破坏：字体变大让字形宽超过区 ⇒ 本格红而 tripwire
        // 未必红；区左缘整体右漂 ⇒ tripwire 红。谁红都先报告再动码。
        QVERIFY2(zone >= (markRight - markLeft) + 1,
                 qPrintable(QStringLiteral("「×」命中区宽 %1px 盖不住画出的墨包络宽 %2px（[%3,%4]）——"
                                            "尾标实宽已超出区公式计算，区不再是「×」的超集"
                                            "——先报告再动码").arg(zone)
                                .arg(markRight - markLeft + 1).arg(markLeft).arg(markRight)));
        QVERIFY2(markCenter >= pill->width() - zone,
                 qPrintable(QStringLiteral("画出的 × 中心列 x=%1 落在命中区 [W-%2, W] 之外（W=%3）："
                                            "QSS padding 与 PM_ButtonMargin 的分叉已吃掉点击目标"
                                            "——先报告再动码").arg(markCenter).arg(zone)
                                .arg(pill->width())));

        // 真行为复核：主体（x=4）不移出，× 中心列 release 移出（生产分派，非测试复算公式）
        QSignalSpy broadcast(&bus, &DeviceBusWidget::deviceSelectionChanged);
        QTest::mouseClick(pill, Qt::LeftButton, Qt::NoModifier, QPoint(4, pill->height() / 2));
        QCOMPARE(bus.selectedDevices().size(), std::size_t{1});   // 主体单击不改选择
        QTest::mouseClick(pill, Qt::LeftButton, Qt::NoModifier,
                          QPoint(markCenter, pill->height() / 2));
        QVERIFY2(bus.selectedDevices().empty(),
                 qPrintable(QStringLiteral("release 落在画出的 × 中心（x=%1，W=%2，zone=%3）"
                                            "却没移出选择——真用户点 × 点不动").arg(markCenter)
                                .arg(pill->width()).arg(zone)));
        QCOMPARE(broadcast.count(), 1);   // 两次点击合计只广播 1 次（主体 0 + 移出 1）
    }

    // ---- v2.16-B 任务7：视觉稿注记段与规格 §7 的实测数字来源（量测步）----
    // 只断"量得出 + 结构关系"（pre-mod 裸铺 80 chips 的最小宽必然远超新形态行），
    // 具体数字全部 qInfo 留证后回填文档；回填后稿面不得再留同项估算值。
    void deviceBarVisualBaselineMeasurements()
    {
        // ① 改前基线：v2.15 形态复刻——80 枚 devicePill 裸 QHBoxLayout 平铺
        //   （文案=旧 pillCaption 无名称回落：纯地址 + 尾标 "  ×"）
        QWidget pre;
        auto* preRow = new QHBoxLayout(&pre);
        preRow->setContentsMargins(0, 2, 0, 2);
        preRow->setSpacing(4);
        for (int i = 1; i <= 80; ++i) {
            auto* pill = new QPushButton(QStringLiteral("10.77.1.%1  \xC3\x97").arg(100 + i), &pre);
            pill->setObjectName(QStringLiteral("devicePill"));
            pill->setFixedHeight(26);
            preRow->addWidget(pill);
        }
        preRow->invalidate();   // 与守卫槽同激活口径（修复轮 1）：读"布局将兑现"的地板
        preRow->activate();
        const int preMin = preRow->totalMinimumSize().width();
        QVERIFY2(preMin > 0, "改前形态复刻行最小宽为 0（量测失效信号）");

        // ② 改后：DeviceBusWidget 设备行（空态 / 3 chips 两态）
        DeviceBusWidget bus;
        bus.resize(1200, 160);
        fillBusWithLegacyDevices(bus, 80);
        auto* mainV = qobject_cast<QVBoxLayout*>(bus.layout());
        QVERIFY(mainV && mainV->count() == 2);
        auto* deviceRow = qobject_cast<QHBoxLayout*>(mainV->itemAt(0)->layout());
        QVERIFY(deviceRow);
        mainV->invalidate();
        mainV->activate();
        const int postMinEmpty = deviceRow->totalMinimumSize().width();
        selectDeviceByFilter(bus, QStringLiteral("10.77.1.101"));
        selectDeviceByFilter(bus, QStringLiteral("10.77.1.102"));
        selectDeviceByFilter(bus, QStringLiteral("10.77.1.103"));
        auto* search = bus.findChild<QLineEdit*>(QStringLiteral("deviceSearchEdit"));
        QVERIFY(search);
        search->setText(QString());
        QCOMPARE(bus.selectedIds().size(), 3);
        mainV->invalidate();
        mainV->activate();
        const int postMin3Chips = deviceRow->totalMinimumSize().width();
        QVERIFY2(postMin3Chips > 0, "改后设备行最小宽为 0（量测失效信号）");
        QVERIFY2(preMin > postMin3Chips,
                 qPrintable(QStringLiteral("改前裸铺 80 chips 最小宽 %1px 竟 ≤ 新形态 %2px——"
                                            "量测口径或形态复刻可疑，回填数字不可信")
                                .arg(preMin).arg(postMin3Chips)));

        // ③ 单枚 chip / 入口 / +M 台 / 搜索框 外框（sizeHint，生产 QSS 下）
        QPushButton chipA(QStringLiteral("S7-1500-01  \xC3\x97"));
        chipA.setObjectName(QStringLiteral("devicePill"));
        chipA.setFixedHeight(26);
        chipA.ensurePolished();
        QPushButton chipB(QStringLiteral("甲 ·10.0.0.7  \xC3\x97"));   // 重名 ·ip 尾标形态
        chipB.setObjectName(QStringLiteral("devicePill"));
        chipB.setFixedHeight(26);
        chipB.ensurePolished();
        QPushButton picker(QStringLiteral("设备 ▾ 已选 12 台"));
        picker.setObjectName(QStringLiteral("devicePickerBtn"));
        picker.setFixedHeight(26);
        picker.setMenu(new QMenu(&picker));   // 生产入口 setMenu——menu-indicator 占宽，桩件同形态
        picker.ensurePolished();
        QPushButton more(QStringLiteral("+8 台"));
        more.setObjectName(QStringLiteral("devicePillMore"));
        more.setFixedHeight(26);
        more.setMenu(new QMenu(&more));       // 「+M 台」挂的是同一个菜单实例（生产形态）
        more.ensurePolished();
        QLineEdit lineEdit;
        lineEdit.setObjectName(QStringLiteral("deviceSearchEdit"));
        lineEdit.setFixedHeight(26);
        lineEdit.setClearButtonEnabled(true);
        lineEdit.setPlaceholderText(QStringLiteral("搜索 名称 / IP / 标签"));
        lineEdit.ensurePolished();

        QVERIFY(chipA.sizeHint().width() > 0);
        QVERIFY(picker.sizeHint().width() > 0);
        // 修复轮1 Minor 3：量测数字随应用字体走（× 区用 pill->font()，#devicePill 的
        // font-size:12px 只在样式层生效、不落进 QWidget::font()），口径行必须记录
        // 运行时**实际**字体，而非誊写记忆值。
        qInfo().noquote() << QStringLiteral(
            "measurement fonts @ runtime: QApplication::font=%1 | chipA.font=%2 | primaryScreen logicalDpi=%3")
            .arg(QApplication::font().toString())
            .arg(chipA.font().toString())
            .arg(QGuiApplication::primaryScreen()
                     ? QString::number(QGuiApplication::primaryScreen()->logicalDotsPerInch())
                     : QStringLiteral("(no screen)"));
        qInfo().noquote() << QStringLiteral(
            "visual-baseline (Fusion+darkstyle.qss, 96dpi): preMod80PillRow=%1px | postRow minW empty=%2px "
            "3chips=%3px | chip \"S7-1500-01  x\"=%4px \"甲 ·10.0.0.7  x\"=%5px | picker=%6px more=%7px "
            "search=%8px")
            .arg(preMin).arg(postMinEmpty).arg(postMin3Chips)
            .arg(chipA.sizeHint().width()).arg(chipB.sizeHint().width())
            .arg(picker.sizeHint().width()).arg(more.sizeHint().width())
            .arg(lineEdit.sizeHint().width());
    }
};

QTEST_MAIN(TstDeployToolbarLayout)
#include "tst_deploy_toolbar_layout.moc"
