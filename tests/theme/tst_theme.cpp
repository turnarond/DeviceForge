#include <QtTest/QtTest>
#include <QFile>
#include "app/ThemeUtils.h"

class TstTheme : public QObject {
    Q_OBJECT
private slots:
    void qssPath_dark() {
        QCOMPARE(themeQssPath("dark"), QStringLiteral(":/styles/darkstyle.qss"));
    }
    void qssPath_light() {
        QCOMPARE(themeQssPath("light"), QStringLiteral(":/styles/darkstyle-light.qss"));
    }
    void qssPath_unknown_defaultsToDark() {
        QCOMPARE(themeQssPath("pink"), QStringLiteral(":/styles/darkstyle.qss"));
    }
    void lightQssResourceExists() {
        QFile f(QStringLiteral(":/styles/darkstyle-light.qss"));
        QVERIFY(f.open(QFile::ReadOnly | QFile::Text));
        QVERIFY(f.size() > 1000);  // 非空样式表
    }
};
QTEST_MAIN(TstTheme)
#include "tst_theme.moc"
