#pragma once
#include <QString>

// 主题工具：QSS 资源路径映射（暗色/亮色）
inline QString themeQssPath(const QString& theme) {
    if (theme == QLatin1String("light")) return QStringLiteral(":/styles/darkstyle-light.qss");
    return QStringLiteral(":/styles/darkstyle.qss");  // 默认暗色
}
