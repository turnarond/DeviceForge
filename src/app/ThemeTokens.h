/*
 * ThemeTokens.h — 双主题色值单源（Instrument 精密仪器视觉语言）
 *
 * 供 QSS 之外的代码（状态点、模型前景、富文本链接等）取色，避免色值散落。
 * 取值与设计文档一致：docs/03-设计/方案设计/2026-11-02-界面视觉语言重构设计.md §5.1
 *
 * 约定：
 *   - accent（浅 #0E766E / 深 #2DD4BF）只用于「此刻可动作/活跃」（青色是动词）
 *   - AccentInk 为主按钮文字色：浅色白字、深色深墨（深色亮青上白字对比度不足）
 *   - 枚举顺序固定（测试按 0..Warning 全遍历）；新增 token 加在 Warning 之后并同步测试上限
 */
#pragma once
#include <QColor>

namespace tokens {

enum class Token {
    Canvas,        // 窗口底 / 分割区背景
    Panel,         // 面板 / 表格 / 日志底
    Elevated,      // 表头 / 工具栏 / 提升面
    Input,         // 输入凹槽底
    Border,        // 普通边框 / 分割线
    BorderStrong,  // 控件边框
    Text,          // 主文字
    TextMuted,     // 次要文字 / 表头
    TextDim,       // 状态栏 / 占位 / 禁用
    Accent,        // 信号色：主按钮 / 焦点 / 选中 / 活跃
    AccentHover,
    AccentPressed,
    AccentSoft,    // 选中底（带 alpha）
    AccentInk,     // 主按钮文字色
    Success,
    Danger,
    Warning,       // 必须保持在枚举末尾（测试遍历上限）
};

inline QColor color(Token t, bool dark)
{
    static const QColor kLight[] = {
        QColor("#E9EDF0"), QColor("#FFFFFF"), QColor("#F7F9FA"), QColor("#F3F6F8"),
        QColor("#D3DAE0"), QColor("#B8C3CC"), QColor("#16222B"), QColor("#5B6B78"),
        QColor("#64707C"), QColor("#0E766E"), QColor("#0B5D56"), QColor("#094A45"),
        QColor(14, 118, 110, 26),  QColor("#FFFFFF"), QColor("#15803D"), QColor("#B91C1C"),
        QColor("#B45309"),
    };
    static const QColor kDark[] = {
        QColor("#0D1117"), QColor("#141B24"), QColor("#1B2430"), QColor("#0A0F15"),
        QColor("#263140"), QColor("#35465A"), QColor("#E6EDF3"), QColor("#8B98A8"),
        QColor("#7D8A99"), QColor("#2DD4BF"), QColor("#5CE8D5"), QColor("#14B8A6"),
        QColor(45, 212, 191, 31),  QColor("#062E2A"), QColor("#3FB950"), QColor("#F85149"),
        QColor("#D29922"),
    };
    const int i = int(t);
    const auto& table = dark ? kDark : kLight;
    if (i < 0 || i >= int(sizeof(kLight) / sizeof(kLight[0]))) return QColor();
    return table[i];
}

} // namespace tokens
