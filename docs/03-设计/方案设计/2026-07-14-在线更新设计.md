# DeviceForge 在线更新 — 设计文档

> 日期: 2026-07-14 · 状态: Design · 作者: turnarond
>
> 将 DeviceForge 从"手动下载 GitHub Release zip"升级为"应用内一键更新"。

---

## 1. 方案选择

| 方案 | 描述 | 结论 |
|------|------|------|
| A: 轻量内嵌式 | UpdateChecker (ServiceTask) + Updater.exe (独立进程，无 Qt) | ✅ 选用 |
| B: WinSparkle 集成 | 引入第三方 DLL，需 appcast XML feed | ❌ 新依赖，UI 风格不可控 |
| C: 纯通知 | 仅查询 API → 弹出浏览器下载链接 | ❌ 未解决核心痛点 |

**选型理由**: 复用 libcurl（FtpAdapter 已有），零新依赖，符合项目精简可控的哲学。Updater.exe 纯 Win32 静态链接，~100KB。

---

## 2. 架构与组件

### 组件全景

```
┌─ DeviceForge.exe ──────────────────────────────────────────────────┐
│                                                                     │
│  UI Layer                        Framework Layer                   │
│  ──────────                      ────────────────                  │
│  MenuBar: "检查更新"             UpdateChecker : ServiceTask       │
│  StatusBar: 版本标签(可点击)       │                                │
│       │                           ├─ checkForUpdate() → GitHub API │
│       │                           ├─ downloadUpdate() → libcurl    │
│       ▼                           ├─ verifyZip()                   │
│  UpdateDialog (非模态)            ├─ installUpdate() → Updater.exe │
│    ├─ 版本号 + Release Notes      │                                │
│    ├─ 下载进度条                  └─ 状态: idle/checking/          │
│    ├─ "立即更新" / "稍后"                     downloading/ready/   │
│    └─ 完成后自动关闭                           error               │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
         │ QProcess::startDetached()
         ▼
┌─ Updater.exe (独立进程, ~100KB, 纯 Win32 API) ─────────────────────┐
│  1. 等待 DeviceForge.exe 进程退出                                    │
│  2. 复制临时目录中的新文件 → 覆盖安装目录旧文件                         │
│  3. 清理临时 zip + 解压目录 + 备份（成功时删备份）                      │
│  4. 启动新版 DeviceForge.exe                                        │
└─────────────────────────────────────────────────────────────────────┘
```

### 新增文件

| 文件 | 职责 |
|------|------|
| `src/updater/UpdateChecker.h/.cpp` | ServiceTask，GitHub API 调用 + 下载 + 校验 |
| `src/updater/UpdaterProcess.h/.cpp` | QProcess 封装，启动 Updater.exe + 写 manifest |
| `src/updater/UpdateDialog.h/.cpp` | QDialog，版本信息 + 进度 + 操作按钮 |
| `src/updater/UpdaterMain.cpp` | Updater.exe 入口（独立 CMake target，无 Qt） |
| `tests/tst_updatechecker.cpp` | UpdateChecker 单元测试 |

### 修改文件

| 文件 | 变更 |
|------|------|
| `CMakeLists.txt` | 新增 `add_executable(Updater ...)` + `tst_updatechecker` 测试目标 |
| `main.cpp` | ServiceManager 注册 UpdateChecker，延迟启动自动检查 |
| `DeployMaster.cpp/.h` | 菜单栏 "检查更新" + 状态栏版本标签 UI 接入 |

---

## 3. 状态机与数据流

### UpdateChecker 状态机

```
                    ┌──────────────────────────┐
                    │         idle              │
                    └────┬──────────┬──────────┘
            checkForUpdate()   autoCheck(启动5s后)
                         │          │
                         ▼          ▼
                    ┌──────────────────┐
                    │    checking      │
                    └────┬─────────┬───┘
                有新版本   无新版    网络/解析错误
                 │         │          │
                 ▼         ▼          ▼
            ┌────────┐ ┌──────┐ ┌─────────┐
            │ ready  │ │idle  │ │ error   │——静默，不弹窗
            └───┬────┘ └──────┘ └─────────┘
      downloadUpdate()
                │
                ▼
         ┌────────────┐
         │downloading │──用户取消──→ idle
         └─────┬──────┘
               │ 完成+校验通过
               ▼
         ┌────────────┐
         │ installed  │——→ 启动 Updater.exe → DeviceForge 退出
         └────────────┘
```

### 完整数据流

```
DeviceForge 启动 (main.cpp)
    │
    ├─ ServiceManager 注册 UpdateChecker
    │       │
    │       └─ OnStart(): 5s 延迟后自动 checkForUpdate()
    │              │
    │              ├─ libcurl GET https://api.github.com/repos/turnarond/DeviceForge/releases/latest
    │              │       │
    │              │       ├─ 解析 JSON: tag_name, prerelease, assets[0].browser_download_url,
    │              │       │              assets[0].size, body (release notes)
    │              │       │
    │              │       ├─ 过滤: prerelease=true → 跳过
    │              │       ├─ 比较 semver: tag_name vs PROJECT_VERSION
    │              │       │       ├─ 新版 > 当前 → 状态 ready, 发出信号 newVersionAvailable()
    │              │       │       └─ 相同 → 状态 idle, 发出信号 versionCurrent()
    │              │       │
    │              │       └─ 网络/解析失败 → 状态 error (静默，仅日志)
    │              │
    │              └─ 用户点击 "立即更新":
    │                     │
    │                     ├─ UpdateDialog::show()
    │                     ├─ downloadUpdate() → libcurl 下载 zip 到 %TEMP%/DeviceForge/
    │                     │       │
    │                     │       ├─ 验证 Content-Length == asset.size == 实际大小
    │                     │       ├─ 解压到 %TEMP%/DeviceForge/v<version>/
    │                     │       └─ 状态 installed
    │                     │
    │                     └─ installUpdate():
    │                            │
    │                            ├─ 写 %TEMP%/DeviceForge/updater_manifest.json
    │                            │
    │                            ├─ QProcess::startDetached("Updater.exe", {manifestPath})
    │                            ├─ QApplication::quit()
    │                            │
    │                            └─ Updater.exe 启动:
    │                                   ├─ 读 manifest
    │                                   ├─ 轮询进程名 "DeviceForge.exe" (WaitForSingleObject)
    │                                   ├─ 进程退出 → SHFileOperation 复制文件覆盖
    │                                   ├─ 清理 temp 文件
    │                                   └─ CreateProcess("DeviceForge.exe")
    │
    └─ 用户点击 "稍后": 状态 idle，状态栏保留"有新版本"徽标
```

### UI 交互

| 场景 | 状态栏行为 | 菜单行为 |
|------|-----------|---------|
| 启动检查中 | 版本标签灰色 + 省略号动画 | "检查更新" 置灰 |
| 已是最新版 | 版本标签: `v2.1.0 (已是最新)` (石墨色) | 正常可点 |
| 有新版本 | 版本标签: `v2.2.0 可用 ▾` (琴色可点击) | 菜单: "下载 v2.2.0..." |
| 下载中 | UpdateDialog 显示进度条 | "检查更新" → "正在下载..." |
| 下载完成 | "点击安装并重启" | — |
| 网络错误 | 状态栏图标变黄(可选)，无弹窗 | 点击时提示 "上次检查失败，重试？" |

---

## 4. GitHub API 集成与安全

### API 端点

```
GET https://api.github.com/repos/turnarond/DeviceForge/releases/latest
Accept: application/vnd.github+json
User-Agent: DeviceForge-UpdateChecker/<version>
```

### 解析字段

```json
{
  "tag_name": "v2.2.0",
  "prerelease": false,
  "html_url": "https://github.com/turnarond/DeviceForge/releases/tag/v2.2.0",
  "body": "## 本版本亮点\n\n...",
  "assets": [
    {
      "name": "DeviceForge-v2.2.0-win64.zip",
      "size": 52345678,
      "browser_download_url": "https://github.com/.../DeviceForge-v2.2.0-win64.zip",
      "content_type": "application/zip"
    }
  ]
}
```

### 校验规则

| 字段 | 校验 |
|------|------|
| `prerelease` | `true` → 跳过（仅正式版） |
| `tag_name` | `v<major>.<minor>.<patch>` 格式，解析后与 `CMAKE_PROJECT_VERSION` 比较 |
| `assets[].name` | 匹配 `DeviceForge-*-win64.zip` |
| `assets[].content_type` | 必须为 `application/zip` 或 `application/x-zip-compressed` |
| `assets[].size` | 下载完成后实际字节数 == `size`，不匹配 → 丢弃并报错 |

### 安全措施

| 层面 | 措施 |
|------|------|
| 传输 | libcurl HTTPS（GitHub 证书链校验） |
| 文件完整 | 三方比对: `Content-Length` header vs `asset.size` vs 实际文件大小 |
| 解压防炸弹 | zip 内文件总数 ≤ 200、单文件 ≤ 200MB |
| 替换回滚 | Updater 先备份旧文件到 `installDir.backup/`，成功后才删备份 |
| API 限流 | 启动只发 1 次请求，远低于未认证 60 req/h 限制 |
| 本地代理 | 复用系统代理设置（libcurl 默认） |
| 仅正式版 | `prerelease: true` 的 Release 自动跳过 |

---

## 5. Updater.exe 详细设计

### 为什么独立 + 无 Qt

- Windows 运行中的 exe/DLL 被锁定，DeviceForge 无法自替换
- 独立进程在 DeviceForge 退出后执行替换
- 无 Qt 依赖 → 极小体积（~100KB 静态链接）+ 启动极快

### 执行流程

```
main(argc, argv)
    │
    ├─ 解析参数: --manifest <path> --log <path>
    │
    ├─ 读 updater_manifest.json
    │     { tempDir, installDir, exeName, backupDir }
    │
    ├─ 打开日志文件(追加写)
    │
    ├─ Step 1: 等待 DeviceForge 退出 (max 30s)
    │     └─ 超时 → MessageBox "请手动关闭 DeviceForge 后重试" → 退出
    │
    ├─ Step 2: 创建备份
    │     └─ 复制 installDir/* → backupDir/*
    │
    ├─ Step 3: 替换文件
    │     ├─ 遍历 tempDir/*, SHFileOperation FO_COPY → installDir
    │     └─ 失败 → 从 backupDir 回滚 → MessageBox 提示
    │
    ├─ Step 4: 清理
    │     ├─ RemoveDirectory(backupDir)   // 成功后删备份
    │     ├─ RemoveDirectory(tempDir)      // 删临时文件
    │     └─ DeleteFile(manifest)
    │
    └─ Step 5: 启动新版
          ├─ CreateProcess("DeviceForge.exe", ...)
          └─ ExitProcess(0)
```

### manifest 格式 (`updater_manifest.json`)

```json
{
  "tempDir": "C:\\Users\\xxx\\AppData\\Local\\Temp\\DeviceForge\\v2.2.0",
  "installDir": "D:\\tools\\DeviceForge",
  "exeName": "DeviceForge.exe",
  "backupDir": "C:\\Users\\xxx\\AppData\\Local\\Temp\\DeviceForge\\backup"
}
```

### 构建 (CMake)

```cmake
# Updater — 独立可执行文件，无 Qt 依赖
add_executable(Updater
    src/updater/UpdaterMain.cpp
)
set_property(TARGET Updater PROPERTY WIN32_EXECUTABLE ON)
# 链接: 仅 CRT（kernel32, shell32, user32）
```

### 临时目录结构

```
%TEMP%/DeviceForge/
    ├── v2.2.0/                  ← 解压后的新版文件
    │     ├── DeviceForge.exe
    │     ├── libcurl-x64.dll
    │     ├── libssh2.dll
    │     ├── ...
    │     └── Updater.exe        ← 新版也包含 Updater（自升级）
    ├── DeviceForge-v2.2.0-win64.zip
    ├── updater_manifest.json
    ├── backup/                  ← 旧文件备份（成功时删除）
    └── updater.log
```

---

## 6. UI 设计

### UpdateDialog

```
┌─────────────────────────────────────────────────┐
│  🔄 DeviceForge 在线更新                         │
├─────────────────────────────────────────────────┤
│                                                  │
│  当前版本: v2.1.0                                │
│  最新版本: v2.2.0         发布时间: 2026-07-20   │
│  文件大小: 52.3 MB                               │
│                                                  │
│  ┌─ Release Notes ──────────────────────────┐   │
│  │ ## v2.2.0                                 │   │
│  │                                            │   │
│  │ ### OPC UA 真实实现                        │   │
│  │ ...                                        │   │
│  └────────────────────────────────────────────┘   │
│                                                  │
│  ┌███████████████████░░░░░░░░░░┐  67%           │
│  └─────────────────────────────┘  已下载: 35MB  │
│                                                  │
│     [稍后再说]              [下载并更新]          │
│                                                  │
└─────────────────────────────────────────────────┘
```

- 非模态 `QDialog`（`setModal(false)`），不阻塞主界面
- 按钮状态切换: 初始"下载并更新" → 下载中(进度条+取消按钮) → 完成"安装并重启"
- 关闭对话框 = 取消当前操作，状态回 idle
- Release Notes 使用 `QTextBrowser` 渲染

### 菜单栏 & 状态栏

```
状态栏:  设备: 3 台  │  [●] 在线  │  v2.1.0 (已是最新)  ← 默认石墨色
状态栏:  设备: 3 台  │  [●] 在线  │  v2.2.0 可用 ▾      ← 琴色可点击
状态栏:  设备: 3 台  │  [●] 在线  │  ↓ 正在下载...       ← 下载中
```

- 菜单: 帮助 → "检查更新..." / "关于 DeviceForge"
- 点击状态栏版本标签 → 打开 UpdateDialog 或弹出 "已是最新版本"

---

## 7. 错误处理

| 阶段 | 错误 | 用户看到 | 日志级别 |
|------|------|---------|---------|
| API 查询 | 网络不可达 / DNS / 超时 | 状态栏无变化（静默） | ERROR |
| API 查询 | 限流 403 / 5xx | 状态栏无变化 | WARN |
| API 查询 | JSON 解析失败 | 状态栏无变化 | ERROR |
| 下载 | 磁盘空间不足 | 弹窗 "磁盘空间不足，需要 XX MB，可用 XX MB" | ERROR |
| 下载 | 网络中断 / 超时 | 弹窗 "下载失败：网络中断" + 重试按钮 | WARN |
| 下载 | 大小校验不匹配 | 弹窗 "文件校验失败，请稍后重试" | ERROR |
| 解压 | zip 损坏 / 路径遍历 | 弹窗 "安装包校验失败" | ERROR |
| Updater | DF 进程 30s 未退出 | MessageBox "请手动关闭 DeviceForge" | ERROR |
| Updater | 文件复制失败 | MessageBox "更新失败，旧版本已恢复" + 回滚 | ERROR |
| 启动自动检查 | 距上次检查 < 24h | 跳过（不重复请求） | INFO |

---

## 8. 测试策略

| 层级 | 测什么 | 工具 |
|------|--------|------|
| 单元 | `UpdateChecker`: API JSON 解析 + 版本比较（mock libcurl） | QtTest (`tst_updatechecker`) |
| 单元 | `UpdaterProcess`: manifest 读写 | QtTest |
| 单元 | 版本号比较逻辑（正常/边界/错误格式） | QtTest |
| 集成 | UpdateChecker 连接 GitHub API（标记 `[network]`，可选跑） | CTest |
| 手动 | 新旧版本完整更新流程（旧版→检查→下载→更新→重启→验证新版） | 打包测试 |

---

## 9. 文件变更清单

| 文件 | 变更 |
|------|------|
| `src/updater/UpdateChecker.h` | 新建 — ServiceTask 头文件 |
| `src/updater/UpdateChecker.cpp` | 新建 — GitHub API + 下载 + 校验实现 |
| `src/updater/UpdaterProcess.h` | 新建 — QProcess 封装 |
| `src/updater/UpdaterProcess.cpp` | 新建 — manifest 写入 + QProcess 启动 |
| `src/updater/UpdateDialog.h` | 新建 — 更新对话框 |
| `src/updater/UpdateDialog.cpp` | 新建 — 对话框交互 |
| `src/updater/UpdaterMain.cpp` | 新建 — Updater.exe 独立入口 |
| `tests/tst_updatechecker.cpp` | 新建 — 单元测试 |
| `CMakeLists.txt` | 新增 Updater target + tst_updatechecker |
| `main.cpp` | 注册 UpdateChecker，配置自动检查 |
| `DeployMaster.h` | 新增菜单/状态栏成员 |
| `DeployMaster.cpp` | 菜单栏 + 状态栏 UI 接入 |
| `docs/superpowers/specs/2026-07-14-online-update-design.md` | 本文件 |
