# DeviceForge 构建指南

## 环境要求

| 组件 | 版本 |
|------|------|
| Windows | 10/11 (x64) |
| Visual Studio | 2022 (v143 工具集) |
| Qt | 6.10.1 (MSVC 2022 64-bit) |
| CMake | 3.16+ |

## 安装 Qt

1. 下载 [Qt Online Installer](https://www.qt.io/download-qt-installer)
2. 选择组件：Qt 6.10.1 → MSVC 2022 64-bit
3. 安装 Qt Visual Studio Tools 扩展（VS 菜单 → 扩展 → 管理扩展 → 搜索 Qt）

## CMake 构建（推荐）

```bash
# 克隆仓库
git clone https://github.com/turnarond/DeviceForge.git
cd DeviceForge

# 配置
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH="C:\Qt\6.10.1\msvc2022_64"

# 编译 Release
cmake --build . --config Release

# 运行
.\Release\DeviceForge.exe
```

## Visual Studio 构建

1. 用 VS2022 打开 `DeployMaster.vcxproj`
2. 配置 Qt 版本：项目属性 → Qt Project Settings → Qt Installation
3. 切换到 Release 配置
4. 生成 → 生成解决方案
5. 确保 `lib/libcurl-x64.dll` 在输出目录（已通过构建后脚本自动复制）

## 依赖说明

| 依赖 | 位置 | 类型 |
|------|------|------|
| lwcomm/lwlog/lwcommunicate/lwserverbase 等 | `lib/*.lib` | 预编译静态库（仅头文件可见） |
| libcurl | `lib/libcurl-x64.dll` + `lib/libcurl_imp.lib` | 运行时 DLL |
| tinyxml2 | `src/thirdparty/tinyxml2/tinyxml2.cpp` | 源码编译（MIT） |
| nanopb | `src/thirdparty/nanopb/pb_*.c` | 源码编译（zlib） |

## 打包发布

```bash
# 1. 编译 Release
cmake --build . --config Release

# 2. 创建发布目录
mkdir dist\DeviceForge
copy build\Release\DeviceForge.exe dist\DeviceForge\
copy lib\libcurl-x64.dll dist\DeviceForge\
copy darkstyle.qss dist\DeviceForge\

# 3. 运行 Qt 部署工具
C:\Qt\6.10.1\msvc2022_64\bin\windeployqt.exe dist\DeviceForge\DeviceForge.exe --release --no-translations

# 4. 打包
powershell Compress-Archive -Path dist\DeviceForge -DestinationPath DeviceForge-v2.1.0-win64.zip
```

## CI/CD

GitHub Actions (`.github/workflows/msbuild.yml`)：push/PR 到 `main` 时自动构建 Debug 配置。

> CI 使用 Qt 6.9.2，本地开发使用 Qt 6.10.1。注意避免使用仅新版 API。
