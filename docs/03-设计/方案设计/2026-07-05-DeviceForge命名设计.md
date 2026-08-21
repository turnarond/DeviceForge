# DeviceForge 项目改名设计

**日期**：2026-07-05
**状态**：待实现
**原名**：DeployMaster

---

## 目标

将项目从 "DeployMaster" 更名为 "DeviceForge"，仅修改外部可见的名称，内部代码（类名、文件名、Tool ID）保持不变。

---

## 改名范围

### 修改（外部可见）

| 位置 | 原文本 | 新文本 |
|------|--------|--------|
| `DeployMaster.ui` — 窗口标题 | `DeployMaster - FTP & Telnet 批量运维工具` | `DeviceForge - 工业设备批量运维平台` |
| `CMakeLists.txt` — project() | `DeployMaster` | `DeviceForge` |
| `README.md` — 标题 | `# DeployMaster 2.0 — 通用设备运维平台` | `# DeviceForge — 工业设备批量运维平台` |
| `README.md` — 正文所有 "DeployMaster" | 各处 | DeviceForge |
| `CLAUDE.md` — 所有 "DeployMaster" 引用 | 各处 | DeviceForge |
| `.github/workflows/msbuild.yml` — 项目名引用 | `DeployMaster` | `DeviceForge` |
| `DeployMaster.qrc` — 资源标签（可选） | 如有显示文本 | DeviceForge |
| `DeployMaster-v2.0.0-win64.zip` — 发布包名 | 旧名称 | `DeviceForge-v2.1.0-win64.zip` |
| `docs/superpowers/` — spec/plan 文档标题 | "DeployMaster 2.0" | "DeviceForge (原名 DeployMaster)" |

### 不修改

- 类名：`DeployMaster`、`FtpDeployBackend`、`FtpDeployWidget` 等
- 文件名：`DeployMaster.cpp`、`DeployMaster.h`、`DeployMaster.ui`
- Tool ID：`com.deploymaster.ftp.deploy` 等
- 内部注释、CHANGELOG 中的历史引用
- lwserverbase/lwcomm 等第三方库

---

## 版本号

更名为契机，版本号从 `2.0.0` 升级为 `2.1.0`，表示改名 + 安全加固 + UI 改进的累积变更。

---

## 文件变更清单

| 文件 | 变更类型 | 预计行数 |
|------|----------|----------|
| `DeployMaster.ui` | 修改 | 1 行 |
| `CMakeLists.txt` | 修改 | 2 行 |
| `README.md` | 修改 | ~15 处 |
| `CLAUDE.md` | 修改 | ~20 处 |
| `.github/workflows/msbuild.yml` | 修改 | ~3 处 |
| `docs/superpowers/specs/*.md` | 修改 | 标题行 |
| `docs/superpowers/plans/*.md` | 修改 | 标题行 |

---

## 不变原则

- 代码零改动（除窗口标题）
- CMake 构建产物名称变化，但源文件路径不变
- Git 历史保持完整，不重写
- 旧名称 "DeployMaster" 在文档历史版本说明中可保留
