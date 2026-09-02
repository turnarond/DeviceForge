; ============================================================================
; DeviceForge NSIS 安装包脚本（v2.8 发布工程新增）
; ----------------------------------------------------------------------------
; 用法（需安装 NSIS 3.x；本地可手工执行，发布验收工作流自动执行）：
;   makensis /DVERSION=2.8.0 packaging\deviceforge.nsi
;   （不传 /DVERSION 时使用下方默认值，须与 CMakeLists.txt project() 保持同步）
;
; 权威版本号来源：CMakeLists.txt 的 project(DeviceForge VERSION x.y.z ...)。
; 打包前用 tools/devtools/versioncheck.py 确认全仓版本一致后再编译本脚本。
;
; 产物名：DeviceForge-v${VERSION}-setup.exe
;
; 打包清单来源（以 CMakeLists.txt POST_BUILD 拷贝到 build/Release 的运行时
; 产物为准，勿手工增删）：
;   · DeviceForge.exe            主程序（qt_add_executable 目标）
;   · Updater.exe                OTA 独立替换进程（POST_BUILD 从 Updater 目标拷入）
;   · libcurl.dll                由 lib/libcurl-x64.dll 改名拷贝（POST_BUILD）
;   · libssh2.dll                SSH/SFTP 运行库（POST_BUILD）
;   · Qt6*.dll                   Qt 运行时——dev 构建目录不含，打包前执行：
;                                  windeployqt --no-compiler-runtime build\Release\DeviceForge.exe
;                                将 Core/Gui/Widgets/Network/SerialBus/WebSockets/
;                                Sql/Concurrent 与 platforms\qwindows.dll 落位到
;                                build/Release（STAGING_DIR）后，由下方通配符收进包
;   · plugins\sqldrivers\qsqlite.dll  ConfigStore 依赖（POST_BUILD 拷贝，
;                                     缺失则启动时 ConfigStore::open() 失败）
;   主题 QSS / WSS 测试证书等资源已随 QRC 编译进 exe，无需外部文件。
;
; VC 运行库：安装时检测系统是否已装 VC++ 2015-2022 x64 Redistributable，
; 已存在则跳过提示；缺失则引导用户下载安装（包内不捆绑 vc_redist）。
; ============================================================================

Unicode true

; ---- 版本号：优先取命令行 -DVERSION，缺省与 CMakeLists.txt 同步为 2.8.0 ----
!ifndef VERSION
  !define VERSION "2.8.0"
!endif

; VIProductVersion 要求四段数字，故补第 4 段 .0（Windows 惯例，同 DeviceForge.rc）
!define PRODUCT_VERSION "${VERSION}.0"

; ---- 打包暂存目录：CMake 构建输出（含 POST_BUILD 产物 + windeployqt 补齐的 Qt 运行时）----
!ifndef STAGING_DIR
  !define STAGING_DIR "..\build\Release"
!endif

!define PRODUCT_NAME "DeviceForge"
!define PRODUCT_PUBLISHER "turnarond"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\DeviceForge"
!define INSTDIR_KEY "Software\DeviceForge"
!define STARTMENU_DIR "$SMPROGRAMS\DeviceForge"

Name "${PRODUCT_NAME} ${VERSION}"
OutFile "DeviceForge-v${VERSION}-setup.exe"
InstallDir "$PROGRAMFILES64\DeviceForge"
; 曾装过则沿用上次安装目录
InstallDirRegKey HKLM "${INSTDIR_KEY}" "InstallDir"
RequestExecutionLevel admin
ManifestDPIAware true

!include "MUI2.nsh"
!include "LogicLib.nsh"

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
; 完成页提供「立即运行」入口
!define MUI_FINISHPAGE_RUN "$INSTDIR\DeviceForge.exe"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"

Var VcRedistInstalled

Function .onInit
  ; VC++ Redistributable 检测：HKLM\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64
  ; 的 Installed==1 表示已安装。x64 注册表视图需显式 SetRegView 64，
  ; 避免 32 位重定向查不到。
  SetRegView 64
  ReadRegDWORD $VcRedistInstalled HKLM \
    "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64" "Installed"
  ${If} $VcRedistInstalled != 1
    MessageBox MB_YESNO|MB_ICONQUESTION \
      "检测到系统尚未安装 Visual C++ 2015-2022 运行库（x64），$\n${PRODUCT_NAME} 可能无法启动。$\n$\n是否现在打开微软官网下载 vc_redist.x64.exe？$\n（安装完成后重新运行本安装包）" \
      IDYES OpenVcRedist IDNO SkipVcRedist
OpenVcRedist:
    ExecShell "open" "https://aka.ms/vs/17/release/vc_redist.x64.exe"
    Abort
SkipVcRedist:
  ${EndIf}
FunctionEnd

; ============================================================
; 安装段
; ============================================================
Section "${PRODUCT_NAME} 主程序" SEC_MAIN
  SectionIn RO

  SetOutPath "$INSTDIR"

  ; ---- POST_BUILD 运行时产物（见文件头「打包清单来源」注释）----
  File "${STAGING_DIR}\DeviceForge.exe"
  File "${STAGING_DIR}\Updater.exe"
  File "${STAGING_DIR}\libcurl.dll"
  File "${STAGING_DIR}\libssh2.dll"

  ; ---- Qt 运行时（windeployqt 预先落位到 STAGING_DIR）----
  File "${STAGING_DIR}\Qt6*.dll"
  File /nonfatal "${STAGING_DIR}\D3Dcompiler_47.dll"
  File /nonfatal "${STAGING_DIR}\opengl32sw.dll"

  ; ---- Qt 插件树（platforms\sqldrivers\imageformats...，含 qsqlite.dll）----
  File /r "${STAGING_DIR}\plugins"

  ; ---- 开始菜单项：主程序快捷方式 + 卸载入口 ----
  CreateDirectory "${STARTMENU_DIR}"
  CreateShortCut "${STARTMENU_DIR}\${PRODUCT_NAME}.lnk" "$INSTDIR\DeviceForge.exe"
  CreateShortCut "${STARTMENU_DIR}\卸载 ${PRODUCT_NAME}.lnk" "$INSTDIR\Uninstall.exe"

  ; ---- 写入卸载信息（控制面板/系统设置可见）----
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\DeviceForge.exe"
  WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
  ; 记录安装目录供下次安装沿用
  WriteRegStr HKLM "${INSTDIR_KEY}" "InstallDir" "$INSTDIR"
SectionEnd

; ============================================================
; 卸载段：删除安装目录与开始菜单项、注销注册表信息
; 注意：按发布约定整目录删除（RMDir /r），用户若在安装目录内存放
; 个人文件将一并清除——安装目录仅应存放程序自身。
; ============================================================
Section "Uninstall"
  ; 先删已知文件再整目录兜底，保证日志/配置残留目录也能清掉
  Delete "$INSTDIR\DeviceForge.exe"
  Delete "$INSTDIR\Updater.exe"
  Delete "$INSTDIR\Uninstall.exe"
  Delete "$INSTDIR\libcurl.dll"
  Delete "$INSTDIR\libssh2.dll"
  Delete "$INSTDIR\Qt6*.dll"
  RMDir /r "$INSTDIR\plugins"
  RMDir /r "$INSTDIR"

  ; 开始菜单项
  Delete "${STARTMENU_DIR}\${PRODUCT_NAME}.lnk"
  Delete "${STARTMENU_DIR}\卸载 ${PRODUCT_NAME}.lnk"
  RMDir "${STARTMENU_DIR}"

  ; 注册表注销
  DeleteRegKey HKLM "${UNINST_KEY}"
  DeleteRegKey HKLM "${INSTDIR_KEY}"
SectionEnd
