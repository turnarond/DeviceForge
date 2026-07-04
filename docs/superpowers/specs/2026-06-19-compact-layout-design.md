# 紧凑模式布局优化设计

## 1. 背景与目标

### 问题描述
当前 DeployMaster 工具界面整体高度过高，在 1366x768 等低分辨率屏幕上无法完整显示，影响用户体验和操作效率。

### 优化目标
在不牺牲功能的前提下，通过紧凑布局优化减少界面高度占用，确保在主流屏幕尺寸上能够完整显示。

### 预期效果
- 1366x768 屏幕：完整显示，有少量余量
- 1920x1080 屏幕：更紧凑，信息密度更高
- 预计节省高度：约 130-150px

---

## 2. 具体改动

### 2.1 主窗口调整

| 属性 | 当前值 | 目标值 | 实际状态 |
|------|--------|--------|----------|
| 默认高度 | 650px | 600px | ✅ 已实现 |
| 启动状态 | 固定大小 | 最大化启动 | ❌ 未实现（保持固定大小启动） |

### 2.2 顶部配置栏优化

**问题**：两个 GroupBox 各有 `margin-top: 12px` + 边框 + 内边距，垂直空间消耗约 80-100px

**优化措施**：
- 减小 GroupBox 的 `margin-top` 从 12px 改为 6px
- 减小内部 layout 的 margin（从 6px 改为 4px）
- 减小 spacing（从 6px 改为 4px）

### 2.3 工作区优化

**优化措施**：
- TabWidget 内部的 GroupBox 设置扁平化样式
- 右侧远端预览面板设置最小宽度限制

### 2.4 日志区优化

| 属性 | 当前值 | 目标值 |
|------|--------|--------|
| 字体大小 | 11pt | 9pt |
| 最小高度 | 150px | 100px |

### 2.5 Splitter 比例调整

```cpp
// 设置垂直 splitter 初始大小比例：工作区占75%，日志区占25%
ui.splitter_log->setSizes(QList<int>() << 375 << 125);
// 设置水平 splitter 初始大小比例：左侧工作区占75%，右侧预览占25%
ui.splitter_main->setSizes(QList<int>() << 600 << 200);
```

---

## 3. 样式表改动

### 3.1 顶部配置栏 GroupBox 紧凑样式

```css
/* 顶部配置栏 GroupBox 紧凑模式 */
QGroupBox#groupBox_ipList,
QGroupBox#groupBox_auth {
    margin-top: 6px;
    padding-top: 6px;
}
```

### 3.2 TabWidget 内 GroupBox 扁平化

```css
/* TabWidget 内 GroupBox 扁平化，去除边框 */
QTabWidget QGroupBox {
    border: none;
    margin-top: 0px;
    padding-top: 0px;
    background-color: transparent;
}
```

### 3.3 日志区紧凑字体

```css
/* 日志区紧凑字体 */
QTextEdit#txt_globalLog {
    font-size: 9pt;
}
```

---

## 4. 文件改动清单

| 文件 | 改动内容 |
|------|----------|
| `DeployMaster.ui` | 调整主窗口高度、GroupBox margin、layout spacing |
| `DeployMaster.cpp` | 调整 splitter 初始比例（最大化启动暂未实现） |
| `darkstyle.qss` | 添加紧凑模式样式规则 |

---

## 5. 高度节省预估

| 区域 | 节省高度 |
|------|----------|
| 主窗口默认高度 | 50px |
| 顶部配置栏 GroupBox margin-top | 12px |
| 顶部配置栏 layout margin | 8px |
| 顶部配置栏 layout spacing | 4px |
| 日志区最小高度 | 50px |
| **总计** | **约 130px** |

---

## 6. 风险评估

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 内容过于紧凑难阅读 | 低 | 保留最小尺寸，确保基本可读性 |
| Tab 内 GroupBox 移除边框后层次不清 | 低 | 使用背景色区分不同区域 |

---

## 7. 验收标准

- [ ] 主窗口默认高度调整为 600px
- [ ] 顶部配置栏高度明显降低
- [ ] 日志区字体与其他区域一致（9pt）
- [ ] 日志区最小高度为 100px
- [ ] Splitter 比例为 75%:25%
- [ ] 1366x768 屏幕下界面完整显示
- [ ] 编译通过，无错误
- [ ] 最大化启动（未实现，推迟）
