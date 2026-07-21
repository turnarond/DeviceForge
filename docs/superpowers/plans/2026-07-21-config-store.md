# ConfigStore 本地配置保存 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 DeviceForge 添加本地配置持久化层（SQLite + Windows DPAPI），覆盖所有 Tool 的输入面板 + 统一的设置面板管理 + JSON 导入导出。

**Architecture:** 单表 SQLite 存所有配置（`type+key` 唯一约束），value 字段是 JSON 序列化的 `QVariantMap`；敏感字段（如密码）由调用方在 value JSON 内自行用 DPAPI 加密（base64 字符串）；设置面板提供查看/编辑/删除/导入/导出。

**Tech Stack:** Qt 6 `QSqlDatabase` + SQLite driver、`QStandardPaths::AppDataLocation`、Windows DPAPI（`CryptProtectData`）、QtTest + CTest、QSS 主题。

## 全局约束

- 项目根目录：`D:/work/project/DeviceForge`，构建：`build/Release/DeviceForge.exe`
- Qt 6.11.1，CMake 3.22+，MSVC v143
- 现有 IProtocolAdapter / ToolBackend / ToolWidget 架构不破坏
- 中文优先（注释、文档）
- darkstyle.qss 是颜色单一来源，UI 必须用 QSS 变量风格（不写死颜色 hex 在代码里）
- `Q_OBJECT` 类必须 moc 编译
- 命名：类 PascalCase，方法 camelCase，成员 `m_xxx`，常量 `UPPER_SNAKE_CASE`
- 调试阶段：`git commit` 之前先确认 `cmake --build . --config Release --target DeviceForge` exit 0

---

## File Structure

| 文件 | 职责 |
|------|------|
| `src/config/ConfigStore.h` | 单例类声明 |
| `src/config/ConfigStore.cpp` | SQLite 读写、JSON 序列化、导入导出 |
| `src/config/DpapiCrypto.h` | DPAPI 跨平台声明（Windows 实 + 其他 stub） |
| `src/config/DpapiCrypto.cpp` | DPAPI Windows 实现 |
| `src/config/SettingsDialog.h` | 设置面板声明 |
| `src/config/SettingsDialog.cpp` | 设置面板实现（表格 + 编辑/删除/导入/导出） |
| `src/ui/DeviceBusWidget.cpp/.h` | 修改：保存设备 + 显示历史下拉 |
| `src/tools/OpcUaClientTool/OpcUaClientWidget.cpp/.h` | 修改：endpoint 历史下拉 |
| `src/tools/FtpDeployTool/FtpDeployWidget.cpp/.h` | 修改：密码字段 DPAPI 加密保存 |
| `tests/config/tst_config_store.cpp` | ConfigStore 单元测试 |
| `tests/config/tst_dpapi_crypto.cpp` | DPAPI 加解密测试 |
| `CMakeLists.txt` | 添加 `src/config/` 子目录 |
| `tests/CMakeLists.txt` | 添加新测试目标 |

---

## Task 1: DpapiCrypto 跨平台封装

**Files:**
- Create: `src/config/DpapiCrypto.h`
- Create: `src/config/DpapiCrypto.cpp`
- Test: `tests/config/tst_dpapi_crypto.cpp`
- Modify: `CMakeLists.txt` (注册新子目录)
- Modify: `tests/CMakeLists.txt` (添加测试目标)

**Interfaces:**
- Produces:
  - `QString DpapiCrypto::protect(const QString& plain)` — Windows DPAPI 加密并 base64；其他平台原值 + log warning
  - `QString DpapiCrypto::unprotect(const QString& cipher)` — Windows DPAPI 解密；其他平台原值 + log warning

- [ ] **Step 1: 写失败测试**

```cpp
// tests/config/tst_dpapi_crypto.cpp
#include <QtTest>
#include "config/DpapiCrypto.h"

class TestDpapiCrypto : public QObject {
    Q_OBJECT
private slots:
    void roundTrip();
    void protectIsNotPlaintext();
};

void TestDpapiCrypto::roundTrip() {
    const QString plain = "secret-123!@#中文密码";
    const QString cipher = DpapiCrypto::protect(plain);
    QCOMPARE(DpapiCrypto::unprotect(cipher), plain);
}

void TestDpapiCrypto::protectIsNotPlaintext() {
    const QString plain = "secret-123";
    const QString cipher = DpapiCrypto::protect(plain);
    QVERIFY2(cipher != plain, "密文不能等于明文");
}

QTEST_MAIN(TestDpapiCrypto)
#include "tst_dpapi_crypto.moc"
```

- [ ] **Step 2: 运行测试确认失败**

```bash
cd build && cmake --build . --config Release --target tst_dpapi_crypto 2>&1 | tail -5
```
Expected: 编译失败（DpapiCrypto.h 不存在）

- [ ] **Step 3: 实现 DpapiCrypto.h**

```cpp
// src/config/DpapiCrypto.h
#pragma once
#include <QString>

class DpapiCrypto {
public:
    // 加密 plain → base64 字符串；非 Windows 平台返回 plain + log warning
    static QString protect(const QString& plain);
    // 解密 cipher → 明文；非 Windows 平台返回 cipher + log warning
    static QString unprotect(const QString& cipher);
};
```

- [ ] **Step 4: 实现 DpapiCrypto.cpp (Windows 实现)**

```cpp
// src/config/DpapiCrypto.cpp
#include "DpapiCrypto.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>

static QByteArray dpapiCall(BOOL (WINAPI *fn)(DATA_BLOB*, DATA_BLOB*),
                            const QByteArray& in) {
    DATA_BLOB inBlob{static_cast<DWORD>(in.size()),
                     reinterpret_cast<BYTE*>(const_cast<char*>(in.constData()))};
    DATA_BLOB outBlob{0, nullptr};
    if (!fn(&inBlob, &outBlob)) {
        return {};
    }
    QByteArray out(reinterpret_cast<char*>(outBlob.pbData),
                   static_cast<int>(outBlob.cbData));
    LocalFree(outBlob.pbData);
    return out;
}

QString DpapiCrypto::protect(const QString& plain) {
    QByteArray bin = dpapiCall(CryptProtectData, plain.toUtf8());
    return QString::fromLatin1(bin.toBase64());
}

QString DpapiCrypto::unprotect(const QString& cipher) {
    QByteArray bin = QByteArray::fromBase64(cipher.toLatin1());
    QByteArray plain = dpapiCall(CryptUnprotectData, bin);
    return QString::fromUtf8(plain);
}

#else
// 非 Windows stub：返回原值 + warning（v2.3 仅支持 Windows）
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcDpapi, "deviceforge.dpapi")

QString DpapiCrypto::protect(const QString& plain) {
    qCWarning(lcDpapi) << "DpapiCrypto::protect 在非 Windows 平台是 no-op（明文存储）";
    return plain;
}

QString DpapiCrypto::unprotect(const QString& cipher) {
    qCWarning(lcDpapi) << "DpapiCrypto::unprotect 在非 Windows 平台是 no-op（明文返回）";
    return cipher;
}
#endif
```

- [ ] **Step 5: 注册 CMake 子目录**

在 `CMakeLists.txt` 找现有 `add_subdirectory(src/...)` 块附近，加：
```cmake
add_subdirectory(src/config)
```
并在 `src/config/CMakeLists.txt` 新建：
```cmake
target_sources(DeviceForge PRIVATE
    ConfigStore.cpp
    DpapiCrypto.cpp
    SettingsDialog.cpp
)
target_include_directories(DeviceForge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
```

- [ ] **Step 6: 在 tests/CMakeLists.txt 添加测试目标**

```cmake
# DpapiCrypto 单元测试
add_executable(tst_dpapi_crypto config/tst_dpapi_crypto.cpp
               ${CMAKE_SOURCE_DIR}/src/config/DpapiCrypto.cpp
               ${CMAKE_SOURCE_DIR}/src/config/ConfigStore.cpp)
target_link_libraries(tst_dpapi_crypto PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_dpapi_crypto COMMAND tst_dpapi_crypto)
```

（先加 ConfigStore.cpp stub 以链接通过；Task 2 实现后再用真实实现）

- [ ] **Step 7: 写临时 ConfigStore.cpp 占位**

```cpp
// src/config/ConfigStore.cpp（Task 1 占位）
#include "ConfigStore.h"
```

```cpp
// src/config/ConfigStore.h（Task 1 占位）
#pragma once
class ConfigStore {};
```

- [ ] **Step 8: 编译 + 运行测试确认通过**

```bash
cd build && cmake .. -DCMAKE_PREFIX_PATH="c:/Qt/6.11.1/msvc2022_64" >/dev/null 2>&1
cmake --build . --config Release --target tst_dpapi_crypto 2>&1 | tail -5
ctest -C Release -R tst_dpapi_crypto --output-on-failure
```
Expected: PASS

- [ ] **Step 9: Commit**

```bash
git add src/config/ tests/config/ CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(config): DpapiCrypto 跨平台封装 + 单元测试"
```

---

## Task 2: ConfigStore 实现

**Files:**
- Modify: `src/config/ConfigStore.h`（替换占位）
- Modify: `src/config/ConfigStore.cpp`（替换占位）
- Modify: `tests/config/tst_dpapi_crypto.cpp`（移除 `ConfigStore.cpp` 链接依赖）
- Create: `tests/config/tst_config_store.cpp`

**Interfaces:**
- Produces:
  ```cpp
  class ConfigStore {
  public:
      static ConfigStore& instance();
      bool open();                    // 打开/创建数据库 + 建表
      void close();
      bool save(const QString& type, const QString& key,
                const QVariantMap& value);
      QVariantMap load(const QString& type, const QString& key);
      bool exists(const QString& type, const QString& key);
      bool remove(const QString& type, const QString& key);
      QList<QVariantMap> list(const QString& type, int limit = 1000);
      bool exportTo(const QString& jsonPath);
      bool importFrom(const QString& jsonPath);
  };
  ```

- [ ] **Step 1: 移除 tst_dpapi_crypto 对 ConfigStore.cpp 的链接**

修改 `tests/CMakeLists.txt` 第 6 步添加的行，改为：
```cmake
add_executable(tst_dpapi_crypto config/tst_dpapi_crypto.cpp
               ${CMAKE_SOURCE_DIR}/src/config/DpapiCrypto.cpp)
target_link_libraries(tst_dpapi_crypto PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_dpapi_crypto COMMAND tst_dpapi_crypto)
```

- [ ] **Step 2: 写失败测试**

```cpp
// tests/config/tst_config_store.cpp
#include <QtTest>
#include <QStandardPaths>
#include <QFile>
#include "config/ConfigStore.h"

class TestConfigStore : public QObject {
    Q_OBJECT
private:
    QString m_dbPath;
private slots:
    void init();        // 每个用例前：删旧 db
    void cleanup();     // 每个用例后：关 ConfigStore
    void roundTrip();
    void uniqueKeyUpsert();
    void listOrder();
    void removeWorks();
    void exportImportRoundtrip();
};

void TestConfigStore::init() {
    m_dbPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
               + "/tst_config_store.db";
    QFile::remove(m_dbPath);
    QVERIFY(ConfigStore::instance().open(m_dbPath));
}
void TestConfigStore::cleanup() {
    ConfigStore::instance().close();
    QFile::remove(m_dbPath);
}

void TestConfigStore::roundTrip() {
    QVariantMap v{{"ip", "10.0.0.1"}, {"port", 22}};
    QVERIFY(ConfigStore::instance().save("device.list", "10.0.0.1:22", v));
    auto got = ConfigStore::instance().load("device.list", "10.0.0.1:22");
    QCOMPARE(got["ip"].toString(), QString("10.0.0.1"));
    QCOMPARE(got["port"].toInt(), 22);
}

void TestConfigStore::uniqueKeyUpsert() {
    QVariantMap v1{{"x", 1}};
    QVariantMap v2{{"x", 2}};
    ConfigStore::instance().save("t", "k", v1);
    ConfigStore::instance().save("t", "k", v2);
    QCOMPARE(ConfigStore::instance().load("t", "k")["x"].toInt(), 2);
}

void TestConfigStore::listOrder() {
    ConfigStore::instance().save("t", "a", QVariantMap{{"n", 1}});
    QTest::qWait(10);
    ConfigStore::instance().save("t", "b", QVariantMap{{"n", 2}});
    auto l = ConfigStore::instance().list("t");
    QCOMPARE(l.size(), 2);
    QCOMPARE(l[0]["key"].toString(), QString("b"));   // 最新在前
}

void TestConfigStore::removeWorks() {
    ConfigStore::instance().save("t", "k", QVariantMap{{"x", 1}});
    QVERIFY(ConfigStore::instance().remove("t", "k"));
    QVERIFY(!ConfigStore::instance().exists("t", "k"));
}

void TestConfigStore::exportImportRoundtrip() {
    ConfigStore::instance().save("t", "k1", QVariantMap{{"v", 1}});
    QString path = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                   + "/tst_config_export.json";
    QVERIFY(ConfigStore::instance().exportTo(path));
    ConfigStore::instance().close();
    QFile::remove(m_dbPath);
    QVERIFY(ConfigStore::instance().open(m_dbPath));
    QVERIFY(ConfigStore::instance().importFrom(path));
    QCOMPARE(ConfigStore::instance().load("t", "k1")["v"].toInt(), 1);
    QFile::remove(path);
}

QTEST_MAIN(TestConfigStore)
#include "tst_config_store.moc"
```

- [ ] **Step 3: 运行测试确认失败**

```bash
cd build && cmake --build . --config Release --target tst_config_store 2>&1 | tail -5
```
Expected: 链接错误（ConfigStore::open 签名不存在）

- [ ] **Step 4: 实现 ConfigStore.h**

```cpp
// src/config/ConfigStore.h
#pragma once
#include <QString>
#include <QVariantMap>
#include <QList>

class ConfigStore {
public:
    static ConfigStore& instance();

    // 路径参数：首次启动传默认路径；测试可指定临时 db
    bool open(const QString& dbPath = QString());
    void close();

    bool save(const QString& type, const QString& key,
              const QVariantMap& value);
    QVariantMap load(const QString& type, const QString& key);
    bool exists(const QString& type, const QString& key);
    bool remove(const QString& type, const QString& key);
    QList<QVariantMap> list(const QString& type, int limit = 1000);

    bool exportTo(const QString& jsonPath);
    bool importFrom(const QString& jsonPath);

private:
    ConfigStore() = default;
    QString m_dbPath;
    bool m_open = false;
};
```

- [ ] **Step 5: 实现 ConfigStore.cpp**

```cpp
// src/config/ConfigStore.cpp
#include "ConfigStore.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QUuid>

ConfigStore& ConfigStore::instance() {
    static ConfigStore s;
    return s;
}

static QSqlDatabase db() {
    return QSqlDatabase::database("config_store");
}

bool ConfigStore::open(const QString& dbPath) {
    QString path = dbPath;
    if (path.isEmpty()) {
        QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(base);
        path = base + "/config.db";
    }
    m_dbPath = path;
    QSqlDatabase d = QSqlDatabase::addDatabase("QSQLITE", "config_store");
    d.setDatabaseName(path);
    if (!d.open()) return false;
    QSqlQuery q(d);
    return q.exec(
        "CREATE TABLE IF NOT EXISTS config_items ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  type TEXT NOT NULL,"
        "  key TEXT NOT NULL,"
        "  value TEXT NOT NULL,"
        "  created_at INTEGER NOT NULL,"
        "  updated_at INTEGER NOT NULL,"
        "  UNIQUE(type, key))"
    );
}

void ConfigStore::close() {
    {
        QSqlDatabase d = db();
        if (d.isOpen()) d.close();
    }
    QSqlDatabase::removeDatabase("config_store");
    m_open = false;
}

bool ConfigStore::save(const QString& type, const QString& key,
                       const QVariantMap& value) {
    QSqlDatabase d = db();
    if (!d.isOpen()) return false;
    const QString json = QString::fromUtf8(
        QJsonDocument(QJsonObject::fromVariantMap(value)).toJson(QJsonDocument::Compact));
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QSqlQuery q(d);
    q.prepare(
        "INSERT INTO config_items(type, key, value, created_at, updated_at) "
        "VALUES(:t, :k, :v, :c, :u) "
        "ON CONFLICT(type, key) DO UPDATE SET "
        "  value=excluded.value, updated_at=excluded.updated_at");
    q.bindValue(":t", type);
    q.bindValue(":k", key);
    q.bindValue(":v", json);
    q.bindValue(":c", now);
    q.bindValue(":u", now);
    return q.exec();
}

QVariantMap ConfigStore::load(const QString& type, const QString& key) {
    QSqlDatabase d = db();
    if (!d.isOpen()) return {};
    QSqlQuery q(d);
    q.prepare("SELECT value FROM config_items WHERE type=:t AND key=:k");
    q.bindValue(":t", type);
    q.bindValue(":k", key);
    if (!q.exec() || !q.next()) return {};
    QJsonDocument doc = QJsonDocument::fromJson(
        q.value(0).toString().toUtf8());
    if (!doc.isObject()) return {};
    return doc.object().toVariantMap();
}

bool ConfigStore::exists(const QString& type, const QString& key) {
    QSqlDatabase d = db();
    if (!d.isOpen()) return false;
    QSqlQuery q(d);
    q.prepare("SELECT 1 FROM config_items WHERE type=:t AND key=:k");
    q.bindValue(":t", type);
    q.bindValue(":k", key);
    return q.exec() && q.next();
}

bool ConfigStore::remove(const QString& type, const QString& key) {
    QSqlDatabase d = db();
    if (!d.isOpen()) return false;
    QSqlQuery q(d);
    q.prepare("DELETE FROM config_items WHERE type=:t AND key=:k");
    q.bindValue(":t", type);
    q.bindValue(":k", key);
    return q.exec();
}

QList<QVariantMap> ConfigStore::list(const QString& type, int limit) {
    QList<QVariantMap> out;
    QSqlDatabase d = db();
    if (!d.isOpen()) return out;
    QSqlQuery q(d);
    q.prepare("SELECT type, key, value, updated_at FROM config_items "
              "WHERE type=:t ORDER BY updated_at DESC LIMIT :n");
    q.bindValue(":t", type);
    q.bindValue(":n", limit);
    if (!q.exec()) return out;
    while (q.next()) {
        QVariantMap m;
        m["type"] = q.value(0).toString();
        m["key"] = q.value(1).toString();
        m["updated_at"] = q.value(3).toLongLong();
        QJsonDocument doc = QJsonDocument::fromJson(q.value(2).toString().toUtf8());
        if (doc.isObject()) {
            QVariantMap inner = doc.object().toVariantMap();
            for (auto it = inner.begin(); it != inner.end(); ++it)
                m[it.key()] = it.value();
        }
        out.append(m);
    }
    return out;
}

bool ConfigStore::exportTo(const QString& jsonPath) {
    QSqlDatabase d = db();
    if (!d.isOpen()) return false;
    QSqlQuery q(d);
    if (!q.exec("SELECT type, key, value, created_at, updated_at FROM config_items")) return false;
    QJsonArray arr;
    while (q.next()) {
        QJsonObject o;
        o["type"] = q.value(0).toString();
        o["key"] = q.value(1).toString();
        o["value"] = QString::fromUtf8(QJsonDocument::fromJson(
                                q.value(2).toString().toUtf8()).toJson(QJsonDocument::Compact));
        o["created_at"] = q.value(3).toLongLong();
        o["updated_at"] = q.value(4).toLongLong();
        arr.append(o);
    }
    QFile f(jsonPath);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    return true;
}

bool ConfigStore::importFrom(const QString& jsonPath) {
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) return false;
    QSqlDatabase d = db();
    if (!d.isOpen()) return false;
    QSqlDatabase::database("config_store").transaction();
    for (auto v : doc.array()) {
        QJsonObject o = v.toObject();
        QSqlQuery q(d);
        q.prepare("INSERT OR REPLACE INTO config_items"
                  "(type, key, value, created_at, updated_at) "
                  "VALUES(:t, :k, :v, :c, :u)");
        q.bindValue(":t", o["type"].toString());
        q.bindValue(":k", o["key"].toString());
        q.bindValue(":v", o["value"].toString());
        q.bindValue(":c", o["created_at"].toVariant().toLongLong());
        q.bindValue(":u", o["updated_at"].toVariant().toLongLong());
        if (!q.exec()) {
            QSqlDatabase::database("config_store").rollback();
            return false;
        }
    }
    QSqlDatabase::database("config_store").commit();
    return true;
}
```

- [ ] **Step 6: 添加 tst_config_store 到 tests/CMakeLists.txt**

```cmake
# ConfigStore 单元测试
add_executable(tst_config_store config/tst_config_store.cpp
               ${CMAKE_SOURCE_DIR}/src/config/ConfigStore.cpp)
target_include_directories(tst_config_store PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_config_store PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
add_test(NAME tst_config_store COMMAND tst_config_store)
```

- [ ] **Step 7: 编译 + 运行所有测试**

```bash
cmake -B build -S . -DCMAKE_PREFIX_PATH="c:/Qt/6.11.1/msvc2022_64" >/dev/null 2>&1
cmake --build . --config Release --target tst_config_store tst_dpapi_crypto 2>&1 | tail -5
ctest -C Release -R "tst_config_store|tst_dpapi_crypto" --output-on-failure
```
Expected: 两组测试都 PASS

- [ ] **Step 8: Commit**

```bash
git add src/config/ tests/config/ tests/CMakeLists.txt
git commit -m "feat(config): ConfigStore 单例 + SQLite + JSON 导入导出 + 单元测试"
```

---

## Task 3: SettingsDialog 设置面板

**Files:**
- Create: `src/config/SettingsDialog.h`
- Create: `src/config/SettingsDialog.cpp`
- Modify: `src/ui/DeviceBusWidget.cpp` 不变（Task 4 接入）
- Modify: `DeployMaster.cpp` 加入口按钮

**Interfaces:**
- 入口按钮：`DeployMaster` 顶部工具栏加一个 QAction `设置`，点击打开 `SettingsDialog`
- SettingsDialog 列出所有配置项 + 编辑/删除/导入/导出

- [ ] **Step 1: 写 SettingsDialog.h**

```cpp
// src/config/SettingsDialog.h
#pragma once
#include <QDialog>
class QTableWidget;
class QPushButton;
class QComboBox;
class QLineEdit;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private slots:
    void onRefresh();
    void onFilterChanged(const QString& text);
    void onEditClicked();
    void onDeleteClicked();
    void onExportClicked();
    void onImportClicked();
    void onClearAllClicked();

private:
    void populateTable();
    QTableWidget* m_table = nullptr;
    QComboBox*    m_typeFilter = nullptr;
    QLineEdit*    m_searchEdit = nullptr;
};
```

- [ ] **Step 2: 实现 SettingsDialog.cpp**

```cpp
// src/config/SettingsDialog.cpp
#include "SettingsDialog.h"
#include "ConfigStore.h"
#include "DpapiCrypto.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("配置管理");
    resize(720, 480);

    auto* topLayout = new QHBoxLayout;
    m_typeFilter = new QComboBox(this);
    m_typeFilter->addItem("所有类型");
    connect(m_typeFilter, &QComboBox::currentTextChanged,
            this, &SettingsDialog::onFilterChanged);
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("搜索...");
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &SettingsDialog::onFilterChanged);

    auto* refreshBtn = new QPushButton("刷新", this);
    connect(refreshBtn, &QPushButton::clicked,
            this, &SettingsDialog::onRefresh);
    topLayout->addWidget(m_typeFilter);
    topLayout->addWidget(m_searchEdit, 1);
    topLayout->addWidget(refreshBtn);

    m_table = new QTableWidget(0, 5, this);
    m_table->setHorizontalHeaderLabels({"类型", "键", "更新时间", "值预览", "操作"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table->horizontalHeader()->resizeSection(0, 100);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_table->horizontalHeader()->resizeSection(1, 200);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_table->horizontalHeader()->resizeSection(2, 140);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_table->horizontalHeader()->resizeSection(4, 120);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);

    auto* bottomLayout = new QHBoxLayout;
    auto* editBtn = new QPushButton("编辑", this);
    auto* delBtn = new QPushButton("删除", this);
    auto* expBtn = new QPushButton("导出...", this);
    auto* impBtn = new QPushButton("导入...", this);
    auto* clrBtn = new QPushButton("清空所有", this);
    auto* closeBtn = new QPushButton("关闭", this);
    connect(editBtn, &QPushButton::clicked, this, &SettingsDialog::onEditClicked);
    connect(delBtn, &QPushButton::clicked, this, &SettingsDialog::onDeleteClicked);
    connect(expBtn, &QPushButton::clicked, this, &SettingsDialog::onExportClicked);
    connect(impBtn, &QPushButton::clicked, this, &SettingsDialog::onImportClicked);
    connect(clrBtn, &QPushButton::clicked, this, &SettingsDialog::onClearAllClicked);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    bottomLayout->addWidget(editBtn);
    bottomLayout->addWidget(delBtn);
    bottomLayout->addStretch();
    bottomLayout->addWidget(expBtn);
    bottomLayout->addWidget(impBtn);
    bottomLayout->addWidget(clrBtn);
    bottomLayout->addWidget(closeBtn);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_table, 1);
    mainLayout->addLayout(bottomLayout);

    populateTable();
}

static QString fmtTs(qint64 sec) {
    return QDateTime::fromSecsSinceEpoch(sec).toString("yyyy-MM-dd hh:mm");
}

void SettingsDialog::populateTable() {
    m_table->setRowCount(0);
    QString typeFilter = m_typeFilter->currentText();
    QString search = m_searchEdit->text();
    // 简单实现：列所有 type，分别 list
    QSet<QString> types;
    // 不再查所有 type；改为按当前 filter 全量 list（取所有 type 一次性）
    QList<QVariantMap> all;
    if (typeFilter == "所有类型") {
        for (const auto& t : {"device.list", "opcua.endpoint",
                               "ftp.credential", "telnet.credential",
                               "ssh.credential", "modbus.slave",
                               "netrelay.server", "websocket.endpoint"}) {
            all.append(ConfigStore::instance().list(t, 1000));
        }
    } else {
        all = ConfigStore::instance().list(typeFilter, 1000);
    }
    for (const auto& item : all) {
        if (!search.isEmpty() &&
            !item["key"].toString().contains(search, Qt::CaseInsensitive)) continue;
        int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(item["type"].toString()));
        m_table->setItem(row, 1, new QTableWidgetItem(item["key"].toString()));
        m_table->setItem(row, 2, new QTableWidgetItem(fmtTs(item["updated_at"].toLongLong())));
        QString preview = "{...}";
        QStringList keys;
        for (auto it = item.begin(); it != item.end(); ++it) {
            if (it.key() == "type" || it.key() == "key" ||
                it.key() == "updated_at") continue;
            keys << it.key() + "=" + it.value().toString().left(20);
        }
        preview = keys.join(", ");
        m_table->setItem(row, 3, new QTableWidgetItem(preview));
        auto* ops = new QWidget(this);
        auto* opsL = new QHBoxLayout(ops);
        opsL->setContentsMargins(0, 0, 0, 0);
        opsL->addStretch();
        m_table->setCellWidget(row, 4, ops);
    }
}

void SettingsDialog::onRefresh() { populateTable(); }
void SettingsDialog::onFilterChanged(const QString&) { populateTable(); }

void SettingsDialog::onEditClicked() {
    int row = m_table->currentRow();
    if (row < 0) return;
    QString type = m_table->item(row, 0)->text();
    QString key = m_table->item(row, 1)->text();
    auto v = ConfigStore::instance().load(type, key);
    if (v.isEmpty()) return;
    // 简化：弹 QMessageBox 显示 JSON
    QString preview = QString::fromUtf8(QJsonDocument(
        QJsonObject::fromVariantMap(v)).toJson(QJsonDocument::Indented));
    QMessageBox::information(this, "编辑配置",
        "类型: " + type + "\n键: " + key + "\n\n" + preview);
}

void SettingsDialog::onDeleteClicked() {
    int row = m_table->currentRow();
    if (row < 0) return;
    QString type = m_table->item(row, 0)->text();
    QString key = m_table->item(row, 1)->text();
    if (QMessageBox::question(this, "确认删除",
            "删除 " + type + " / " + key + " ?") != QMessageBox::Yes) return;
    ConfigStore::instance().remove(type, key);
    populateTable();
}

void SettingsDialog::onExportClicked() {
    QString path = QFileDialog::getSaveFileName(this, "导出配置",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        "JSON (*.json)");
    if (path.isEmpty()) return;
    if (ConfigStore::instance().exportTo(path))
        QMessageBox::information(this, "导出", "已导出到\n" + path);
    else
        QMessageBox::warning(this, "导出失败", "写入失败");
}

void SettingsDialog::onImportClicked() {
    QString path = QFileDialog::getOpenFileName(this, "导入配置",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        "JSON (*.json)");
    if (path.isEmpty()) return;
    if (ConfigStore::instance().importFrom(path))
        QMessageBox::information(this, "导入", "已导入");
    else
        QMessageBox::warning(this, "导入失败", "解析失败");
}

void SettingsDialog::onClearAllClicked() {
    if (QMessageBox::question(this, "确认清空",
            "清空所有配置？此操作不可撤销。") != QMessageBox::Yes) return;
    for (const auto& t : {"device.list", "opcua.endpoint",
                           "ftp.credential", "telnet.credential",
                           "ssh.credential", "modbus.slave",
                           "netrelay.server", "websocket.endpoint"}) {
        for (const auto& item : ConfigStore::instance().list(t, 10000))
            ConfigStore::instance().remove(t, item["key"].toString());
    }
    populateTable();
}
```

- [ ] **Step 3: 在 DeployMaster.cpp 加入口按钮**

读 `D:/work/project/DeviceForge/DeployMaster.cpp` 找主窗口构造逻辑（约第 100-150 行）。在 `setupOpcUaClientTool()` 之类调用前，加：
```cpp
#include "config/SettingsDialog.h"
// ...
auto* settingsAct = new QAction("设置", this);
settingsAct->setShortcut(QKeySequence("Ctrl+,"));
connect(settingsAct, &QAction::triggered, this, [](){
    SettingsDialog dlg;
    dlg.exec();
});
menuBar()->actions().first()->menu()->addAction(settingsAct);  // 或 toolbar
```

具体位置根据 `DeployMaster.cpp` 实际菜单结构定。如果菜单已经有"工具"/"Help"菜单，加到那里；否则加到主菜单末尾。**用 Edit 工具前先 Read DeployMaster.cpp 确认结构**。

- [ ] **Step 4: 编译 + 手动运行验证入口**

```bash
cmake --build . --config Release --target DeviceForge 2>&1 | tail -5
./Release/DeviceForge.exe
# 手动点设置菜单，确认 SettingsDialog 弹出
```

- [ ] **Step 5: Commit**

```bash
git add src/config/ src/ui/ DeployMaster.cpp
git commit -m "feat(config): SettingsDialog 设置面板 + DeployMaster 入口"
```

---

## Task 4: DeviceBusWidget 设备记录集成

**Files:**
- Modify: `src/ui/DeviceBusWidget.cpp`
- Modify: `src/ui/DeviceBusWidget.h`（加 ConfigStore 调用）
- Modify: `src/main.cpp` 或 `DeployMaster.cpp`（启动时 `ConfigStore::open()`）

- [ ] **Step 1: 在 main.cpp / DeployMaster.cpp 启动时 open ConfigStore**

读现有 `main.cpp`（`D:/work/project/DeviceForge/main.cpp`），在 `LogBridge::install()` 后、`DeployMaster` 构造前加：
```cpp
#include "config/ConfigStore.h"
ConfigStore::instance().open();
```

并在 `main()` return 前加：
```cpp
ConfigStore::instance().close();
```

- [ ] **Step 2: 在 DeviceBusWidget 添加设备后调 ConfigStore::save**

读 `D:/work/project/DeviceForge/src/ui/DeviceBusWidget.cpp`，找到"添加设备"的 slot 或回调（搜 `m_devices` / `appendDevice` / `addDevice`）。在添加成功后（设备加入 list 后），加：
```cpp
#include "config/ConfigStore.h"
// 在 addDevice() 末尾
QVariantMap dev{
    {"ip", device.ip},
    {"port", device.port},
    {"displayName", device.name},
    {"note", device.note}
};
ConfigStore::instance().save(
    "device.list",
    QString("%1:%2").arg(device.ip).arg(device.port),
    dev);
```

如果现有 DeviceInfo 结构没有 `note` 字段，添加并保留空字符串默认值。

- [ ] **Step 3: 启动时加载历史到下拉（如果当前没有下拉，先加一个 QComboBox）**

在 DeviceBusWidget 构造函数末尾加：
```cpp
auto historyCombo = new QComboBox(this);
historyCombo->setEditable(true);
auto items = ConfigStore::instance().list("device.list", 20);
for (const auto& it : items)
    historyCombo->addItem(it["ip"].toString() + ":" +
                            QString::number(it["port"].toInt()),
                         it);
historyCombo->setInsertPolicy(QComboBox::NoInsert);
```
下拉内容填入 IP:port，关联到 IP 输入框。**如果当前 IP 输入布局已有下拉样式，跳过本步，只把 list 接到现有下拉**。

- [ ] **Step 4: 编译验证**

```bash
cmake --build . --config Release --target DeviceForge 2>&1 | tail -3
```

- [ ] **Step 5: 手动端到端验证**

```bash
./Release/DeviceForge.exe
# 1. 加一台设备（IP=10.0.0.5, port=22）
# 2. 关闭应用
# 3. 重开 → 在设备总线应该看到该设备（如果 UI 列出来自当前内存就行，但至少 ConfigStore::list 应有数据）
# 4. 打开设置 → device.list 分类下应能看到 10.0.0.5:22 一行
```

- [ ] **Step 6: Commit**

```bash
git add src/ui/ src/main.cpp src/config/CMakeLists.txt
git commit -m "feat(config): DeviceBusWidget 设备记录持久化 + 启动加载"
```

---

## Task 5: OpcUaClientWidget endpoint 历史下拉

**Files:**
- Modify: `src/tools/OpcUaClientTool/OpcUaClientWidget.cpp`
- Modify: `src/tools/OpcUaClientTool/OpcUaClientWidget.h`

- [ ] **Step 1: 把 m_endpointEdit 改成 QComboBox（editable）**

读 OpcUaClientWidget.cpp 当前 `m_endpointEdit = new QLineEdit(...)`，改为：
```cpp
m_endpointEdit = new QComboBox(this);
m_endpointEdit->setEditable(true);
m_endpointEdit->setMinimumWidth(280);
m_endpointEdit->lineEdit()->setPlaceholderText("opc.tcp://192.168.1.10:4840");
```

同步修改 `OpcUaClientWidget.h`：`QLineEdit* m_endpointEdit` → `QComboBox* m_endpointEdit`。

- [ ] **Step 2: 启动时加载历史 + 连接成功后保存**

构造函数末尾（在 setupUi 完成后）：
```cpp
#include "config/ConfigStore.h"
for (const auto& it : ConfigStore::instance().list("opcua.endpoint", 20)) {
    QString url = it["url"].toString();
    if (m_endpointEdit->findText(url) < 0)
        m_endpointEdit->addItem(url);
}
```

`onConnectClicked()` 中，连接成功后保存：
```cpp
if (m_connected) {
    m_backend->disconnectFromServer();
    return;
}
// 已有连接逻辑
const QString endpoint = m_endpointEdit->currentText().trimmed();
// ... 现有 endpoint 校验 ...
QVariantMap v{{"url", endpoint}, {"updated_at", QDateTime::currentSecsSinceEpoch()}};
ConfigStore::instance().save("opcua.endpoint", endpoint, v);
// ... m_backend->connectToServer(endpoint) ...
```

- [ ] **Step 3: 编译 + 端到端验证**

```bash
cmake --build . --config Release --target DeviceForge 2>&1 | tail -3
./Release/DeviceForge.exe
# 1. 连一次 10.13.104.225:4840（成功或失败都保存）
# 2. 重开 → endpoint 下拉应该自动出现该 URL
# 3. 打开设置 → opcua.endpoint 分类能看到
```

- [ ] **Step 4: Commit**

```bash
git add src/tools/OpcUaClientTool/
git commit -m "feat(config): OpcUaClientWidget endpoint 历史下拉"
```

---

## Task 6: FtpDeployWidget 密码字段 DPAPI 端到端

**Files:**
- Modify: `src/tools/FtpDeployTool/FtpDeployWidget.cpp`
- Modify: `src/tools/FtpDeployTool/FtpDeployWidget.h`

- [ ] **Step 1: 找到密码输入框和"连接"按钮的处理函数**

读 `D:/work/project/DeviceForge/src/tools/FtpDeployTool/FtpDeployWidget.cpp`，定位：
- `m_passwordEdit` 声明（`.h` 和 `.cpp`）
- 连接/登录 slot（搜 `m_backend->connect` 或 `onConnect`）

- [ ] **Step 2: 连接成功后保存 host+user+密码（DPAPI 加密）**

在连接成功的回调里加：
```cpp
#include "config/ConfigStore.h"
#include "config/DpapiCrypto.h"
QVariantMap cred{
    {"host", m_hostEdit->text()},
    {"port", m_portEdit->text().toInt()},
    {"username", m_userEdit->text()},
    {"password", DpapiCrypto::protect(m_passwordEdit->text())},  // base64
    {"updated_at", QDateTime::currentSecsSinceEpoch()}
};
ConfigStore::instance().save(
    "ftp.credential",
    m_userEdit->text() + "@" + m_hostEdit->text() + ":" + m_portEdit->text(),
    cred);
```

- [ ] **Step 3: 启动时加载最近一个 credential 到 UI**

构造函数末尾加：
```cpp
auto creds = ConfigStore::instance().list("ftp.credential", 1);
if (!creds.isEmpty()) {
    m_hostEdit->setText(creds.first()["host"].toString());
    m_portEdit->setText(QString::number(creds.first()["port"].toInt()));
    m_userEdit->setText(creds.first()["username"].toString());
    QString cipher = creds.first()["password"].toString();
    QString plain = DpapiCrypto::unprotect(cipher);
    m_passwordEdit->setText(plain);
}
```

- [ ] **Step 4: 编译 + 端到端 + DB 验证**

```bash
cmake --build . --config Release --target DeviceForge 2>&1 | tail -3
./Release/DeviceForge.exe
# 1. 输入 host=10.0.0.1, user=admin, pwd=secret123, 登录
# 2. 用 SQLite 工具打开 %APPDATA%/DeviceForge/config.db：
#    SELECT value FROM config_items WHERE type='ftp.credential';
#    → password 字段应是 base64 字符串而非明文
# 3. 重开 → 密码应自动填充（DPAPI 当前用户作用域可解密）
# 4. 退出后用其他用户登录 Windows，再打开该 db → 密码字段无法解密（base64 但 unprotect 返回原值）
```

- [ ] **Step 5: Commit**

```bash
git add src/tools/FtpDeployTool/
git commit -m "feat(config): FtpDeployWidget 密码字段 DPAPI 加密持久化"
```

---

## Task 7: 其他 Tool 集成 + 文档同步

**Files:**
- Modify: `src/tools/TelnetTool/TelnetWidget.cpp`（保存 host+user）
- Modify: `src/tools/WebSocketTool/WebSocketWidget.cpp`（保存 endpoint）
- Modify: `src/tools/ModbusTool/ModbusWidget.cpp`（保存从站配置）
- Modify: `src/tools/NetRelayTool/NetRelayWidget.cpp`（保存监听参数）
- Modify: `docs/architecture.md`（添加 ConfigStore 章节）
- Modify: `CHANGELOG.md`（v2.3 条目）
- Modify: `docs/ROADMAP.md`（标记 v2.3 完成项）

- [ ] **Step 1: TelnetTool 集成**

仿照 Task 6 步骤，TelnetWidget.cpp 在连接成功后保存：
```cpp
ConfigStore::instance().save("telnet.credential",
    user + "@" + host + ":23",
    QVariantMap{{"host", host}, {"port", 23}, {"username", user}});
```

SSH Tool 类似（key 用 `ssh.credential`，type 可加 `keyPath` 字段）。

- [ ] **Step 2: WebSocket / Modbus / NetRelay 集成**

每个 Tool 保存自己专属 type（`websocket.endpoint` / `modbus.slave` / `netrelay.server`），保存最少必要字段。**不引入密码/DPAPI**，这些 Tool 不涉及凭证。

- [ ] **Step 3: 文档同步**

`docs/architecture.md` 加新章节：
```markdown
## ConfigStore — 本地配置持久化

位于 `src/config/ConfigStore.h`。SQLite 单表存储所有 Tool 的输入配置：

- 数据库：`%APPDATA%/DeviceForge/config.db`
- 表 schema：`config_items(type, key, value JSON, created_at, updated_at)` 唯一 `(type, key)`
- API：`ConfigStore::instance().save/load/list/remove/exportTo/importFrom`
- 加密：`DpapiCrypto::protect` (Windows DPAPI base64)，非 Windows 平台 stub
- 设置面板：`src/config/SettingsDialog.cpp`，主菜单 → 设置
```

`CHANGELOG.md` 加 `## [2.3.0]` 条目：
- 新增 ConfigStore 本地配置保存（SQLite + JSON）
- 新增 SettingsDialog 设置面板（查看/编辑/删除/导入/导出）
- 集成：设备总线 + OPC UA + FTP（密码字段 DPAPI 加密）+ 其他 Tool 历史下拉
- 测试：`tst_config_store`（读写/唯一键/排序/删除/导入导出）+ `tst_dpapi_crypto`（加解密往返）

`docs/ROADMAP.md` 把"OPC UA 安全策略扩展"那部分改成 v2.3 完成项。

- [ ] **Step 4: 全量测试 + 全量构建**

```bash
cmake --build . --config Release --target DeviceForge 2>&1 | tail -3
ctest -C Release --output-on-failure
```
Expected: 全部 PASS（tst_nrec / tst_opcua_encode / tst_opcua_loopback / tst_config_store / tst_dpapi_crypto）

- [ ] **Step 5: Commit + push**

```bash
git add src/tools/ docs/ CHANGELOG.md docs/ROADMAP.md
git commit -m "feat(config): 其他 Tool 集成 + v2.3 文档同步

- Telnet/SSH/WebSocket/Modbus/NetRelay 集成 ConfigStore 历史下拉
- architecture.md 增加 ConfigStore 章节
- CHANGELOG.md v2.3 条目
- ROADMAP.md 标记完成项"
git push origin main
```

---

## 自检对照

- [x] G1 设备记录持久化 → Task 4
- [x] G2 OPC UA 连接记录持久化 → Task 5
- [x] G3 用户名密码 DPAPI 加密 → Task 6
- [x] G4 所有 Tool 输入面板隐式保存 → Task 4/5/6/7
- [x] G5 设置面板查看/编辑/删除 → Task 3
- [x] G6 JSON 导入导出 → Task 2（`exportTo/importFrom`） + Task 3（UI 按钮）
- [x] `tst_config_store` 覆盖读写/唯一键/排序/删除/导入导出 → Task 2
- [x] `tst_dpapi_crypto` 覆盖加解密往返 → Task 1
- [x] 密码字段非 Windows stub + warning → Task 1
- [x] 应用启动时 `ConfigStore::open()` → Task 4

---

**计划结束。** 请选择执行方式：
1. **Subagent-Driven** — 每任务新开子代理 + 阶段评审（推荐）
2. **Inline Execution** — 在当前会话批量执行 + 检查点