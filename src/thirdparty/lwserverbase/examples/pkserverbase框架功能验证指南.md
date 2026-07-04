# lwserverbase 框架功能验证指南

## 两种验证路径

| 路径 | 需求 | 验证范围 |
|------|------|----------|
| A. 离线验证 | 仅 lwserverbase 编译 | ServiceTask/Config/Logger/Metrics/ProcessController |
| B. 在线验证 | VSOA 运行时 (libvsoa + vsoa_master) | 上述全部 + VsoaNode RPC/Pub/Sub/Health |

---

## A. 离线验证（无需 VSOA 环境）

### A1. 构建

```bash
cd src/ext/lwserverbase/build
cmake .. -DBUILD_EXAMPLES=ON
make example_classic_service
```

### A2. 运行：一次性验证 5 大组件

```bash
cp ../examples/config.json .
timeout 3 ./example_classic_service
```

预期输出及对应验证项：

```
  ← ConfigManager: 加载 config.json
[INFO] data-collector v2.0.0 starting...          ← 服务元信息
[INFO] Config loaded: interval=200ms level=2      ← ConfigManager Get<T>() 类型转换
[INFO] Metrics initialized: 2 counters ...        ← MetricsCollector::CreateCounter/Gauge/Histogram
[INFO] Worker threads activated: 3                 ← ServiceTask::activate(3) 线程池
[INFO] [Worker 0] started                          ← svc() 线程入口
[INFO] Stopping data-collector...                  ← SIGTERM 触达 → ProcessController
[INFO] data-collector stopped                      ← OnStop 完成, threads join
```

**一行命令验证了**: ServiceTask 线程池、ConfigManager JSON+类型转换、MetricsCollector 创建、Logger 格式化、ProcessController 信号处理+优雅退出。

---

## B. 在线验证（需完整 VSOA 环境）

### B0. 环境准备

```bash
# 确保 libvsoa 等运行库在 LD_LIBRARY_PATH 中
export LD_LIBRARY_PATH=/path/to/vsoa/lib:$LD_LIBRARY_PATH
export VSOA_POS_SERVER=0.0.0.0:13000

# 先启动 vsoa_master
./bin/vsoa_master &
```

### B1. 构建 VSOA 监控节点

```bash
cd src/ext/lwserverbase/build
cmake .. -DBUILD_EXAMPLES=ON
make vsoa_system_monitor
```

### B2. 验证 VsoaNode — 内置端点

```bash
cd ../examples
cp vsoa_monitor_config.json config.json
./vsoa_system_monitor &
sleep 2

# /version 端点
curl http://<host>:<port>/version
# → {"service":"system-monitor","version":"1.0.0",...}

# /health 端点
curl http://<host>:<port>/health
# → {"status":"healthy","uptime_sec":2,"checks":{"cpu":"OK: 12.3%","memory":"OK: 45.6%"}}

# /config 端点
curl http://<host>:<port>/config
# → {"node":{"service_name":"system-monitor","server_host":"0.0.0.0",...}}

# /metrics 端点 (Prometheus 格式)
curl http://<host>:<port>/metrics
# → # HELP process_cpu_usage ...
#    ... vsoa 内置指标
```

**验证点**: 4 个内置端点在未写一行 HTTP 代码的情况下自动可用。

### B3. 验证 registerRpc — 业务 RPC 端点

```bash
curl http://<host>:<port>/system/status
# → {"cpu_percent":12.3,"memory_percent":45.6,...}

curl http://<host>:<port>/system/cpu
# → {"cpu_percent":12.3}

curl http://<host>:<port>/system/memory
# → {"mem_percent":45.6,"mem_used_mb":3580,"mem_total_mb":7840}
```

**验证点**: `registerRpc()` 注册的 3 个端点正确响应。

### B4. 验证 publish — VSOA 发布

使用 vsoa_cli 或一个订阅节点订阅 `/system/cpu_usage`，应每秒收到：
```json
{"cpu_percent":12.3,"timestamp":1715850000}
```

**验证点**: svc() 线程每秒 publish，数据通过 VSOA pub/sub 分发。

### B5. 验证 runtime OnCommand

```bash
# 通过 ServiceManager::ExecuteCommand 调用
curl http://<host>:<port>/<cmd_endpoint>
```

或在代码中通过 `getServiceManager()->ExecuteCommand("cpu", "", response)` 查询实时 CPU。

---

## 验证清单

| # | 功能 | 验证方式 | 示例文件 |
|---|------|---------|----------|
| 1 | ServiceTask 线程池 | A2: "activated: 3" + 优雅退出 | example_classic_service |
| 2 | ConfigManager JSON + 类型转换 | A2: "interval=200ms" 从 JSON 读取 | example_classic_service |
| 3 | MetricsCollector Counter/Gauge/Histogram | A2: "2 counters, 2 gauges, 1 histogram" | example_classic_service |
| 4 | Logger 格式化输出 | A2: `[INFO][timestamp][thread-id]` | example_classic_service |
| 5 | ProcessController 信号捕获 | A2: timeout 发送 SIGTERM → OnStop 执行 | example_classic_service |
| 6 | VsoaNode 内置端点 | B2: /version /health /config /metrics | vsoa_system_monitor |
| 7 | VsoaNode registerRpc | B3: /system/status /cpu /memory | vsoa_system_monitor |
| 8 | VsoaNode publish | B4: 订阅 /system/cpu_usage 每秒收到 JSON | vsoa_system_monitor |
| 9 | VsoaNode HealthCheck | B2: cpu>95% 时 /health 返回 degraded | vsoa_system_monitor |
|10 | 优雅关闭 | A2/B: OnStop → threads join → VSOA shutdown | 两者 |
