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
    // 全 token 值锁定（设计 §5.1 两表逐字）：防止 token 表与实现漂移
    void allTokenValuesMatchDesign_data()
    {
        QTest::addColumn<int>("token");
        QTest::addColumn<bool>("dark");
        QTest::addColumn<QString>("expected");
        struct { int t; bool d; const char* v; } rows[] = {
            {int(tokens::Token::Canvas), false, "#E9EDF0"}, {int(tokens::Token::Panel), false, "#FFFFFF"},
            {int(tokens::Token::Elevated), false, "#F7F9FA"}, {int(tokens::Token::Input), false, "#F3F6F8"},
            {int(tokens::Token::Border), false, "#D3DAE0"}, {int(tokens::Token::BorderStrong), false, "#B8C3CC"},
            {int(tokens::Token::Text), false, "#16222B"}, {int(tokens::Token::TextMuted), false, "#5B6B78"},
            {int(tokens::Token::TextDim), false, "#64707C"}, {int(tokens::Token::Accent), false, "#0E766E"},
            {int(tokens::Token::AccentHover), false, "#0B5D56"}, {int(tokens::Token::AccentPressed), false, "#094A45"},
            {int(tokens::Token::AccentInk), false, "#FFFFFF"}, {int(tokens::Token::Success), false, "#15803D"},
            {int(tokens::Token::Danger), false, "#B91C1C"}, {int(tokens::Token::Warning), false, "#B45309"},
            {int(tokens::Token::Canvas), true, "#0D1117"}, {int(tokens::Token::Panel), true, "#141B24"},
            {int(tokens::Token::Elevated), true, "#1B2430"}, {int(tokens::Token::Input), true, "#0A0F15"},
            {int(tokens::Token::Border), true, "#263140"}, {int(tokens::Token::BorderStrong), true, "#35465A"},
            {int(tokens::Token::Text), true, "#E6EDF3"}, {int(tokens::Token::TextMuted), true, "#8B98A8"},
            {int(tokens::Token::TextDim), true, "#7D8A99"}, {int(tokens::Token::Accent), true, "#2DD4BF"},
            {int(tokens::Token::AccentHover), true, "#5CE8D5"}, {int(tokens::Token::AccentPressed), true, "#14B8A6"},
            {int(tokens::Token::AccentInk), true, "#062E2A"}, {int(tokens::Token::Success), true, "#3FB950"},
            {int(tokens::Token::Danger), true, "#F85149"}, {int(tokens::Token::Warning), true, "#D29922"},
        };
        for (const auto& r : rows)
            QTest::newRow(qPrintable(QStringLiteral("%1/%2").arg(r.v).arg(r.d ? "dark" : "light")))
                << r.t << r.d << QString::fromLatin1(r.v);
    }
    void allTokenValuesMatchDesign()
    {
        QFETCH(int, token);
        QFETCH(bool, dark);
        QFETCH(QString, expected);
        QCOMPARE(tokens::color(tokens::Token(token), dark), QColor(expected));
    }

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
