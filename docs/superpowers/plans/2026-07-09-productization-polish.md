# DeviceForge 产品化打磨 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 发布前产品化收尾——关闭 console 黑框、加自定义图标、补全 exe 版本/版权信息、清除全项目公司标识改用个人身份。

**Architecture:** 集中在构建配置 + Windows 资源(.rc/.ico)+ 版权头文本,不改功能逻辑。核心是修复 CMake/vcxproj 不对等(rc 未编译、WIN32 子系统未设),让两套构建都产出带图标+版本信息、无 console 的 exe。

**Tech Stack:** CMake 3.16 + MSVC(rc.exe 资源编译)、Qt 6.10.1(Qt6::EntryPoint 处理 WIN32 入口 + QIcon)、Windows VERSIONINFO、Python 3.14 / PowerShell(.ico 生成)。

## Global Constraints

- 署名：LICENSE 版权人用真名 `yanchaodong`（仅此处）；其余所有对外展示面（rc CompanyName、24 源码版权头、exe 属性）用 `turnarond`。
- 全项目 0 处 `ACOINFO` / `翼辉` / `@acoinfo.com`，**例外保留**：`docs/architecture.md` 与 `CMakeLists.txt` 中描述 lw* 库"来自 ACOINFO / ACOINFO 内部"的依赖来源事实。
- rc 字段最终值：ProductName=`DeviceForge`、FileDescription=`工业级设备批量运维平台`、CompanyName=`turnarond`、LegalCopyright=`Copyright (C) 2024-2026 turnarond`、FileVersion/ProductVersion=`2.1.0.0`、InternalName/OriginalFilename=`DeviceForge.exe`。
- `DeployMaster.rc` 必须保持 **UTF-16LE** 编码（含 BOM），否则 rc.exe 解析中文字段乱码。
- 源码版权头统一格式（只换版权行+作者行，File/Date/Description 原样保留）：
  ```
  * Copyright (c) 2024-2026 turnarond.
  * All rights reserved.
  ...
  * Author: turnarond
  ```
- 图标源：`docs/images/logo.png`（1328×1328 PNG）→ 生成 `app.ico`（含 16/32/48/256 多尺寸）放仓库根目录。
- 双构建都要验证：CMake 产 `DeviceForge.exe`、VS 产 `DeployMaster.exe`。发布以 CMake 产物为主。
- 设计依据：`docs/superpowers/specs/2026-07-09-productization-polish-design.md`。
- 环境：验证运行需 `export PATH="/c/Qt/6.10.1/msvc2022_64/bin:$PATH"` 且输出目录有 `libcurl.dll`（POST_BUILD 已自动复制）。

---

## 文件结构

| 文件 | 动作 | 责任 |
|------|------|------|
| `app.ico` | 新建 | 多尺寸应用图标（logo.png 转换） |
| `CMakeLists.txt` | 修改 | `add_executable` 加 `WIN32` + SOURCES 加 `DeployMaster.rc` |
| `DeployMaster.rc` | 重写 | 版本/版权字段定稿 + ICON 条目（UTF-16LE） |
| `DeployMaster.qrc` | 修改 | 加 `app.ico` 资源条目 |
| `main.cpp` | 修改 | `setWindowIcon` |
| `LICENSE` | 修改 | 版权人 → yanchaodong |
| `src/**/*.cpp/.h`（24 个） | 修改 | 版权头 → turnarond |

---

## Task 1: 生成 app.ico

**Files:**
- Create: `app.ico`（仓库根目录）
- 源：`docs/images/logo.png`（1328×1328）

**Interfaces:**
- Produces: 根目录 `app.ico`，含 256/48/32/16 多尺寸，供 Task 3（rc ICON）和 Task 4（QRC/setWindowIcon）使用。

- [ ] **Step 1: 优先用 Python Pillow 生成多尺寸 ico**

先尝试装 Pillow（Python 3.14 已装，pip 用 `python -m pip`）：
```bash
cd /d/persional/QtDeployMaster
python -m pip install Pillow 2>&1 | tail -2
python -c "
from PIL import Image
img = Image.open('docs/images/logo.png').convert('RGBA')
img.save('app.ico', sizes=[(256,256),(48,48),(32,32),(16,16)])
print('app.ico written')
"
```
Expected: 输出 `app.ico written`。

- [ ] **Step 2: 若 Pillow 装不上，用 PowerShell + .NET 兜底（单尺寸 256，可接受）**

仅当 Step 1 失败时执行：
```bash
powershell -Command "
Add-Type -AssemblyName System.Drawing;
\$png = [System.Drawing.Image]::FromFile('D:\persional\QtDeployMaster\docs\images\logo.png');
\$bmp = New-Object System.Drawing.Bitmap(\$png, 256, 256);
\$hicon = \$bmp.GetHicon();
\$icon = [System.Drawing.Icon]::FromHandle(\$hicon);
\$fs = [System.IO.File]::Create('D:\persional\QtDeployMaster\app.ico');
\$icon.Save(\$fs); \$fs.Close();
Write-Output 'app.ico written (256 single-size)'
"
```
Expected: 输出 app.ico written。

- [ ] **Step 3: 验证 ico 生成**

Run:
```bash
cd /d/persional/QtDeployMaster && ls -la app.ico && file app.ico 2>/dev/null
```
Expected: 文件存在，`file` 报 `MS Windows icon resource`（或至少非空二进制）。

- [ ] **Step 4: Commit**

```bash
cd /d/persional/QtDeployMaster
git add app.ico
git commit -m "assets: 新增 app.ico 应用图标(由 logo.png 生成)"
```

---

## Task 2: CMake 关闭 console + 编译 rc

**Files:**
- Modify: `CMakeLists.txt`（`add_executable` 行 + SOURCES 段 + 注释去 ACOINFO）

**Interfaces:**
- Consumes: `DeployMaster.rc`（Task 3 会重写内容，但本任务只需它作为文件存在并被 CMake 纳入编译；rc 当前已存在）。
- Produces: CMake 构建的 `DeviceForge.exe` 无 console 窗口、链接 Windows 资源（图标+版本信息随 Task 3 rc 内容生效）。

- [ ] **Step 1: add_executable 加 WIN32 + 把 rc 加入 SOURCES**

在 `CMakeLists.txt` 中，`add_executable(DeviceForge ${SOURCES})` 改为：
```cmake
# 创建可执行文件（WIN32：GUI 子系统，不弹 console 黑框）
add_executable(DeviceForge WIN32 ${SOURCES} DeployMaster.rc)
```
（`DeployMaster.rc` 直接列在 add_executable，MSVC 下 CMake 自动调用 rc 编译器；Qt6::Gui/Widgets 已链接会带入 Qt6::EntryPoint，`int main(argc,argv)` 入口在 WIN32 子系统下正常工作，无需改 main 签名。）

- [ ] **Step 2: CMake 注释去 ACOINFO 署名（保留库来源事实）**

`CMakeLists.txt` 第 36 行注释：
```
# Thirdparty 静态库（预编译 .lib，源码为 ACOINFO 内部库）
```
保持不变——这是描述 lw* 库来源的事实，属 Global Constraints 的保留例外。**本步无需改动**，仅确认无其他 ACOINFO 署名（`grep -n "ACOINFO" CMakeLists.txt` 应只有这一行库来源描述）。

- [ ] **Step 3: 构建验证 console 消失**

Run:
```bash
cd /d/persional/QtDeployMaster/build && cmake .. -DCMAKE_PREFIX_PATH="C:/Qt/6.10.1/msvc2022_64" >/tmp/cfg.log 2>&1 && cmake --build . --config Release 2>&1 | tail -4
```
Expected: 构建成功，链接 `DeviceForge.exe`，无 `WinMain` 缺失错误。

- [ ] **Step 4: 运行确认无 console**

Run:
```bash
cd /d/persional/QtDeployMaster/build/Release && export PATH="/c/Qt/6.10.1/msvc2022_64/bin:$PATH" && ./DeviceForge.exe & PID=$!; sleep 4; kill -0 $PID 2>/dev/null && echo "ALIVE 无console" && taskkill //F //PID $PID 2>/dev/null || echo "EXITED"
```
Expected: `ALIVE 无console`（进程存活；WIN32 子系统下不再有 stderr 日志刷屏——若从 bash 启动仍可能看到 stdout，但独立双击不再弹黑框，这是子系统属性决定的）。

- [ ] **Step 5: Commit**

```bash
cd /d/persional/QtDeployMaster
git add CMakeLists.txt
git commit -m "fix: CMake 关闭 console(WIN32 子系统) + 编译 DeployMaster.rc(图标/版本信息)"
```

---

## Task 3: 重写 rc — 版本/版权字段 + ICON

**Files:**
- Modify: `DeployMaster.rc`（UTF-16LE 重写）

**Interfaces:**
- Consumes: `app.ico`（Task 1）。
- Produces: rc 含正确 VERSIONINFO（turnarond/DeviceForge）+ `IDI_ICON1 ICON "app.ico"`，被 Task 2 的 CMake 编译进 exe。

- [ ] **Step 1: 用 Python 以 UTF-16LE 写出新 rc**

rc 编码敏感（当前是 UTF-16LE），用 Python 精确写出，避免手改 UTF-16 字节出错：
```bash
cd /d/persional/QtDeployMaster
python - <<'PY'
content = '''#include "resource.h"
#include "winres.h"

/////////////////////////////////////////////////////////////////////////////
// 图标
IDI_ICON1  ICON  "app.ico"

/////////////////////////////////////////////////////////////////////////////
// 版本信息
#if !defined(AFX_RESOURCE_DLL) || defined(AFX_TARG_CHS)
LANGUAGE LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED

VS_VERSION_INFO VERSIONINFO
 FILEVERSION 2,1,0,0
 PRODUCTVERSION 2,1,0,0
 FILEFLAGSMASK 0x3fL
#ifdef _DEBUG
 FILEFLAGS 0x1L
#else
 FILEFLAGS 0x0L
#endif
 FILEOS 0x40004L
 FILETYPE 0x1L
 FILESUBTYPE 0x0L
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "080404b0"
        BEGIN
            VALUE "CompanyName", "turnarond"
            VALUE "FileDescription", "\\u5de5\\u4e1a\\u7ea7\\u8bbe\\u5907\\u6279\\u91cf\\u8fd0\\u7ef4\\u5e73\\u53f0"
            VALUE "FileVersion", "2.1.0.0"
            VALUE "InternalName", "DeviceForge.exe"
            VALUE "LegalCopyright", "Copyright (C) 2024-2026 turnarond"
            VALUE "OriginalFilename", "DeviceForge.exe"
            VALUE "ProductName", "DeviceForge"
            VALUE "ProductVersion", "2.1.0.0"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x804, 1200
    END
END

#endif
'''
# FileDescription 里的 \\uXXXX 是 "工业级设备批量运维平台"，此处用 Python 解码为真实中文
content = content.encode('utf-8').decode('unicode_escape') if False else content
# 直接写真实中文，避免 escape 歧义：
content = content.replace('\\\\u5de5\\\\u4e1a\\\\u7ea7\\\\u8bbe\\\\u5907\\\\u6279\\\\u91cf\\\\u8fd0\\\\u7ef4\\\\u5e73\\\\u53f0', '工业级设备批量运维平台')
with open('DeployMaster.rc', 'w', encoding='utf-16-le') as f:
    f.write('﻿')  # BOM
    f.write(content)
print('rc written UTF-16LE')
PY
```
Expected: 输出 `rc written UTF-16LE`。

> 注：若上面 replace 分支处理 escape 显繁琐，实现者可直接在字符串里写真实中文 `"工业级设备批量运维平台"`，只要最终以 `encoding='utf-16-le'` + BOM 写出即可。关键约束是**编码 UTF-16LE + BOM**，字段值见 Global Constraints。

- [ ] **Step 2: 校验 rc 编码与内容**

Run:
```bash
cd /d/persional/QtDeployMaster
file DeployMaster.rc && echo "---" && iconv -f UTF-16LE -t UTF-8 DeployMaster.rc | grep -E "ICON|CompanyName|ProductName|LegalCopyright|FileDescription|OriginalFilename"
```
Expected: `file` 报 `Little-endian UTF-16 Unicode text`；grep 显示 `IDI_ICON1 ICON "app.ico"`、CompanyName=turnarond、ProductName=DeviceForge、版权 turnarond、FileDescription 中文正常、OriginalFilename=DeviceForge.exe。**不得出现 南京翼辉 / DeployMa.exe / TODO**。

- [ ] **Step 3: 确认 resource.h 有 IDI_ICON1 定义**

Run:
```bash
cd /d/persional/QtDeployMaster && cat resource.h
```
若 `resource.h` 未定义 `IDI_ICON1`，追加一行（resource.h 是 ASCII，可直接 echo 追加）：
```bash
grep -q "IDI_ICON1" resource.h || printf '\n#define IDI_ICON1 101\n' >> resource.h
```
Expected: resource.h 含 `#define IDI_ICON1 101`（或已有定义）。

- [ ] **Step 4: 重新构建并验证 exe 版本信息 + 图标**

Run:
```bash
cd /d/persional/QtDeployMaster/build && cmake --build . --config Release 2>&1 | tail -3
powershell -Command "(Get-Item 'D:\persional\QtDeployMaster\build\Release\DeviceForge.exe').VersionInfo | Format-List ProductName,FileDescription,CompanyName,LegalCopyright,ProductVersion"
```
Expected: PowerShell 输出 ProductName=DeviceForge、CompanyName=turnarond、LegalCopyright=Copyright (C) 2024-2026 turnarond、FileDescription=工业级设备批量运维平台。

- [ ] **Step 5: Commit**

```bash
cd /d/persional/QtDeployMaster
git add DeployMaster.rc resource.h
git commit -m "feat: rc 版本/版权信息定稿(turnarond/DeviceForge) + 应用图标条目"
```

---

## Task 4: 窗口图标（QRC + setWindowIcon）

**Files:**
- Modify: `DeployMaster.qrc`（加 app.ico 条目）, `main.cpp`（setWindowIcon）

**Interfaces:**
- Consumes: `app.ico`（Task 1）。
- Produces: 运行时窗口左上角 + 任务栏显示自定义图标。

- [ ] **Step 1: QRC 加 app.ico 资源**

`DeployMaster.qrc` 现有 `styles` 前缀。加一个 `icons` 前缀条目。改为：
```xml
<RCC>
    <qresource prefix="DeployMaster">
    </qresource>
    <qresource prefix="styles">
        <file>darkstyle.qss</file>
    </qresource>
    <qresource prefix="icons">
        <file>app.ico</file>
    </qresource>
</RCC>
```
（资源路径将是 `:/icons/app.ico`。）

- [ ] **Step 2: main.cpp 加 setWindowIcon**

`main.cpp` 中 `#include` 段加 `#include <QIcon>`（若无）；在 `DeployMaster window;`（第 43 行）之后、`window.show();` 之前加：
```cpp
    window.setWindowIcon(QIcon(":/icons/app.ico"));
```
同时给 QApplication 设置（任务栏图标更可靠）——在 `QApplication app(argc, argv);` 之后加：
```cpp
    app.setWindowIcon(QIcon(":/icons/app.ico"));
```

- [ ] **Step 3: 构建 + 冒烟验证窗口图标**

Run:
```bash
cd /d/persional/QtDeployMaster/build && cmake --build . --config Release 2>&1 | tail -3
cd Release && export PATH="/c/Qt/6.10.1/msvc2022_64/bin:$PATH" && ./DeviceForge.exe & PID=$!; sleep 4; kill -0 $PID 2>/dev/null && echo "ALIVE" && taskkill //F //PID $PID 2>/dev/null || echo "EXITED"
```
Expected: `ALIVE`（构建含 app.ico 资源；窗口左上角图标需人工目视确认，冒烟只验证不崩溃）。

- [ ] **Step 4: Commit**

```bash
cd /d/persional/QtDeployMaster
git add DeployMaster.qrc main.cpp
git commit -m "feat: 窗口/任务栏图标(QRC 打包 app.ico + setWindowIcon)"
```

---

## Task 5: LICENSE 版权人改真名

**Files:**
- Modify: `LICENSE`

- [ ] **Step 1: 改版权行**

`LICENSE` 第 3 行 `Copyright (c) 2024-2026 ACOINFO CloudNative Team` 改为：
```
Copyright (c) 2024-2026 yanchaodong
```

- [ ] **Step 2: 校验**

Run: `cd /d/persional/QtDeployMaster && head -3 LICENSE && grep -c "ACOINFO" LICENSE`
Expected: 第 3 行为 yanchaodong；`grep -c ACOINFO` 输出 `0`。

- [ ] **Step 3: Commit**

```bash
cd /d/persional/QtDeployMaster
git add LICENSE
git commit -m "docs: LICENSE 版权人改为 yanchaodong"
```

---

## Task 6: 24 个源码版权头去公司标识

**Files:**
- Modify: `src/framework/{DeviceInfo.h,ToolBackend.h,ToolHost.cpp,ToolHost.h,ToolWidget.cpp,ToolWidget.h}`、`src/tools/FtpDeployTool/*`、`src/tools/NetRelayTool/*`、`src/tools/TelnetTool/*`、`src/tools/WebSocketTool/*`、`src/ui/DeviceBusWidget.{cpp,h}`（共 24 个含 ACOINFO 的文件）

**Interfaces:**
- 无代码接口变化，纯注释文本替换。

- [ ] **Step 1: 批量替换版权行 + 作者行（sed，仅这两类行）**

版权头是统一格式，用 sed 精确替换两类字符串（仅改注释文本，不碰代码）：
```bash
cd /d/persional/QtDeployMaster
FILES=$(grep -rl "ACOINFO\|acoinfo.com" --include="*.cpp" --include="*.h" src/ | grep -v thirdparty)
for f in $FILES; do
  sed -i \
    -e 's/Copyright (c) 2024 ACOINFO CloudNative Team\./Copyright (c) 2024-2026 turnarond./' \
    -e 's/Author: Yan\.chaodong <yanchaodong@acoinfo\.com>/Author: turnarond/' \
    "$f"
done
echo "processed: $(echo "$FILES" | wc -l) files"
```
Expected: `processed: 24 files`。

- [ ] **Step 2: 校验无残留**

Run:
```bash
cd /d/persional/QtDeployMaster
grep -rn "ACOINFO\|acoinfo.com\|翼辉" --include="*.cpp" --include="*.h" src/ | grep -v thirdparty || echo "✓ 源码 0 残留"
```
Expected: `✓ 源码 0 残留`。若有残留（个别文件版权头格式不同），手动改那几处版权行/作者行为 turnarond。

- [ ] **Step 3: 构建确认未破坏代码**

Run: `cd /d/persional/QtDeployMaster/build && cmake --build . --config Release 2>&1 | tail -3`
Expected: 构建成功（纯注释改动，不影响编译）。

- [ ] **Step 4: Commit**

```bash
cd /d/persional/QtDeployMaster
git add src/
git commit -m "docs: 源码版权头去公司标识,统一为 turnarond(24 文件)"
```

---

## Task 7: 全局收尾校验 + vcxproj 对齐

**Files:**
- Modify: `DeployMaster.vcxproj`（若 rc ICON 需同步）— 仅在验证发现 VS 构建缺图标时

**Interfaces:**
- 无新增，最终一致性验证。

- [ ] **Step 1: 全项目 ACOINFO/翼辉 终检**

Run:
```bash
cd /d/persional/QtDeployMaster
grep -rn "ACOINFO\|翼辉\|acoinfo.com" . 2>/dev/null | grep -viE "build/|build_|\.git/|thirdparty|CMakeFiles|\.claude|\.vs/|\.superpowers/|\.omc/|dist/|x64/|\.exe"
```
Expected: 仅剩 `CMakeLists.txt` 库来源注释 + `docs/architecture.md` 的 lw* 库来源描述（Global Constraints 保留例外）。其余 0 处。

- [ ] **Step 2: VS 构建验证图标/版本一致（vcxproj 已含 ResourceCompile DeployMaster.rc）**

vcxproj 第 245 行已有 `<ResourceCompile Include="DeployMaster.rc" />`，rc 内容 Task 3 已更新，VS 构建自动生效。确认 vcxproj 无需改动：
```bash
cd /d/persional/QtDeployMaster && grep -n "ResourceCompile" DeployMaster.vcxproj
```
Expected: 显示 `<ResourceCompile Include="DeployMaster.rc" />`（已存在，无需改）。VS 侧图标/版本随共享的 rc 自动生效。

- [ ] **Step 3: 最终 exe 属性人工确认清单**

打包/发布前目视确认（记录到 commit 或交付说明）：
- [ ] 双击 exe 无 console 黑框
- [ ] 任务栏 + 窗口左上角为 app.ico 图标
- [ ] exe 右键→属性→详细信息：产品名 DeviceForge、版本 2.1.0.0、版权 turnarond、无翼辉字样

- [ ] **Step 4: 文档同步 + Commit**

按 CLAUDE.md「文档同步」要求，若 spec/CLAUDE.md 需记录产品化状态则更新（CLAUDE.md 可在「注意事项」加一条：exe 元信息/图标已配置，构建产物带版本信息）。
```bash
cd /d/persional/QtDeployMaster
git add -A
git commit -m "docs: 产品化收尾校验(ACOINFO 清理完成 + exe 元信息验证)"
```

---

## Self-Review（对照 spec）

- **spec 第 1 部分（去公司标识）** → Task 5（LICENSE）+ Task 6（24 源码头）+ Task 3（rc CompanyName）+ Task 7（终检）。✅
- **spec 第 2 部分（rc 字段）** → Task 3，字段值与 Global Constraints/spec 表逐条对应。✅
- **spec 第 3 部分（关 console）** → Task 2（WIN32）。✅
- **spec 第 4 部分（图标）** → Task 1（ico 生成）+ Task 3（rc ICON）+ Task 2（CMake 编译 rc）+ Task 4（QRC/setWindowIcon）。✅
- **保留例外（lw* 库来源）** → Task 2 Step 2 + Task 7 Step 1 明确保留。✅
- **双构建验证** → Task 3 Step 4（CMake exe 属性）+ Task 7 Step 2（vcxproj）。✅
- **UTF-16LE 约束** → Task 3 Step 1（Python 写 + BOM）+ Step 2（file 校验）。✅
- **署名分工（LICENSE 真名 / 其余 turnarond）** → Task 5 用 yanchaodong，Task 3/6 用 turnarond，一致。✅
- **占位符扫描**：无 TBD/TODO；每步含可执行命令/代码。✅
- **类型一致性**：本计划无跨任务代码类型；资源路径 `:/icons/app.ico`（Task 4 Step 1 QRC 前缀 `icons` + Step 2 setWindowIcon 引用一致）。✅
