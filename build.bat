@echo off
REM ============================================================
REM  DeviceForge Build Script
REM  用途：一键配置并生成 Visual Studio 2022 工程
REM  前置条件：
REM    - Qt 6.10.1 已安装（默认路径 C:\Qt\6.10.1\msvc2022_64）
REM    - Visual Studio 2022（v143 工具集）已安装
REM ============================================================

setlocal

echo.
echo ============================================================
echo  DeviceForge Build Script
echo ============================================================
echo.

REM ---------- 配置 ----------
set PROJECT_ROOT=%~dp0
set BUILD_DIR=%PROJECT_ROOT%build
set QT_PREFIX=C:\Qt\6.10.1\msvc2022_64

REM ---------- 检查 Qt 路径 ----------
echo [1/4] 检查 Qt 安装...
if not exist "%QT_PREFIX%\bin\qmake.exe" (
    echo [错误] 找不到 Qt 6.10.1，请修改 QT_PREFIX 变量
    echo        当前路径：%QT_PREFIX%
    echo        如果 Qt 安装在其他位置，请编辑此脚本修改 QT_PREFIX
    pause
    exit /b 1
)
echo       Qt 路径：%QT_PREFIX%

REM ---------- 创建构建目录 ----------
echo.
echo [2/4] 创建构建目录：%BUILD_DIR%
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

REM ---------- CMake 配置 ----------
echo.
echo [3/4] 运行 CMake 配置（生成 VS2022 工程）...
cd /d "%BUILD_DIR%"
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="%QT_PREFIX%"
if errorlevel 1 (
    echo.
    echo [错误] CMake 配置失败！
    echo        请检查：
    echo          - Qt 版本是否正确（6.10.1）
    echo          - Qt MSVC 编译器是否匹配（msvc2022_64）
    echo          - Qt Visual Studio Tools 是否安装
    pause
    exit /b 1
)
echo       CMake 配置成功

REM ---------- 生成 VS 解决方案 ----------
echo.
echo [4/4] 生成 Visual Studio 解决方案...
cmake --build . --config Release
if errorlevel 1 (
    echo.
    echo [警告] 构建失败，但工程文件已生成
    echo        你可以打开 VS 手动编译：
    echo        解决方案：%BUILD_DIR%\DeviceForge.sln
    pause
    exit /b 1
)
echo       构建成功

REM ---------- 完成 ----------
echo.
echo ============================================================
echo  完成！
echo ============================================================
echo.
echo  下一步：
echo    1. 在 VS2022 中打开：
echo       %BUILD_DIR%\DeviceForge.sln
echo.
echo    2. 或使用 CMake 编译：
echo       cd %BUILD_DIR%
echo       cmake --build . --config Release
echo.
echo    3. 运行（Release）：
echo       %BUILD_DIR%\Release\DeviceForge.exe
echo.
pause
endlocal
