# DeviceForge 版本一致性说明

## Qt 版本统一

**本地开发基线：Qt 6.11.1**；**CI 兼容基线：Qt 6.9.2**

### 为什么是 6.11.1？

- 当前验收机实际安装的 Qt 版本是 **6.11.1**（`D:\Qt\6.11.1\msvc2022_64`）
- Qt 6.10.1 并未安装
- CI 使用 Qt 6.9.2（GitHub Actions）

### 版本历史

| 时期 | Qt 版本 | 说明 |
|------|---------|------|
| 早期开发 | 6.10.1 | 文档指定，但实际未安装 |
| 当前 | **6.11.1** | 本地开发基线；路径可配置，当前验收机位于 `D:\Qt` |
| CI | 6.9.2 | GitHub Actions（jurplel/install-qt-action@v4） |

### 文档更新记录

- ✅ **2026-07-17**：批量统一所有文档为 Qt 6.11.1（文档路径后经 2026-08-21 目录重整更新）
  - build.bat
  - docs/05-部署文档/构建指南.md
  - docs/05-部署文档/调试指南.md
  - README.md
  - docs/04-使用手册/架构设计.md
  - CLAUDE.md
  - CONTRIBUTING.md
  - docs/03-设计/实施计划/*.md（7 个计划文档）

### Qt 版本兼容性说明

项目代码应避免使用仅在 Qt 6.11.x 新增的 API，以保持与 CI（Qt 6.9.2）的兼容性。

**已验证 API**：
- Qt 6.9.2 及以上版本均支持的核心模块：
  - Qt::Core / Qt::Gui / Qt::Widgets
  - Qt::Network / Qt::SerialBus / Qt::WebSockets

**开发建议**：
- 本地开发使用 Qt 6.11.1
- CI 自动测试使用 Qt 6.9.2
- 提交前确保两版本均能编译通过

### 本地工具发现

- `build.bat` 优先使用调用方显式设置的 `QT_PREFIX`；未设置时按 `D:\Qt`、`C:\Qt` 顺序探测 Qt 6.11.1。盘符和安装位置不是工程约束。
- Python 工具可使用 PATH 中可实际启动的 Python 3；PATH 被 WindowsApps 占位时，当前验收机可使用 `D:\Qt\Tools\mingw1310_64\opt\bin\python3.exe`。
- 本地 CTest 的 `tst_dpapi_crypto` 需要正常 Windows 用户 profile。CI/服务账户若失败，应保留 Windows 错误码，并在正常用户账户复测。

## CI Qt 版本差异处理

CI 使用的 Qt 版本（6.9.2）与本地开发版本（6.11.1）不同，注意避免使用仅新版才有的 API。

### 检查 API 兼容性

1. **查阅 Qt 官方文档**：确认 API 在 Qt 6.9.2 中可用
2. **本地测试**：在 Qt 6.11.1 下编译和运行
3. **CI 验证**：GitHub Actions 会自动用 Qt 6.9.2 编译，失败会通知

### 如果 CI 构建失败

可能原因：
- 使用了 Qt 6.10.0+ 新增的 API
- Qt 6.9.2 中该 API 不存在或行为不同

解决：
- 回退到 Qt 6.9.2 兼容的 API
- 或在 GitHub Actions workflow 中升级 Qt 版本

## CI 与发布验收边界

- `.github/workflows/ci.yml` 是日常入口，在 Qt 6.9.2 下分别验证 Debug/Release。
- `.github/workflows/release-validation.yml` 仅手动触发，执行 Release 构建、CTest 与 `windeployqt`，生成绿色 zip 后解压到全新目录；它先解析 Python 路径，再从 Smoke 子进程的 PATH 移除 `QT_ROOT_DIR` 下的条目并清空 Qt/QML 插件变量，最后只上传 portable zip artifact。
- 该手动工作流不创建 GitHub Release 或 Tag，既有 `v2.8.0` Tag 不变。NSIS 自动化与安装包分发因现有递归卸载语义不安全而阻塞，必须由独立 PATCH 修复后恢复。
