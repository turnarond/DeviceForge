# tools/devtools — Python 配套工具集

配套工程工具统一放 `tools/`（Python，独立架构 + 自带测试）。服务于构建校验、冒烟、部署验证。

## 架构

```
tools/
├── devtools/           # 工具包（import devtools.xxx 使用）
│   ├── __init__.py
│   ├── versioncheck.py # 全仓版本号一致性校验（以 CMakeLists.txt project() VERSION 为准）
│   ├── smoke.py        # UI 冒烟：启动/主窗口/退出验证
│   ├── ftpguard.py     # SylixOS FTP 三禁静态守卫（#59；退出码 0/1/2/3 语义化，零证据即 FAIL）
│   ├── gen_wss_cert.py # WSS 自签名证书生成
│   └── cli.py          # 统一 CLI 入口
└── tests/              # unittest 测试（test_smoke.py / test_versioncheck.py / test_ftpguard.py）
```

## 命令

```bash
python tools/devtools/versioncheck.py     # 版本一致性（发版/改版本号后必跑）
python tools/devtools/smoke.py            # UI 冒烟（构建产物存在时）
python tools/devtools/ftpguard.py         # SylixOS FTP 三禁单源守卫（改 FtpAdapter.cpp 后必跑）
python -m unittest discover -s tools/tests   # 工具集自身测试
```

## 新增工具规约（TDD）

1. 先在 `tools/tests/test_<name>.py` 写红灯用例（unittest）
2. 实现 `tools/devtools/<name>.py`：单一职责、可独立运行（`if __name__ == "__main__"`）+ 可导入复用
3. 需要命令行入口时挂到 `cli.py`，保持子命令风格一致
4. 纯逻辑与 IO 分离，便于 mock 测试；退出码语义化（0 成功 / 非 0 失败），供 CI 使用
5. 同一提交更新 `docs/README.md`「配套工程工具」表
6. **输出契约=UTF-8，与控制台码页无关**：CLI 入口（`main()` 首部）对 `sys.stdout`/`sys.stderr` 调 `reconfigure(encoding="utf-8", errors="replace")`。两种坏形态都实测过：① 写侧——cp1252 这类编不出中文的码页让 `print` 抛 `UnicodeEncodeError`，versioncheck 在 CI 里表现为 4 个用例 ERROR，smoke 表现为约定退出码 2 变成 1（"工具自己崩了"冒充"检查未通过"）；② 读侧——父进程按本地 GBK 解子进程的 UTF-8 字节，在读线程抛 `UnicodeDecodeError`（本仓 test_smoke 的 `text=True` 就踩过）。所以**读工具输出的地方必须显式声明编码**：测试里 `subprocess.run(..., encoding="utf-8")`，人工排查用 `Get-Content -Encoding UTF8`，不要依赖 `text=True` 的 locale 默认值。
