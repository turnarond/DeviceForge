@echo off
REM ============================================================
REM  DeviceForge Build Script
REM  Purpose: One-click generate Visual Studio 2022 project
REM  Prerequisites:
REM    - Qt 6.11.1 installed (QT_PREFIX override; probes D:\Qt then C:\Qt)
REM    - Visual Studio 2022 (v143 toolset) installed
REM  Encoding: ANSI/ASCII only (no UTF-8 BOM) for cmd.exe compat
REM ============================================================

setlocal enabledelayedexpansion

echo.
echo ============================================================
echo  DeviceForge Build Script
echo ============================================================
echo.

REM ---------- Configuration ----------
set PROJECT_ROOT=%~dp0
set BUILD_DIR=%PROJECT_ROOT%build
REM Caller may set QT_PREFIX explicitly; otherwise probe known local roots.
if not defined QT_PREFIX if exist "D:\Qt\6.11.1\msvc2022_64\bin\qmake.exe" set "QT_PREFIX=D:\Qt\6.11.1\msvc2022_64"
if not defined QT_PREFIX if exist "C:\Qt\6.11.1\msvc2022_64\bin\qmake.exe" set "QT_PREFIX=C:\Qt\6.11.1\msvc2022_64"
if not defined QT_PREFIX (
    echo [ERROR] Qt 6.11.1 not found. Set QT_PREFIX to the msvc2022_64 directory.
    exit /b 1
)

REM ---------- Check Qt ----------
echo [1/4] Checking Qt installation...
if not exist "%QT_PREFIX%\bin\qmake.exe" (
    echo [ERROR] Qt not found at: %QT_PREFIX%
    echo        Set QT_PREFIX to a valid msvc2022_64 directory before calling this script.
    echo.
    echo        Common Qt paths:
    echo          D:\Qt\6.11.1\msvc2022_64
    echo          C:\Qt\6.11.1\msvc2022_64
    pause
    exit /b 1
)
echo       Qt path: %QT_PREFIX%

REM ---------- Create build directory ----------
echo.
echo [2/4] Creating build directory: %BUILD_DIR%
echo       Removing old build cache to avoid platform mismatch...
REM #43-①：整目录删除——只删 CMakeCache/CMakeFiles 会留下 build/tests 下
REM 陈旧 .obj，类布局变更型提交后增量编译产生栈越位崩溃（CLAUDE.md 坑记录）
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

REM ---------- CMake configuration ----------
echo.
echo [3/4] Running CMake configuration...
cd /d "%BUILD_DIR%"
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="%QT_PREFIX%"
set CMAKE_ERR=!ERRORLEVEL!
REM #43-②：以产物判成败——cmake 配置成功但返回码非零（探测告警等）曾致误退出；
REM CMakeCache.txt 与 .sln 均在即放行，仅告警
if exist "%BUILD_DIR%\CMakeCache.txt" if exist "%BUILD_DIR%\DeviceForge.sln" (
    if not !CMAKE_ERR! == 0 echo       [WARN] CMake exit code was !CMAKE_ERR! but artifacts look complete, continuing
    goto :cmake_ok
)
if !CMAKE_ERR! NEQ 0 (
    echo.
    echo [ERROR] CMake configuration failed (exit code: !CMAKE_ERR!)
    echo        Please check:
    echo          - Qt version (check the caller-provided QT_PREFIX, if any)
    echo          - Qt MSVC compiler (should be msvc2022_64)
    echo          - Qt Visual Studio Tools extension installed
    exit /b !CMAKE_ERR!
)
echo       CMake configuration succeeded
:cmake_ok

REM ---------- Build ----------
echo.
echo [4/4] Building Release version...
cmake --build . --config Release
if !ERRORLEVEL! NEQ 0 (
    echo.
    echo [WARNING] Build failed (exit code: !ERRORLEVEL!), but project files generated
    echo           You can open VS and build manually:
    echo           Solution: %BUILD_DIR%\DeviceForge.sln
    pause
    exit /b !ERRORLEVEL!
)
echo       Build succeeded

REM ---------- Done ----------
echo.
echo ============================================================
echo  Done!
echo ============================================================
echo.
echo  Next steps:
echo    1. Open in VS2022:
echo       %BUILD_DIR%\DeviceForge.sln
echo.
echo    2. Or build with CMake:
echo       cd %BUILD_DIR%
echo       cmake --build . --config Release
echo.
echo    3. Run (Release):
echo       %BUILD_DIR%\Release\DeviceForge.exe
echo.
endlocal
