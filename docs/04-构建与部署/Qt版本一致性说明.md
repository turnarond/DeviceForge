# DeviceForge 版本一致性说明

## Qt 版本统一

**当前版本：Qt 6.11.1**（系统实际安装版本）

### 为什么是 6.11.1？

- 系统实际安装的 Qt 版本是 **6.11.1**（`C:\Qt\6.11.1\msvc2022_64`）
- Qt 6.10.1 并未安装
- CI 使用 Qt 6.9.2（GitHub Actions）

### 版本历史

| 时期 | Qt 版本 | 说明 |
|------|---------|------|
| 早期开发 | 6.10.1 | 文档指定，但实际未安装 |
| 当前 | **6.11.1** | 系统实际安装版本 |
| CI | 6.9.2 | GitHub Actions（jurplel/install-qt-action@v4） |

### 文档更新记录

- ✅ **2026-07-17**：批量统一所有文档为 Qt 6.11.1
  - build.bat
  - docs/build-guide.md
  - docs/debugging-guide.md
  - README.md
  - docs/architecture.md
  - CLAUDE.md
  - CONTRIBUTING.md
  - docs/superpowers/plans/*.md（7 个计划文档）

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
