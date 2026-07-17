# UI 现代化改造设计文档

| 文档版本 | V1.1 |
| :--- | :--- |
| **项目名称** | DeployMaster UI 现代化 |
| **目标** | 将现有 Qt Widgets 界面改造为现代深色主题，并规划 Qt Quick 导航框架迁移 |
| **创建日期** | 2026-06-14 |
| **更新日期** | 2026-06-30（与实际代码同步） |
| **状态** | 第一阶段已完成，第二阶段推迟 |

---

## 1. 背景

### 1.1 当前问题

DeployMaster 作为工业级 PLC 部署运维工具，功能完善但界面存在以下问题：

| 问题 | 影响 |
|------|------|
| **视觉厚重** | 大量 QGroupBox 堆砌，间距紧凑，缺乏层次感 |
| **工具感强** | 纯 Widgets 实现，沿用默认 Fusion 主题，缺乏专业软件质感 |
| **信息密度过高** | 所有功能平铺在一个界面，7个 Tab 平级并列 |
| **缺乏视觉引导** | 无状态指示、进度反馈等高级交互元素 |

### 1.2 改造目标

- **短期目标**：通过 QDarkStyleSheet 快速实现深色主题，提升视觉专业度
- **中期目标**：重构主窗口导航为 Qt Quick，保留现有 C++ 业务逻辑
- **长期目标**：持续优化，支持深色/浅色主题切换，添加过渡动画

---

## 2. 技术方案

### 2.1 方案选型

| 方案 | 改动量 | 效果 | 费用 | 适用阶段 | 实际采用 |
|------|--------|------|------|----------|----------|
| ~~**QDarkStyleSheet**~~ | 极小 | 深色主题，组件高亮 | 免费 (MIT) | ~~第一阶段~~ | ❌ 改用自定义 QSS |
| **Qt Quick 混合架构** | 中等 | 原生现代化 UI | 免费 (Qt 内置) | 第二阶段 | ❌ 推迟 |
| **自定义 QSS** | 中等 | 高度定制 | 免费 | 第一阶段 | ✅ 已采用 |

### 2.2 推荐路径

```
✅ 第一阶段（已完成）         ⏸️ 第二阶段（推迟）        ⏸️ 第三阶段（待定）
    │                          │                         │
    ▼                          ▼                         ▼
自定义 darkstyle.qss ────► Qt Quick 导航框架 ──────► 主题切换 + 动画
    │                          │                         │
    │  已实现                   │  待规划                  │  远期目标
    │  :/styles/darkstyle.qss   │  需 Qt 6.4+             │
```

---

## 3. 第一阶段：自定义 darkstyle.qss（✅ 已完成）

> **实现决策**：最终采用自定义手写 `darkstyle.qss` 而非 QDarkStyleSheet 开源库。原因：需要针对工业运维工具的特定组件（QGroupBox 多面板、QSplitter、QTabWidget）进行精确样式控制，QDarkStyleSheet 的通用主题在此场景下表现欠佳。

### 3.1 实施步骤（实际执行）

**Step 1**: 创建自定义深色主题样式表

文件：`darkstyle.qss`（`D:\personal\QtDeployMaster\darkstyle.qss`）

主要设计规范：
- 背景色：`#1A1A2E`（深邃海军蓝）
- 文本色：`#D0D0D0`
- 强调色：`#3498DB`（蓝色高亮）
- 字体：Microsoft YaHei, Segoe UI, 9pt

**Step 2**: 在 `main.cpp` 中加载样式

```cpp
QFile styleFile(":/styles/darkstyle.qss");
if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
    QTextStream stream(&styleFile);
    QString styleSheet = stream.readAll();
    app.setStyleSheet(styleSheet);
    styleFile.close();
}
```

**Step 3**: 资源文件配置（`DeployMaster.qrc`）

```xml
<RCC version="1.0">
    <qresource prefix="/styles">
        <file>darkstyle.qss</file>
    </qresource>
</RCC>
```

### 3.2 实际效果

| 组件 | 实现方式 |
|------|----------|
| 主窗口背景 | `#1A1A2E` (深海军蓝) |
| QGroupBox | 深色面板 + 细边框 `#2C2C3E` |
| QPushButton | 深色背景 + `#3498DB` 蓝色高亮悬停 |
| QTableWidget | 深灰底 + `#3498DB` 高亮行 |
| QTextEdit | 深灰底 `#252540` |
| QTabWidget | 深色标签页 + 选中态底部蓝色边框 |
| QSplitter | 手柄宽度 5px，悬停变蓝 |

### 3.3 改动文件清单

| 文件 | 改动内容 |
|------|----------|
| `darkstyle.qss` | 新增，自定义深色主题样式表 |
| `main.cpp` | 加载样式表 `:/styles/darkstyle.qss` |
| `DeployMaster.qrc` | 添加 darkstyle.qss 到资源文件 |

---

## 4. 第二阶段：Qt Quick 导航框架（⏸️ 推迟）

> **状态**：推迟实施。当前优先完善 MVP+EventBus 架构重构和核心功能稳定性。QML 主窗口框架、侧边导航、QWidget 嵌入等方案保留为长期规划。

### 4.1 架构设计（规划）

```
┌──────────────────────────────────────────────────────────────┐
│                      QML 主窗口 (Main.qml)                     │
│                                                               │
│  ┌──────────────┐   ┌─────────────────────────────────────┐  │
│  │              │   │                                     │  │
│  │   侧边导航    │   │          内容区 (ContentArea)        │  │
│  │   (Sidebar)  │   │                                     │  │
│  │              │   │   ┌─────────────────────────────┐   │  │
│  │  ○ 部署      │   │   │                             │   │  │
│  │  ○ 命令      │   │   │   QWidget 嵌入区域           │   │  │
│  │  ○ 诊断      │   │   │   (现有 TabWidget 内容)      │   │  │
│  │  ○ 日志      │   │   │                             │   │  │
│  │  ○ Modbus    │   │   └─────────────────────────────┘   │  │
│  │  ○ OPC UA    │   │                                     │  │
│  │              │   │                                     │  │
│  └──────────────┘   └─────────────────────────────────────┘  │
│                                                               │
│  ┌──────────────────────────────────────────────────────────┐│
│  │                    状态栏 (StatusBar)                     ││
│  └──────────────────────────────────────────────────────────┘│
└──────────────────────────────────────────────────────────────┘
```

### 4.2 关键技术点

**QWidget 嵌入 QML**：

使用 `QQuickWidget` 或 `QWidget::createWindowContainer()` 将现有 QWidget 模块嵌入 QML：

```cpp
// C++ 侧：将 QWidget 暴露给 QML
QWidget* createDeployTabWidget() {
    return new DeployMasterContent(); // 现有的 centralwidget
}

// 注册到 QML
qmlRegisterSingletonType<QWidgetProxy>("AppWidgets", 1, 0, "WidgetProxy", 
    [](QQmlEngine* engine, QJSEngine*) -> QObject* {
        return new QWidgetProxy(engine);
    });
```

**QML 侧边导航**：

```qml
// Sidebar.qml
ListView {
    id: navList
    model: [
        { name: "部署", icon: "upload", target: "deploy" },
        { name: "命令", icon: "terminal", target: "telnet" },
        { name: "诊断", icon: "medical", target: "diagnostic" },
        { name: "日志", icon: "file", target: "logquery" },
        { name: "Modbus", icon: "database", target: "modbus" },
        { name: "OPC UA", icon: "network", target: "opcua" }
    ]
    
    delegate: NavItemDelegate {
        onClicked: root.currentModule = model.target
    }
}
```

### 4.3 渐进式迁移策略

| 阶段 | 改动内容 | 风险 |
|------|----------|------|
| **Phase 2.1** | 主窗口框架改为 QML，内容区嵌入现有 QWidget | 低 — 业务逻辑不变 |
| **Phase 2.2** | 侧边导航替代顶部 TabWidget | 低 — 导航逻辑简单 |
| **Phase 2.3** | 状态栏改为 QML | 低 |
| **Phase 2.4** | 单个 Tab 内容逐步改为 QML（可选） | 中 — 需重写 UI |

### 4.4 改动文件清单

| 文件 | 改动内容 |
|------|----------|
| `CMakeLists.txt` | 添加 Qt Quick 模块依赖 |
| `main.cpp` | 创建 QQmlApplicationEngine，加载 QML |
| `qml/Main.qml` | 新增，主窗口 QML 定义 |
| `qml/Sidebar.qml` | 新增，侧边导航组件 |
| `qml/ContentArea.qml` | 新增，内容区容器 |
| `QWidgetProxy.h/cpp` | 新增，QWidget 嵌入代理类 |

---

## 5. 第三阶段：持续优化（⏸️ 远期规划）

### 5.1 主题切换

```qml
// ThemeManager.qml
QtObject {
    id: themeManager
    property bool darkMode: true
    
    function toggleTheme() {
        darkMode = !darkMode
        // 通知 C++ 侧切换样式表
        ThemeSwitcher.setDarkMode(darkMode)
    }
}
```

### 5.2 动画效果

```qml
// 导航切换动画
Behavior on currentModule {
    enabled: true
    NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
}

// 按钮悬停效果
Button {
    hoverEnabled: true
    Behavior on scale {
        NumberAnimation { duration: 100 }
    }
}
```

---

## 6. 风险与约束

### 6.1 技术风险

| 风险 | 缓解措施 |
|------|----------|
| QDarkStyleSheet 与自定义样式冲突 | 使用 `!important` 或调整优先级 |
| QWidget 嵌入 QML 性能问题 | 限制嵌入数量，使用异步加载 |
| Qt Quick 与 Widgets 事件传递 | 使用信号槽桥接，避免直接调用 |

### 6.2 约束条件

- **Qt 版本**：需 Qt 6.4+ 以获得最佳 Qt Quick Controls 2 效果
- **编译器**：MSVC 2022 或 MinGW (支持 C++17)
- **平台**：Windows 10/11 优先，Linux 兼容

---

## 7. 实施优先级

| 优先级 | 任务 | 预计时间 | 实际状态 |
|--------|------|----------|----------|
| **P0** | 第一阶段：自定义 darkstyle.qss 改造 | 已完成 | ✅ 已实现 |
| **P1** | 第二阶段 Phase 2.1-2.2：Qt Quick 导航框架 | 1-2 周 | ⏸️ 推迟 |
| **P2** | 第三阶段：主题切换 | 可延后 | ⏸️ 远期规划 |
| **P3** | 单个 Tab 内容 QML 化 | 可选，按需实施 | ⏸️ 远期规划 |

---

## 8. 附录

### 8.1 QDarkStyleSheet 获取方式

- GitHub: https://github.com/ColinDuquesnoy/QDarkStyleSheet
- 直接下载 `qdarkstyle.qss` 文件即可使用

### 8.2 Qt Quick Controls 2 Material 风格

```qml
// 配置 Material 飾格
import QtQuick.Controls.Material 2.15

ApplicationWindow {
    Material.theme: Material.Dark
    Material.accent: Material.Blue
}
```

---

## 9. 待确认事项

- [ ] 确认 Qt 版本是否满足 6.4+ 要求
- [ ] 确认是否需要保留浅色主题切换能力
- [ ] 确认第二阶段是否立即实施或延后

---

**文档审核状态**：待用户审核