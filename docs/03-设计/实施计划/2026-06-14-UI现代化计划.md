# UI 现代化改造实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 DeployMaster 界面从浅色 Fusion 主题改造为现代深色主题

**Architecture:** 通过 QSS 样式表全局应用深色主题，不修改业务逻辑代码，仅调整 main.cpp 和资源文件

**Tech Stack:** Qt 6 Widgets + QSS (Qt Style Sheet)

---

## 文件结构

| 文件 | 操作 | 职责 |
|------|------|------|
| `darkstyle.qss` | 新增 | 深色主题样式定义 |
| `DeployMaster.qrc` | 修改 | 添加样式文件到资源 |
| `main.cpp` | 修改 | 加载样式表 |
| `CMakeLists.txt` | 无需修改 | 已配置 AUTORCC |

---

## Task 1: 创建深色主题样式文件

**Files:**
- Create: `d:\personal\QtDeployMaster\darkstyle.qss`

- [ ] **Step 1: 创建 darkstyle.qss 文件**

```qss
/* DeployMaster Dark Theme - 工业级深色主题 */

/* 全局背景和文字 */
QWidget {
    background-color: #1E1E1E;
    color: #E0E0E0;
    font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
    font-size: 9pt;
}

/* 主窗口 */
QMainWindow {
    background-color: #1E1E1E;
}

/* 分组框 */
QGroupBox {
    background-color: #252526;
    border: 1px solid #3C3C3C;
    border-radius: 4px;
    margin-top: 12px;
    padding-top: 10px;
    font-weight: bold;
}

QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 10px;
    padding: 0 5px;
    color: #569CD6;
}

/* 按钮 */
QPushButton {
    background-color: #0E639C;
    border: 1px solid #007ACC;
    border-radius: 4px;
    padding: 6px 12px;
    color: #FFFFFF;
    min-width: 80px;
}

QPushButton:hover {
    background-color: #1177BB;
    border-color: #1B9DDD;
}

QPushButton:pressed {
    background-color: #005A9E;
}

QPushButton:disabled {
    background-color: #2D2D30;
    border-color: #3C3C3C;
    color: #656565;
}

/* 输入框 */
QLineEdit, QTextEdit, QPlainTextEdit {
    background-color: #1E1E1E;
    border: 1px solid #3C3C3C;
    border-radius: 3px;
    padding: 4px;
    color: #E0E0E0;
    selection-background-color: #264F78;
}

QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus {
    border-color: #007ACC;
}

/* 下拉框 */
QComboBox {
    background-color: #252526;
    border: 1px solid #3C3C3C;
    border-radius: 3px;
    padding: 4px 8px;
    color: #E0E0E0;
    min-width: 100px;
}

QComboBox:hover {
    border-color: #007ACC;
}

QComboBox::drop-down {
    border: none;
    width: 20px;
}

QComboBox QAbstractItemView {
    background-color: #252526;
    border: 1px solid #3C3C3C;
    selection-background-color: #264F78;
}

/* 列表控件 */
QListWidget, QTreeWidget, QTableWidget {
    background-color: #1E1E1E;
    border: 1px solid #3C3C3C;
    border-radius: 3px;
    color: #E0E0E0;
    gridline-color: #3C3C3C;
}

QListWidget::item, QTreeWidget::item, QTableWidget::item {
    padding: 4px;
}

QListWidget::item:selected, QTreeWidget::item:selected, QTableWidget::item:selected {
    background-color: #264F78;
    color: #FFFFFF;
}

QListWidget::item:hover, QTreeWidget::item:hover {
    background-color: #2A2D2E;
}

QTableWidget {
    alternate-background-color: #252526;
}

/* 表头 */
QHeaderView::section {
    background-color: #252526;
    border: 1px solid #3C3C3C;
    padding: 4px;
    color: #E0E0E0;
    font-weight: bold;
}

/* 标签页 */
QTabWidget::pane {
    background-color: #1E1E1E;
    border: 1px solid #3C3C3C;
    border-radius: 3px;
}

QTabBar::tab {
    background-color: #2D2D30;
    border: 1px solid #3C3C3C;
    border-bottom: none;
    border-radius: 3px 3px 0 0;
    padding: 6px 12px;
    color: #E0E0E0;
    min-width: 80px;
}

QTabBar::tab:selected {
    background-color: #1E1E1E;
    border-color: #007ACC;
    color: #FFFFFF;
}

QTabBar::tab:hover:!selected {
    background-color: #3E3E42;
}

/* 滚动条 */
QScrollBar:vertical {
    background-color: #1E1E1E;
    width: 12px;
    border-radius: 6px;
}

QScrollBar::handle:vertical {
    background-color: #3C3C3C;
    min-height: 30px;
    border-radius: 6px;
}

QScrollBar::handle:vertical:hover {
    background-color: #4A4A4D;
}

QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0px;
}

QScrollBar:horizontal {
    background-color: #1E1E1E;
    height: 12px;
    border-radius: 6px;
}

QScrollBar::handle:horizontal {
    background-color: #3C3C3C;
    min-width: 30px;
    border-radius: 6px;
}

QScrollBar::handle:horizontal:hover {
    background-color: #4A4A4D;
}

QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0px;
}

/* 进度条 */
QProgressBar {
    background-color: #1E1E1E;
    border: 1px solid #3C3C3C;
    border-radius: 3px;
    text-align: center;
    color: #E0E0E0;
}

QProgressBar::chunk {
    background-color: #007ACC;
    border-radius: 2px;
}

/* 复选框和单选按钮 */
QCheckBox, QRadioButton {
    color: #E0E0E0;
    spacing: 8px;
}

QCheckBox::indicator, QRadioButton::indicator {
    width: 16px;
    height: 16px;
    border: 1px solid #3C3C3C;
    background-color: #1E1E1E;
}

QCheckBox::indicator:checked, QRadioButton::indicator:checked {
    background-color: #007ACC;
    border-color: #007ACC;
}

QCheckBox::indicator:hover, QRadioButton::indicator:hover {
    border-color: #1B9DDD;
}

/* 数值输入框 */
QSpinBox, QDoubleSpinBox {
    background-color: #1E1E1E;
    border: 1px solid #3C3C3C;
    border-radius: 3px;
    padding: 4px;
    color: #E0E0E0;
}

QSpinBox:focus, QDoubleSpinBox:focus {
    border-color: #007ACC;
}

QSpinBox::up-button, QSpinBox::down-button,
QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
    background-color: #252526;
    border: none;
    width: 16px;
}

QSpinBox::up-button:hover, QSpinBox::down-button:hover,
QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover {
    background-color: #3C3C3C;
}

/* 滑块 */
QSlider::groove:horizontal {
    background-color: #3C3C3C;
    height: 4px;
    border-radius: 2px;
}

QSlider::handle:horizontal {
    background-color: #007ACC;
    width: 14px;
    height: 14px;
    margin: -5px 0;
    border-radius: 7px;
}

QSlider::handle:horizontal:hover {
    background-color: #1B9DDD;
}

/* 工具提示 */
QToolTip {
    background-color: #252526;
    border: 1px solid #3C3C3C;
    padding: 4px;
    color: #E0E0E0;
}

/* 菜单 */
QMenu {
    background-color: #252526;
    border: 1px solid #3C3C3C;
    color: #E0E0E0;
}

QMenu::item {
    padding: 6px 24px;
}

QMenu::item:selected {
    background-color: #264F78;
}

/* 分割器 */
QSplitter::handle {
    background-color: #3C3C3C;
}

/* 状态栏 */
QStatusBar {
    background-color: #007ACC;
    color: #FFFFFF;
}

/* 框架 */
QFrame {
    background-color: #1E1E1E;
}
```

---

## Task 2: 更新资源文件

**Files:**
- Modify: `d:\personal\QtDeployMaster\DeployMaster.qrc`

- [ ] **Step 1: 添加样式文件到资源文件**

将 `DeployMaster.qrc` 内容修改为：

```xml
<RCC>
    <qresource prefix="DeployMaster">
    </qresource>
    <qresource prefix="styles">
        <file>darkstyle.qss</file>
    </qresource>
</RCC>
```

---

## Task 3: 修改 main.cpp 加载样式表

**Files:**
- Modify: `d:\personal\QtDeployMaster\main.cpp`

- [ ] **Step 1: 添加样式表加载代码**

将 `main.cpp` 内容修改为：

```cpp
#include "DeployMaster.h"
#include <QtWidgets/QApplication>
#include <QFile>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    // 加载深色主题样式表
    QFile styleFile(":/styles/darkstyle.qss");
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        QTextStream stream(&styleFile);
        QString styleSheet = stream.readAll();
        app.setStyleSheet(styleSheet);
        styleFile.close();
    }
    
    DeployMaster window;
    window.show();
    return app.exec();
}
```

---

## Task 4: 编译验证

**Files:**
- 无文件修改，仅编译测试

- [ ] **Step 1: 清理并重新生成构建文件**

Run: `cmake -B build -G "Visual Studio 17 2022"`
Expected: CMake 配置成功，无错误

- [ ] **Step 2: 编译项目**

Run: `cmake --build build --config Release`
Expected: 编译成功，生成 DeployMaster.exe

- [ ] **Step 3: 运行程序验证样式**

Run: `./build/Release/DeployMaster.exe`
Expected: 界面显示深色主题，背景为 #1E1E1E，按钮为蓝色高亮

---

## Task 5: 提交改动

**Files:**
- 无文件修改，仅 Git 操作

- [ ] **Step 1: 查看改动状态**

Run: `git status`
Expected: 显示 darkstyle.qss (new), DeployMaster.qrc (modified), main.cpp (modified)

- [ ] **Step 2: 添加文件到暂存区**

Run: `git add darkstyle.qss DeployMaster.qrc main.cpp`

- [ ] **Step 3: 提交改动**

Run: `git commit -m "feat(ui): 添加深色主题样式表，实现界面现代化第一阶段"`
Expected: 提交成功

---

## 自检清单

| 检查项 | 状态 |
|--------|------|
| Spec 覆盖 | ✅ 第一阶段 QDarkStyleSheet 改造已覆盖 |
| Placeholder | ✅ 无 TBD/TODO，所有代码完整 |
| 类型一致性 | ✅ 资源路径 `:/styles/darkstyle.qss` 在 qrc 和 main.cpp 中一致 |
| 文件路径 | ✅ 所有路径使用绝对路径 |

---

**Plan complete and saved to `docs/superpowers/plans/2026-06-14-ui-modernization.md`.**

**Two execution options:**

**1. Subagent-Driven (recommended)** - I dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** - Execute tasks in this session using executing-plans, batch execution with checkpoints

**Which approach?**