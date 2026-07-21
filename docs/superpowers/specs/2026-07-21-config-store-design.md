# 配置本地保存（ConfigStore）设计

**日期：** 2026-07-21
**版本目标：** v2.3
**状态：** 设计中 → 待审阅

---

## 背景

DeviceForge 现有多个 Tool（FTP/Telnet/SSH/WebSocket/Modbus/NetRelay/OPC UA）都需要用户输入连接参数（IP、端口、endpoint、用户名、密码等）。当前每次启动都要重新输入，缺少历史记录与复用机制。需要一个统一的本地存储层来持久化这些配置。

## 一、目标

| 编号 | 目标 |
|------|------|
| G1 | 设备记录（IP/端口/名称）持久化，启动时自动填充历史。约束：`key` 必须稳定（如 IP:port），UI 显示用 `displayName` 字段，避免重命名破坏唯一性 |
| G2 | OPC UA 连接记录（endpoint + 安全策略/认证选项）持久化 |
| G3 | 用户名/密码持久化；密码字段由调用方在存入前调用 `DpapiCrypto::protect` 加密（Windows DPAPI base64），读出时再 unprotect |
| G4 | 所有 Tool 的输入面板支持隐式保存（用户输入即持久化） |
| G5 | 提供统一的"设置"面板用于查看、修改、删除已存项 |
| G6 | 支持 JSON 导入/导出，方便配置迁移与备份。敏感字段在 JSON 中保持密文标记 |

## 二、方案选择

### 已评估方案

| 方案 | 复杂度 | 增量写入 | 并发安全 | 类型安全 |
|------|--------|----------|----------|----------|
| **A. 单表 SQLite + JSON 值**（推荐） | 低 | ✅ 一行 upsert | ✅ SQLite 锁 | 应用层保证 |
| B. 多表强 schema | 高 | ✅ | ✅ | ✅ DB 约束 | 但 schema migration 复杂 |
| C. JSON 文件 | 低 | ❌ 全文重写 | ❌ | ❌ |

选 A：单表 + JSON 值最贴合需求，SQLite 免 schema migration 负担，Qt 6 `QSqlDatabase` 原生支持。

## 三、架构

```
┌─────────────────────────────────────────────────────────┐
│           UI 层（所有 Tool 输入面板 + 设置面板）        │
├─────────────────────────────────────────────────────────┤
│  ConfigStore  (单例，src/config/ConfigStore.h)          │
│    ├ save(type, key, value) → upsert 一行              │
│    ├ load(type, key) → QVariantMap                       │
│    ├ list(type) → QList<QVariantMap>                     │
│    ├ remove(type, key)                                   │
│    └ exportTo(path) / importFrom(path)                   │
├─────────────────────────────────────────────────────────┤
│           Windows DPAPI（src/config/DpapiCrypto.h）    │
│           + SQLite via QSqlDatabase (Qt 6)             │
└─────────────────────────────────────────────────────────┘
```

## 四、数据库 schema

**文件位置：** `%APPDATA%/DeviceForge/config.db`（首次启动自动创建）
**库：** SQLite via Qt `QSqlDatabase`

```sql
-- 注意：敏感字段标记（"哪些字段是密文"）写在 value JSON 内部（调用方自行管理），
-- 不在表结构里用单独列。原因：敏感字段可能只是一条记录里的部分字段（如只有 password），
-- 放在 JSON 内更灵活，避免表结构复杂化。
CREATE TABLE IF NOT EXISTS config_items (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    type        TEXT NOT NULL,            -- 来源 Tool/类别，如 'device.list' 'opcua.endpoint' 'ftp.credential'
    key         TEXT NOT NULL,            -- 稳定标识（IP:port、endpoint URL 等）
    value       TEXT NOT NULL,            -- JSON 序列化的 QVariantMap
    created_at  INTEGER NOT NULL,
    updated_at  INTEGER NOT NULL,
    UNIQUE(type, key)
);

CREATE INDEX IF NOT EXISTS idx_config_type ON config_items(type);
```

## 五、ConfigStore API

```cpp
// src/config/ConfigStore.h
class ConfigStore {
public:
    static ConfigStore& instance();

    // 初始化（应用启动时调用一次）
    bool open();   // 打开/创建数据库，建表
    void close();  // 关闭（应用退出时）

    // 基本读写：value 始终是 QVariantMap。敏感字段值在存入前自行调用
    // DpapiCrypto::protect 转成 base64 密文（QString），读出时再 unprotect。
    // 这样 API 类型统一，调用方明确知道什么时候解密。
    bool save(const QString& type, const QString& key,
              const QVariantMap& value);
    QVariantMap load(const QString& type, const QString& key);
    bool exists(const QString& type, const QString& key);
    bool remove(const QString& type, const QString& key);
    QList<QVariantMap> list(const QString& type, int limit = 1000);

    // 导入/导出（JSON 文件，敏感字段保持密文标记）
    bool exportTo(const QString& jsonPath);
    bool importFrom(const QString& jsonPath);
};
```

**调用示例：**

```cpp
// 设备总线（DeviceBusWidget）保存一条设备
// 约束：key 用稳定标识（如 "ip:port"），UI 显示用 value["displayName"]
QVariantMap dev;
dev["ip"] = "192.168.1.10";
dev["port"] = 22;
dev["displayName"] = "PLC-Lab-1";   // UI 显示，可改
dev["note"] = "西门子 S7-1200";
ConfigStore::instance().save("device.list", "192.168.1.10:22", dev);

// OPC UA Tool 启动时加载历史 endpoint
auto list = ConfigStore::instance().list("opcua.endpoint", 20);
for (auto& e : list) {
    m_endpointCombo->addItem(e["url"].toString());
}

// FTP 凭证保存（密码字段自行 DPAPI 加密再存）
QVariantMap cred;
cred["host"] = "192.168.1.10";
cred["port"] = 21;
cred["username"] = "admin";
cred["password"] = DpapiCrypto::protect("secret123");  // → base64
ConfigStore::instance().save("ftp.credential", "admin@192.168.1.10:21", cred);
```

## 六、DPAPI 封装

```cpp
// src/config/DpapiCrypto.h
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
class DpapiCrypto {
public:
    // 加密 → 返回 base64 字符串（直接可存 SQLite TEXT）
    static QString protect(const QString& plain);
    // 解密（base64 输入 → 明文）
    static QString unprotect(const QString& cipher);
};
#else
// 非 Windows 平台 stub：返回原值 + warning 日志
// v2.3 仅支持 Windows；Linux/macOS 见 ROADMAP v2.4
#include <QString>
class DpapiCrypto {
public:
    static QString protect(const QString& plain);    // 返回原值 + log warning
    static QString unprotect(const QString& cipher); // 返回原值 + log warning
};
#endif
```

**关键点：**
- `CryptProtectData` 用默认 flag（`CRYPTPROTECT_LOCAL_MACHINE = 0`）= 当前用户作用域，其他 Windows 用户无法解密
- 跨平台 stub 保证编译通过 + 显式 warning，调用方知道降级行为

## 七、UI 集成

### 7.1 隐式保存（所有 Tool 输入面板）

每个 Tool 的输入面板在以下时机触发 `ConfigStore::save`：
- **提交时**（点击"连接"/"读"/"订阅"等操作按钮前）保存当前输入
- **失焦时**（QLineEdit::editingFinished）保存当前输入

实现：通过 `ConfigStore` 静态方法 + 简单包装，避免每个 Tool 重复样板代码。

```cpp
// 例：DeviceBusWidget 添加设备时
ConfigStore::instance().save("device.list", name, dev);
m_historyCombo->addItem(name);  // 更新 UI 历史下拉
```

### 7.2 设置面板（新增 `SettingsDialog`）

入口：**主菜单/工具栏 → 设置**（位置：`DeployMaster` 顶部添加一个齿轮图标按钮）

布局：
```
┌─ 配置管理 ──────────────────────────────────────────┐
│ 类型过滤: [所有类型 ▼]  搜索: [_______]  [刷新]    │
├────────────────────────────────────────────────────┤
│ 类型      键          更新时间        操作        │
│ device    PLC-Lab-1   2026-07-21 14:23  [编辑][删除]│
│ opcua     opc.tcp://…  2026-07-21 12:01  [编辑][删除]│
│ ftp       admin@…     2026-07-20 09:30  [编辑][删除]│
│                                                    │
├────────────────────────────────────────────────────┤
│ [导入]  [导出]  [清空所有]                         │
└────────────────────────────────────────────────────┘
```

- 表格列：类型、键、更新时间、操作（编辑/删除）
- 编辑：弹窗显示原始 value（密码字段默认遮罩，点击"显示"触发解密）
- 删除：二次确认弹窗
- 导入/导出：JSON 文件，导入时敏感字段保持加密（需同用户 DPAPI 才能解密）

### 7.3 历史下拉（Quick Access）

每个 Tool 的输入框旁添加 QComboBox 历史下拉：
- 设备总线 IP 输入框
- OPC UA endpoint 输入框
- FTP/Telnet/SSH host 输入框

实现：启动时 `list(type)` 拉取最近 20 条，下拉选中即填回。

## 八、安全考虑

| 风险 | 缓解 |
|------|------|
| 密码以明文形式读取 | DPAPI 用当前用户作用域，其他用户无法解密 |
| 数据库文件被复制到其他机器 | 同上，DPAPI 绑定 Windows 用户账号 |
| 导入到不同机器失败 | 导入时识别 value JSON 中的密文字段，提示"密文将无法在新机器解密，建议重新输入密码" |
| 误删配置 | 设置面板二次确认；可选"最近 N 天软删除"（v2.3 不实现，v2.4 再说） |

## 九、实现拆分

| 文件 | 说明 |
|------|------|
| `src/config/ConfigStore.h/.cpp` | 单例，SQLite 读写、JSON 序列化 |
| `src/config/DpapiCrypto.h/.cpp` | Windows DPAPI 封装（其他平台 stub） |
| `src/config/SettingsDialog.h/.cpp` | 设置面板（QDialog） |
| `src/ui/DeviceBusWidget.cpp` 等 | 集成 `ConfigStore::save/list` |
| `CMakeLists.txt` | 添加 `src/config/` 子目录 |
| `tests/CMakeLists.txt` | 添加 `tst_config_store` 测试 |

## 十、测试覆盖（`tst_config_store`）

- 基本读写往返
- 唯一键约束（同 type+key upsert 覆盖）
- 密码 DPAPI 加解密往返（明文 → 密文 → 明文相等）
- list 按 updated_at 降序
- remove 生效
- 并发写不丢数据（10 个线程同时 upsert 不同 key）
- 导入导出 round-trip

## 十一、验收标准

1. 启动 DeviceForge，添加一台设备，关闭再开，设备记录自动出现在历史下拉
2. 设备记录的密码字段在 SQLite 中是 DPAPI 密文（用 DB Browser 看是乱码）
3. OPC UA endpoint 历史下拉显示最近 20 条
4. 设置面板可查看/编辑/删除所有配置项
5. 导出 JSON → 在另一台机器导入（同一 Windows 用户）→ 数据可解密还原
6. 导出 JSON 中密码字段保持密文（不降级为明文）

## 十二、范围之外（v2.4+）

- Linux/macOS 跨平台密码加密（Keychain / libsecret）
- 云同步（云端配置备份）
- 配置版本历史（软删除 + 回滚）
- 多用户隔离（当前绑定 Windows 用户）
- 配置模板分享