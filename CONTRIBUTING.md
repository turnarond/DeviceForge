# Contributing to DeviceForge

欢迎贡献！请先阅读本文。

## 入门

1. Fork 本仓库
2. 克隆到本地：`git clone https://github.com/YOUR_USER/DeviceForge.git`
3. 运行 `build.bat`（CMake 构建）或用 VS2022 打开生成的 `build/DeviceForge.sln`
4. 创建你的功能分支：`git checkout -b feature/my-feature`

## 开发环境

- Qt 6.11.1 (MSVC 2022 64-bit)
- Visual Studio 2022
- CMake 3.22+

详见 [CLAUDE.md](CLAUDE.md) 了解完整架构说明。

## 代码规范

- 中文注释和 UI 文案
- 类名 PascalCase，方法 camelCase，成员变量 `m_camelCase`
- 修改接口/架构时同步更新对应文档（文档是唯一对外标准，见 [docs/README.md](docs/README.md)）

## 开发规范（重要）

- **完整 16 条规范**（强制）：见 [docs/00-开发规范/开发规范.md](docs/00-开发规范/开发规范.md)
- **TDD**：红-绿-重构循环，每次实现必须附带测试代码或指明要变绿的测试用例，红灯代码不提交
- **SDD 流程**：需求 → 方案设计 → 任务规划 → 实施 → 交付，关键阶段专家评审（尤其方案设计），见 [SDD-工程化流程.md](docs/00-开发规范/SDD-工程化流程.md)
- **分支**：从 `main` 新建 `feature/xxx`、`fix/xxx`、`docs/xxx`、`refactor/xxx` 分支开发，验证后合并回 `main`，一个分支只做一件事

## 提交规范

使用约定式提交：

- `feat:` 新功能
- `fix:` Bug 修复
- `docs:` 文档更新
- `refactor:` 代码重构
- `chore:` 构建/工具变更

## 提交流程

1. 确保代码在 Windows + Qt 6.11.1 上编译通过
2. 提交 PR 并填写 PR 模板
3. 维护者会在一周内 review

## 沟通

- 使用问题或功能请求请开 [Issue](https://github.com/YOUR_USER/DeviceForge/issues)
- 使用咨询请开 [Discussion](https://github.com/YOUR_USER/DeviceForge/discussions)
