# 文件部署模块改进设计

**日期**：2026-07-05  
**状态**：✅ 已实现  
**关联**：[[2026-07-04-tool-framework-design]] | 实施计划：[[2026-07-05-ftp-deploy-improvement-plan]]

---

## 目标

改进 DeployMaster "文件部署" 功能模块，解决端口冗余、日志混乱、远端预览协议单一等问题。

---

## 1. 端口管理：设备总线与 Tool 职责分离

### 原则

- **DeviceBusWidget**：存储设备 IP 列表（纯 IP，不含端口），用户名/密码
- **每个 Tool**：独立管理自己的协议端口，提供合理默认值

### 变更

| 组件 | 默认端口 | 说明 |
|------|----------|------|
| FtpDeployWidget | 21 | 保留端口 SpinBox（用户可配置，默认 21）。端口通过 `startUpload(&hellip;, port)` 传入 Backend，覆盖 `DeviceInfo.port` |
| TelnetWidget | 23 | 保持不变 |
| WebSocketWidget | 8080 | 保持不变 |
| （未来 SSH/SCP） | 22 | 新建时设置 |

FtpDeployBackend::startUpload() 接受 `int port` 参数（默认 21），覆盖 `DeviceInfo.port`（`DeviceBusWidget` 添加设备时 `port=0`，端口由各 Tool 自行配置）。FtpAdapter::connect() 中 `device.port > 0 ? device.port : 21` 兜底。

---

## 2. 日志体系：双日志 + 级别管理

### 结构

```
┌─ Tool 内部日志 (m_logView) ─────┐
│ 级别: INFO                       │
│ 内容: 部署进度、文件列表、        │
│       单文件上传结果              │
│ 保留: 当前会话                    │
└─────────────────────────────────┘

┌─ 全局日志 (ui.txt_globalLog) ───┐
│ 级别: INFO + WARN + ERROR        │
│ 内容: 连接/断开、系统错误、       │
│       跨 Tool 汇总、安全警告      │
│ 保留: 所有会话（支持清除）        │
└─────────────────────────────────┘
```

### 职责划分

| 事件 | Tool 日志 | 全局日志 |
|------|-----------|----------|
| 添加文件到列表 | ✅ 已添加 3 个文件 | - |
| 连接设备 | - | ✅ 正在连接 192.168.1.1:21 |
| 单文件上传完成 | ✅ app.bin 上传完成 | - |
| 全部部署完成 | ✅ 部署完成: 3/3 成功 | ✅ 部署完成: 3/3 成功 |
| 连接失败 | - | ❌ 连接失败: 192.168.1.1 |
| FTPS 启用 | ✅ FTPS 模式已启用 | ✅ FTPS 模式已启用: 192.168.1.1 |
| 安全警告 | - | ⚠️ Telnet 明文传输风险 |

### 命名统一

将 `appendFtpLog()` 重命名为 `appendGlobalLog()`，更准确反映用途。旧方法保留为别名兼容。

---

## 3. 远端预览重设计

### 3.1 多协议支持

```
┌─ 远端预览 ───────────────────────────┐
│ 协议: [FTP ▾]  [SCP ▾]  (SCP 预留)  │  ← 新增
│ 设备: [192.168.1.100 ▾] [刷新]       │  ← 同步 DeviceBusWidget
│ 路径: [/apps/m580cn/bin/___________] │  ← 支持编辑+回车刷新
│ ┌──────────────────────────────────┐ │
│ │ 📁 config/                       │ │
│ │ 📄 app.bin          1.2 MB      │ │
│ │ 📄 params.ini         4 KB      │ │
│ └──────────────────────────────────┘ │
└──────────────────────────────────────┘
```

- **FTP**：复用现有 `FtpManager`，无改动
- **SCP**：UI 预留，后端待 `SshAdapter` (Phase 3) 实现后接入。选择 SCP 时显示 "SCP 功能即将推出"
- 协议切换时，设备下拉框过滤匹配协议的设备（FTP→port 21 或 0，SCP→port 22）

### 3.2 设备下拉框同步

**当前问题**：`setupRemotePreview()` 只在初始化时填充 `cmb_targetIPs`，DeviceBusWidget 设备变更后不更新。

**修复**：监听 `DeviceBusWidget::deviceSelectionChanged` 信号，自动刷新设备下拉框：

```
DeviceBusWidget::deviceSelectionChanged()
  → DeployMaster::refreshDeviceCombo()
    → 获取 allDevices()
    → 根据当前协议过滤
    → 更新 cmb_targetIPs items
    → 保持当前选中项（若仍存在）
```

### 3.3 协议感知的设备列表

`DeviceInfo` 已有 `protocol` 字段（"ftp", "telnet", "modbus"…），但当前未使用。远端预览中：
- 添加设备时可指定协议标签
- 下拉框根据当前选择的预览协议过滤设备

若设备未标注协议，默认按端口推断（21→FTP, 22→SCP, 23→Telnet）。

---

## 4. 移除的旧 UI 元素

旧 `tab_deploy`（"批量部署"）已经完全被 "文件部署" Tab 替代，执行以下清理：

| 控件 | 处理 |
|------|------|
| `groupBox_upload` | 隐藏（.ui 保留） |
| `chk_rebootAfterDeploy` | 隐藏 |
| `chk_clearRemoteBeforeUpload` | 隐藏 |
| `btn_deploy` | 隐藏 |
| `ui.txt_remotePath` | 替换为独立 `m_remotePathEdit`（移至远端预览面板） |
| `tab_deploy` 整个 Tab | `setTabVisible(false)` |

保留 `btn_clearLog`（移至日志区）和远端预览面板。

### 移除的信号连接

- `btn_addFiles` / `btn_addFolders` / `btn_clearUploadList` → 对应的 slot 方法保留但不再连接
- `list_uploadedItems` → filesDropped 事件
- `btn_deploy` → `onDeployClicked` (已显示迁移提示)
- `AppState::isBusyChanged` → `btn_deploy->setEnabled`（无用）

---

## 5. 文件变更清单

| 文件 | 变更 |
|------|------|
| `src/tools/FtpDeployTool/FtpDeployWidget.h` | 保留 `m_portSpin`（已修正注释） |
| `src/tools/FtpDeployTool/FtpDeployWidget.cpp` | `onDeployClicked()` 传递 `m_portSpin->value()` 到 `startUpload()` |
| `DeployMaster.h` | 新增 `m_remotePathEdit`、`protocolCombo`、`refreshDeviceCombo()` |
| `DeployMaster.cpp` | 远端预览面板重建（协议选择+设备同步）；`setupRemotePreview()` 重写；`appendFtpLog` → `appendGlobalLog` |
| `src/ui/DeviceBusWidget.h` | 确认 `deviceSelectionChanged` 信号在设备增删时 emit |

---

## 6. 不变部分

- TelnetTool / WebSocketTool / ModbusCluster / OpcUaClientTab / LogQueryTab：本次不修改
- FtpManager / FtpAdapter / TelnetAdapter：本次不修改
- darkstyle.qss：本次不修改
- 第三方库：不新增依赖

---

## 7. 与后续 Phase 的关系

| 后续 Phase | 本次预留 |
|------------|----------|
| Phase 3 SSH Adapter | 远端预览 SCP 协议选项（UI 显示"即将推出"） |
| Phase 3 QPluginLoader | 不影响 |
| 全局配置热加载 | 端口默认值未来可从 ConfigManager 读取 |
