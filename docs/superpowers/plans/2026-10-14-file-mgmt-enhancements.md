# 文件管理增强（元数据列/目录对比/F4 编辑回传）实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 FTP/SFTP 部署 Tab 双栏面板落地三项能力：所有者/权限/修改时间元数据列、元数据口径的目录对比（着色+行对齐+过滤+汇总）、远程文件 F4 预览编辑与异步回传。

**Architecture:** 元数据沿现有链路补齐（LIST 解析/SFTP attrs → FtpFileInfo → RemoteFileModel）；新增同步的内存读写原语（libcurl WRITEFUNCTION/READFUNCTION、libssh2 sftp 句柄），异步语义沿用 FileBrowserPanel 既有 QtConcurrent+代际令牌通道（与 list() 同模式，GUI 线程零阻塞为硬约束）；目录对比与编码探测抽成纯函数独立单测目标。

**Tech Stack:** Qt 6.11.1（CI 6.9.2，禁新版 API）/ C++17 / libcurl / libssh2 / QtTest+CTest / CMake VS2022 x64 / Windows-only（GBK 解码用 Win32 `MultiByteToWideChar`，CP_ACP 用 `GetACP`，项目未链 Qt6::Core5Compat，**不得使用 QTextCodec**）。

**Spec:** `docs/superpowers/specs/2026-10-14-file-mgmt-enhancements-design.md`（UI 稿 `docs/superpowers/specs/mockups/2026-10-14-file-mgmt-ui.html`）

**工作位置：** worktree `.worktrees/file-mgmt`，分支 `feature/file-mgmt-enhancements`（规格提交 2f54bde）。构建：`cmake --build build_fm --config Release`（首次需 `cmake -S . -B build_fm -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=D:/Qt/6.11.1/msvc2022_64`）；测试 `cd build_fm && ctest -C Release --output-on-failure`；GUI 类测试无 stdout 时用 `-o out.txt,txt`。

## 实施勘误（相对规格，已裁决）

- **R1 异步契约就实**：规格 §2.3 把 `readFile/writeFile` 写成回调式虚接口。代码现状是：`IFileSource` 全部为**同步原语**，异步与代际令牌守卫统一在 `FileBrowserPanel` 的 QtConcurrent 通道（`m_loadGeneration` 惯例，spec 亦要求沿用）。故接口定为同步：`bool readFile(path, std::string& out, uint64_t maxBytes)` / `bool writeFile(path, const std::string&)`；**"GUI 线程零阻塞"约束由面板/宿主侧 QtConcurrent 包装保证**（任务 4 的编辑器流程实现），语义不变、通道归一。
- **R2 本地 owner 口径**：`std::filesystem` 无 owner API，改在 `LocalFileSource::list` 内用 `QFileInfo(path).owner()`（取不到留空 → 显示 `—`）。
- **R3 对比态行模型**：占位行不进 `FtpFileInfo` 语义，模型层引入行描述 `ModelRow{info,isPlaceholder,kind}`；`fileAt()` 对占位行返回带 `name="__gap__"` 哨兵的空条目且新增 `isPlaceholderAt(row)` 供面板双击/选中跳过。
- **R4 冲突检查退化**：mtime 缺失侧仅比 size；两侧皆不可比则跳过检查并在编辑器状态栏注明。
- **R5 汇总条简化**：规格 §3.3"点击分类数字切换过滤"降级为「仅显示差异」checkbox + 汇总 QLabel（数字仅作展示不可点）。五态过滤控件对现场收益有限、交互复杂度高，YAGNI；如现场反馈需要再升回。

## 全局约束

- TDD 红-绿强制：每个任务先写失败测试再实现；每个任务尾步提交。
- 全中文注释与文档；日志脱敏：编辑保存只记路径+字节数。
- SylixOS FTP 三条勿回退：内存传输的句柄配置照抄 `uploadFile/downloadFile`（PASV=0、MULTICWD、LOW_SPEED 闲置限时无总 TIMEOUT、FTPS 分支）。
- 琴色 #F0A030（亮色 #D48820）仅信号态；行色经 `Qt::BackgroundRole` brush，不入 QSS。
- Tool 设置持久化 ConfigStore；本特性暂无新增持久化键（对比态是瞬态，不入库）。
- 测试目标数：43 → 45（+`tst_dir_compare`、`tst_file_encoding`），`tests/AGENTS.md` 与 AGENTS.md 计数行同一提交更新。
- ABI：`FtpFileInfo`/`IFileSource` 改动为追加字段/追加纯虚——本仓单体构建可接受追加纯虚（所有实现同仓），Mock 实现同提交补齐。

---

### 任务 1：元数据列贯通（owner 端到端 + ColOwner 列）

**Files:**
- Modify: `src/tools/FtpDeployTool/FtpFileInfo.h`（FtpFileInfo 增 owner）
- Modify: `src/tools/FtpDeployTool/FtpListParser.cpp:73-140`（Unix 行取 user token）
- Modify: `src/adapter/SshAdapter.cpp:303-348`（sftpListDirectory 填 uid 数字）
- Modify: `src/ui/LocalFileSource.cpp:47-83`（QFileInfo::owner）
- Modify: `src/tools/FtpDeployTool/RemoteFileModel.h/.cpp`（ColOwner）
- Modify: `src/ui/FileBrowserPanel.cpp`（applyFileList 设列宽/拉伸）
- Test: `tests/ftplist/tst_ftplist_parser.cpp`、`tests/remote_model/tst_remote_model.cpp`、`tests/file_source/tst_file_source.cpp` 扩展

**Interfaces:**
- Produces: `FtpFileInfo.owner : std::string`（空=不可得）；`RemoteFileModel::ColOwner`（枚举在 ColPermissions 之后、ColCount 之前）；表头"所有者"。
- Consumes: 既有 `FtpListParser::parse` / `sftpListDirectory` / `LocalFileSource::list` 签名不变。

- [ ] **Step 1（红）**：`tst_ftplist_parser.cpp` 追加：

```cpp
void unixLine_extractsOwner()
{
    auto files = FtpListParser::parse(
        "-rw-r--r-- 1 admin staff 1234 Jul 20 14:30 app.conf\n");
    QCOMPARE(files.size(), size_t(1));
    QCOMPARE(files[0].owner, std::string("admin"));
}
void windowsLine_ownerEmpty()
{
    auto files = FtpListParser::parse(
        "07/20/2026  02:30 PM         1,234,567 setup.exe\n");
    QCOMPARE(files.size(), size_t(1));
    QVERIFY(files[0].owner.empty());
}
```

`tst_remote_model.cpp` 追加：owner 显示 `"—"`（空 owner 时 DisplayRole）、非空时原样；表头 section==ColOwner 返回"所有者"；`ColCount==6`。`tst_file_source.cpp` 追加：本地临时目录建文件后 `list()` 条目 `owner` 与 `QFileInfo(...).owner()` 一致（该调用失败返回空串时断言 owner 可空但不得崩溃）。

- [ ] **Step 2**：跑三个测试确认新用例失败（编译失败也算红）：`ctest -C Release -R "tst_ftplist|tst_remote_model|tst_file_source"`。
- [ ] **Step 3（绿）**：实现——
  1. `FtpFileInfo.h`：`std::string owner; // Unix LIST user 列 / SFTP uid 十进制；空=不可得`。
  2. `FtpListParser::tryParseUnixLine` 在定出 dateIdx 后取 user：标准 `ls -l` 中 user 是 tokens[2]（dateIdx 前第 3 个字段）；为容忍 "user group" 中 group 含空格导致的漂移，取 `dateIdx>=5 ? tokens[dateIdx-4] : std::string()` 前先校验 `tokens[dateIdx-1]` 是纯数字 size（已有逻辑）——即 user=tokens[2]、group 跳过；Windows 分支不填。
  3. `SshAdapter::sftpListDirectory` 循环内：`if (attrs.flags & LIBSSH2_SFTP_ATTR_UIDGID) fi.owner = std::to_string(attrs.uid);`。
  4. `LocalFileSource::list`：`#include <QFileInfo>`；`const QFileInfo qi(pathQString); info.owner = qi.owner().toStdString();`（pathQString 循环内已有的 entry.path() 转 QString）。
  5. `RemoteFileModel`：枚举插 `ColOwner`；`data()` DisplayRole 增 `case ColOwner: return fi.owner.empty() ? QString(QChar(0x2014)) : QString::fromStdString(fi.owner);`（`—`=U+2014，与 size/permissions 空态统一；**同时**把 ColSize 目录、ColPermissions 空值也改为 `—` 显示——现状 permissions 空返回空串）；headerData 增"所有者"；ColOwner 右对齐不需要、左对齐默认。
  6. `FileBrowserPanel::applyFileList` 首次建模型处（`:402-407`）后设置列宽：

```cpp
auto* hh = m_table->horizontalHeader();
hh->setSectionResizeMode(RemoteFileModel::ColIcon, QHeaderView::Fixed);
hh->setSectionResizeMode(RemoteFileModel::ColName,  QHeaderView::Stretch);
hh->setSectionResizeMode(RemoteFileModel::ColSize,      QHeaderView::Fixed);
hh->setSectionResizeMode(RemoteFileModel::ColDateTime,  QHeaderView::Fixed);
hh->setSectionResizeMode(RemoteFileModel::ColPermissions, QHeaderView::Fixed);
hh->setSectionResizeMode(RemoteFileModel::ColOwner,    QHeaderView::Fixed);
m_table->setColumnWidth(RemoteFileModel::ColIcon, 28);
m_table->setColumnWidth(RemoteFileModel::ColSize, 76);
m_table->setColumnWidth(RemoteFileModel::ColDateTime, 130);  // "yyyy-MM-dd HH:mm" 全串
m_table->setColumnWidth(RemoteFileModel::ColPermissions, 84); // Consolas 9 字符
m_table->setColumnWidth(RemoteFileModel::ColOwner, 72);
```

  7. `RemoteFileModel::sort` 的 switch 增 `case ColOwner: less=a.owner<b.owner; eq=a.owner==b.owner; break;`。
- [ ] **Step 4**：三测试全绿；`cmake --build build_fm --config Release` 主程序编译通过（dynamic_cast 无接口变化）。
- [ ] **Step 5**：提交 `git add -A && git commit -m "feat(file-mgmt): 所有者列端到端（LIST user/SFTP uid/本地 owner）与列宽定型"`。

---

### 任务 2：内存读写原语（适配器 + IFileSource + 两实现）

**Files:**
- Modify: `src/adapter/FtpAdapter.h/.cpp`（readFileToMemory/writeFileFromMemory）
- Modify: `src/adapter/SshAdapter.h/.cpp`（sftpReadFileToMemory/sftpWriteFileFromMemory）
- Modify: `src/ui/IFileSource.h`（追加两纯虚同步原语，见 R1）
- Modify: `src/ui/LocalFileSource.h/.cpp`、`src/ui/RemoteFileSource.h/.cpp`
- Modify: `tests/panel_async/tst_panel_async.cpp`（Mock IFileSource 补实现）
- Test: `tests/file_source/tst_file_source.cpp` 扩展

**Interfaces:**
- Produces:

```cpp
// IFileSource 追加（同步原语；异步包装在面板层）
virtual bool readFile(const QString& path, std::string& out, uint64_t maxBytes) = 0;   // 超限/失败 false，lastError 置原因
virtual bool writeFile(const QString& path, const std::string& data) = 0;               // 覆盖写，失败置 lastError
// FtpAdapter 追加
bool readFileToMemory(const std::string& remotePath, std::string& out, uint64_t maxBytes);
bool writeFileFromMemory(const std::string& remotePath, const std::string& data);
// SshAdapter 追加
bool sftpReadFileToMemory(const std::string& remotePath, std::string& out, uint64_t maxBytes);
bool sftpWriteFileFromMemory(const std::string& remotePath, const std::string& data);
```

- Consumes: `Impl::buildUrl/userPwd/setupCommonOpts`（FtpAdapter.cpp:93-101,162）；任务 1 的 FtpFileInfo。

- [ ] **Step 1（红）**：`tst_file_source.cpp` 追加本地往返用例（LocalFileSource 即可测）：

```cpp
void readFile_roundtripAndLimit()
{
    const QString p = m_tmpDir.filePath("payload.bin");
    std::string body(4096, 'x');
    LocalFileSource src;
    QVERIFY(src.writeFile(p, body));
    std::string out;
    QVERIFY(src.readFile(p, out, 2 * 1024 * 1024));
    QCOMPARE(out.size(), size_t(4096));
    out.clear();
    QVERIFY(!src.readFile(p, out, 1024));          // 超限拒绝
    QVERIFY(!src.lastError().isEmpty());           // 且有原因
}
void writeFile_missingDir_reportsError()
{
    LocalFileSource src; std::string body("a");
    QVERIFY(!src.writeFile(m_tmpDir.filePath("no-such-dir/f.txt"), body));
    QVERIFY(!src.lastError().isEmpty());
}
```

- [ ] **Step 2**：`ctest -C Release -R tst_file_source` 确认编译失败/红。
- [ ] **Step 3（绿）**：
  1. `LocalFileSource`：readFile=`std::filesystem::file_size` 预检>maxBytes→`m_lastError="文件超过上限"`；`std::ifstream(binary)` slurp；writeFile=`std::ofstream(binary|trunc)`，error_code 置错。
  2. `FtpAdapter` 两原语：克隆 `downloadFile`（FtpAdapter.cpp:475+）的 URL/凭据/选项序列，改 `CURLOPT_WRITEFUNCTION` 为带 `maxBytes` 守卫的内存回调——新增 Impl 静态回调：

```cpp
struct MemSink { std::string* buf; uint64_t cap; bool overflow; };
static size_t writeBoundedCallback(void* ptr, size_t size, size_t nmemb, void* up) {
    auto* s = static_cast<MemSink*>(up);
    size_t total = size * nmemb;
    if (s->buf->size() + total > s->cap) { s->overflow = true; return 0; } // 返回 0 触发 CURLE_WRITE_ERROR 断流
    s->buf->append(static_cast<char*>(ptr), total);
    return total;
}
```

   write 侧：`struct MemSource { const std::string* data; size_t off; };` + `readFromMemoryCallback`（fread 语义：copy min(remain, size*nmemb)）；`CURLOPT_UPLOAD=1`、`CURLOPT_INFILESIZE_LARGE=data.size()`、`CURLOPT_FTP_CREATE_MISSING_DIRS` 与 uploadFile 一致（STOR 覆盖既有文件为默认语义，不改 NOBODY/APPEND）。两函数保持与既有传输同一闲置/进度配置（LOW_SPEED、XFERINFO、协议白名单、EPSV=0、FTPS SSL 分支），**不新增总 TIMEOUT**。成功后 `m_lastError.clear()`。
  3. `SshAdapter`：read=open(EX|READ)→循环 `libssh2_sftp_read` 追加，超 cap 关闭返回错；write=open(WRITE|CREAT|TRUNC, 0644)→循环 `libssh2_sftp_write`（处理 `LIBSSH2_ERROR_EAGAIN` 自旋等待，与既有 sftpUploadFile 的写循环一致）。
  4. `RemoteFileSource`：两方法按既有模式锁内分发 asFtp/asSsh，未连接/协议不支持置 lastError。
  5. `IFileSource.h` 注释块补两原语语义（超限/覆盖/lastError 约定）；`tests/panel_async/tst_panel_async.cpp` 的 MockIFileSource 补两实现（内存 map 存取），保持既有用例绿。
- [ ] **Step 4**：`ctest -C Release -R "tst_file_source|tst_panel_async"` 全绿；全量配置编译通过。
- [ ] **Step 5**：提交 `"feat(file-mgmt): 内存读写原语（libcurl/libssh2 无盘中转）与 IFileSource 扩展"`。

---

### 任务 3：目录对比（纯函数 + 模型对比态 + 宿主工具条）

**Files:**
- Create: `src/ui/DirCompare.h/.cpp`（纯逻辑）
- Modify: `src/tools/FtpDeployTool/RemoteFileModel.h/.cpp`（对比视图行模型）
- Modify: `src/ui/FileBrowserPanel.h/.cpp`（占位行守卫 + `fileList()` 访问器 + 对比态退出钩子信号）
- Modify: `src/tools/FtpDeployTool/FtpDeployWidget.h/.cpp`（跨双栏工具条/Ctrl+M/汇总条）
- Create: `tests/dir_compare/tst_dir_compare.cpp`；Modify: `tests/remote_model/tst_remote_model.cpp`、`tests/CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
// src/ui/DirCompare.h —— 全部自由函数，可独立单测
enum class CompareClass { Same, Diff, OnlyLeft, OnlyRight };
struct CompareEntry { CompareClass cls; int leftRow; int rightRow; };   // 行下标；无对面=-1
struct CompareSummary { int same=0, diff=0, onlyLeft=0, onlyRight=0; };
// 判定口径：同名文件 size 主判据；两侧 mtime 均可得时分钟前 16 字符相等作辅助；
// isDir 只判存在性。"." ".." 不参与配对。
CompareSummary compareDirectories(const std::vector<FtpFileInfo>& left,
                                  const std::vector<FtpFileInfo>& right,
                                  QVector<CompareEntry>* alignedOut = nullptr); // 左优先并集序
// 供两侧模型分别消费的展开表：diffOnly 过滤 + 占位行标记
struct AlignedRow { int sourceRow;   // -1=占位
                    CompareClass cls; };
QVector<AlignedRow> buildSideRows(const QVector<CompareEntry>& entries, bool leftSide,
                                  bool diffOnly);
bool mtimeMinuteEqual(const std::string& a, const std::string& b); // ISO 前 16 字符
```

  `RemoteFileModel` 追加：

```cpp
struct ModelRow { FtpFileInfo info; bool isPlaceholder = false; CompareClass cls; };
void setCompareView(const std::vector<FtpFileInfo>& source, const QVector<AlignedRow>& rows);
void exitCompareView();                 // 回到 setFileList 原样
bool isPlaceholderAt(int row) const;
Qt::ItemFlags flags(const QModelIndex&) const override;  // 占位行不可选不可点
```

  `FileBrowserPanel` 追加：`std::vector<FtpFileInfo> fileList() const;`、`void applyCompareRows(const QVector<AlignedRow>& rows);`、`signals: void filesChanged();`（applyFileList 尾 emit，宿主据此清对比态）、`bool editRequested(...)` 见任务 4。
- Consumes: 任务 1 的 `FtpFileInfo`（含 owner）。

- [ ] **Step 1（红）**：`tst_dir_compare.cpp`（QtTest，新目标）覆盖：

```cpp
void compare_sameWhenSizeEqual()      // size 等 + mtime 等 → Same
{ /* left={f("a",10,"2026-10-12 10:00:00")}, right 同 */ }
void compare_diffWhenSizeDiffers()    // size 不等 → Diff
void compare_diffWhenMtimeDiffersSameSize()  // size 等 mtime 分钟不同 → Diff
void compare_missingMtimeFallsBackToSize()   // 一侧 dateTime 空、size 等 → Same
void compare_dirsOnlyPresence()       // 同名目录两侧都有 → Same（不比 size/mtime）
void compare_dotsSkipped()            // ".." 不参与分类
void summaryCounts()                  // 2 same+1 diff+1 onlyLeft+1 onlyRight
void buildSideRows_gapInsertion()     // OnlyLeft 行：右侧 -1；OnlyRight 反之；顺序=左优先并集
void buildSideRows_diffOnlyShrink()   // diffOnly=true 时 Same 行（含对侧）全部消失
```

  `tst_remote_model.cpp` 追加：`setCompareView` 后 rowCount=rows.size()；占位行 `isPlaceholderAt` true、`flags()` 不含 `Qt::ItemIsSelectable|ItemIsEnabled`；对比态 BackgroundRole：Diff=#1F1A10（琴淡底深）、OnlyLeft/Right=#2A2214；`exitCompareView` 恢复旧绿/黄着色路径仍工作（回归保护）。
- [ ] **Step 2**：注册新测试目标并跑红。`tests/CMakeLists.txt` 仿 `tst_ftplist_parser` 块追加：

```cmake
add_executable(tst_dir_compare
    dir_compare/tst_dir_compare.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/DirCompare.cpp
)
target_include_directories(tst_dir_compare PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR})
target_link_libraries(tst_dir_compare PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_dir_compare COMMAND tst_dir_compare)
# ENVIRONMENT_MODIFICATION PATH 前置 Qt bin —— 照抄相邻目标最后一段
```

  同步给 `tst_remote_model` 的 add_executable 源列表加 `src/ui/DirCompare.cpp`；`tst_panel_async` 源列表若含模型也补上（以其现有文件清单为准，增量添加即可）。
- [ ] **Step 3（绿）**：实现 `DirCompare.cpp`（unordered_map<string,索引> 两侧配对；`mtimeMinuteEqual` 取 ISO 串前 16 字符；`isDir` 分支跳过 size/mtime）；`RemoteFileModel` 行模型与 flags/BackgroundRole（`m_compareMode` 标志位分流新旧着色；exitCompareView 保留原 m_files 快照）；`FileBrowserPanel::applyCompareRows` 组装 `ModelRow` 注入当前模型；双击/`selectedFiles()`/`enterSelectedDirectory`/`copySelectedTo` 全部跳过占位行（用 `isPlaceholderAt`）。
- [ ] **Step 4（宿主 UI）**：`FtpDeployWidget` splitter 上方插一行工具条：`QPushButton mbDirCompare`（checkable，文案「目录对比 Ctrl+M」）+ `QCheckBox「仅显示差异」`；下方 splitter 与批量区之间插汇总条 `QLabel`（"差异 n · 仅左 n · 仅右 n · 相同 n"，琴色数字）。逻辑：toggle 开启→取两面板 `fileList()` 调 `compareDirectories`→两侧 `applyCompareRows(buildSideRows(...))`→设 label；勾选变化重调 `buildSideRows(diffOnly)`；任一侧 `filesChanged` 或 `currentPathChanged`→退出对比（两模型 `exitCompareView`、按钮复位）。`QShortcut(QKeySequence("Ctrl+M"), this)` 绑 toggle。objectName 依惯例 `mb*` 前缀供 QSS/冒烟定位；**不新增 QSS 颜色**（行色在模型）。
- [ ] **Step 5**：`ctest -C Release -R "tst_dir_compare|tst_remote_model|tst_panel_async"` 全绿；全量编译。
- [ ] **Step 6**：提交 `"feat(file-mgmt): 目录对比（元数据口径纯函数+对齐行模型+宿主工具条）"`。

---

### 任务 4：F4 预览编辑器（编码探测纯函数 + 对话框 + 异步接线）

**Files:**
- Create: `src/ui/FileEncoding.h/.cpp`（纯逻辑，Win32 解码）
- Create: `src/ui/FileEditorDialog.h/.cpp`
- Modify: `src/ui/FileBrowserPanel.h/.cpp`（F4/双击/菜单入口 + 异步打开/保存/冲突 + 代际令牌）
- Modify: `src/tools/FtpDeployTool/FtpDeployWidget.cpp`（保存日志路由到全局日志）
- Create: `tests/file_encoding/tst_file_encoding.cpp`；Modify: `tests/panel_async/tst_panel_async.cpp`、`tests/CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
// src/ui/FileEncoding.h —— 全部自由函数
struct TextProbe { bool looksBinary = false; QString codec;      // "UTF-8"/"GBK"/"System"
                   bool fromBom = false; QString eol; };          // "LF"/"CRLF"/"mixed"
TextProbe probeTextBuffer(const QByteArray& raw);                 // BOM→严格UTF8→GBK试探→ASCII
bool utf8StrictValidate(const char* data, int len);               // 手写校验（禁 QTextCodec）
bool gbkDecodeable(const QByteArray& raw);                        // MultiByteToWideChar(936, MB_ERR_INVALID_CHARS)
QByteArray decodeToUtf16(const QByteArray& raw, const QString& codec); // 解码失败返回 false 语义由 out 是否为空+错误串回调表达
bool decodeText(const QByteArray& raw, const QString& codec, QString& outText, QString& outErr);
QByteArray encodeText(const QString& text, const QString& codec, const QString& eol); // 保行尾：先按 eol 归一
struct ConflictSnapshot { uint64_t size = 0; QString mtime; bool hasSize = false; };
bool remoteChanged(const ConflictSnapshot& atOpen, const FtpFileInfo& now); // R4 退化口径
constexpr uint64_t kMaxEditBytes = 2 * 1024 * 1024;
```

  `FileEditorDialog`（QDialog，非模态）：`void openFor(const QString& titlePath, const QString& codec, const QString& text, bool readOnly, const QString& readOnlyReason)`；信号 `saveRequested(const QByteArray& utf8BytesPlaceholder /*实际=encode 后字节*/, QString codec)` 由面板处理；接口按下面签名收敛：

```cpp
class FileEditorDialog : public QDialog {
public:
    explicit FileEditorDialog(QWidget* parent = nullptr);
    void bind(const QString& fileName, const QString& fullPath, const QString& text,
              const QStringList& codecs, const QString& codec, const QString& eolNote,
              bool readOnly, const QString& readOnlyReason);
    QString currentCodec() const;  QByteArray currentBytes(const QString& codec) const; // 保存时用（encodeText）
    bool isDirty() const;  void markSaved();
    void setBusy(bool);            // 上传中禁用按钮
signals:
    void saveRequested();
    void codecChangedRequest();    // 面板重新解码回灌 bind()
    void closeRequested(bool discard);
};
```

  `FileBrowserPanel` 追加：`void openEditorForSelectedFile();`；`signals: void fileSavedForLog(const QString& path, quint64 bytes);`
- Consumes: 任务 2 `IFileSource::readFile/writeFile`、任务 3 `fileList()`。

- [ ] **Step 1（红）**：`tst_file_encoding.cpp`：

```cpp
void probe_utf8Bom()-like 四例   // BOM 三种 + 无 BOM
void probe_strictUtf8AcceptReject() // "中文字节"合法；0xC3 0x28 非法
void probe_gbkFallback()            // GBK 串"中文"（0xD6 0xD0 0xCE 0xC4）UTF8 校验失败、gbkDecodeable true → codec=GBK
void probe_pureAscii()             // codec=UTF-8
void probe_binaryNul()             // 含 '\0' → looksBinary
void probe_controlRatio()          // 高比例控制字符 → looksBinary
void eol_detect_crlf_lf_mixed()
void encode_roundtrip_gbk_keeps_crlf()   // decode→encode 同 codec 字节回等，行尾不变
void remoteChanged_sizeDiffer_true()     /  bothUnknown_false()  /  mtimeDifferOnly_true()
```

  `tst_panel_async.cpp` 追加（Mock readFile/writeFile 已具备）：双击文件行触发编辑流程——断言 Mock `readFile` 被以 `kMaxEditBytes` 调用一次且过期代际（readFile 返回前 refresh()）不会开出对话框（用可观察计数代替真窗口：面板测试钩子 `editorOpenCount()`，实现里 ++ 于 bind 前）。
- [ ] **Step 2**：注册 `tst_file_encoding`（仿任务 3 块，链 Qt6::Core Qt6::Gui Qt6::Test；源含 `src/ui/FileEncoding.cpp`），跑红。
- [ ] **Step 3（绿-纯函数）**：实现 FileEncoding.cpp（Win32：`#include <windows.h>`，`MultiByteToWideChar(936/GetACP(), MB_ERR_INVALID_CHARS,...)`；UTF-16 BOM 判定 len>=2；严格 UTF-8 手写状态机；encode 反向 `WideCharToMultiByte`）。全部逻辑不依赖 QWidget。
- [ ] **Step 4（绿-对话框）**：FileEditorDialog：顶栏 `QLabel(fileName)+QLabel(path 弱化)+QComboBox(codecs)`；`QPlainTextEdit`（`setObjectName("fileEditorText")`，字体 Consolas 10pt，`setLineWrapMode(NoWrap)`）；只读态=文本 `setReadOnly(true)`+顶栏琴色边框 QLabel(reason)+保存禁用；底栏状态 `QLabel` + 「另存本地…」「关闭」「保存并上传」（objectName `feSave` 等供冒烟）。主题色沿用全局 QSS，无新增渐变。
- [ ] **Step 5（绿-面板接线）**：`FileBrowserPanel`：
  - 快捷键 `QShortcut(Qt::Key_F4, this)`（WidgetWithChildrenShortcut，与 F2/F5/F6 并排 :183-193）→ `openEditorForSelectedFile()`；`onTableDoubleClicked` 非目录分支 `return` 前改为进入编辑；右键菜单「复制路径」前插「预览/编辑」。
  - `openEditorForSelectedFile`：取当前行 `FtpFileInfo`+绝对路径 → 已知 size>kMaxEditBytes 直接开只读对话框（原因"文件过大"）；否则 `++m_editGeneration`，QtConcurrent::run 捕获 `{guard=QPointer, gen, source, path}`：worker 内 `source->readFile(path, bytes, kMaxEditBytes)`，结果经 `QMetaObject::invokeMethod(this, ...)` 回 GUI；gen 不符直接丢弃（**断连/切目录/重刷时 `m_editGeneration`++**：加在 `loadDirectory`、`setSource`、`showLoadError` 重连路径）。回 GUI 后：错误→QMessageBox 警告不弹窗编辑；成功→`probeTextBuffer`→二进制→只读对话框（原因"二进制文件不可编辑"）；文本→`decodeText`（失败回退 UTF-8 替换解码并提示切编码）→ `bind()` → show。快照 `ConflictSnapshot{size, dateTime}` 随对话框 `setProperty`/成员暂存。
  - 保存流：`saveRequested`→对话框 setBusy→worker：远程源且快照可比→`source->list(父目录)` 找同名（QtConcurrent 第二段）→`remoteChanged` → 变化则回 GUI 弹「远端文件已变化」覆盖/放弃并刷新（覆盖=继续 writeFile；刷新=重走 openEditor）→ `source->writeFile(path, currentBytes(codec))`；本地源直接 writeFile。成功：`markSaved`+`refresh()`+`emit fileSavedForLog(path, bytes)`（**日志文案只含路径+字节数**）；失败：恢复按钮+警告保留内容。关闭：`isDirty()` → QMessageBox 丢弃/返回继续。
  - 编码切换：`codecChangedRequest` → 用暂存原始字节 `decodeText` 重 bind（若 dirty 先确认丢弃）。
- [ ] **Step 6（宿主路由）**：`FtpDeployWidget` 连接两面板 `fileSavedForLog` → 既有全局日志回调（`setGlobalLogCallback` 通道）输出一行 `"[编辑] <path> 已保存（N 字节）"`。
- [ ] **Step 7**：`ctest -C Release -R "tst_file_encoding|tst_panel_async"` 全绿；全量编译。
- [ ] **Step 8**：提交 `"feat(file-mgmt): F4 预览编辑器（编码探测/异步打开/冲突确认/回传全链路）"`。

---

### 任务 5：集成验证、文档与收尾

**Files:**
- Modify: `docs/04-使用手册/*`（FTP/SFTP 部署节：新列说明、目录对比操作、F4 编辑流程与限制）
- Modify: `README.md` 功能模块表（文件传输行补"元数据列/目录对比/在线编辑"）
- Modify: `tests/AGENTS.md`（43→45 与两个新目标注册说明）、根 `AGENTS.md` 计数行
- Modify: `docs/superpowers/specs/2026-10-14-file-mgmt-enhancements-design.md`（追加 §11 实施勘误：R1-R4）

- [ ] **Step 1**：全量 `cmake --build build_fm --config Release` + `ctest -C Release --output-on-failure` 两轮全绿（tst_deploy_runner flake 基线允许复跑一次，记录复跑证据）。
- [ ] **Step 2**：`python tools/devtools/versioncheck.py` OK（版本不动，仍 2.11.1）。
- [ ] **Step 3**：`python tools/devtools/smoke.py` exit 0（若被用户实例占窗按既有流程处理，勿杀进程）。
- [ ] **Step 4**：人工验收清单（规格 §10 六项）执行并记录；不能执行的注明原因。
- [ ] **Step 5**：文档同步 + 勘误写入规格，`git add -A && git commit -m "docs(file-mgmt): 使用手册/README/测试清单同步与实施勘误"`。

## 审查重点（每任务复审与终审必查）

1. 异步通道：所有 QtConcurrent worker 回调是否都带 `QPointer`+代际令牌双守卫；重连/切目录/断连三处 `m_editGeneration` 是否都推进。
2. SylixOS 句柄配置一致性：readFileToMemory/writeFileFromMemory 与 uploadFile/downloadFile 选项逐项对照（PASV/LOW_SPEED/协议白名单/FTPS 分支）。
3. 对比态回归：非对比态旧绿/黄、拖拽、F5/F6、选中恢复逻辑不被占位行破坏（`isPlaceholderAt` 守卫全覆盖：双击/选中/复制移动/选中恢复）。
4. 编码链路无 QTextCodec；Win32 API 错误分支不崩；行尾保持断言在测试中真实存在。
5. 日志脱敏：保存事件只记路径+字节数。
6. 新增 45 测试目标注册 ENVIRONMENT_MODIFICATION 完整（Qt DLL 前置）。
