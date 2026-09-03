# DeviceForge v2.8.0 Release Notes

> 2026-08-22 · [完整变更日志](CHANGELOG.md) · [路线图](ROADMAP.md)

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
- 手动发布验收：`.github/workflows/release-validation.yml`，校验输入版本后执行 Release 构建、20 个 CTest、`windeployqt`、UI 冒烟、NSIS 打包并上传 zip/setup artifacts。
- 手动工作流不创建 GitHub Release，也不创建或移动 Git Tag；正式发布仍需人工核对验收证据。

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
4. 使用绿色包时应整体替换程序目录，避免混用旧 Qt DLL 或插件；安装包/绿色包以发布验收 artifacts 为准。
