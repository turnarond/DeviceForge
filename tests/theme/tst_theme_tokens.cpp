#include <QtTest/QtTest>
#include <QColor>
#include "app/ThemeTokens.h"

// ThemeTokens 双主题色值单源（Instrument 精密仪器）：
// 断言两套主题的 token 值与设计 §5.1 一致；全枚举双主题可解析。
class TstThemeTokens : public QObject {
    Q_OBJECT
private slots:
    void lightCanvasIsColdGrey()      { QCOMPARE(tokens::color(tokens::Token::Canvas, false), QColor("#E9EDF0")); }
    void lightAccentIsDeepTeal()      { QCOMPARE(tokens::color(tokens::Token::Accent, false), QColor("#0E766E")); }
    void lightAccentInkIsWhite()      { QCOMPARE(tokens::color(tokens::Token::AccentInk, false), QColor("#FFFFFF")); }
    void darkAccentIsBrightTeal()     { QCOMPARE(tokens::color(tokens::Token::Accent, true),  QColor("#2DD4BF")); }
    void darkAccentInkIsDeepInk()     { QCOMPARE(tokens::color(tokens::Token::AccentInk, true), QColor("#062E2A")); }
    void darkPanelIsGreyBlue()        { QCOMPARE(tokens::color(tokens::Token::Panel, true),   QColor("#141B24")); }
    void warningExistsInBothThemes()  { QVERIFY(tokens::color(tokens::Token::Warning, false).isValid());
                                        QVERIFY(tokens::color(tokens::Token::Warning, true).isValid()); }
    void allTokensResolveInBothThemes()
    {
        for (int i = 0; i <= int(tokens::Token::Warning); ++i) {
            QVERIFY2(tokens::color(tokens::Token(i), false).isValid(), "light token invalid");
            QVERIFY2(tokens::color(tokens::Token(i), true).isValid(),  "dark token invalid");
        }
    }
};
QTEST_MAIN(TstThemeTokens)
#include "tst_theme_tokens.moc"
