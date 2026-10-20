#include <QtTest/QtTest>
#include <QFile>
#include <QGroupBox>
#include <QImage>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QWidget>

// 双主题像素验证（#18）：
//   1) 色值字符串断言（渲染降级保底）——块级解析 QSS，确认关键选择器内色值与设计稿一致；
//   2) 离屏渲染采样——加载双 QSS → 渲染关键控件到 QPixmap → 采样中心像素断言色值。
//   断言项：面板背景 / 导航栏 / 按钮主色 / 进度条（暗/亮各一份）。
class TstQssPixels : public QObject {
    Q_OBJECT

private:
    // 渲染控件到离屏 pixmap 并采样 (x, y) 像素（隐藏控件 render 即可，无需 show）
    static QColor samplePixel(QWidget* w, int x, int y) {
        w->ensurePolished();
        QPixmap pm(w->size());
        pm.fill(Qt::transparent);
        w->render(&pm);
        return pm.toImage().pixelColor(x, y);
    }

    // 块级解析：找到 selector 后首个 {..} 块，断言其中包含 fragment（防同名色值串匹配到别的规则）
    static bool blockContains(const QString& qss, const QString& selector, const QString& fragment) {
        const int sel = qss.indexOf(selector);
        if (sel < 0) return false;
        const int brace = qss.indexOf('{', sel);
        if (brace < 0) return false;
        const int close = qss.indexOf('}', brace);
        if (close < 0) return false;
        return qss.mid(brace, close - brace).contains(fragment);
    }

    static QString readQss(const QString& path) {
        QFile f(path);
        if (!f.open(QFile::ReadOnly | QFile::Text)) return QString();
        return QString::fromUtf8(f.readAll());
    }

    // v2.16-B 任务7 样式口径守卫：本版设备栏**每主题最多新增一行** QSS（裁定③/规格 §2.3）——
    // 空态提示 `QLabel#deviceBusHint` 之外不得再为设备栏发明专属规则。判据：
    // ① deviceBusHint 选择器全主题恰出现一次（注释里也不许写这个名字，写进注释=分叉预备）；
    // ② 其余四个对象名（selectedPillHost/devicePickerBtn/devicePillMore/deviceSearchEdit）
    //    零命中——chips 沿用现成 #devicePill 节，入口/「+M 台」沿用通用 QPushButton 节
    //   （min-width:85px 够用，第二版稿已核），搜索框沿用通用 QLineEdit 节。
    static void assertDeviceBarQssBudget(const QString& qss) {
        QCOMPARE(qss.count(QStringLiteral("deviceBusHint")), 1);
        QVERIFY2(!qss.contains(QStringLiteral("selectedPillHost")),
                 "为 selectedPillHost 发明了专属规则——本版口径是至多一行 deviceBusHint");
        QVERIFY2(!qss.contains(QStringLiteral("devicePickerBtn")),
                 "为 devicePickerBtn 发明了专属规则——入口应按通用 QPushButton 节渲染");
        QVERIFY2(!qss.contains(QStringLiteral("devicePillMore")),
                 "为 devicePillMore 发明了专属规则——「+M 台」应按通用 QPushButton 节渲染");
        QVERIFY2(!qss.contains(QStringLiteral("deviceSearchEdit")),
                 "为 deviceSearchEdit 发明了专属规则——搜索框应按通用 QLineEdit 节渲染");
    }

private slots:
    // ===== 色值字符串断言（CTest 渲染不可用的保底） =====

    void qssStrings_dark() {
        const QString qss = readQss(QStringLiteral(":/styles/darkstyle.qss"));
        QVERIFY(!qss.isEmpty());
        QVERIFY(blockContains(qss, QStringLiteral("#panelBreadcrumb"), QStringLiteral("#7B8494")));
        QVERIFY(blockContains(qss, QStringLiteral("#navBar"), QStringLiteral("#0E1219")));
        QVERIFY(blockContains(qss, QStringLiteral("QPushButton#btnPrimary"), QStringLiteral("#F0A030")));
        QVERIFY(blockContains(qss, QStringLiteral("QProgressBar::chunk"), QStringLiteral("#40C8A0")));
    }

    void qssStrings_light() {
        const QString qss = readQss(QStringLiteral(":/styles/darkstyle-light.qss"));
        QVERIFY(!qss.isEmpty());
        QVERIFY(blockContains(qss, QStringLiteral("#panelBreadcrumb"), QStringLiteral("#6B7480")));
        QVERIFY(blockContains(qss, QStringLiteral("#navBar"), QStringLiteral("#ECEFF3")));
        QVERIFY(blockContains(qss, QStringLiteral("QPushButton#btnPrimary"), QStringLiteral("#D48820")));
        QVERIFY(blockContains(qss, QStringLiteral("QProgressBar::chunk"), QStringLiteral("#2FA88A")));
    }

    // ===== 离屏渲染像素采样 =====

    void pixelSample_dark() {
        const QString qss = readQss(QStringLiteral(":/styles/darkstyle.qss"));
        QVERIFY(!qss.isEmpty());
        qApp->setStyleSheet(qss);

        // 面板背景（QGroupBox = 面板/容器 #141820）
        QGroupBox panel;
        panel.resize(200, 80);
        QCOMPARE(samplePixel(&panel, 100, 40), QColor(QStringLiteral("#141820")));

        // 导航栏（#navBar 规则）
        QWidget navBar;
        navBar.setObjectName(QStringLiteral("navBar"));
        navBar.resize(120, 240);
        QCOMPARE(samplePixel(&navBar, 60, 120), QColor(QStringLiteral("#0E1219")));

        // 主按钮（琴色填充；空文本避免中心像素命中字形）
        QPushButton btn;
        btn.setObjectName(QStringLiteral("btnPrimary"));
        btn.resize(120, 36);
        QCOMPARE(samplePixel(&btn, 60, 18), QColor(QStringLiteral("#F0A030")));

        // 进度条（chunk 青绿；满值 + 关文本，中心像素稳定落在 chunk 上）
        QProgressBar bar;
        bar.resize(200, 20);
        bar.setTextVisible(false);
        bar.setValue(100);
        QCOMPARE(samplePixel(&bar, 100, 10), QColor(QStringLiteral("#40C8A0")));
    }

    void pixelSample_light() {
        const QString qss = readQss(QStringLiteral(":/styles/darkstyle-light.qss"));
        QVERIFY(!qss.isEmpty());
        qApp->setStyleSheet(qss);

        QGroupBox panel;
        panel.resize(200, 80);
        QCOMPARE(samplePixel(&panel, 100, 40), QColor(QStringLiteral("#FFFFFF")));

        QWidget navBar;
        navBar.setObjectName(QStringLiteral("navBar"));
        navBar.resize(120, 240);
        QCOMPARE(samplePixel(&navBar, 60, 120), QColor(QStringLiteral("#ECEFF3")));

        QPushButton btn;
        btn.setObjectName(QStringLiteral("btnPrimary"));
        btn.resize(120, 36);
        QCOMPARE(samplePixel(&btn, 60, 18), QColor(QStringLiteral("#D48820")));

        QProgressBar bar;
        bar.resize(200, 20);
        bar.setTextVisible(false);
        bar.setValue(100);
        QCOMPARE(samplePixel(&bar, 100, 10), QColor(QStringLiteral("#2FA88A")));
    }

    // ===== v2.16-B 任务7：设备栏空态提示样式（每主题恰一行 QLabel#deviceBusHint） =====
    // 提示语「未选择设备——点「设备 ▾」选目标」（src/ui/DeviceBusWidget.cpp:475-477）
    // 既无"提示"也无"拖拽"，QLabel[text*=…]（:22-25）命中不了；不补行就按通用
    // QLabel 主文字色渲染（暗 #C8CCD4/亮 #2A2F38），比主内容还亮。色值取次级文字
    // 现成用法（暗 :23/:28 = #7B8494；亮 :23/:28 = #6B7480），不是新 token。

    void deviceBusHintQss_dark() {
        const QString qss = readQss(QStringLiteral(":/styles/darkstyle.qss"));
        QVERIFY(!qss.isEmpty());
        QVERIFY2(blockContains(qss, QStringLiteral("QLabel#deviceBusHint"),
                               QStringLiteral("#7B8494")),
                 "darkstyle.qss 缺 QLabel#deviceBusHint { color:#7B8494; }（次级文字色，现成用法 :23/:28）");
        assertDeviceBarQssBudget(qss);
    }

    void deviceBusHintQss_light() {
        const QString qss = readQss(QStringLiteral(":/styles/darkstyle-light.qss"));
        QVERIFY(!qss.isEmpty());
        QVERIFY2(blockContains(qss, QStringLiteral("QLabel#deviceBusHint"),
                               QStringLiteral("#6B7480")),
                 "darkstyle-light.qss 缺 QLabel#deviceBusHint { color:#6B7480; }（次级文字色，现成用法 :23/:28）");
        assertDeviceBarQssBudget(qss);
    }
};
QTEST_MAIN(TstQssPixels)
#include "tst_qss_pixels.moc"
