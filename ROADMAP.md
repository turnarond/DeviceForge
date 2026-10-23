# DeviceForge 路线图 (Roadmap)

> 本文件描述 DeviceForge 的发展方向。计划会随社区反馈调整——**如果你正好需要某个功能,欢迎在 [Issues](../../issues) 提出或 👍,这会直接影响优先级。**

**当前版本**：v2.16.0 · **平台**：Windows（x64）· **许可**：MIT License

---

## 已交付（至 v2.16.0）

| 模块 | 能力 |
|------|------|
| 文件部署 | FTP/FTPS + **SFTP** 批量部署（并发度 1–8、逐设备进度、报告导出、失败重试；IDeployable 统一部署循环），TLS 加密，双栏远程文件管理；**元数据列（所有者/修改时间/权限）+ Ctrl+M 目录对比（差异着色/行对齐/仅差异过滤）+ F4 在线预览编辑回传（编码探测/冲突确认）** |
| SFTP 文件管理 | 列目录/上传/下载/删除/重命名/新建目录，双栏 FTP/SFTP 协议一键切换（端口自动 21/22） |
| 批量命令 | Telnet / **SSH**（libssh2，密码认证 + TOFU）批量 Shell 命令 |
| Modbus 测试 | **多 PLC 主看偶写重构（v2.12）**：档案身份 + modbus.target 目标档案（旧从站自动迁移）、250ms 轮询调度器（写插队/指数退避）、双栏目标清单 + 逐寄存器变化视图、本机环回集成测试 |
| WebSocket 通信 | **双栏控制台重构（v2.12）**：连接操作区 + 2000 行实时事件流（方向/对端/字节数/预览），Token DPAPI 加密存储 |
| **网络调试中继** | TCP/UDP/**组播** 透明中继旁路抓包 + 流量录制（`.nrec`）+ 按原始时序回放 + 组播回灌 |
| **OPC UA 客户端** | open62541 客户端：连接（None+匿名）+ 批量读/写节点 + DataChange 订阅 + 地址空间浏览 |
| **OTA 在线更新** | 主程序内检查 + 独立 Updater.exe 双进程替换（备份/回滚/重启） |
| **配置持久化** | ConfigStore（SQLite）+ DPAPI 凭证加密，设备/凭证/端点历史/Tool 设置持久化 |
| 日志统一 | 所有 Tool 日志统一路由到底部可折叠全局日志面板 |
| **设备任务中心** | 统一设备档案（多协议端点 + 旧设备列表兼容迁移）+ 任务模板（部署→命令→重启→恢复，凭据拒绝入库）+ 编排执行引擎（阶段恢复/重启断开按成功）+ 执行记录持久化（不可变快照/筛选/保留清理/脱敏）+ 三栏任务中心页 + 任务运行 CSV/HTML 报告 |

### 版本节奏

- **v2.1**（2026-07-09）：Modbus Tool 迁移 + NetRelayTool 网络中继（录制回放 .nrec）+ SSH 适配器
- **v2.2**（2026-07-18）：OTA 在线更新 + 远端预览重构 + CMake 标准构建
- **v2.3**（2026-07-24）：ConfigStore 配置持久化（SQLite + DPAPI）+ OPC UA 订阅卡死修复
- **v2.4**（2026-07-26）：FTP 双栏重构 + 主窗口布局现代化（NavBar/胶囊设备栏/可折叠日志）+ 日志统一
- **v2.5**（2026-07-26）：远程文件管理补完（重命名/新建目录/精确对比）+ SFTP 文件管理 + SylixOS 适配（EPSV/MULTICWD/递归删除）
- **v2.6**（2026-08-07）：SFTP 批量部署——IDeployable 部署能力接口 + SshAdapter 部署链路 + 部署循环协议化（FTP/SFTP 同一逻辑）+ UI 解锁 + 双主题/紧凑密度
- **v2.7**（2026-08-18）：UX 收尾——远程列表/连接异步化（QtConcurrent + 代际令牌，慢速目录不冻结 UI）+ 面板源选择器（本地/FTP/SFTP 独立浏览）+ 系统文件拖入上传恢复 + 顺带项（readdir 错误上报/sort 降序 SWO/双主题像素验证）
- **v2.8**（2026-08-22）：并行批量部署（并发度 1–8）+ 每设备实时进度 + CSV/HTML 部署报告 + 失败设备一键重试 + NSIS 安装包
- **v2.9**（2026-09-16，补丁 09-26）：传输可靠性内核——双栏/批量部署统一重试、SHA-256 校验、原子提交、取消与已交付文件恢复跳过
- **v2.10**（2026-09-29）：统一批量命令下发内核（BatchCommandRunner）+ 部署后成功设备重启闭环（重启断开按预期成功）+ 重启参数持久化
- **v2.11**（2026-10-08）：设备任务中心——统一设备档案 + 任务模板库 + 编排执行引擎（阶段恢复）+ 执行记录持久化 + 三栏任务中心页
- **v2.11.1**（2026-10-08）：现场反馈可用性补丁——设备编辑器协议/凭据下拉与就地新建、综合日志折叠记忆、批量命令结果列宽可拖动
- **v2.12**（2026-10-14）：三大界面重构——WebSocket 双栏控制台 + 事件流、MODBUS 多 PLC 主看偶写重构、文件管理增强（元数据列/目录对比/F4 在线编辑回传）
- **v2.13**（2026-10-16）：轻还债版——任务模板跨机迁移（导出/导入，"U 盘带模板"）+ 任务模板编辑器（新建/编辑/删除补齐产品内入口）+ 还债波 #46/#47/#48/#51/#54/#55
- **v2.14**（2026-10-18）：加固与可见性版——部署页「保存为模板」（v2.11 spec 承诺兑现）+ 任务中心加固批（#49 收编：校验单源/异常脱敏/重试文案分档/档案落盘回滚/凭据降级定档）+ WebSocket 五项（#50 收编：对端列/事件过滤/Token 常量时间/计数语义/运行守卫）+ 两处对外文档勘误
- **v2.15.0**（2026-10-20）：全量收编版——文件夹条目入口递归展开镜像部署（三入口同口径、5000 上限整体拒绝、远端子目录自动创建；SFTP 目录落点加深一层）+ 清单去 1000 条隐性截断 + 导入/保存为模板同落库通道 + 运行终态聚合单源 + 运行级已确保远端目录复用 + 部署页工具栏两行化（窄屏不再右侧裁切）+ telnet 工装有界红；测试基线 **52** 目标。已知问题 #59（SylixOS 三禁未单源化）留 v2.16
- **v2.14.1**（2026-10-18）：评审后续波补丁——主窗口初始几何主屏优先兑现 + 传输层目录条目口径钉桩（注释/手册就实）+ v2.13 规格文案勘误
- **v2.16.0**（2026-10-23）：设备栏标签分组版——只显已选 chips + 「设备 ▾」分组菜单（标签即分组、零迁移）+ 双击/右键「打开这台设备」按端点协议跳页 + 未选择设备＝没有目标（现场可见变更）+ 标签归一单源化 + SylixOS FTP 三禁单源化与静态守卫（#59 闭环）；测试基线 **53** 目标

底层架构：可扩展 Tool 框架（Backend + Widget）+ Protocol Adapter 抽象层 + IDeployable 部署能力接口 + 工业仪表盘深色主题（「琴色是动词」体系）。
产品化：自定义 app.ico + exe VERSIONINFO（turnarond/DeviceForge）+ 无 console（WIN32 子系统）。
测试：53 个 QtTest/CTest 目标（v2.16 新增 tst_device_bus_groups；v2.15 新增 tst_folder_expand / tst_deploy_entry_builder / tst_deploy_toolbar_layout；v2.14 新增 tst_deploy_template_map / tst_task_step_validation / tst_window_placement；v2.13 新增 tst_task_template_codec）（以 `tests/CMakeLists.txt` 为准；v2.11 新增 tst_device_registry(_store) / tst_task_template_store / tst_task_execution_engine / tst_task_run_store / tst_task_center_widget / tst_task_center_e2e；v2.12 新增 tst_batch_result_header / tst_log_panel_state / tst_ws_event_types / tst_ws_page_layout / tst_ws_loopback / tst_modbus_target_store / tst_modbus_scheduler / tst_modbus_page / tst_modbus_loopback / tst_dir_compare / tst_file_encoding）。

---

## v2.9 → v2.11 已交付：可靠传输 → 命令闭环 → 设备任务中心

v2.9 传输可靠性内核（详见 `docs/03-设计/方案设计/2026-09-09-v2.9-现场工作台蓝图.md`）与 v2.10 批量命令/重启闭环之上，v2.11 落地设备任务中心：统一设备档案、任务模板、编排执行引擎（阶段恢复）与执行记录持久化。下一步聚焦快速启动与懒加载的现场验收。

## 中期

- **OPC UA 客户端增强** — 首期为 None+匿名连接。后续加安全策略加密（Basic256Sha256）、用户名/证书认证、Method 调用、数组类型展开。
- **SCP 部署上传** — SCP 无目录列表，仅用于部署上传；复用 v2.9 传输可靠性内核。
- **网络中继安全增强** — 非回环绑定确认、客户端来源 allowlist。
- **🔷 Linux 平台适配** — 当前仅 Windows。工业/嵌入式场景（含 SylixOS）对 Linux 需求大，是跨平台化的关键一步。
- **插件化 DLL 加载** — 通过 `QPluginLoader` 支持第三方 Tool 以 DLL 形式动态加载（`ManifestParser` 清单解析已就绪）。
- **ToolHost 多 Tool 并发** — 当前 Tool 由主窗口直接创建，改为经 ToolHost 统一管理多活跃 Tool。

## 远期 / 探索

- 更多工业协议适配器（CANopen、EtherCAT 诊断等，视需求）
- 网络中继录制文件的 pcap 导出（供 Wireshark 分析）

---

## 不在计划内（Non-Goals）

保持工具聚焦,以下暂不考虑：

- 云端/SaaS 化——DeviceForge 定位为本地运维工具
- 完整 SCADA/组态功能——与 PLCBasicConfigurator 分工，本项目专注部署/测试/运维
- 中继数据注入/篡改——网络中继坚持"原样转发"语义

---

## 如何参与

- 有需求或 bug → [提 Issue](../../issues)
- 想贡献代码 → 见 [CONTRIBUTING.md](CONTRIBUTING.md)
- 想加某个功能 → 在对应 Issue 下 👍 或留言你的使用场景，真实场景比投票更有说服力

> 路线图不是承诺书,而是方向说明。实际节奏取决于维护精力与社区参与度。
