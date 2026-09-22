# 版本发布与 Tag 规范

> 对应开发规范第 10、13 条：产品白皮书 / roadmap / 版本边界 / 打包节点打 tag / SDK 向前兼容。

---

## 1. 版本号规则（语义化版本 MAJOR.MINOR.PATCH）

- **MAJOR**：不兼容变更（含对外 SDK 的 ABI 破坏）
- **MINOR**：新增功能（向后兼容）
- **PATCH**：缺陷修复

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
5. 打包：`DeviceForge-vX.Y.Z-win64.zip`（exe + Qt DLL + `libcurl-x64.dll` + `libssh2.dll` + `Updater.exe` + 资源）
6. 更新 `CHANGELOG.md` / `RELEASE_NOTES.md`
7. **打 tag**：`git tag -a vX.Y.Z -m "vX.Y.Z (YYYY-MM-DD)：<变更摘要或指向 CHANGELOG>"`（annotated tag）
8. 发布：上传发布包到分发渠道（GitHub Releases），发布说明随 tag 附上

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
| 6 | tag 已打（annotated） | `git tag -n` |
| 7 | 发布包完整 | 包内容清单 |

## 6. 版本历史记录

- 版本历史统一维护在 `CHANGELOG.md` 与 `docs/01-白皮书/白皮书.md` 附录。
- 每次发布在 `docs/01-白皮书/产品路线图.md` 中把对应条目标记完成并新增版本历史行。
