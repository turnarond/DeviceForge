# DeviceForge v2.11.1 Release Notes

## v2.11.1 补丁（现场反馈可用性）

- 设备档案编辑器：协议下拉选择 + 凭据下拉选择/就地新建（DPAPI 加密入库），不再手填协议与凭据引用键。
- 综合日志面板：折叠条常驻可见、可一键折叠/展开，比例记忆；修复旧版折叠后难以恢复的问题。
- 批量命令执行结果表：列宽可手动拖动。
- 测试基线：全量 CTest 36 目标（新增 `tst_batch_result_header`、`tst_log_panel_state`）。

# DeviceForge v2.11.0 Release Notes

## v2.11.0 新增（设备任务中心）

- 新增统一设备档案：`DeviceProfile`/`DeviceRegistry` 支持一台设备多协议端点，ConfigStore 持久化并兼容旧设备列表；设备总线胶囊名称优先、悬浮显示端点详情、双击编辑档案。
- 新增任务模板库：`TaskTemplateStore` 持久化有序步骤（文件部署→批量命令→重启→恢复检查），版本化管理并拒绝模板携带任何凭据秘密值。
- 新增任务执行引擎：`TaskExecutionEngine` 按模板编排设备任务，复用 v2.9 传输内核与 v2.10 命令内核（设备间串行、重启断开按预期成功、取消在检查点收敛）；`retryFailed` 阶段恢复——已交付文件与已成功步骤不重复执行，成功设备整体跳过。
- 新增执行历史记录：`TaskRunStore` 落库运行/设备/步骤三级记录（设备名称与地址在执行期定格为不可变快照），支持按模板/设备/结果筛选、按保留天数清理（启动时自动执行：崩溃遗留的 running 记录先按失败（中断）对账收口，再清理超期终态记录；默认保留 90 天，ConfigStore `task/retention.days` 可覆盖），错误文本落库前统一脱敏，凭据绝不持久化。
- 新增任务中心页（NavBar 直达）：三栏布局选择模板与目标设备、查看执行状态与历史、失败阶段一键重试、导出任务运行 CSV/HTML 报告；含重启动作的任务执行前强制风险确认（默认拒绝）。
- 测试基线：新增 7 个专项 QtTest 目标与端到端集成测试 `tst_task_center_e2e`，全量 CTest 34 目标。

# DeviceForge v2.10.0 Release Notes

## v2.10.0 新增

- 新增统一批量命令执行器，支持 Telnet/SSH、顺序命令、超时、重试与取消。
- 部署完成后仅对上传成功设备执行重启命令，并识别重启导致的断开连接。
- FTP 部署工具支持重启协议、命令、超时和重试参数持久化。

# DeviceForge v2.9.1 Release Notes

> 2026-09-26 · [完整变更日志](CHANGELOG.md) · [路线图](ROADMAP.md)

---

## v2.9.1 补丁修复

- 修复 FTP `QUOTE` 操作在 GUI 进程中默认写入标准输出导致的瞬时 `CURLE_WRITE_ERROR`。
- 批量部署遇到瞬时 FTP 响应写入错误时自动重连并重试当前文件。

---

## 本版本亮点

### 并行批量部署

- FTP/FTPS 与 SFTP 共用 `DeploymentRunner` 调度，部署并发度可配 1-8，默认 1，兼容原有串行行为。
- 单台部署事务由 `DeployJob` 执行，全局取消同时作用于待调度设备和传输中的协议检查点。

### 每设备实时进度

- 进度面板为每个 `ip:port` 展示独立进度和等待中、上传中、成功、失败或已取消状态。
- 总进度按批次生命周期聚合，完成时收口到本轮设备结果。

### CSV/HTML 部署报告

- 部署完成后可导出 CSV 或打印友好的 HTML 报告。
- 报告按设备记录结果、失败文件、错误摘要和耗时，写盘失败会明确报错。

### 失败设备一键重试

- 保留上一轮实际部署参数，仅对失败设备集合发起新一轮部署。
- 重试轮沿用当前并发度，并重新生成该轮进度和报告结果。

## 同版本其他变更

- NetRelayTool 增加组播 M→U/M→M 实时转发、组播回灌修复和对应地址校验测试。
- FtpListParser 修复单数字日期与小写 `pm` 的 LIST 行解析。
- UpdateChecker 增加取消回调并收紧连接超时，减少退出窗口期阻塞。

## 质量基线

- `tests/CMakeLists.txt` 注册 20 个 QtTest/CTest 目标，覆盖部署调度、报告、FTP LIST、Updater、配置、协议和 UI 基础逻辑。
- 日常 CI 使用 Windows + Qt 6.9.2，分别构建 Debug/Release，并运行全部 CTest、Python 工具测试和版本一致性检查。
- 本地 CTest 中的 DPAPI 用例需要正常 Windows 用户 profile；CI 或服务账户失败时应保留 Windows 错误码，并在正常用户账户复测。

## 系统与构建要求

| 项目 | 要求 |
|------|------|
| 操作系统 | Windows 10/11 x64 |
| 本地 Qt | Qt 6.11.1 `msvc2022_64`；安装路径可配置 |
| CI Qt | Qt 6.9.2 `win64_msvc2022_64` |
| 编译器 | Visual Studio 2022（v143） |
| 构建系统 | CMake 3.22+，C++17 |

手动配置示例：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="D:\Qt\6.11.1\msvc2022_64"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

示例中的 Qt 目录可替换为实际安装位置。使用 `build.bat` 时可先设置 `QT_PREFIX`；未设置时脚本按 `D:\Qt`、`C:\Qt` 顺序探测。

## 构建与验收入口

- 日常质量门禁：`.github/workflows/ci.yml`。
- 手动发布验收：`.github/workflows/release-validation.yml`，校验输入版本后执行 Release 构建、20 个 CTest 与 `windeployqt`，生成绿色 zip 后从全新解压目录在隔离 Qt SDK 的环境中执行 UI 冒烟，并且只上传 portable zip artifact。
- 手动工作流不创建 GitHub Release，也不创建或移动 Git Tag；既有 `v2.8.0` Tag 保持不变。NSIS 自动化与安装包分发因现有递归卸载语义不安全而阻塞，必须由独立 PATCH 修复后才能恢复。

## 已知限制

- 当前仅支持 Windows；Linux 适配仍在路线图中。
- OPC UA 客户端当前为 None 安全策略 + 匿名认证，安全策略与证书认证属于中期候选。
- SCP 未实现，仅在真实设备需求驱动时评估；现有批量部署协议为 FTP/FTPS/SFTP。
- 真实 FTP/SFTP 设备集成、16 台 50 轮长稳和远端发布验收工作流结果以实际验收报告为准；没有日志或产物时不视为通过。
- GUI 冒烟需要可交互 Windows 桌面；无法枚举主窗口时验收应失败并保留原始日志。

## 从 v2.7 升级

1. 升级前备份 `%APPDATA%\DeviceForge\config.db`。
2. v2.8 新增 `deploy.concurrency` 配置，未设置时默认 1；原有设备、凭证和 Tool 配置继续由 ConfigStore 加载。
3. 首次批量部署建议保持并发度 1，确认目标设备承载能力后再逐步调整；部署结束后保存 CSV/HTML 报告。
4. 使用绿色包时应整体替换程序目录，避免混用旧 Qt DLL 或插件；当前只接受发布验收生成的 portable zip artifact，NSIS 安装包不得分发。
