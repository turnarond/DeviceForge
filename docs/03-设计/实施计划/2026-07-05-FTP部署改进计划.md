# 文件部署模块改进 — 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 改进文件部署模块：端口去重、日志统一命名、远端预览多协议 + 设备同步

**Architecture:** 4 个独立 Task，无相互依赖。FtpDeployWidget 移除端口 SpinBox，DeployMaster 统一日志命名 + 重建远端预览面板（协议选择器 + 设备同步）

**Tech Stack:** Qt 6.11.1 Widgets, C++17, lwlog

**Spec:** `docs/superpowers/specs/2026-07-05-ftp-deploy-improvement-design.md`

## Global Constraints

- Qt 6.11.1 + C++17
- 中文注释和 UI 文案
- 不新增第三方依赖
- FtpManager / FtpAdapter / TelnetAdapter 本次不修改
- darkstyle.qss 不修改

---

### Task 1: FtpDeployWidget 移除端口输入框

**Files:**
- Modify: `src/tools/FtpDeployTool/FtpDeployWidget.h:58`
- Modify: `src/tools/FtpDeployTool/FtpDeployWidget.cpp:47-52`

- [ ] **Step 1: 移除 m_portSpin 成员**

在 `FtpDeployWidget.h` 中删除 `m_portSpin` 声明：
```cpp
// 删除这一行:
// QSpinBox*     m_portSpin       = nullptr;
```

- [ ] **Step 2: 移除 setupUi() 中的端口行**

在 `FtpDeployWidget.cpp` 的 `setupUi()` 中，将原来的行 1（端口|超时|加密模式）简化为仅保留超时：
```cpp
// 替换原来的行 1（端口 | 超时 | 加密模式）:
// 行 1: 超时设置
configLayout->addWidget(new QLabel("超时(秒):", this), 1, 0);
m_timeoutSpin = new QSpinBox(this);
m_timeoutSpin->setRange(5, 600);
m_timeoutSpin->setValue(30);
configLayout->addWidget(m_timeoutSpin, 1, 1);
```

注意：GridLayout 的列布局随之调整。FTPS 和清空复选框从行 2 移到行 1，重启复选框从行 3 移到行 2：
```cpp
// 行 2（原行 1）: 安全选项
m_ftpsCheck = new QCheckBox("FTPS 加密传输", this);
m_ftpsCheck->setToolTip("使用 FTP over TLS 加密，防止凭证和文件在网络中被窃听");
configLayout->addWidget(m_ftpsCheck, 1, 2, 1, 2);

m_clearCheck = new QCheckBox("部署前清空远程目录", this);
configLayout->addWidget(m_clearCheck, 2, 0, 1, 2);

// 行 3（原行 3）: 部署选项
m_rebootCheck = new QCheckBox("部署完成后重启设备", this);
configLayout->addWidget(m_rebootCheck, 2, 2, 1, 2);
```

- [ ] **Step 3: 编译验证**

```bash
cd D:/persional/QtDeployMaster/build && cmake --build . --config Release
```
Expected: `DeployMaster.vcxproj -> DeployMaster.exe`

- [ ] **Step 4: 提交**

```bash
git add src/tools/FtpDeployTool/FtpDeployWidget.h src/tools/FtpDeployTool/FtpDeployWidget.cpp
git commit -m "refactor: 移除 FtpDeployWidget 端口输入框，FTP 默认使用端口 21"
```

---

### Task 2: 日志方法重命名 + DeviceBusWidget 信号增强

**Files:**
- Modify: `DeployMaster.h:57` — `appendFtpLog` → `appendGlobalLog`
- Modify: `DeployMaster.cpp` — 全局替换 `appendFtpLog` → `appendGlobalLog`
- Modify: `src/ui/DeviceBusWidget.cpp:134-155` — `addDevice()` 末尾 emit `deviceSelectionChanged`
- Modify: `src/ui/DeviceBusWidget.cpp:158-` — `removeDevice()` 末尾 emit `deviceSelectionChanged`

- [ ] **Step 1: DeployMaster.h 重命名声明**

```cpp
// 替换:
// void appendFtpLog(const QString& log);
public slots:
    void appendGlobalLog(const QString& log);
```

- [ ] **Step 2: DeployMaster.cpp 全局替换**

将所有 `appendFtpLog` 替换为 `appendGlobalLog`。在 VS Code 或编辑器中用查找替换，范围仅限 `DeployMaster.cpp`。

受影响的调用点（约 15 处）：
- `setupFtpDeployTab()` 中的错误日志
- `setupTelnetDeployTab()` 中的错误日志
- `setupWebSocketClientTab()` 中的错误日志
- `onIPSelectionChanged()` 中的路径错误日志
- `refreshRemoteFiles()` 中的所有日志
- `onRemoteFileDoubleClicked()` 中的日志
- 所有 `appendFtpLog("❌ ...")` 和 `appendFtpLog("🔄 ...")` 调用

- [ ] **Step 3: DeviceBusWidget::addDevice() 增加信号发射**

在 `addDevice()` 末尾（`m_deviceList->addItem(item);` 之后）添加：
```cpp
emit deviceSelectionChanged();
```

- [ ] **Step 4: DeviceBusWidget::removeDevice() 增加信号发射**

在 `removeDevice()` 中删除操作完成后添加：
```cpp
emit deviceSelectionChanged();
```

- [ ] **Step 5: 编译验证**

```bash
cd D:/persional/QtDeployMaster/build && cmake --build . --config Release
```
Expected: 0 errors.

- [ ] **Step 6: 提交**

```bash
git add DeployMaster.h DeployMaster.cpp src/ui/DeviceBusWidget.cpp
git commit -m "refactor: appendFtpLog → appendGlobalLog; DeviceBusWidget 增删设备时 emit deviceSelectionChanged"
```

---

### Task 3: 远端预览面板重建

**Files:**
- Modify: `DeployMaster.h` — 新增成员 `m_protocolCombo`、`m_deviceCombo`、`m_refreshBtn`、`refreshDeviceCombo()`
- Modify: `DeployMaster.cpp` — 重写 `setupRemotePreview()`；新增 `refreshDeviceCombo()`

- [ ] **Step 1: DeployMaster.h 新增成员声明**

```cpp
// 远端预览面板（重建后）
QComboBox*   m_protocolCombo = nullptr;  // 协议选择（FTP / SCP）
QPushButton* m_refreshBtn    = nullptr;  // 刷新按钮（替代 ui.btn_refreshRemote）
```

新增方法：
```cpp
private:
    void refreshDeviceCombo();  // 根据 DeviceBusWidget 刷新设备下拉框
```

- [ ] **Step 2: 隐藏旧的远端预览控件，创建新的**

在 `DeployMaster` 构造函数中，`setupRemotePreview()` 调用之前，隐藏旧的 .ui 控件：
```cpp
// 隐藏旧远端预览控件（将被 m_protocolCombo + m_deviceCombo 替代）
ui.btn_refreshRemote->hide();
```

- [ ] **Step 3: 重写 setupRemotePreview()**

```cpp
void DeployMaster::setupRemotePreview()
{
    // --- 协议选择行 ---
    auto* protoRow = new QHBoxLayout();
    protoRow->addWidget(new QLabel("协议:", this));
    m_protocolCombo = new QComboBox(this);
    m_protocolCombo->addItem("FTP");
    m_protocolCombo->addItem("SCP (即将推出)");
    connect(m_protocolCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (idx == 1) { // SCP 选中
            appendGlobalLog("SCP 功能即将推出，请使用 FTP 协议");
            m_protocolCombo->setCurrentIndex(0); // 回弹到 FTP
        }
        refreshDeviceCombo();
    });
    protoRow->addWidget(m_protocolCombo);
    protoRow->addStretch();

    // --- 设备选择行 ---
    auto* devRow = new QHBoxLayout();
    devRow->addWidget(new QLabel("设备:", this));
    // 复用 ui.cmb_targetIPs（保留 .ui 中的 QComboBox）
    // 但需要从旧的父布局中移除并重新插入
    devRow->addWidget(ui.cmb_targetIPs, 1);
    m_refreshBtn = new QPushButton("刷新", this);
    connect(m_refreshBtn, &QPushButton::clicked, this, &DeployMaster::refreshRemoteFiles);
    devRow->addWidget(m_refreshBtn);

    // --- 路径行 ---
    auto* pathRow = new QHBoxLayout();
    pathRow->addWidget(new QLabel("路径:", this));
    pathRow->addWidget(m_remotePathEdit, 1);

    // --- 插入到远端预览面板 ---
    auto* remoteLayout = qobject_cast<QVBoxLayout*>(ui.groupBox_remotePreview->layout());
    if (remoteLayout) {
        // 清除旧的 cmb_targetIPs + btn_refreshRemote（它们已在 .ui 中，需要先移除）
        // 新的 protoRow、devRow、pathRow 按序插入
        remoteLayout->insertLayout(0, pathRow);
        remoteLayout->insertLayout(0, devRow);
        remoteLayout->insertLayout(0, protoRow);
    }

    // 设置初始路径
    currentRemotePath = m_remotePathEdit->text().trimmed();
    if (!currentRemotePath.endsWith('/')) currentRemotePath += '/';
    if (!currentRemotePath.startsWith('/')) currentRemotePath = '/' + currentRemotePath;

    // 连接信号
    connect(ui.cmb_targetIPs, &QComboBox::currentTextChanged, this, &DeployMaster::onIPSelectionChanged);
    connect(ui.tree_remoteFiles, &QTreeView::doubleClicked, this, &DeployMaster::onRemoteFileDoubleClicked);
    connect(m_remotePathEdit, &QLineEdit::returnPressed, this, &DeployMaster::refreshRemoteFiles);

    // 监听设备总线变化
    connect(m_deviceBusWidget, &DeviceBusWidget::deviceSelectionChanged,
            this, &DeployMaster::refreshDeviceCombo);

    // 初始填充
    refreshDeviceCombo();
}
```

- [ ] **Step 4: 实现 refreshDeviceCombo()**

```cpp
void DeployMaster::refreshDeviceCombo()
{
    QString currentText = ui.cmb_targetIPs->currentText();
    ui.cmb_targetIPs->blockSignals(true);
    ui.cmb_targetIPs->clear();

    auto devices = m_deviceBusWidget->allDevices();
    for (const auto& dev : devices) {
        QString ip = QString::fromStdString(dev.ip);
        // 协议过滤：FTP 模式显示端口为 21/0 或协议为 ftp 的设备
        bool include = false;
        if (dev.protocol.empty() || dev.protocol == "ftp") {
            include = (dev.port == 21 || dev.port == 0);
        }
        if (include && !ip.isEmpty()) {
            ui.cmb_targetIPs->addItem(ip, ip);
        }
    }

    // 恢复之前选中项
    int idx = ui.cmb_targetIPs->findText(currentText);
    if (idx >= 0) {
        ui.cmb_targetIPs->setCurrentIndex(idx);
    }

    ui.cmb_targetIPs->blockSignals(false);

    // 如果之前无选中但列表不空，自动选中第一个并刷新
    if (ui.cmb_targetIPs->currentIndex() < 0 && ui.cmb_targetIPs->count() > 0) {
        ui.cmb_targetIPs->setCurrentIndex(0);
        refreshRemoteFiles();
    }
}
```

- [ ] **Step 5: 编译验证**

```bash
cd D:/persional/QtDeployMaster/build && cmake --build . --config Release
```
Expected: 0 errors, `DeployMaster.exe` 生成成功。

- [ ] **Step 6: 提交**

```bash
git add DeployMaster.h DeployMaster.cpp
git commit -m "feat: 远端预览面板重建 — 协议选择 + 设备同步 DeviceBusWidget"
```

---

### Task 4: 清理旧信号连接 + 最终验证

**Files:**
- Modify: `DeployMaster.cpp` — 移除废弃的 `cmb_targetIPs` 旧信号连接、清理注释

- [ ] **Step 1: 清理 setupRemotePreview 中与 ui 重复的连接**

确保 `setupRemotePreview()` 中没有重复连接 `cmb_targetIPs`（已经在 Task 3 的 setupRemotePreview 中处理）。

- [ ] **Step 2: 全量编译 + 运行验证**

```bash
cd D:/persional/QtDeployMaster/build && cmake --build . --config Release
```
Expected: `DeployMaster.exe` 生成成功，0 errors。

手动验证：
1. 启动应用 → 确认旧 `tab_deploy` 不显示
2. 点击"文件部署" Tab → 确认无端口输入框，FTPS 复选框可见
3. 在设备总线添加设备 → 确认远端预览设备下拉框自动更新
4. 选择设备 + 输入路径 → 点击刷新 → 确认远程文件列表正常加载
5. 切换协议到 SCP → 确认弹出提示并回弹到 FTP

- [ ] **Step 3: 提交**

```bash
git add -A
git commit -m "chore: 清理旧 UI 信号连接，验证远端预览完整功能"
```
