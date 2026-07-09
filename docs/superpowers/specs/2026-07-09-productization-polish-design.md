# DeviceForge 产品化打磨设计（去公司标识 + 图标 + 版本信息 + 关闭 console）

- **日期**：2026-07-09
- **状态**：已确认（brainstorming 逐条确认）
- **归属**：发布前产品化收尾

## 背景

v2.1.0 功能与文档已就绪并发布，但发布版（CMake 产物 `DeviceForge.exe`）在"产品化"上还差三块，且散落着不应对外的公司标识：

1. **启动弹 console 黑框**：CMake `add_executable` 未加 `WIN32` → 默认 console 子系统。
2. **图标是默认的**：无 `.ico`、rc 无 ICON 条目、代码无 `setWindowIcon`，且 CMake 未编译 rc。
3. **版本/版权信息缺失或含公司标识**：rc 有 VERSIONINFO 但含"南京翼辉"、字段为占位符/旧名；CMake 未编译 rc 导致产物完全无版本信息。
4. **公司标识散布**：LICENSE、24 个源码版权头、rc CompanyName 含 `ACOINFO`/`翼辉`/公司邮箱——本项目为个人开源项目，需改为个人身份。

根因归纳：**多处仍是 CMake/vcxproj 构建不对等**（rc 只在 vcxproj 编译，WIN32 子系统只在 vcxproj 设置）。本次一并对齐。

## 决策（已确认）

### 署名策略
- **LICENSE 版权人**：`yanchaodong`（真名，法律署名，仅此处）
- **其余所有对外展示面**（rc CompanyName、源码版权头、exe 属性）：`turnarond`（GitHub 账号名，与项目主页一致）
- **全项目 0 处** `ACOINFO` / `翼辉` / `@acoinfo.com`（第三方库来源描述除外，见下）
- **例外——保留**：`docs/architecture.md` 与 `CMakeLists.txt` 中"lw* 库来自 ACOINFO"的描述——这是**客观的依赖来源事实**，非本项目署名，保留。

### rc 版本/版权字段（最终值）
| 字段 | 值 |
|------|-----|
| ProductName | `DeviceForge` |
| FileDescription | `工业级设备批量运维平台` |
| CompanyName | `turnarond` |
| LegalCopyright | `Copyright (C) 2024-2026 turnarond` |
| FileVersion / ProductVersion | `2.1.0.0` |
| InternalName / OriginalFilename | `DeviceForge.exe` |

### console
- **彻底关闭**：CMake `add_executable(DeviceForge WIN32 ...)`，与 vcxproj `SubSystem=Windows` 对齐。
- 保留 `LogBridge.cpp` 里的 `fprintf(stderr,...)`（无 console 时 stderr 无处显示，不影响；日志仍走 lwlog）。附带效果：console 消失后，此前的 UTF-8/GBK 中文乱码问题自然不可见。

### 图标
- 用户提供 logo 图 → 转多尺寸 `app.ico`（16/32/48/256）放仓库根目录。
- rc 加 `IDI_ICON1 ICON "app.ico"`（任务栏/文件管理器图标）。
- QRC 打包 `app.ico`；`main.cpp` 加 `window.setWindowIcon(QIcon(":/app.ico"))`（窗口左上角 + 任务栏运行时图标）。

## 变更清单

### 文件结构

| 文件 | 动作 | 说明 |
|------|------|------|
| `app.ico` | 新建 | 多尺寸图标（由用户 logo 转换） |
| `DeployMaster.rc` | 修改 | 字段改为最终值 + 加 ICON 条目 + CompanyName turnarond |
| `LICENSE` | 修改 | 版权人 → `yanchaodong` |
| `CMakeLists.txt` | 修改 | ① `add_executable` 加 `WIN32` ② SOURCES 加 `DeployMaster.rc`（AUTORCC 不处理 .rc，需显式加入让 MSVC 资源编译器处理）③ 版权注释去 ACOINFO |
| `DeployMaster.qrc` | 修改 | 加 `<file>app.ico</file>`（供 setWindowIcon） |
| `main.cpp` | 修改 | `window.show()` 前加 `setWindowIcon` |
| 24 个 `src/**/*.cpp/.h` | 修改 | 版权头 `ACOINFO CloudNative Team` → `turnarond`，作者行去公司邮箱 |
| `docs/architecture.md` | 不改 | lw* 库来源描述保留 |

### 源码版权头统一格式（24 文件）
```
/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: <保留原文件名>
 * Date: <保留原日期>
 * Author: turnarond
 *
 * Description: <保留原描述>
 */
```
只替换版权行 + 作者行，File/Date/Description 原样保留。

## 实现要点与风险

1. **rc 编码**：`DeployMaster.rc` 是 UTF-16LE。修改时必须保持 UTF-16LE 编码，否则 MSVC `rc.exe` 解析中文字段（CompanyName 等）会乱码。用支持编码的写入方式，改后校验。
2. **CMake 编译 .rc**：MSVC 下把 `.rc` 直接加进 `add_executable` 的 SOURCES，CMake 会自动调用 rc 编译器链接资源（AUTORCC 只管 Qt `.qrc`，不管 Windows `.rc`，二者不同）。
3. **WIN32 + Qt**：`add_executable(DeviceForge WIN32 ...)` 后入口仍是标准 `main()`（Qt 的 `qtmain`/`WinMain` 由 Qt6::Core 链接处理），不需要改 `main` 签名。需验证链接不报 `WinMain` 缺失。
4. **setWindowIcon 资源路径**：QRC 里 app.ico 的前缀要与 `QIcon(":/...")` 匹配。现有 qrc 有 `styles` 前缀；app.ico 可放根前缀或新前缀，spec 实现时对齐。
5. **双构建验证**：CMake（DeviceForge.exe）和 VS（DeployMaster.exe）都要验证图标 + 版本信息生效。发布用 CMake 产物为主。

## 验证

- 构建后 `DeviceForge.exe` 右键属性→详细信息：显示 ProductName/Version/Copyright（turnarond，无翼辉）。
- 双击运行：无 console 黑框；任务栏 + 窗口左上角为自定义图标。
- 全项目 `grep -rn "ACOINFO\|翼辉\|acoinfo.com"`（排除 build/thirdparty/.git/历史 diff）：仅剩 architecture.md/CMakeLists 的库来源描述。
- LICENSE 版权人为 yanchaodong。

## 非目标

- 不改 lw* 第三方库来源描述
- 不做安装包（NSIS/MSI）——本次仅 exe 元信息，安装包是独立课题
- 不改功能代码逻辑，仅版权头 + 构建配置 + 资源
