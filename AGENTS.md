# DeviceForge 知识库（AGENTS.md）

**生成**：2026-08-22 ｜ **提交**：6fe4114 ｜ **分支**：main ｜ **版本**：2.7.0（以 CMakeLists.txt `project()` 为准）

## 概述

Qt 6.11.1 + C++17 工业级系统部署调试工具（原名 DeployMaster）：FTP/FTPS/SFTP 批量部署、Telnet/SSH 批量命令、Modbus 测试、OPC UA 客户端、WebSocket、TCP/UDP/组播中继。
双层架构：**Tool = ToolBackend（ServiceTask 后端）+ ToolWidget（QWidget 前端）**，经 lwmsgq 解耦；协议走 **IProtocolAdapter + ProtocolRegistry** 连接池。

> 本文件只做导航。权威细节源：`CLAUDE.md`（工程档案/版本史/坑清单）与 `docs/README.md`（文档中心索引）。二者与本文件冲突时，先核对代码，再修文档。

## 文档纪律（强制）

1. 全中文交流与文档，专有名词（MVP、EventBus、ABI…）除外
2. 文档是唯一对外标准：改需求/接口/设计/版本号 → **同一提交内**同步 `docs/` 对应文档，失效引用立即清理
3. `docs/` 中文命名 + 数字前缀排序（00-开发规范 … 06-运营与文章）；新文档名 `YYYY-MM-DD-主题.md`
4. 从 `main` 新建分支（`feature|fix|docs|refactor|chore/xxx`），验证后 `--no-ff` 合回；一个分支一件事
5. TDD 红-绿-重构强制：实现必须附带测试或指明变绿用例
6. 版本边界打 tag `vX.Y.Z`；SDK 场景头文件/接口改动必须向前兼容（ABI）
7. 非本项目问题提 issue 给供应商，不在本仓修
8. 重构需讨论评审后独立分支进行

## 结构

```
DeviceForge/
├── src/app/            # 应用壳：main.cpp 初始化顺序敏感（LogBridge→ProtocolRegistry→主题→窗口）+ 主窗口 + 双主题 QSS
├── src/framework/      # Tool 框架基类：ToolBackend/ToolWidget/ToolHost/ToolRegistry/ManifestParser/AppState
├── src/adapter/        # 协议适配器：Ftp(libcurl)/Ssh(libssh2)/Telnet(lwcommunicate)/OpcUa(open62541) + IDeployable
├── src/ui/             # 跨工具组件：FileBrowserPanel/IFileSource(+Local|Remote实现)/NavBar/DeviceBusWidget
├── src/tools/          # 六个 Tool，Backend+Widget 配对 → 见 src/tools/AGENTS.md
├── src/config/         # ConfigStore(SQLite 键值) + DpapiCrypto(DPAPI 加密) + SettingsDialog
├── src/updater/        # OTA 双进程：UpdateChecker(主进程) + Updater.exe(独立纯 Win32，替换 exe)
├── src/logging/        # LogBridge：qDebug/qWarning/… → lwlog
├── src/thirdparty/     # vendored 静态库链 lwcomm→lwevent→lwmsgq→lwlog→lwcommunicate→lwserverbase，勿改
├── tests/              # QtTest/CTest 16 目标 → 见 tests/AGENTS.md
├── tools/devtools/     # Python 配套工具集 → 见 tools/devtools/AGENTS.md
├── docs/               # 文档中心（唯一对外接口，索引 docs/README.md）
├── include/curl lib/   # libcurl 头文件与 x64 二进制（DLL 入库）
└── build.bat           # 一键构建（清缓存→CMake VS2022→Release）
```

## 去哪找

| 任务 | 位置 | 备注 |
|---|---|---|
| 新增/修改 Tool | `src/tools/` | 配方见 `src/tools/AGENTS.md` |
| 新增协议支持 | `src/adapter/` | 实现 IProtocolAdapter → main.cpp 注册工厂到 ProtocolRegistry |
| 部署能力扩展 | `src/adapter/IDeployable.h` | uploadFile/uploadFolder/clearRemoteDirectory/setProgressCallback/setCancelFlag |
| 双栏面板行为 | `src/ui/FileBrowserPanel.*` | TC 快捷键/拖拽三分支/异步加载代际令牌都在此 |
| 配置持久化 | `src/config/ConfigStore` | 键值 SQLite + JSON 导入导出；密码走 DpapiCrypto |
| 新增测试 | `tests/` | 注册模板见 `tests/AGENTS.md` |
| 版本发布 | `docs/01-白皮书/版本发布与Tag规范.md` | 全仓同步 + `tools/devtools/versioncheck.py` 校验 |
| 行为准则全文 | `docs/00-开发规范/开发规范.md` | 16 条 |

## 代码地图（核心符号）

| 符号 | 类型 | 位置 | 角色 |
|---|---|---|---|
| `ToolBackend` | 抽象基类 | src/framework/ToolBackend.h | 继承 lwserverbase::core::ServiceTask；toolId/bindDevices/applyConfig |
| `ToolWidget` | 抽象基类 | src/framework/ToolWidget.h | onToolStart/onToolStop + setGlobalLogCallback 统一日志路由 |
| `IProtocolAdapter` | 接口 | src/adapter/IProtocolAdapter.h | protocolId/connect/request/subscribe 纯虚 |
| `ProtocolRegistry` | 单例 | src/adapter/ProtocolRegistry.h | 工厂注册表：registerFactory/create |
| `ConfigStore` | 单例 | src/config/ConfigStore.h | SQLite 键值持久化 |
| `FileBrowserPanel` | QWidget | src/ui/FileBrowserPanel.h | 通用双栏文件面板宿主件 |
| `IFileSource` | 接口 | src/ui/IFileSource.h | 协议无关文件系统操作；Local/Remote 两实现 |
| `AppState` | 单例 | src/framework/AppState.h | 全局状态（QMutexLocker 线程安全） |
| `DeviceForge` | QMainWindow | src/app/DeviceForge.cpp | 组装 NavBar + 设备栏 + QStackedWidget + 各 Tool Tab |

## 反模式（本项目明令禁止）

- **open62541 开关只能改 `open62541.h` 头内 `#define`**；传 `-DUA_ENABLE_X=0` 反而激活代码路径（它用 `#ifdef` 判断）
- **「琴色是动词」**：#F0A030 仅用于信号态（focus/选中/活跃 tab/主按钮）；静止结构边框一律石墨色；进度条青绿 #40C8A0；Qt QSS 不支持 CSS 渐变/阴影语法，用 `qlineargradient(...)` 或纯色
- **SylixOS FTP 三条勿回退**：`CURLOPT_FTP_USE_EPSV=0`（强制 PASV）、`CURLFTPMETHOD_MULTICWD` 逐级 CWD（不支持多级路径）、QUOTE 命令用根 URL+绝对路径
- **ToolHost 仅单活跃 Tool**（预留未启用）：新 Tool 直接在 `DeviceForge::setupXxxTab()` 创建 Backend+Widget，勿走 `ToolHost::registerBuiltinFactory`
- Tool 内部设置持久化用 ConfigStore，**禁用 QSettings**
- 禁止绕过 IProtocolAdapter 直连协议库；依赖方向 UI → 业务 → 适配器，禁止反向
- **CI 是 Qt 6.9.2**（本地 6.11.1）：不得使用仅新版才有的 API
- 禁止 `as any` 式类型压制等 AI slop；空 catch、删测试换绿灯均不允许
- 日志脱敏：Telnet/WebSocket/NetRelay 只记方向+字节数，不记原始内容；密码字段用 AuthInfo::clear 擦除

## 命令

```bash
./build.bat                                  # 一键构建（默认 Qt 路径 c:\Qt\6.11.1\msvc2022_64，不同则改脚本顶部）
cmake --build build --config Release         # 增量编译（已配置时）
cd build && ctest -C Release --output-on-failure          # 全部测试
cd build && ctest -C Release -R tst_nrec --output-on-failure   # 单测
python tools/devtools/versioncheck.py        # 版本一致性校验
python tools/devtools/smoke.py               # UI 冒烟
```

## 注意事项

- Windows 下直接跑 ctest 若报 `0xc0000135`（Qt DLL 缺失）说明测试属性丢失——tests/CMakeLists.txt 的 `ENVIRONMENT_MODIFICATION PATH=path_list_prepend:<Qt bin>` 必须保留
- CMake POST_BUILD 自动拷贝 `libcurl-x64.dll`/`libssh2.dll` 到输出目录；VS 手动调试需自行确认
- 长稳评审关注内存/句柄泄漏（异步回调务必代际令牌 + QPointer 守卫，防 UAF）
- Debug：Windows 用 cdb（难调转 WSL/gdb）
