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
};
QTEST_MAIN(TstQssPixels)
#include "tst_qss_pixels.moc"
