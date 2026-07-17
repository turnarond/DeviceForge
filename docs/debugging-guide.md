# 调试问题排查指南

## 问题：RelWithDebInfo 无法调试

### 问题原因

Visual Studio 的 **RelWithDebInfo** 配置默认不生成调试符号（PDB 文件），导致无法调试。

**症状**：
- ✅ 构建成功
- ❌ 点击"本地 Windows 调试器"无响应
- ❌ 断点不命中
- ❌ `build/RelWithDebInfo/` 下没有 `.pdb` 文件

### 解决方案

**已修复**：CMakeLists.txt 第 169-182 行已添加 RelWithDebInfo 调试支持：

```cmake
# 设置所有配置类型的调试信息生成（包括 RelWithDebInfo）
set_target_properties(DeviceForge PROPERTIES
    DEBUG_POSTFIX "d"
    RELWITHDEBINFO_POSTFIX ""
)

# 确保 RelWithDebInfo 生成调试信息
target_compile_options(DeviceForge PRIVATE
    $<$<CONFIG:RelWithDebInfo>:/Zi>
)
target_link_options(DeviceForge PRIVATE
    $<$<CONFIG:RelWithDebInfo>:/DEBUG>
)
```

---

## 使用 VS2022 调试的步骤

### 方法 1：使用 CMake 生成的工程（推荐）

1. **生成工程**
   ```cmd
   build.bat
   ```

2. **打开 VS2022**
   ```cmd
   start build\DeviceForge.sln
   ```

3. **选择配置**
   - 在 VS 顶部工具栏：`解决方案配置` → 选择 `RelWithDebInfo`
   - `解决方案平台` → 选择 `x64`

4. **设置调试**
   - 在 `DeviceForge` 项目上右键 → `属性`
   - `配置属性` → `调试`
   - `要启动的调试器` → `本地 Windows 调试器`
   - `命令` → `$(TargetPath)`（默认）

5. **开始调试**
   - 按 `F5` 或点击 `开始调试` 按钮

### 方法 2：直接打开 DeployMaster.sln

如果你使用原来的 `DeployMaster.sln`：

1. 打开 VS2022 → `文件` → `打开` → `项目/解决方案`
2. 选择 `DeployMaster.sln`
3. 确保配置为 `RelWithDebInfo | x64`
4. 按 `F5` 调试

---

## 常见调试问题

### Qt 版本不匹配

**症状**：
```
CMAKE_PREFIX_PATH 指向 Qt 6.10.1，但系统实际安装 Qt 6.11.1
```

**解决**：修改 `build.bat` 中的 `QT_PREFIX`：
```batch
set QT_PREFIX=C:\Qt\6.11.1\msvc2022_64  # 改为实际安装路径
```

### 构建目录被占用

**症状**：
```
rmdir: cannot remove 'build': Device or resource busy
```

**解决**：
1. 关闭所有 VS2022 窗口（包括后台进程）
2. 打开任务管理器 → 结束 `devenv.exe`、`MSBuild.exe` 进程
3. 重新运行 `build.bat`

### lw* 库运行时库不匹配

**症状**：
```
error LNK2038: 检测到"RuntimeLibrary"的不匹配项
值"MD_DynamicRelease"不匹配值"MDd_DynamicDebug"
```

**原因**：lwcomm/lwlog/lwcommunicate/lwserverbase 等第三方库是用 Release 编译的，
但你在 Debug 配置下链接它们。

**解决**：
- **方案 A**：改用 `RelWithDebInfo` 配置（推荐）
- **方案 B**：用 Release 配置
- **方案 C**：重新编译所有 lw* 库为 Debug 版本（不推荐）

### libcurl/libssh2 DLL 找不到

**VS F5 调试时**：
```cmd
copy lib\libcurl-x64.dll x64\Debug\
copy lib\libssh2.dll x64\Debug\
```

**CMake 构建**：已通过 `POST_BUILD` 自动复制到输出目录。

---

## 推荐配置

| 场景 | 配置 | 原因 |
|------|------|------|
| **日常调试** | RelWithDebInfo | 有优化 + 有调试信息，性能接近 Release |
| **深度调试** | Debug | 无优化，调试体验最佳（但注意 lw* 库不匹配） |
| **发布测试** | Release | 与最终发布一致 |

---

## 验证调试是否成功

1. 设置断点（在代码行号左边点击）
2. 按 `F5` 启动调试
3. 程序运行到断点 → **自动暂停** → 黄色箭头出现
4. 查看变量值 → `鼠标悬停` 或 `调试` → `窗口` → `局部变量`

如果断点不命中：
- 确认配置是 `RelWithDebInfo`（不是 `Release`）
- 确认 `.pdb` 文件在输出目录存在
- 清理重新构建：`rmdir /s /q build && build.bat`

---

## 技术细节

### 为什么 RelWithDebInfo 默认不生成 PDB？

RelWithDebInfo 是"带调试信息的发布版"，默认行为：
- `/O2`（最大速度优化）
- **不生成 PDB**（节省空间）

我们通过 CMake 强制添加：
- `/Zi`（编译时生成完整调试信息）
- `/DEBUG`（链接器生成 PDB 文件）

这样既保留优化，又支持调试。

### 调试符号格式

| 配置 | DebugInformationFormat | PDB 文件 |
|------|----------------------|---------|
| Debug | ProgramDatabase | ✅ DeviceForge.pdb |
| Release | 空（不生成） | ❌ 无 |
| RelWithDebInfo | **ProgramDatabase**（已修复） | ✅ DeviceForge.pdb |
| MinSizeRel | 空（不生成） | ❌ 无 |
