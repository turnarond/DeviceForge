# DeviceForge 构建指南

## 前置要求

| 依赖 | 版本 | 说明 |
|------|------|------|
| Visual Studio 2022 | v17.x | MSVC v143 工具集 |
| Qt | 6.10.1 | msvc2022_64 版本 |
| CMake | 3.16+ | VS2022 自带或 [下载](https://cmake.org/download/) |

## 快速开始

### 方法 1：一键构建脚本（推荐）

```cmd
build.bat
```

脚本会自动完成：
1. ✅ 检查 Qt 6.10.1 安装路径
2. ✅ 运行 CMake 配置（生成 VS2022 工程）
3. ✅ 编译 Release 版本
4. ✅ 显示运行路径

### 方法 2：CMake 手动构建

```bash
# 1. 配置（生成 VS2022 工程）
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\6.10.1\msvc2022_64"

# 2. 编译 Release
cmake --build . --config Release

# 3. 运行
.\Release\DeviceForge.exe
```

### 方法 3：Visual Studio 直接打开

```cmd
start DeployMaster.sln
```

或直接在 VS2022 中打开 `DeployMaster.sln`，选择 `Release|x64`，按 F5。

## Qt 路径配置

如果 Qt 安装在其他位置，修改以下任一方式：

**CMake 命令**：
```bash
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="D:\Qt\6.10.1\msvc2022_64"
```

**build.bat**：编辑脚本中的 `QT_PREFIX` 变量：
```batch
set QT_PREFIX=D:\Qt\6.10.1\msvc2022_64
```

## 构建输出

| 构建系统 | 输出目录 | 可执行文件 |
|---------|---------|-----------|
| CMake | `build/Release/` | `DeviceForge.exe` |
| VS/vcxproj | `x64/Release/` | `DeployMaster.exe` |

> ⚠️ **注意**：两套构建系统输出的 exe 名不同，勿混淆。CMake 产物是 `DeviceForge.exe`，VS/vcxproj 产物是 `DeployMaster.exe`。

## 常见问题

### Qt 路径错误

**错误**：
```
CMake Error: Could not find Qt6
```

**解决**：
1. 确认 `C:\Qt\6.10.1\msvc2022_64\lib\cmake\Qt6` 目录存在
2. 或修改 `CMAKE_PREFIX_PATH` 指向你的 Qt 安装路径

### libcurl DLL 未找到

VS F5 调试时，如果 `libcurl-x64.dll` 不在输出目录，需手动复制：

```cmd
copy lib\libcurl-x64.dll x64\Debug\
```

CMake 构建已通过 `POST_BUILD` 自动复制到输出目录。

### libssh2 未找到（SSH 功能不可用）

SSH 适配器依赖 libssh2（**软依赖**，未找到时仅警告）：

```cmd
dir lib\libssh2*
```

应看到：
- `lib\libssh2.lib`（导入库）
- `lib\libssh2.dll`（运行时 DLL）

如缺失，联系项目维护者或查看 `src/adapter/SshAdapter.cpp` 集成说明。

### 清理构建

```cmd
# 清理 CMake 构建
rmdir /s /q build

# 清理 VS 构建
rmdir /s /q x64

# 清理 VS 临时文件
rmdir /s /q .vs
```

## 测试

```bash
# 在 build/ 目录运行全部测试
ctest -C Release --output-on-failure

# 运行单个测试
.\tests\Release\tst_nrec.exe testRoundTrip
```

更多细节见 [CLAUDE.md](../CLAUDE.md)
