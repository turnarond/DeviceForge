# 文件管理增强设计（元数据列 / 目录对比 / F4 预览编辑）

**日期**：2026-10-14 ｜ **分支**：`feature/file-mgmt-enhancements` ｜ **版本线**：v2.12
**UI 稿**：`docs/superpowers/specs/mockups/2026-10-14-file-mgmt-ui.html`（浏览器直接打开）

## 1 · 背景与目标

FTP/SFTP 部署 Tab 的双栏文件面板（`FileBrowserPanel`，左本地右远程）目前只显示名称/大小/修改时间/权限四列，元数据在底层被白白丢弃（FTP LIST 的 user 列、SFTP 的 uid/atime）；本地与远程没有目录级对比；远程文件无法就地预览与编辑（双击文件无任何动作），现场改设备上的一行配置要"下载→本地改→上传"三步绕行。

目标（均落在现有双栏面板语境内，TC 式操作）：

1. **元数据列扩展**：新增所有者列；时间只保留修改时间一列（创建时间协议层给不了，砍掉）。
2. **目录对比**：元数据口径（size+修改时间）的左右差异分类、着色、行对齐、仅显示差异过滤与汇总条。
3. **预览/编辑回传**：F4/双击打开内置编辑器，文本编码自动探测+手动切换，保存异步回传原路径，带远端变化冲突确认。

**非目标**（本期明确不做，见 §9 待办）：内容级 diff 视图（用户明确不要）、hex 预览、外部编辑器关联、行号栏、同步向导、命名/批量权限修改。

## 2 · 数据层

### 2.1 `FtpFileInfo` 扩字段（src/tools/FtpDeployTool/FtpFileInfo.h）

- 新增 `QString owner`（空串=不可得）；其余字段不变。时间维持单一 `dateTime`（修改时间）。

### 2.2 各生产者口径

| 来源 | owner | permissions | dateTime |
|---|---|---|---|
| Unix 格式 FTP LIST | user 列（现在被丢弃的第 3 个 token） | 已有 | mtime，分钟粒度 |
| Windows 格式 FTP LIST | 空 → 显示 `—` | 空 → `—` | 已有 |
| SFTP readdir | **uid 十进制数字**（"0"=root；不做名字反查） | 已有 | mtime |
| LocalFileSource | `QFileInfo::owner()`（取不到留空） | 已有 formatPermissions | last_write_time |

单元格缺失统一显示弱化灰 `—`；**远程恒无创建时间**（SFTP v3 属性集无 birth time，FTP LIST 无），故不设该列。

### 2.3 `IFileSource` 新增异步读写接口

```cpp
// 回调在 GUI 线程投递（QueuedConnection 语义由实现方保证）；
// gen 由调用方以代际令牌模式守卫，过期回调必须被丢弃
virtual void readFile(const QString& path, std::function<void(QByteArray, QString /*err*/)> done, quint64 maxSize) = 0;
virtual void writeFile(const QString& path, const QByteArray& data, std::function<void(QString /*err*/)> done) = 0;
```

- **强制异步**（用户拍板）：GUI 线程零阻塞；协议 IO 在既有执行器线程上跑（与 `list()` 同通道），回调携带结果或错误串；面板侧以代际令牌丢弃过期回调（沿用 FtpDeployWidget `m_connGeneration` 模式），切目录/断连/关闭即失效。
- FTP：libcurl 内存回调（`WRITEFUNCTION`/`READFUNCTION` 对接 `QByteArray`，**不落临时文件**），句柄配置与现有下载完全一致（PASV、MULTICWD、QUOTE 绝对路径——SylixOS 三条勿回退）。`maxSize` 超限在写回调内中断传输返回错误。
- SFTP：`libssh2_sftp_open` + 分块读循环（超 2MB 即关）；写用 `OPEN(WRITE|CREAT|TRUNC)` 分块写。
- LocalFileSource：直读直写，同样以异步签名投递（`QTimer::singleShot(0)` 即返），保证面板两侧代码路径统一。

## 3 · 目录对比

### 3.1 触发与失效

- 工具条「目录对比」toggle（快捷键 **Ctrl+M**）。进入需两侧都已加载；未加载侧提示先连接/加载。
- **不自动常驻**。任一侧目录刷新（切目录、上传/删除/重命名后）→ 自动退出对比态并清色，避免旧快照误导。
- 对比只读：不改动任何一侧文件。

### 3.2 判定纯函数（新头 `src/ui/DirCompare.h`，可单测）

输入左右两份 `QVector<FtpFileInfo>`（目录项仅按名称配对，不递归子目录），输出每个名称的分类与两侧行序对齐表：

- `same`：**size 相等**且（两侧 mtime 都可得时）分钟精度时间相等。mtime 作辅助判据——Unix LIST 只有分钟粒度、上传未必保时间戳，故 **size 为主判据**；一侧无 mtime 时仅比 size。
- `diff`：同名且（size 不等，或 size 等但 mtime 不等）。
- `onlyLeft` / `onlyRight`：单侧存在。
- 目录（isDir）参与同名配对但只按"两侧都有=同、缺一侧=only"处理，不比时间。

### 3.3 呈现（RemoteFileModel + FileBrowserPanel）

- 两模型按对齐表重排行序：配对行并排；单侧独有行的**对面插入占位行**（更深底色 #080B10/#E2E6EC，不可选、显示"本地独有/远端独有"）。
- 行色经 `Qt::BackgroundRole`（QSS 无行级伪类；两套主题各一组常量）：`diff` 琴色淡底（深 #F0A030 α0.10 / 亮 #D48820 α0.10），`only*` 同族加深一档（α0.18/0.16）；`same` 不动。
- **「仅显示差异」**：对比态工具条 checkbox，勾上隐藏 `same` 行与多余占位行；与列排序共存（排序作用于对比结果集）。
- **汇总条**：底部"差异 n · 仅左 n · 仅右 n · 相同 n"，点击分类数字切换过滤。
- 既有"同名+同 size→绿/黄"自动着色：**对比态下被接管替换**；非对比态行为维持现状不变（回归面见 §8）。

## 4 · 预览/编辑（FileEditorDialog）

### 4.1 入口与窗口

- 远程/本地栏选中文件按 **F4 或双击**（目录双击仍是进入目录）；右键菜单增「预览/编辑」。
- 非模态大对话框（主窗 ~70%）：顶栏=文件名+完整路径+**编码下拉**；正文 `QPlainTextEdit`（Consolas）；底栏=状态文本+「另存本地…」「关闭」「保存并上传」（琴色主按钮）。

### 4.2 打开流程（异步）

1. 列表 size 已知且 >2MB → 直接**只读横幅**（"文件过大，建议下载后编辑"），不发读请求；未知或 ≤2MB → `readFile(maxSize=2MB)`，读侧超限硬断同样转只读。
2. 二进制判定（纯函数）：首 8KB 含 NUL，或控制字符（非换行/制表）占比 >30% → 只读横幅"二进制文件不可编辑"。
3. 编码探测（纯函数，见 §4.4）预选下拉；探测结果旁注置信来源；手动切换编码=按新解码重刷正文（保留用户未保存编辑的提示确认）。

### 4.3 保存回传与冲突确认

- 「保存并上传」→ 先按当前编码把正文 encode 回字节 → **冲突检查**：异步重拉目标目录列表，同名条目与打开时快照（size+mtime）比对；不一致 → 弹「远端文件已变化」确认框（**仍要覆盖 / 放弃并刷新**，"放弃并刷新"=丢弃本地编辑、拉最新内容重开）。快照字段不可得（如 Windows 格式 FTP 无 mtime）时退化为仅比 size，size 不可得则跳过检查并在状态栏注明"无法校验远端变化"。
- 一致 → `writeFile` 异步上传，按钮态"上传中…"；成功→全局日志一行（**只记路径+字节数，不记内容**，遵守脱敏纪律）+ 刷新该目录；失败→错误弹窗保留编辑内容。
- 脏守卫：有未保存修改时关闭对话框/切换目标目录 → 确认"丢弃修改？"。
- 本地栏文件同对话框：保存=本地直写（异步签名），无冲突弹（同进程内自改）。

### 4.4 编码探测纯函数（`src/ui/FileEncoding.h`，可单测）

优先级：BOM（UTF-8/UTF-16LE/BE）→ UTF-8 严格解码通过 → GBK 试探解码通过且高位字节成对合法 → 全 ASCII（等价 UTF-8）。均不过 → 默认 UTF-8 + Latin-1 兜底可手动选。下拉候选：UTF-8 / GBK / System（本地代码页）/ Latin-1。行尾显示 CRLF/LF 探测结果；保存保持原行尾（不擅自转换）。

## 5 · UI 纪律与色值

- 琴色只用于信号态：对比差异/独有行、主按钮、选中态、汇总可点数字。结构（表头/网格/占位行）一律石墨。
- 列布局：名称 stretch｜大小 64px 右对齐｜修改时间 104px（`MM-dd HH:mm`，跨年 `yyyy-MM-dd HH:mm`）｜权限 74px｜所有者 66px；数值/权限/所有者列 Consolas 等宽。
- 无 QSS 渐变/阴影；行色走模型 BackgroundRole brush，主题切换由 ThemeTokens 一处供色。

## 6 · 集成点

- `FtpDeployWidget`：工具条加 toggle/快捷键；双击/F4 路由；对比结果注入两模型；编辑器对话框生命周期挂代际令牌。
- `RemoteFileModel`：新列枚举 `ColOwner`（对比着色改由对比结果驱动）；本地侧 `LocalFileModel` 对称扩展。
- `RemoteFileSource`/`LocalFileSource`：实现 readFile/writeFile；`FtpAdapter`/`SshAdapter` 增内存传输原语（经既有适配器接口层，UI 不直连协议库）。

## 7 · 测试（TDD 红-绿）

新增目标（43→45，tests/AGENTS.md 同步注册）：

1. **tst_dir_compare**：分类口径（size 主判/mtime 辅助/缺失退化/isDir 规则）、对齐序列与占位行插入、仅差异过滤收缩、汇总计数。
2. **tst_file_encoding**：BOM 三式、UTF-8 严格过/不过、GBK 试探、全 ASCII、二进制启发（NUL/控制字符占比）、2MB 判定边界、encode 回写行尾保持。
3. 扩展 **tst_ftplist_parser**：Unix 行 user 提取、Windows 行 owner 空。
4. 扩展 **tst_file_source**：Local readFile/writeFile 往返（临时目录）、owner 字段填充。
5. 扩展 **tst_remote_model**：ColOwner 表头/显示（`—` 缺失态）、对比 BackgroundRole 输出、非对比态旧着色回归保护。
6. 扩展 **tst_panel_async**：Mock IFileSource 补 readFile/writeFile；断连/切目录后过期回调被代际令牌丢弃；双击文件→编辑信号（不弹真窗，验证路由）。

环回/集成：SFTP/FTP 真链路无法进 CI，编辑回传以 Mock 全链路（读→改→冲突检查→写→刷新信号次序）覆盖；冲突确认框弹出条件写成纯谓词单测。

## 8 · 回归面

- 非对比态列表行为、旧绿/黄自动着色、拖拽上传、删除/重命名/新建目录、面包屑导航不受影响（着色仅在对比态接管）。
- SylixOS FTP 三条（PASV/MULTICWD/QUOTE）：readFile/writeFile 复用同一句柄配置，测试中以配置断言保护。
- FtpDeploy 批量部署链路不共用面板回调通道，仅共享列模型——列枚举追加在尾部，旧列序不变。

## 9 · 明确延期（v2.13 待办池）

hex 预览、外部编辑器关联、行号栏、命名修改权限（chmod）、目录对比递归模式、同步向导（按对比结果一键复制差异文件）。

## 10 · 验收清单（人工）

1. 现场设备（Unix FTP + SylixOS）列表出现所有者/权限/时间，取不到的显示 `—`。
2. Ctrl+M 进入对比，差异行琴色淡底、对面占位行灰纹；勾选仅差异后 same 行消失；上传一个差异文件后对比态自动退出。
3. F4 打开 GBK 编码的配置文件，下拉显示预选 GBK，手动切 UTF-8 正文重解码不乱码；改一行保存→上传成功→列表时间刷新。
4. 编辑期间用另一台机器改远端同名文件→保存时弹冲突确认，选"放弃并刷新"看到最新内容。
5. 打开 2.1MB 日志→只读横幅；打开二进制→只读横幅；关闭有未保存修改→丢弃确认。
6. 断连瞬间编辑器打开中→不崩溃、旧回调丢弃、界面提示错误。

## 11 · 实施勘误（2026-10-14，随实现落定）

- **R1 异步契约就实**：§2.3 的回调式接口调整为**同步原语**（`IFileSource::readFile/writeFile`），异步与代际令牌守卫沿用 `FileBrowserPanel` 既有 QtConcurrent 通道（`m_editGeneration`），"GUI 线程零阻塞"约束不变、通道与 `list()` 归一。
- **R2 本地 owner 口径**：`std::filesystem` 无 owner API，`LocalFileSource::list` 改用 `QFileInfo::owner()`（取不到留空显示 `—`）。
- **R3 对比态行模型**：占位行不进 `FtpFileInfo` 语义，模型层 `ModelRow{info,placeholder,cls}` + `isPlaceholderAt()` 守卫（双击/选中/复制移动全部跳过）。
- **R4 冲突检查退化**：mtime 缺失侧仅比 size；完全不可比则跳过检查（编辑器状态栏注明快照）。
- **R5 汇总条简化**：§3.3 "点击分类数字切换过滤" 降级为「仅显示差异」checkbox + 汇总 QLabel（数字仅展示），YAGNI。

### 评审后修复波（终审 94de732..7ca84e0 之后，同规格口径的就实）

- **R6 混合行尾只读化**：§4.4 "行尾保持"对 CRLF/LF 单态文件字节级成立；**mixed 行尾**在编辑器统一 LF 模型下无法无损还原，实现改为按只读横幅打开（拒绝编辑优于擅自整改写行尾），保存路径不触碰 mixed 文件。
- **R7 编辑器打开即推进代际**：每次打开（含只读路径）`++m_editGeneration` 并作废旧在途读，回调携带 info/path/snap 值对回写——修复"慢链路连开两文件、旧回调串包致甲内容写进乙路径"的误覆盖通道（评审 C-1）。保存成功后快照续用写入大小（mtime 清空），同对话框二次保存不再假冲突（评审 I-1）。
- **R8 刷新即出对比态**：`RemoteFileModel::setFileList` 内部先重置对比视图行再排名称序——对比态中任一侧刷新（上传/删除/回传）自动回落到名称排序视图而非服务器乱序（评审 I-3）；同名"目录↔文件"类型变化判 `diff`（评审 M1）。
