# OPC UA 客户端 UI 重构设计

**日期：** 2026-07-20
**状态：** 设计中 → 待实现
**文件：** `src/tools/OpcUaClientTool/OpcUaClientWidget.cpp`

---

## 背景

OPC UA 客户端 Tool 当前布局存在三个问题：
1. 地址空间浏览表格列设计不合理（显示名列过宽，类型仅显示数字 ID）
2. 全量节点一次性塞入 QTableWidget，300+ 节点时滚动卡顿
3. 读/写批量表只能添加行，无法删除

---

## 一、地址空间浏览表格

### 问题

当前 3 列：`显示名 | NodeId | 类型`
- `显示名` 列撑满剩余空间，但节点名普遍较短，造成大量空白
- `类型` 列显示原始数字 ID（如 `i=24`），用户无法直观理解
- `NodeId` 默认挤在窄列，内容换行/截断

### 方案

改为 **5 列**，列宽固定：

| 列 | 宽度策略 | 说明 |
|----|----------|------|
| # | 40px 固定 | 行号，作为视觉锚点 |
| 显示名 | 200px 固定 | 节点友好名，不撑满 |
| NodeId | 自适应（Interactive） | 按内容宽度展开，保留原始 ID |
| 数据类型 | 80px 固定 | 友好名：`i=24` → `Int32` |
| 节点类 | 80px 固定 | 文本标签：`Variable` / `Object` / `Folder`，色块区分 |

**类型映射**（browseRoot 返回的 dataType 是数字 NodeId）：
```
i=24  → Int32       i=12 → String      i=13 → DateTime
i=15  → ByteString   i=1  → Boolean     i=6  → Int16
i=5   → UInt16      i=8  → Int64       i=9  → UInt64
i=10  → Float       i=11 → Double      空/"" → —
```

**节点类**（nodeClass 是数字）：
```
1=Object  2=Variable  4=Method  8=Folder  0=Unspecified
```
显示时用色块背景色区分：
- `Variable`：青绿色调（#40C8A0，低饱和）
- `Object`/`Folder`：石墨灰
- `Method`：琴色（#F0A030）

### 数据类型映射实现

在 `OpcUaClientWidget.cpp` 增加静态函数：
```cpp
static QString dataTypeIdToName(const QString& nodeId); // 已有，补充空值处理
static QString nodeClassToName(int nodeClass);
static QString nodeClassToColor(int nodeClass); // 返回 QSS 色值
```

---

## 二、卡顿 → 虚滚 + 分批渲染

### 问题

300+ 节点一次性 `setRowCount` + `setItem`，Qt 在插入过程中反复触发 `updatesEnabled(false)` 导致 UI 阻塞。

### 方案

**两步渲染**：
1. `setSortingEnabled(false)` 禁排序
2. 每插入 **100 行** 调用一次 `viewport()->update()`，让 Qt 处理一帧渲染
3. 全部插入后 `setSortingEnabled(true)` + `viewport()->update()`

同时：
- `setUniformRowHeights(true)` — 开启 Qt 虚滚优化
- `verticalScrollBar()->setSingleStep(20)` — 改善滚动手感

**分页提示**（`m_browseCount`）：`共 412 条，第 1 页`，分页大小固定 200 条（超过则加页码按钮"加载更多"）

---

## 三、批量表行删除

### 问题

`m_batchTable`（读/写结果）和 `m_subscriptionTable`（订阅）只能通过"读"/"订阅"操作添加行，无法手动删除误添加的行。

### 方案

每行最后一列（`质量`/时间戳 右侧）加一个 **× 按钮**，点击即删除该行：

```cpp
void OpcUaClientWidget::onRemoveBatchRow(int row) {
    m_batchTable->removeRow(row);
}
```

实现方式：
- 在 `upsertSubscriptionRow` / `onReadClicked` 添加行时，同时创建删除按钮
- 用 `QTableWidget::setCellWidget(row, 3, deleteButton)`
- 按钮样式：小号、圆形、红色 hover、`×` 符号

---

## 四、实现范围

**仅修改文件：** `src/tools/OpcUaClientTool/OpcUaClientWidget.cpp`

| 改动点 | 行数估计 |
|--------|---------|
| 列定义改为 5 列 | ~5 行 |
| populateBrowseTable 加类型/节点类映射 | ~40 行 |
| 虚滚 + 分批渲染 | ~20 行 |
| 批量表 × 删除按钮 | ~30 行 |

**不需要改动：**
- `OpcUaClientBackend`（业务逻辑不变）
- `OpcUaAdapter`（协议层不变）
- `tab_opcua_client.ui`（已废弃，新布局全编程式）

---

## 五、验收标准

1. 浏览 300+ 节点，滚动帧率正常，无卡顿
2. 表头：`# | 显示名 | NodeId | 数据类型 | 节点类`
3. 数据类型列显示友好名（`Int32`）而非数字 ID
4. 批量表每行有 × 删除按钮，点击可删除
5. 订阅表每行有 × 删除按钮
