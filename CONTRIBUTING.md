# Contributing to DeviceForge

欢迎贡献！请先阅读本文。

## 入门

1. Fork 本仓库
2. 克隆到本地：`git clone https://github.com/YOUR_USER/DeviceForge.git`
3. 用 VS2022 打开 `DeployMaster.vcxproj` 或使用 CMake 构建
4. 创建你的功能分支：`git checkout -b feature/my-feature`

## 开发环境

- Qt 6.10.1 (MSVC 2022 64-bit)
- Visual Studio 2022
- CMake 3.16+

详见 [CLAUDE.md](CLAUDE.md) 了解完整架构说明。

## 代码规范

- 中文注释和 UI 文案
- 类名 PascalCase，方法 camelCase，成员变量 `m_camelCase`
- 修改接口/架构时同步更新 `docs/superpowers/` 文档

## 提交规范

使用约定式提交：

- `feat:` 新功能
- `fix:` Bug 修复
- `docs:` 文档更新
- `refactor:` 代码重构
- `chore:` 构建/工具变更

## 提交流程

1. 确保代码在 Windows + Qt 6.10.1 上编译通过
2. 提交 PR 并填写 PR 模板
3. 维护者会在一周内 review

## 沟通

- 使用问题或功能请求请开 [Issue](https://github.com/YOUR_USER/DeviceForge/issues)
- 使用咨询请开 [Discussion](https://github.com/YOUR_USER/DeviceForge/discussions)
