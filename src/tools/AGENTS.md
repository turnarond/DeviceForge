# src/tools — Tool 实现层

六个 Tool，全部遵循 **Backend + Widget 配对**：`XxxBackend`（继承 `ToolBackend`/ServiceTask，重写 OnStart/OnStop）+ `XxxWidget`（继承 `ToolWidget`，重写 toolId/onToolStart/onToolStop）。

## 目录

| Tool | 目录 | 后端协议栈 | 备注 |
|---|---|---|---|
| FtpDeployTool | `FtpDeployTool/` | FtpDeployBackend → ProtocolRegistry(FtpAdapter/SshAdapter) | 双栏面板宿主（组装两个 FileBrowserPanel）；批量部署链路在 Backend |
| TelnetTool | `TelnetTool/` | TelnetAdapter(lwcommunicate)/SshAdapter(libssh2) | 提示驱动登录；取消 100ms 内中断 |
| WebSocketTool | `WebSocketTool/` | QWebSocket | 默认绑 127.0.0.1 + Token 认证；WSS 自签名证书入 QRC |
| ModbusTool | `ModbusTool/` | QModbusTcpClient | 读前从 ConfigStore 同步设备列表（bindDevices 无调用方，勿依赖） |
| NetRelayTool | `NetRelayTool/` | QTcpServer + QUdpSocket | RelayRecorder/RelayRecording/RelayPlayer；`.nrec` 录制回放；地址校验收敛在 NetRelayTypes.h 纯逻辑 |
| OpcUaClientTool | `OpcUaClientTool/` | OpcUaAdapter(open62541) | UA_MULTITHREADING=100 |

## 新增 Tool 配方（TDD）

1. **红灯先行**：在 `tests/` 为后端可测逻辑建目标（见 `tests/AGENTS.md`）；纯逻辑尽量抽成自由函数/独立头（参照 NetRelayTypes.h）
2. 建 `src/tools/<Name>Tool/XxxBackend.{h,cpp}`：继承 ToolBackend，实现 toolId（`com.deviceforge.<域>` 格式）/toolName/toolVersion/toolCategory/bindDevices/bindCredentials/applyConfig + OnStart/OnStop
3. 建 `XxxWidget.{h,cpp}`：继承 ToolWidget；内部日志一律走 `m_globalLogCb`（构造后在 setupXxxTab 里 setGlobalLogCallback），不自建日志面板
4. 在 `src/app/DeviceForge.cpp` 加 `setupXxxTab()`（make_shared Backend → OnStart 校验 rc → widget->setBackend/setGlobalLogCallback/onToolStart → addWidget 到 m_toolStack），并在 NavBar addItem + 构造函数调用序列挂接；`DeviceForge.h` 加成员与前向声明
5. 协议访问经 ProtocolRegistry 取适配器实例，禁止 Widget 直连协议库
6. 配置持久化走 ConfigStore（键建议 `<tool>.<field>`），密码类走 DpapiCrypto
7. 同一提交更新 `docs/04-使用手册/*` 与 README 功能模块表

## 反模式（本目录特有）

- 勿把业务逻辑写进 Widget——Widget 只做展示与输入，逻辑归 Backend 或纯函数
- 勿通过 ToolHost 创建（单活跃限制未解除），直接 setupXxxTab
- 异步回调必须代际令牌/QPointer 守卫（参照 FtpDeployWidget m_connGeneration 模式），防 stop/restart 后陈旧回调 UAF
- 勿在 Tool 里绕过全局日志直接 printf/qDebug 输出业务信息
