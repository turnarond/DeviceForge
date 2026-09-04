# 开发工具集（Python 系列配套工具）

> 对应开发规范第 5 条：根据项目工程需要创建的工具集，独立工程架构，用于测试、部署、冒烟等验证。
> 要求：**零第三方依赖**（仅标准库）、每个工具自带 unittest 测试、通过统一 CLI 入口调用。

---

## 目录结构

```
tools/
├── README.md              本说明
├── devtools/              工具包
│   ├── __init__.py
│   ├── cli.py             统一命令行入口（versioncheck / smoke）
│   ├── versioncheck.py    版本一致性校验（规范第 2、10 条）
│   └── smoke.py           UI 冒烟（启动 → 主窗口出现 → 优雅退出）
└── tests/                 工具自测（unittest，标准库）
    ├── __init__.py
    ├── test_versioncheck.py
    └── test_smoke.py
```

## 运行测试（TDD 红-绿-重构）

```bash
# 在仓库根目录
python -m unittest discover -s tools/tests -v
```

## 工具说明

### 1. versioncheck.py — 版本一致性校验

改版本号时必须全仓同步（规范第 2 条），本工具校验以下来源是否一致，以 `CMakeLists.txt` 的 `project(... VERSION ...)` 为唯一权威：

| 来源 | 解析规则 |
|------|----------|
| `CMakeLists.txt` | `project(DeviceForge VERSION x.y.z ...)`（权威） |
| `src/app/DeviceForge.rc` | `FILEVERSION x,y,z,0` / `"FileVersion", "x.y.z.0"` |
| `README.md` | `**版本**：x.y.z` |
| `CHANGELOG.md` | 首个 `## [x.y.z]` |
| `CLAUDE.md` | `当前版本 x.y.z` |
| `docs/01-白皮书/白皮书.md` | `版本：x.y.z` |
| `RELEASE_NOTES.md` | `# DeviceForge vX.Y.Z Release Notes` |
| `ROADMAP.md` | `**当前版本**：vX.Y.Z` |
| `docs/01-白皮书/产品路线图.md` | `**当前版本**：vX.Y.Z` |

用法：

```bash
python tools/devtools/cli.py versioncheck            # 检查仓库根目录
python tools/devtools/cli.py versioncheck --root <dir>
```

- 全部一致：输出每个来源的版本并退出码 0
- 任一强制来源缺失、解析失败或版本不一致：**退出码 1**（发布阻断，见 `docs/01-白皮书/版本发布与Tag规范.md`）

### 2. smoke.py — UI 冒烟验证

发布/打包前验证可执行文件能启动、主窗口出现、正常退出（规范第 16 条交付评审强制项）：

```bash
python tools/devtools/cli.py smoke build/Release/DeviceForge.exe
python tools/devtools/cli.py smoke <exe> --timeout 60 --title DeviceForge
```

流程：启动进程 → 轮询主窗口标题（EnumWindows 包含匹配，默认 `DeviceForge`）→ 找到后发送 `WM_CLOSE` 优雅退出 → 等待退出 → 退出码 0 即通过；超时/提前崩溃/退出码非 0 均失败。

### 3. gen_wss_cert.py — 重新生成 WSS 测试自签名证书

Qt 6.11 无运行时生成自签名证书的 API，`src/app/certs/` 中的测试证书（RSA 2048、SAN 含 localhost）由本脚本预生成并随 QRC 打包。证书过期或需更换密钥时执行：

```bash
python tools/devtools/gen_wss_cert.py
python tools/devtools/gen_wss_cert.py --openssl <openssl路径> --days 3650
```

## 添加新工具

1. 在 `devtools/` 新建 `xxx.py`，纯标准库实现
2. 在 `tests/test_xxx.py` 写测试（TDD 红-绿）
3. 在 `cli.py` 注册子命令
4. 在 `README.md` 工具说明表补充一行
