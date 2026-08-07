# DeviceForge 构建指南

## 环境要求

| 组件 | 版本 |
|------|------|
| Windows | 10/11 (x64) |
| Visual Studio | 2022 (v143 工具集) |
| Qt | 6.11.1 (MSVC 2022 64-bit) |
| CMake | 3.22+ |

## 安装 Qt

1. 下载 [Qt Online Installer](https://www.qt.io/download-qt-installer)
2. 选择组件：Qt 6.11.1 → MSVC 2022 64-bit
3. 安装 Qt Visual Studio Tools 扩展（VS 菜单 → 扩展 → 管理扩展 → 搜索 Qt）

## CMake 构建（推荐）

```bash
# 克隆仓库
git clone https://github.com/turnarond/DeviceForge.git
cd DeviceForge

# 配置
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH="C:\Qt\6.11.1\msvc2022_64"

# 编译 Release
cmake --build . --config Release

# 运行
.\Release\DeviceForge.exe
```

## Visual Studio 构建

旧 `DeployMaster.vcxproj` 已移除，CMake 是唯一构建系统：

1. 运行 `build.bat`，生成 `build/DeviceForge.sln`
2. 用 VS2022 打开 `build/DeviceForge.sln`
3. 切换到 Release 配置
4. 生成 → 生成解决方案（`lib/libcurl-x64.dll`/`libssh2.dll` 已由 CMake POST_BUILD 自动复制）

> 单元测试目标（tst_*）仅在 CMake/CTest 构建中提供。

## 运行测试

单元测试目标 `tst_nrec`（QtTest + CTest）随 CMake 构建自动生成，需要 Qt Test 模块。

```bash
cd build
# 构建测试目标
cmake --build . --config Release --target tst_nrec
# 运行（CTest 已配置 Qt DLL 路径）
ctest -C Release -R tst_nrec --output-on-failure
```

覆盖内容：`.nrec` 录制往返、坏 magic / 坏版本 / 超长 length / 截断文件拒绝、回放上行端到端（10 个用例）。

## 依赖说明

| 依赖 | 位置 | 类型 |
|------|------|------|
| lwcomm/lwlog/lwcommunicate/lwserverbase 等 | `lib/*.lib` | 预编译静态库（仅头文件可见） |
| libcurl | `lib/libcurl-x64.dll` + `lib/libcurl_imp.lib` | 运行时 DLL |
| tinyxml2 | `src/thirdparty/tinyxml2/tinyxml2.cpp` | 源码编译（MIT） |
| nanopb | `src/thirdparty/nanopb/pb_*.c` | 源码编译（zlib） |

## 打包发布

```bash
# 1. 编译 Release（POST_BUILD 已自动复制 libcurl.dll/libssh2.dll/Updater.exe 到 build\Release\）
cmake --build . --config Release

# 2. 创建发布目录（DLL 从 build\Release\ 取；darkstyle.qss 内嵌 QRC，无需外部复制）
mkdir dist\DeviceForge
copy build\Release\DeviceForge.exe dist\DeviceForge\
copy build\Release\libcurl.dll dist\DeviceForge\
copy build\Release\libssh2.dll dist\DeviceForge\
copy build\Release\Updater.exe dist\DeviceForge\
xcopy /E /I build\Release\plugins dist\DeviceForge\plugins   # SQLite 驱动等

# 3. 运行 Qt 部署工具
C:\Qt\6.11.1\msvc2022_64\bin\windeployqt.exe dist\DeviceForge\DeviceForge.exe --release --no-translations

# 4. 验证启动（部署目录直接运行，确认进程存活后再打包）
dist\DeviceForge\DeviceForge.exe

# 5. 打包
powershell Compress-Archive -Path dist\DeviceForge -DestinationPath DeviceForge-v2.5.0-win64.zip

# 6. 上传 GitHub Release
gh release upload v2.5.0 dist\DeviceForge-v2.5.0-win64.zip --clobber
```

## CI/CD

GitHub Actions (`.github/workflows/msbuild.yml`)：push/PR 到 `main` 时自动构建 Debug 配置。

> CI 使用 Qt 6.9.2，本地开发使用 Qt 6.11.1。注意避免使用仅新版 API。
