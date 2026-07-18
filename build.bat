@echo off
REM ============================================================
REM  DeviceForge Build Script
REM  Purpose: One-click generate Visual Studio 2022 project
REM  Prerequisites:
REM    - Qt 6.11.1 installed (default: C:\Qt\6.11.1\msvc2022_64)
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
set QT_PREFIX=c:\Qt\6.11.1\msvc2022_64

REM ---------- Check Qt ----------
echo [1/4] Checking Qt installation...
if not exist "%QT_PREFIX%\bin\qmake.exe" (
    echo [ERROR] Qt not found at: %QT_PREFIX%
    echo        Please edit this script and set QT_PREFIX to your Qt path
    echo.
    echo        Common Qt paths:
    echo          C:\Qt\6.11.1\msvc2022_64
    echo          C:\Qt\6.10.1\msvc2022_64
    echo          D:\Qt\6.11.1\msvc2022_64
    pause
    exit /b 1
)
echo       Qt path: %QT_PREFIX%

REM ---------- Create build directory ----------
echo.
echo [2/4] Creating build directory: %BUILD_DIR%
echo       Removing old build cache to avoid platform mismatch...
if exist "%BUILD_DIR%\CMakeCache.txt" del /f /q "%BUILD_DIR%\CMakeCache.txt"
if exist "%BUILD_DIR%\CMakeSettings.json" del /f /q "%BUILD_DIR%\CMakeSettings.json"
if exist "%BUILD_DIR%\CMakeFiles" rmdir /s /q "%BUILD_DIR%\CMakeFiles"
if exist "%BUILD_DIR%\.vs" rmdir /s /q "%BUILD_DIR%\.vs" 2>nul
del /f /q "%BUILD_DIR%\*.vcxproj.user" 2>nul
del /f /q "%BUILD_DIR%\*.sdf" 2>nul
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

REM ---------- CMake configuration ----------
echo.
echo [3/4] Running CMake configuration...
cd /d "%BUILD_DIR%"
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="%QT_PREFIX%"
set CMAKE_ERR=!ERRORLEVEL!
if !CMAKE_ERR! NEQ 0 (
    echo.
    echo [ERROR] CMake configuration failed (exit code: !CMAKE_ERR!)
    echo        Please check:
    echo          - Qt version (check QT_PREFIX in this script)
    echo          - Qt MSVC compiler (should be msvc2022_64)
    echo          - Qt Visual Studio Tools extension installed
    exit /b !CMAKE_ERR!
)
echo       CMake configuration succeeded

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
