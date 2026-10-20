# 版本发布与 Tag 规范

> 对应开发规范第 10、13 条：产品白皮书 / roadmap / 版本边界 / 打包节点打 tag / SDK 向前兼容。

---

## 1. 版本号规则（语义化版本 MAJOR.MINOR.PATCH）

判据按「**对外契约**」与「**能力包**」两条线定，**自 v2.16 起生效**；v2.16 之前的 2.x 历史按当时规则解释，不追溯、不重编号。

- **MAJOR ＝ 对外契约破坏**（用户或集成方必须行动才能继续工作）：
  - 对外 SDK 的头文件/接口 ABI 破坏（见 §3）；
  - 磁盘格式不兼容——任务模板文件、`ConfigStore` 记录结构、任务历史等**旧数据读不了或跨机迁移失效**；
  - 现场可见的行为/落点变更，且**必须改配置**才能保持原有流程。
  - **纯内部分层、模块拆分、架构重构不升 MAJOR。**本仓先例：v2.11 任务中心子系统、v2.12 三大界面重构（含 MODBUS 线程模型改动）均按 MINOR 发布，按本条判据仍然成立。
- **MINOR ＝ 至少一个「能力包」**：发布说明能单独写出「新增能力」一节，且该能力**有独立验收标准**（可测、可现场核对）。零散小按钮、单点开关**并进最近的版本目标一起发**，不单独占一个 MINOR 号。
- **PATCH ＝ 修复、还债、性能与工装改进（无新能力）**。性能与工装条目仍须写进 CHANGELOG 对应小节，但不因此升 MINOR。
- **自检题**（防止版本号越升越便宜）：把本版发布说明的「新增能力」一节整段删掉，还剩什么？只剩修复与还债 ⇒ 该发 `.1`。
  > 判据启用后的首个对照样本：v2.15.0 判 MINOR 成立——波2 交付了「目录条目选中即部署 + 远端子目录自动创建」这一有验收标准的能力包；同批的波1（清单去 1000 条隐性截断）与波3（已确保目录缓存、编辑器批量刷表、工具栏两行化）单独看都只够 PATCH。若某版只含这两波，按本规则应发 `v2.14.2`。

### 唯一权威与全仓同步

- **唯一权威**：`CMakeLists.txt` 的 `project(DeviceForge VERSION x.y.z ...)`。
- 改动版本号必须全仓同步九个强制来源（以权威为准）：
  1. `CMakeLists.txt`（`project(DeviceForge VERSION x.y.z ...)`，唯一权威）
  2. `src/app/DeviceForge.rc`（FILEVERSION / FileVersion）
  3. `README.md`（版本字段）
  4. `CHANGELOG.md`（首个版本标题）
  5. `CLAUDE.md`（当前版本表述）
  6. `docs/01-白皮书/白皮书.md`（版本头）
  7. `RELEASE_NOTES.md`（Release Notes 标题）
  8. 根 `ROADMAP.md`（当前版本字段）
  9. `docs/01-白皮书/产品路线图.md`（当前版本字段）
- **强制校验**：版本变更后运行 `tools/devtools/versioncheck.py`；任一来源缺失、无法解析或版本不一致均返回非零并阻断发布。

## 2. 版本边界

- 每个版本在发布计划中定义**边界**：版本目标、范围（In / Out）、验收标准（可测）。
- 打包前逐项核对验收标准；**范围外内容不混入当前版本**（记入 roadmap / 下一版本需求，规范第 10 条）。
- 版本规划来源：`docs/01-白皮书/产品路线图.md`（roadmap）。

## 3. SDK 向前兼容（对外 SDK 库）

- 升级涉及头文件/接口改动时**必须向前兼容**（规范第 13 条）：
  - 不改已有导出函数签名、类成员布局、虚表顺序（保持二进制 ABI 稳定）
  - 新能力优先**新增接口**，不修改旧接口语义
- 必须的不兼容变更：**提升 MAJOR** + 提供迁移说明；杜绝使用者被迫用新 SDK 重编。
- 涉及 SDK 接口的版本评审（方案设计评审强制项）必须给出 ABI 兼容性结论。

## 4. 打包与 Tag

### 打包节点流程（SDD 阶段 4）

1. 代码冻结（`main` 合入全部本版本变更）
2. 全量回归：`ctest -C Release --output-on-failure` 全绿
3. 冒烟：`tools/devtools/smoke.py build/Release/DeviceForge.exe` 通过
4. 版本一致性：`tools/devtools/versioncheck.py` 通过
5. 打包：`DeviceForge-vX.Y.Z-win64.zip`（exe + Qt DLL + `libcurl-x64.dll` + `libssh2.dll` + `Updater.exe` + 资源）。已由 `.github/workflows/release-validation.yml` 自动化（产出 workflow artifact + `dist\` 下 zip）
6. 更新 `CHANGELOG.md` / `RELEASE_NOTES.md`
7. **打 tag**：`git tag -a vX.Y.Z -m "vX.Y.Z (YYYY-MM-DD)：<变更摘要或指向 CHANGELOG>"`（annotated tag）
8. 发布：建 GitHub Release（与 tag 同名；标题+正文**从 `RELEASE_NOTES.md` 对应节逐字抽取**，不另写一份对外文本，防止两处漂移）
9. **挂发布包**：MINOR/MAJOR **必须**把 `DeviceForge-vX.Y.Z-win64.zip` 作为 **Release 资产**上传（`gh release upload vX.Y.Z <zip>`）；PATCH 可不挂。
   - 依据（实测）：workflow artifact **90 天后过期**——v2.15.0 的包到期时间 2027-01-18，而 Release 永久存在；只留 artifact 等于 90 天后版本页上没有任何可下载物。
   - 现状：包在 CI 的 artifact 里，需人工下载后上传。**把这一步自动化进 workflow 要给它 `contents: write` 权限**（现为 `read`），属 CI 权限变更，需单独评审后再做。

### Tag 命名与信息

- 格式：`v<MAJOR>.<MINOR>.<PATCH>`（如 `v2.7.0`）
- tag 打在 `main` 的发布提交上；tag 消息必须含：**版本号 / 发布日期 / 变更摘要**。
- 已发布 tag 不回退不覆盖；紧急修复走 `vX.Y.Z+1`（PATCH）。

## 5. 发布清单（交付评审用，见 `00-开发规范/评审检查单.md`）

| # | 检查项 | 工具/证据 |
|---|--------|-----------|
| 1 | 版本号全仓一致 | `versioncheck.py` |
| 2 | 全量测试绿 | `ctest` 输出 |
| 3 | 冒烟通过 | `smoke.py` |
| 4 | 验收标准逐项确认 | 发布计划 |
| 5 | CHANGELOG / RELEASE_NOTES 更新 | 文件 |
| 6 | 版本档位符合 §1 判据（MINOR 需有「新增能力」节） | 发布计划 + 自检题 |
| 7 | GitHub Release 正文与 `RELEASE_NOTES.md` 同源 | 逐字比对（脚本 `read==live`） |
| 8 | 发布包已挂 Release 资产（MINOR/MAJOR 必挂） | `gh release view vX.Y.Z --json assets` |
| 6 | tag 已打（annotated） | `git tag -n` |
| 7 | 发布包完整 | 包内容清单 |

## 6. 版本历史记录

- 版本历史统一维护在 `CHANGELOG.md` 与 `docs/01-白皮书/白皮书.md` 附录。
- 每次发布在 `docs/01-白皮书/产品路线图.md` 中把对应条目标记完成并新增版本历史行。
