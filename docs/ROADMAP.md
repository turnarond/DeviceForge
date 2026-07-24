# DeviceForge 路线图 (Roadmap)

> 工业级设备批量运维平台 — 白皮书  
> 最后更新: 2026-07-24

---

## 已完成 (v2.0 → v2.1.0 → v2.2.0 → v2.3.0)

| 模块 | 说明 | 版本 |
|------|------|------|
| **文件部署** | FTP/FTPS 批量文件部署，libcurl 底层，支持目录递归、端口/FTPS 加密 | v2.0 |
| **批量命令** | Telnet + SSH 双协议批量命令执行，SSH 密码认证 + TOFU 主机密钥 | v2.0 |
| **WebSocket 通信** | Server/Client 双模式，默认 127.0.0.1 绑定 + 可选 Token 认证 | v2.0 |
| **Modbus 集群测试** | QModbusTcpClient 批量读写寄存器，QTimer 自动刷新 | v2.0 |
| **网络调试中继** | TCP/UDP/组播透明中继代理，双向流量捕获 + Hex 实时视图 | v2.1.0 |
| **网络调试录制回放** | `.nrec` 自定义二进制格式录制 + 按原始时序回放，组播零影响加入抄收 | v2.1.0 |
| **OPC UA 客户端** | open62541 v1.5.5，读/写/订阅/浏览，None 安全策略 + 匿名认证；地址空间浏览 5 列 UI + 类型友好名 + 节点类色块 + × 删除按钮；针对非规范服务端的 ActivateSession policyId 深拷贝补丁；订阅卡死修复（请求超时 + 后台线程 + 移除冗余轮询） | v2.2.0 / v2.3.0 |
| **在线更新 (OTA)** | GitHub Releases API 查询 + zip 下载 + 校验 + 解压 + Updater.exe 独立替换进程 | v2.2.0 |
| **远端预览面板** | FTP 远程文件树浏览 + 多选下载 + 右键操作（查看/重命名/删除）+ 目录递归 | v2.2.0 |
| **构建系统** | CMake 标准构建（qt_standard_project_setup），Debug/Release/RelWithDebInfo，/FS PDB 共享 | v2.2.0 |
| **UI 主题** | 「琴色是动词」工业深色主题，QSS 体系，全控件统一 | v2.0 |
| **本地配置持久化（ConfigStore）** | SQLite 单表 + Windows DPAPI 密码加密；设备/OPC UA endpoint/Modbus 从站/NetRelay/WebSocket/Telnet 偏好 + FTP 凭证全部自动恢复；统一设置面板（Ctrl+,）查看/导入/导出 | v2.3.0 |

---

## 短期规划 (v2.4)

| 项目 | 优先级 | 说明 |
|------|--------|------|
| **OPC UA 安全策略扩展** | 高 | 支持 Basic256/Basic256Sha256 + 证书认证 |
| **远端预览 SCP 支持** | 中 | UI 已预留协议选择器，需要 scp 传输层实现 |
| **NetRelay 非阻塞增强** | 中 | 非回环绑定模态确认弹窗、背压节流（setReadBufferSize）、客户端来源 allowlist |
| **SettingsDialog 完整编辑** | 低 | 当前编辑为只读 JSON 弹窗；完整编辑写回留待后续 |

---

## 中期规划 (v2.5+)

| 项目 | 优先级 | 说明 |
|------|--------|------|
| **Linux/SylixOS 适配** | 🔴 高 | DpapiCrypto 在非 Windows 需替换为 libsecret；CMake 构建已就绪 |
| **ToolHost 多 Tool 并发** | 中 | 当前 DeployMaster 直接创建 Tool，待 ToolHost 支持多 Tool 并行管理 |
| **插件化加载 (QPluginLoader)** | 中 | DLL 动态加载 .dll Tool 插件，manifest.xml 入口点 |
| **单元测试扩展** | 中 | 从 tst_nrec / tst_config_store / tst_dpapi_crypto / tst_opcua_* 扩展到 FtpAdapter/TelnetAdapter/ToolRegistry 覆盖率 |

---

## 长期愿景

| 方向 | 说明 |
|------|------|
| **SylixOS PLC 完整工具链** | 与 PLCBasicConfigurator 配套，覆盖设备配置 → 部署 → 测试 → 运维全生命周期 |
| **跨平台 GUI** | Linux (X11/Wayland) 原生运行，SylixOS 嵌入式部署 |
| **多语言国际化** | i18n 支持，优先英文 |
| **社区生态** | 插件市场、模板分享、第三方 Tool 贡献机制 |

---

## 版本历史

| 版本 | 日期 | 里程碑 |
|------|------|--------|
| v2.0 | 2026-07-05 | DeviceForge 命名 + Tool 框架 + FTP/Telnet/Modbus/WebSocket |
| v2.1.0 | 2026-07-10 | NetRelayTool (中继+录制回放) + SSH 适配器 + 琴色主题 + OPC UA |
| v2.2.0 | 2026-07-18 | 在线更新 OTA + 远端预览重构 + CMake 标准构建 + 7 模块全绿 |
| v2.3.0 | 2026-07-24 | ConfigStore 本地持久化 (SQLite + DPAPI) + OPC UA 订阅卡死修复 + CI Debug SEGFAULT 修复 |

---

*路线图会根据社区反馈和实际需求动态调整。欢迎通过 GitHub Issues 提交功能建议。*
