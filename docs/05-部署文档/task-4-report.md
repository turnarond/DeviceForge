# v2.8 Task 4 最终证据链与文档一致性检查报告

**检查时间**：2026-09-05 14:45（Asia/Shanghai）  
**工作树**：`D:\work\project\DeviceForge\.worktrees\v28-release-closure`  
**分支 / 基线**：`chore/v28-release-closure` / `53cd6c57f3c9cd91fdc81007b09e29f9b492db9a`  
**范围**：发布治理、证据和文档；未修改 `src/`、运行时功能、ABI 或 NSIS 安装/卸载逻辑。

## 本次本地门禁

| 检查 | 命令 | 环境 | 时间与退出码 | 结果 / 证据 |
|---|---|---|---|---|
| 发布治理聚焦测试 | `D:\Qt\Tools\mingw1310_64\opt\bin\python3.exe -m unittest tools.tests.test_release_governance tools.tests.test_versioncheck tools.tests.test_smoke -v` | Python 3.9.10 | 2026-09-05 14:45:22 +08:00；退出 0 | 31/31 通过；unittest 0.913 秒、进程计时 1.157 秒。静态锁定 portable-only、无 installer artifact、解压 ZIP 后的隔离 Smoke 环境、九源版本阻断和 Smoke CLI。 |
| Python 全量工具测试 | `D:\Qt\Tools\mingw1310_64\opt\bin\python3.exe -m unittest discover -s tools/tests -v` | Python 3.9.10 | 2026-09-05 14:45:23 +08:00；退出 0 | 31/31 通过；unittest 0.938 秒、进程计时 1.043 秒。 |
| 强版本门禁 | `D:\Qt\Tools\mingw1310_64\opt\bin\python3.exe tools/devtools/versioncheck.py` | Python 3.9.10 | 2026-09-05 14:45:24 +08:00；退出 0 | 九个强制来源均为 `2.8.0`；进程计时 0.082 秒。缺失、不可解析或不一致均会阻断。 |
| 补丁格式 | `git diff --check` | Git 工作树 | 2026-09-05 14:45:24 +08:00；退出 0 | 无输出；进程计时 0.059 秒。 |
| 发布 Tag | `git tag --points-at a6e9283` | Git 历史 | 2026-09-05 14:45:24 +08:00；退出 0 | 唯一输出 `v2.8.0`；进程计时 0.060 秒。 |

## 发布验收状态矩阵

| 门 | 当前状态 | 可复核证据 / 处理边界 |
|---|---|---|
| 本地发布治理与版本门禁 | 通过 | 本报告“本次本地门禁”五项；不替代远端 CI。 |
| GitHub Actions：CI / Release Validation | 阻塞，未触发 | `2026-09-05 14:25:05 +08:00` 的 `gh auth status` 退出 1（默认令牌无效）；`14:25:17 +08:00` 的 `git ls-remote` 退出 128（`known_hosts` 读取拒绝、Host key verification failed）。原始细节见[发布验收报告](2026-09-01-v2.8-发布验收报告.md)。未尝试绕过认证。 |
| portable ZIP 解压隔离 Smoke | 未执行 | 仅由成功的远端 `Release Validation` 消费 ZIP 并产生 artifact/日志；当前没有 run URL、artifact 或 Smoke 日志。 |
| 本地 GUI Smoke | 环境阻塞 | 2026-09-04 指定 Python 缺少 `_ctypes`，`smoke.py` 退出 1；没有窗口识别和关闭证据。未改脚本、未安装或替换依赖。 |
| NSIS 自动化与安装包分发 | 安全阻塞 | 当前卸载器会递归删除用户可选 `$INSTDIR`；本轮不编译、上传或分发 NSIS，等待独立 PATCH。 |
| 真实 FTP/SFTP、3 台 ftpd 模拟 | 未执行 | 没有设备/模拟器日志、报告或截图。 |
| 16 台 × 50 轮长稳及资源基线 | 未执行 | 没有轮次记录或内存、句柄、线程和连接趋势数据。 |

## 文档一致性复核

已复核专项计划与设计、发布验收报告、`ROADMAP.md`、`RELEASE_NOTES.md`、产品路线图、`README.md`、`CLAUDE.md`、当前两个 GitHub 工作流和 `packaging/deviceforge.nsi` 文件头。

- 没有发现将远端 CI、portable Smoke、真实 FTP/SFTP、长稳或 NSIS 自动化错误标为完成的活跃发布声明。
- 版本规则明确为九个强制来源，且当前门禁实测为阻断式成功，不存在“版本源缺失不阻断”的陈述。
- 本次仅回填 2026-09-05 的本地门禁记录及本报告链接；未以本地结果推断远端验收通过。

## 结论

本地最终门禁和 Tag 基线证据完整，但发布验收尚未闭环。恢复有效的 GitHub 凭据与远端访问后，应确认/推送当前 HEAD，再按顺序触发 `CI` 和 `Release Validation(version=2.8.0)`，并将实际 URL、artifact 和隔离 Smoke 日志回填至发布验收报告。其余人工、环境与安全阻塞保持不变。
