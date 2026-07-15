# Task 3 报告: Updater.exe 独立替换进程

**状态**: DONE_WITH_CONCERNS
**日期**: 2026-07-15
**分支**: `feat/online-update`

## 完成内容

- 新增 `src/updater/UpdaterMain.cpp`。
- 实现 `--manifest <path> --log <path>` 参数解析、manifest 读取和日志记录。
- 实现等待 DeviceForge.exe 退出、备份安装目录、复制更新文件、失败回滚、临时文件清理和启动新版流程。
- 使用纯 Win32 API + CRT，无 Qt 或其他第三方依赖。

## Commit hash

待提交（本任务要求的提交将在报告生成后完成）。

## 编译结果

- `cl /nologo /EHsc /std:c++17 /utf-8 /c src\\updater\\UpdaterMain.cpp`
- MSVC 环境已通过 Visual Studio 2022 `vcvars64.bat` 初始化，命令未返回编译错误。
- 当前环境的 `cmake`/`cl` 直接命令未加入 Git Bash PATH，因此无法运行不带环境初始化的原始命令；未修改 CMakeLists.txt，完整目标构建留待 Task 6。
- `git diff --check -- src/updater/UpdaterMain.cpp` 通过。

## 关注点

1. 当前 JSON 解析器仅支持简单的 `"key": "value"` 形式，路径中的引号和 JSON 转义字符不适合此轻量解析器；manifest 由本项目生成时应保持约定格式。
2. 文件复制使用 `CopyFileA`，依赖当前 Windows ANSI 代码页；若安装路径包含无法由系统代码页表示的字符，后续可改为 UTF-16 Win32 API。长路径也未启用 `\\?\\` 前缀。
3. 备份复制失败时按流程继续更新，若替换失败只能回滚已成功备份的文件；后续可将备份失败作为阻断条件以增强安全性。
4. 进程等待按可执行文件名匹配，若同名进程来自其他目录也会被等待；后续可通过进程路径进一步限定。
5. `CreateDirectoryA` 仅创建单层目录，manifest 中的目标目录应由前置流程保证存在或其父目录已存在。
6. 暂不修改 `CMakeLists.txt`，由 Task 6 统一加入独立 `Updater` target。
