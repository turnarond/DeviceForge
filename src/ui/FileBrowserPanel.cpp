#include "ui/FileBrowserPanel.h"
#include "ui/FileEditorDialog.h"
#include "ui/IFileSource.h"
#include "tools/FtpDeployTool/RemoteFileModel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QTableView>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QMimeData>
#include <QEvent>
#include <QShortcut>
#include <QInputDialog>
#include <QDateTime>
#include <QMenu>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>
#include <QPointer>
#include <QThread>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>

namespace {

QString joinedPath(QString base, const QString& name)
{
    if (base.isEmpty())
        return name;
    if (!base.endsWith(u'/'))
        base += u'/';
    return base + name;
}

QString normalizedTransferProtocol(QString protocol)
{
    protocol = protocol.trimmed().toLower();
    return protocol == QStringLiteral("ssh") ? QStringLiteral("sftp") : protocol;
}

QString transferStateText(TransferState state)
{
    switch (state) {
    case TransferState::Queued: return QStringLiteral("已排队");
    case TransferState::Preparing: return QStringLiteral("准备传输");
    case TransferState::Transferring: return QStringLiteral("传输中");
    case TransferState::Verifying: return QStringLiteral("校验中");
    case TransferState::Committing: return QStringLiteral("提交中");
    case TransferState::Reconnecting: return QStringLiteral("正在重连");
    case TransferState::RetryWaiting: return QStringLiteral("等待重试");
    case TransferState::NeedsAttention: return QStringLiteral("需要处理");
    case TransferState::Succeeded: return QStringLiteral("传输完成");
    case TransferState::PartiallySucceeded: return QStringLiteral("部分完成");
    case TransferState::Failed: return QStringLiteral("传输失败");
    case TransferState::Cancelling: return QStringLiteral("正在取消");
    case TransferState::Cancelled: return QStringLiteral("已取消");
    }
    return QStringLiteral("传输状态未知");
}

bool hasCommittedItem(const TransferTaskSnapshot& snapshot)
{
    return std::any_of(snapshot.itemResults.cbegin(), snapshot.itemResults.cend(),
                       hasDeliveredTarget);
}

bool hasSourceRemovedItem(const TransferTaskSnapshot& snapshot)
{
    return std::any_of(snapshot.itemResults.cbegin(), snapshot.itemResults.cend(),
                       [](const TransferItemResult& result) {
        return result.sourceRemoved;
    });
}

bool hasNonAtomicSuccess(const TransferTaskSnapshot& snapshot)
{
    return std::any_of(snapshot.itemResults.cbegin(), snapshot.itemResults.cend(),
                       [](const TransferItemResult& result) {
        return result.nonAtomic && hasDeliveredTarget(result);
    });
}

QString transferStatusText(const TransferTaskSnapshot& snapshot)
{
    QString text = snapshot.error.message.isEmpty()
        ? transferStateText(snapshot.state)
        : QStringLiteral("%1：%2").arg(transferStateText(snapshot.state),
                                         snapshot.error.message);
    if (hasNonAtomicSuccess(snapshot))
        text += QStringLiteral("（已完成但使用非原子替换，请复核目标文件）");
    return text;
}

bool supportsPanelTransfer(const FileBrowserPanel* source,
                           const FileBrowserPanel* target)
{
    if (!source || !target || !source->source() || !target->source())
        return false;
    return source->source()->sourceId() == QStringLiteral("local")
        || target->source()->sourceId() == QStringLiteral("local");
}

} // namespace

FileBrowserPanel::FileBrowserPanel(QWidget* parent) : QWidget(parent)
{
    setupUi();
}

void FileBrowserPanel::setupUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // 源选择器行（面板顶部，路径栏上方）：源类型（本地/FTP/SFTP）+ 设备（远程源时显示）。
    // TC 彻底化：每面板可独立配置浏览源；面板源=浏览配置，批量部署目标由工具栏（宿主）决定。
    auto* chooserRow = new QHBoxLayout();
    m_sourceCombo = new QComboBox(this);
    m_sourceCombo->setObjectName("panelSourceCombo");
    m_sourceCombo->addItem(tr("本地"), QStringLiteral("local"));
    m_sourceCombo->addItem("FTP", QStringLiteral("ftp"));
    m_sourceCombo->addItem("SFTP", QStringLiteral("sftp"));
    m_sourceCombo->setFixedWidth(80);
    connect(m_sourceCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        const QString proto = m_sourceCombo->currentData().toString();
        m_deviceCombo->setVisible(proto != QStringLiteral("local"));
        emit sourceChooserChanged(proto, m_deviceCombo->currentText());
    });
    m_deviceCombo = new QComboBox(this);
    m_deviceCombo->setObjectName("panelDeviceCombo");
    m_deviceCombo->setMinimumWidth(140);
    m_deviceCombo->setVisible(false);   // 本地源默认隐藏设备下拉
    // 设备下拉变化同样上报（面板级设备=浏览配置的一部分，宿主据此重建对应面板源）
    connect(m_deviceCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        emit sourceChooserChanged(m_sourceCombo->currentData().toString(),
                                  m_deviceCombo->currentText());
    });
    chooserRow->addWidget(m_sourceCombo);
    chooserRow->addWidget(m_deviceCombo);
    chooserRow->addStretch();
    layout->addLayout(chooserRow);

    // 路径栏（TC 风格：可编辑 + Enter 跳转）
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setPlaceholderText(tr("输入路径后回车跳转..."));
    connect(m_pathEdit, &QLineEdit::returnPressed, this, &FileBrowserPanel::onPathEnterPressed);
    layout->addWidget(m_pathEdit);

    // 统一表格视图
    m_table = new QTableView(this);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(22);   // 紧凑密度
    connect(m_table, &QTableView::doubleClicked, this, &FileBrowserPanel::onTableDoubleClicked);

    // 右键菜单（统一菜单项走 source 接口）
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested, this, &FileBrowserPanel::showContextMenu);

    // 拖拽：本面板可拖出（DragOnly 源），接收对面面板拖入（视口事件过滤器拦截）
    m_table->setDragEnabled(true);
    m_table->setDragDropMode(QAbstractItemView::DragOnly);
    m_table->setAcceptDrops(true);
    m_table->viewport()->setAcceptDrops(true);
    m_table->installEventFilter(this);
    m_table->viewport()->installEventFilter(this);
    setAcceptDrops(true);   // 面板级兜底：非表格区域（路径栏/面包屑）也能接收拖入

    // 面板快捷键（TC 风格，面板焦点内生效）：
    //   F2 重命名 / F4 预览编辑 / F5 复制到对面 / F6 移动到对面 / Tab 焦点切到对面面板
    auto* f2Shortcut = new QShortcut(QKeySequence(Qt::Key_F2), this);
    f2Shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(f2Shortcut, &QShortcut::activated, this, &FileBrowserPanel::renameSelected);

    auto* f4Shortcut = new QShortcut(QKeySequence(Qt::Key_F4), this);
    f4Shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(f4Shortcut, &QShortcut::activated, this, &FileBrowserPanel::openEditorForSelectedFile);

    auto* f5Shortcut = new QShortcut(QKeySequence(Qt::Key_F5), this);
    f5Shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(f5Shortcut, &QShortcut::activated, this, [this] { copySelectedTo(m_peerPanel); });

    auto* f6Shortcut = new QShortcut(QKeySequence(Qt::Key_F6), this);
    f6Shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(f6Shortcut, &QShortcut::activated, this, [this] { moveSelectedTo(m_peerPanel); });

    auto* tabShortcut = new QShortcut(QKeySequence(Qt::Key_Tab), this);
    tabShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(tabShortcut, &QShortcut::activated, this, [this] {
        // 聚焦对面表格（StrongFocus 可聚焦；同时激活对面面板的 F2/F5/F6/Tab 快捷键）
        if (m_peerPanel && m_peerPanel->fileTable())
            m_peerPanel->fileTable()->setFocus();
    });

    layout->addWidget(m_table, 1);

    // 可靠传输状态条只消费 Scheduler 的值快照，不保存或暴露任何 worker。
    auto* transferRow = new QHBoxLayout();
    transferRow->setSpacing(4);
    m_transferStatus = new QLabel(tr("无传输任务"), this);
    m_transferStatus->setObjectName("panelTransferStatus");
    m_transferStatus->setMinimumWidth(96);
    m_transferProgress = new QProgressBar(this);
    m_transferProgress->setObjectName("panelTransferProgress");
    m_transferProgress->setRange(0, 100);
    m_transferProgress->setValue(0);
    m_transferProgress->setTextVisible(false);
    m_transferProgress->setFixedHeight(8);
    m_transferCancelButton = new QPushButton(tr("取消"), this);
    m_transferCancelButton->setObjectName("transferCancelButton");
    m_transferCancelButton->setEnabled(false);
    m_transferRetryButton = new QPushButton(tr("重试/恢复"), this);
    m_transferRetryButton->setObjectName("transferRetryButton");
    m_transferRetryButton->setEnabled(false);
    connect(m_transferCancelButton, &QPushButton::clicked,
            this, &FileBrowserPanel::cancelActiveTransfer);
    connect(m_transferRetryButton, &QPushButton::clicked,
            this, &FileBrowserPanel::resumeLastTransfer);
    transferRow->addWidget(m_transferStatus);
    transferRow->addWidget(m_transferProgress, 1);
    transferRow->addWidget(m_transferCancelButton);
    transferRow->addWidget(m_transferRetryButton);
    layout->addLayout(transferRow);

    // 底部面包屑（文本展示当前路径，后续版本可点击）
    m_breadcrumb = new QLabel(this);
    m_breadcrumb->setObjectName("panelBreadcrumb");
    layout->addWidget(m_breadcrumb);
}

// ============================================================
// Task 3：源选择器 API — 程序化设置一律 blockSignals（宿主联动防循环；
// 仅用户操作源/设备下拉时发射 sourceChooserChanged）
// ============================================================

void FileBrowserPanel::setSourceChooserVisible(bool visible)
{
    m_sourceCombo->setVisible(visible);
    if (!visible) m_deviceCombo->setVisible(false);   // 行隐藏时设备下拉一并隐藏
}

void FileBrowserPanel::setSourceProto(const QString& proto)
{
    // "ssh" 是 SshAdapter 的协议注册表键（RemoteFileSource::sourceId 对 SFTP 返回 "ssh"），
    // 选择器 data role 用 "sftp"——归一化后 findData 命中，消除隐式依赖
    QString p = proto;
    if (p == QStringLiteral("ssh")) p = QStringLiteral("sftp");
    const int idx = m_sourceCombo->findData(p);
    if (idx >= 0 && idx != m_sourceCombo->currentIndex()) {
        m_sourceCombo->blockSignals(true);
        m_sourceCombo->setCurrentIndex(idx);
        m_sourceCombo->blockSignals(false);
    }
    m_deviceCombo->setVisible(p != QStringLiteral("local"));
}

void FileBrowserPanel::setSourceDevice(const QString& device)
{
    if (device.isEmpty()) return;
    const int idx = m_deviceCombo->findText(device);
    if (idx < 0 || idx == m_deviceCombo->currentIndex()) return;
    m_deviceCombo->blockSignals(true);
    m_deviceCombo->setCurrentIndex(idx);
    m_deviceCombo->blockSignals(false);
}

void FileBrowserPanel::setSourceDevices(const QStringList& devices)
{
    // 重填保留当前选择（与工具栏设备下拉同步同套路）：clear() 后 currentIndex 回 0，
    // 用户选中的非首台设备会被静默切回首台；恢复操作同样在 blockSignals 内
    const QString prev = m_deviceCombo->currentText();
    m_deviceCombo->blockSignals(true);
    m_deviceCombo->clear();
    m_deviceCombo->addItems(devices);
    const int idx = m_deviceCombo->findText(prev);
    if (idx >= 0) m_deviceCombo->setCurrentIndex(idx);
    m_deviceCombo->blockSignals(false);
}

void FileBrowserPanel::setSource(std::shared_ptr<IFileSource> source)
{
    ++m_loadGeneration;   // 源切换/置空也作废在途加载（连接失败 detach 旧源等）
    ++m_editGeneration;   // 在途编辑器读/写同样作废（v2.12 F4 链路）
    m_source = std::move(source);
    m_pathEdit->clear();
    m_currentPath.clear();
    if (m_source) {
        setSourceProto(m_source->sourceId());   // 选择器显示与实际源一致（blockSignals，不发射）
        // 本地默认当前目录；远程默认根
        m_currentPath = m_source->sourceId() == "local"
            ? QDir::currentPath() : QStringLiteral("/");
        navigateTo(m_currentPath);
    } else {
        // 置空源（连接失败 detach 等）：清空表格 + 路径栏 + 面包屑 + 当前路径，
        // 避免残留旧源的目录列表（部署目标 = 面板当前路径，残留会误导部署目标）。
        // 面包屑提示（「未连接远程...」）由调用方随后的 refresh()（loadDirectory
        // 无源分支）呈现
        m_files.clear();
        if (auto* model = qobject_cast<RemoteFileModel*>(m_table->model()))
            model->clear();
        m_breadcrumb->clear();
        emit currentPathChanged(QString());   // 状态栏方向指示同步（右路径为空）
        emit selectionChanged();
    }
}

void FileBrowserPanel::navigateTo(const QString& path)
{
    m_pathEdit->setText(path);
    loadDirectory(path);
}

void FileBrowserPanel::refresh() { loadDirectory(m_currentPath); }

void FileBrowserPanel::setTransferSubmitter(TransferSubmitter submitter)
{
    m_transferSubmitter = std::move(submitter);
}

void FileBrowserPanel::setTransferCanceller(TransferCanceller canceller)
{
    m_transferCanceller = std::move(canceller);
}

void FileBrowserPanel::setOverwritePolicyChooser(OverwritePolicyChooser chooser)
{
    m_overwritePolicyChooser = std::move(chooser);
}

void FileBrowserPanel::loadDirectory(const QString& path)
{
    // 无源（远程面板在协议/设备确定前 m_source 为 null）：明确提示而非静默无列表
    if (!m_source) {
        m_breadcrumb->setText(tr("未连接远程，请选择设备并刷新"));
        return;
    }
    const quint64 gen = ++m_loadGeneration;   // 代际令牌
    ++m_editGeneration;                       // 导航/刷新作废在途编辑器读写（v2.12）
    if (m_restoreSelectionGeneration != 0 && m_restoreSelectionGeneration != gen) {
        m_restoreSelectionName.clear();
        m_restoreSelectionGeneration = 0;
    }
    const QString p = path;
    auto source = m_source;                    // shared_ptr 拷贝（线程安全）
    m_pathEdit->setText(path);

    // 异步 list：慢速/断网目录读取不再冻结 UI；队列回调按代际令牌丢弃过期结果。
    // guard 在捕获列表用主线程（调度时 this 必然存活）构造 QPointer——若在 worker 内
    // 构造，worker 启动前面板已析构时读已释放 QObjectPrivate 即 UAF（与 Task 2
    // onRefreshRemote 的 guard 构造时机同模式）
    QtConcurrent::run([this, guard = QPointer<FileBrowserPanel>(this), gen, p, source]() {
        auto files = source->list(p);
        if (!guard) return;
        QMetaObject::invokeMethod(this, [this, gen, p, files, source]() {
            if (gen != m_loadGeneration) return;   // 过期：快速导航竞态丢弃
            if (files.empty() && !source->lastError().isEmpty())
                retryWithReconnect(gen, p, source);
            else
                applyFileList(p, files);
        }, Qt::QueuedConnection);
    });
}

void FileBrowserPanel::retryWithReconnect(quint64 gen, const QString& path,
                                          std::shared_ptr<IFileSource> source)
{
    // 异步重连重试一次（本地源 reconnect no-op 成功，仅多一次重试；对齐 v2.6 行为）。
    // guard 同 loadDirectory：主线程调度时构造（防 worker 启动前面板已析构的 UAF）
    QtConcurrent::run([this, guard = QPointer<FileBrowserPanel>(this), gen, path, source]() {
        bool ok = source->reconnect();
        auto files = ok ? source->list(path) : std::vector<FtpFileInfo>{};
        if (!guard) return;
        QString err = source->lastError();
        QMetaObject::invokeMethod(this, [this, gen, path, files, err, ok]() {
            if (gen != m_loadGeneration) return;
            if (ok && !(files.empty() && !err.isEmpty()))
                applyFileList(path, files);
            else
                showLoadError(gen, ok ? err : tr("重连失败: %1").arg(err));
        }, Qt::QueuedConnection);
    });
}

void FileBrowserPanel::showLoadError(quint64, const QString& err)
{
    m_breadcrumb->setText(tr("加载失败: %1").arg(err));
    m_pathEdit->setText(m_currentPath);   // 失败回写：路径栏与当前有效路径一致
}

void FileBrowserPanel::applyFileList(const QString& path,
                                     const std::vector<FtpFileInfo>& files)
{
    m_currentPath = path;
    m_files = files;

    // 统一渲染：本地也用 RemoteFileModel（FtpFileInfo 统一结构），首次加载惰性创建
    auto* model = qobject_cast<RemoteFileModel*>(m_table->model());
    if (!model) {
        model = new RemoteFileModel(m_table);
        m_table->setModel(model);
        // 列宽定型（v2.12 元数据列）：名称吃满剩余宽度，其余固定像素；仅建模型时设置一次
        {
            auto* hh = m_table->horizontalHeader();
            hh->setSectionResizeMode(RemoteFileModel::ColIcon, QHeaderView::Fixed);
            hh->setSectionResizeMode(RemoteFileModel::ColName, QHeaderView::Stretch);
            hh->setSectionResizeMode(RemoteFileModel::ColSize, QHeaderView::Fixed);
            hh->setSectionResizeMode(RemoteFileModel::ColDateTime, QHeaderView::Fixed);
            hh->setSectionResizeMode(RemoteFileModel::ColPermissions, QHeaderView::Fixed);
            hh->setSectionResizeMode(RemoteFileModel::ColOwner, QHeaderView::Fixed);
            m_table->setColumnWidth(RemoteFileModel::ColIcon, 28);
            m_table->setColumnWidth(RemoteFileModel::ColSize, 76);
            m_table->setColumnWidth(RemoteFileModel::ColDateTime, 130);
            m_table->setColumnWidth(RemoteFileModel::ColPermissions, 84);
            m_table->setColumnWidth(RemoteFileModel::ColOwner, 72);
        }
        // setModel 会重建 selection model（setupUi 时连接会失效），必须在设模型之后连接
        if (auto* selModel = m_table->selectionModel()) {
            connect(selModel, &QItemSelectionModel::selectionChanged, this,
                    [this] { emit selectionChanged(); });
        }
    }
    // 过滤 . 补 ..（与现有远程面板一致）
    auto full = files;
    full.erase(std::remove_if(full.begin(), full.end(),
        [](const FtpFileInfo& f) { return f.name == "."; }), full.end());
    bool hasDotDot = false;
    for (const auto& f : full) if (f.name == "..") hasDotDot = true;
    if (!hasDotDot) { FtpFileInfo dd; dd.name = ".."; dd.isDir = true; full.insert(full.begin(), dd); }
    model->setFileList(full);
    model->sort(RemoteFileModel::ColName, Qt::AscendingOrder);

    // 返回父目录后按目录名恢复选中；按名称而非行号定位，避免排序或列表变化导致错位。
    if (m_restoreSelectionGeneration == m_loadGeneration) {
        for (int row = 0; row < model->rowCount({}); ++row) {
            if (model->fileAt(row).name != m_restoreSelectionName.toStdString())
                continue;
            const QModelIndex target = model->index(row, 0);
            if (m_table->selectionModel()) {
                m_table->selectionModel()->setCurrentIndex(
                    target, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            }
            m_table->scrollTo(target, QAbstractItemView::EnsureVisible);
            m_table->setFocus(Qt::OtherFocusReason);
            break;
        }
        m_restoreSelectionName.clear();
        m_restoreSelectionGeneration = 0;
    }

    m_breadcrumb->setText(path);
    m_pathEdit->setText(path);
    emit currentPathChanged(path);
    emit filesChanged();
}

void FileBrowserPanel::onTableDoubleClicked(const QModelIndex& index)
{
    if (!index.isValid()) return;
    auto* model = qobject_cast<RemoteFileModel*>(m_table->model());
    if (!model) return;
    if (model->isPlaceholderAt(index.row())) return;   // 对比态占位行不可激活
    const auto& fi = model->fileAt(index.row());
    if (fi.name == "..") {
        navigateToParent();
        return;
    }
    if (!fi.isDir) {
        openEditorForRow(index.row());   // v2.12：双击文件 = 预览/编辑（TC F3/F4 合并入口）
        return;
    }
    QString newPath = m_currentPath;
    if (!newPath.endsWith('/')) newPath += '/';
    newPath += QString::fromStdString(fi.name);
    navigateTo(newPath);
    emit directoryActivated(fi);
}

void FileBrowserPanel::onPathEnterPressed()
{
    QString p = m_pathEdit->text().trimmed();
    if (!p.isEmpty()) navigateTo(p);
}

void FileBrowserPanel::enterSelectedDirectory()
{
    QModelIndex index = m_table->currentIndex();
    if (!index.isValid() && m_table->selectionModel()) {
        const auto rows = m_table->selectionModel()->selectedRows();
        if (!rows.isEmpty())
            index = rows.front();
    }
    if (!index.isValid())
        return;
    auto* model = qobject_cast<RemoteFileModel*>(m_table->model());
    if (!model || !model->fileAt(index.row()).isDir)
        return;
    onTableDoubleClicked(index);
}

void FileBrowserPanel::navigateToParent()
{
    if (m_currentPath.isEmpty())
        return;
    if (m_source && m_source->sourceId() == QStringLiteral("local")) {
        QDir dir(m_currentPath);
        const QString childName = dir.dirName();
        if (dir.cdUp()) {
            m_restoreSelectionName = childName;
            m_restoreSelectionGeneration = m_loadGeneration + 1;
            navigateTo(QDir::fromNativeSeparators(dir.absolutePath()));
        }
        return;
    }
    if (m_currentPath == QStringLiteral("/"))
        return;
    QString parent = m_currentPath;
    while (parent.size() > 1 && parent.endsWith(u'/'))
        parent.chop(1);
    const qsizetype lastSlash = parent.lastIndexOf(u'/');
    const QString childName = parent.mid(lastSlash + 1);
    parent = lastSlash <= 0 ? QStringLiteral("/") : parent.left(lastSlash);
    m_restoreSelectionName = childName;
    m_restoreSelectionGeneration = m_loadGeneration + 1;
    navigateTo(parent);
}

std::vector<FtpFileInfo> FileBrowserPanel::selectedFiles() const
{
    std::vector<FtpFileInfo> result;
    auto* model = qobject_cast<RemoteFileModel*>(m_table->model());
    if (!model) return result;
    const auto sel = m_table->selectionModel()->selectedRows();
    for (const auto& idx : sel) {
        if (model->isPlaceholderAt(idx.row())) continue;   // 占位行防御（不可选，双保险）
        result.push_back(model->fileAt(idx.row()));
    }
    return result;
}

std::vector<FtpFileInfo> FileBrowserPanel::fileList() const
{
    auto* model = qobject_cast<RemoteFileModel*>(m_table->model());
    return model ? model->files() : std::vector<FtpFileInfo>{};
}

void FileBrowserPanel::applyCompareRows(const QVector<AlignedRow>& rows, const QString& gapCaption)
{
    auto* model = qobject_cast<RemoteFileModel*>(m_table->model());
    if (model)
        model->setCompareView(model->files(), rows, gapCaption);
}

void FileBrowserPanel::clearCompareView()
{
    auto* model = qobject_cast<RemoteFileModel*>(m_table->model());
    if (model)
        model->exitCompareView();
}

// ============================================================
// Task 3：面板操作（F2 重命名 / F5 复制 / F6 移动 / 右键菜单 / 拖拽）
// 方向语义：目标面板源类型 → 传输方式
// ============================================================

void FileBrowserPanel::renameSelected()
{
    if (!m_source) return;
    const auto files = selectedFiles();
    if (files.empty()) { m_breadcrumb->setText(tr("未选择文件")); return; }
    if (files.size() > 1) { m_breadcrumb->setText(tr("重命名仅支持单选")); return; }
    const auto& f = files.front();
    if (f.name == "..") return;

    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("重命名"),
        tr("新名称："), QLineEdit::Normal, QString::fromStdString(f.name), &ok);
    if (!ok || newName.trimmed().isEmpty()) return;
    if (newName.trimmed() == QString::fromStdString(f.name)) return;

    const QString srcFull = m_currentPath + "/" + QString::fromStdString(f.name);
    const QString dstFull = m_currentPath + "/" + newName.trimmed();
    if (!m_source->rename(srcFull, dstFull))
        m_breadcrumb->setText(tr("重命名失败: %1").arg(m_source->lastError()));
    else
        refresh();
}

void FileBrowserPanel::copySelectedTo(FileBrowserPanel* target)
{
    if (!m_source || !target || !target->source()) return;
    const auto files = selectedFiles();
    if (files.empty()) { m_breadcrumb->setText(tr("未选择文件")); return; }
    const QString srcKind = m_source->sourceId();
    const QString dstKind = target->source()->sourceId();
    if (srcKind == "local" || dstKind == "local") {
        submitSelectedTransfer(target, false);
    } else {
        // 远程→远程：禁用提示
        m_breadcrumb->setText(tr("远程间复制暂不支持"));
        return;
    }
}

void FileBrowserPanel::moveSelectedTo(FileBrowserPanel* target)
{
    if (!m_source || !target || !target->source()) return;
    const auto files = selectedFiles();
    if (files.empty()) { m_breadcrumb->setText(tr("未选择文件")); return; }
    const QString srcKind = m_source->sourceId();
    const QString dstKind = target->source()->sourceId();
    // 目录项统一跳过删源（浅拷贝限制：mkpath 仅建空目录/单文件上传下载不递归，
    // 删源将造成内容丢失）——跳过并提示，文件项仍按移动语义执行
    int dirSkipped = 0;
    if (srcKind == "local" || dstKind == "local") {
        submitSelectedTransfer(target, true);
    } else {
        // 远程→远程：禁用提示
        m_breadcrumb->setText(tr("远程间移动暂不支持"));
    }
    if (dirSkipped > 0)
        m_breadcrumb->setText(tr("目录移动暂不支持（浅拷贝限制），已跳过 %1 个目录").arg(dirSkipped));
}

QUuid FileBrowserPanel::submitSelectedTransfer(FileBrowserPanel* target, bool move)
{
    if (!m_source || !target || !target->source() || !m_transferSubmitter)
        return {};

    const bool sourceLocal = m_source->sourceId() == QStringLiteral("local");
    const bool targetLocal = target->source()->sourceId() == QStringLiteral("local");
    if (!sourceLocal && !targetLocal)
        return {};

    QVector<TransferItemRequest> items;
    int directoriesSkipped = 0;
    for (const auto& file : selectedFiles()) {
        if (file.name == "..")
            continue;
        if (file.isDir) {
            ++directoriesSkipped;
            continue;
        }

        const QString name = QString::fromStdString(file.name);
        TransferItemRequest item;
        const bool localToLocal = sourceLocal && targetLocal;
        item.direction = sourceLocal && !localToLocal ? TransferDirection::Upload
                                                      : TransferDirection::Download;
        item.localPath = joinedPath(localToLocal || !sourceLocal
                                        ? target->m_currentPath : m_currentPath,
                                    name);
        item.remotePath = joinedPath(localToLocal || !sourceLocal
                                         ? m_currentPath : target->m_currentPath,
                                     name);
        items.push_back(std::move(item));
    }

    if (items.isEmpty()) {
        m_breadcrumb->setText(directoriesSkipped > 0
            ? tr("文件夹传输尚未支持，未提交任务")
            : tr("未选择可传输文件"));
        return {};
    }

    const auto policy = chooseOverwritePolicy(target, items);
    if (!policy)
        return {};
    for (auto& item : items)
        item.overwrite = *policy;

    TransferTask task;
    task.displayName = tr("%1 %2 个文件")
        .arg(sourceLocal ? tr("上传") : tr("下载"))
        .arg(items.size());
    task.protocol = sourceLocal && targetLocal
        ? QStringLiteral("local")
        : normalizedTransferProtocol(
              sourceLocal ? target->source()->sourceId() : m_source->sourceId());
    task.generation = ++m_transferGeneration;
    task.removeSourceAfterCommit = move;
    task.items = std::move(items);
    return submitTransferTask(std::move(task), this, target);
}

std::optional<OverwritePolicy> FileBrowserPanel::chooseOverwritePolicy(
    FileBrowserPanel* target,
    const QVector<TransferItemRequest>& items) const
{
    if (!target)
        return std::nullopt;

    int conflicts = 0;
    for (const auto& item : items) {
        const QString targetName = QFileInfo(item.direction == TransferDirection::Upload
                                                 ? item.remotePath
                                                 : item.localPath)
                                       .fileName();
        const auto found = std::find_if(target->m_files.cbegin(), target->m_files.cend(),
            [&targetName](const FtpFileInfo& existing) {
                return QString::fromStdString(existing.name) == targetName;
            });
        if (found != target->m_files.cend())
            ++conflicts;
    }

    if (conflicts == 0)
        return OverwritePolicy::Overwrite;
    if (m_overwritePolicyChooser)
        return m_overwritePolicyChooser(conflicts);

    const auto answer = QMessageBox::question(
        const_cast<FileBrowserPanel*>(this),
        tr("同名文件"),
        tr("有 %1 个同名项。覆盖这些文件吗？\n"
           "选择“否”将跳过同名项。").arg(conflicts),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel)
        return std::nullopt;
    return answer == QMessageBox::Yes ? OverwritePolicy::Overwrite
                                      : OverwritePolicy::Skip;
}

QUuid FileBrowserPanel::submitTransferTask(TransferTask task,
                                           FileBrowserPanel* sourcePanel,
                                           FileBrowserPanel* targetPanel)
{
    if (!m_transferSubmitter || task.items.isEmpty())
        return {};
    if (task.generation == 0)
        task.generation = ++m_transferGeneration;

    const QUuid id = m_transferSubmitter(task);
    if (id.isNull()) {
        m_transferStatus->setText(tr("任务提交失败"));
        return {};
    }

    PendingTransfer pending;
    pending.task = std::move(task);
    pending.sourcePanel = sourcePanel;
    pending.targetPanel = targetPanel;
    pending.sourceLoadGeneration = sourcePanel ? sourcePanel->m_loadGeneration : 0;
    pending.targetLoadGeneration = targetPanel ? targetPanel->m_loadGeneration : 0;
    m_pendingTransfers.insert(id, pending);
    m_activeTransferId = id;
    m_transferProgress->setValue(0);
    m_transferStatus->setText(tr("已提交"));
    m_transferCancelButton->setEnabled(true);
    m_transferRetryButton->setEnabled(false);
    return id;
}

void FileBrowserPanel::cancelActiveTransfer()
{
    if (m_activeTransferId.isNull() || !m_transferCanceller)
        return;
    m_transferCanceller(m_activeTransferId);
    m_transferStatus->setText(transferStateText(TransferState::Cancelling));
    m_transferCancelButton->setEnabled(false);
}

void FileBrowserPanel::resumeLastTransfer()
{
    if (!m_lastTransfer || !m_transferSubmitter)
        return;
    if ((m_lastTransfer->sourcePanel
         && m_lastTransfer->sourcePanel->m_loadGeneration
             != m_lastTransfer->sourceLoadGeneration)
        || (m_lastTransfer->targetPanel
            && m_lastTransfer->targetPanel->m_loadGeneration
                != m_lastTransfer->targetLoadGeneration)) {
        m_transferStatus->setText(tr("源或目标已切换，请重新选择文件"));
        m_transferRetryButton->setEnabled(false);
        return;
    }

    TransferTask resumed = m_lastTransfer->task;
    QVector<TransferItemRequest> remaining;
    for (qsizetype index = 0; index < resumed.items.size(); ++index) {
        const bool needsResume = index >= m_lastTransferSnapshot.itemResults.size()
            || !hasDeliveredTarget(m_lastTransferSnapshot.itemResults.at(index));
        if (needsResume) {
            remaining.push_back(resumed.items.at(index));
        }
    }
    if (remaining.isEmpty()) {
        m_transferStatus->setText(tr("没有需要恢复的项"));
        m_transferRetryButton->setEnabled(false);
        return;
    }

    resumed.items = std::move(remaining);
    resumed.generation = ++m_transferGeneration;
    resumed.credentialKey.clear();
    submitTransferTask(std::move(resumed),
                       m_lastTransfer->sourcePanel.data(),
                       m_lastTransfer->targetPanel.data());
}

void FileBrowserPanel::consumeTransferEvent(const TransferEvent& event)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, event] { applyTransferEvent(event); },
                                  Qt::QueuedConnection);
        return;
    }
    applyTransferEvent(event);
}

void FileBrowserPanel::applyTransferEvent(const TransferEvent& event)
{
    const auto found = m_pendingTransfers.find(event.snapshot.id);
    if (found == m_pendingTransfers.end()
        || found->task.generation != event.snapshot.generation) {
        return;
    }

    const bool isActive = event.snapshot.id == m_activeTransferId;
    if (isActive) {
        m_transferStatus->setText(transferStatusText(event.snapshot));
        m_transferProgress->setValue(std::clamp(event.snapshot.progress, 0, 100));
    }

    if (event.type != TransferEventType::TaskFinished) {
        if (isActive)
            m_transferCancelButton->setEnabled(event.snapshot.state != TransferState::Cancelling);
        emit transferSnapshotApplied();
        return;
    }

    PendingTransfer completed = found.value();
    const bool committed = hasCommittedItem(event.snapshot);
    if (committed && completed.targetPanel
        && completed.targetPanel->m_loadGeneration == completed.targetLoadGeneration) {
        completed.targetPanel->refresh();
        completed.targetLoadGeneration = completed.targetPanel->m_loadGeneration;
    }
    if (hasSourceRemovedItem(event.snapshot) && completed.sourcePanel
        && completed.sourcePanel->m_loadGeneration == completed.sourceLoadGeneration) {
        completed.sourcePanel->refresh();
        completed.sourceLoadGeneration = completed.sourcePanel->m_loadGeneration;
    }

    m_lastTransfer = completed;
    m_lastTransferSnapshot = event.snapshot;
    m_pendingTransfers.erase(found);
    if (isActive)
        m_activeTransferId = {};
    const TransferState state = event.snapshot.state;
    if (isActive) {
        m_transferCancelButton->setEnabled(false);
        m_transferRetryButton->setEnabled(state == TransferState::NeedsAttention
            || state == TransferState::Failed
            || state == TransferState::Cancelled
            || state == TransferState::PartiallySucceeded);
    }
    emit transferSnapshotApplied();
}

void FileBrowserPanel::showContextMenu(const QPoint& pos)
{
    if (!m_source) return;

    // 点击行不在当前选中集合时改为单选该行（标准文件管理器行为）
    const QModelIndex idx = m_table->indexAt(pos);
    if (idx.isValid()) {
        auto* selModel = m_table->selectionModel();
        if (selModel && !selModel->isSelected(idx)) {
            selModel->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
    }

    const auto files = selectedFiles();
    const bool hasSel = !files.empty() && !(files.size() == 1 && files.front().name == "..");
    const bool singleRow = files.size() == 1;
    const bool singleSel = singleRow && files.front().name != "..";
    const bool selIsDir = singleRow && files.front().isDir;

    QMenu menu(this);
    auto* enterAct   = menu.addAction(tr("进入"));           // 目录 / .. 进入
    auto* editAct    = menu.addAction(tr("预览/编辑"));       // 文件（F4 同义，v2.12）
    editAct->setEnabled(singleSel && !selIsDir);
    menu.addSeparator();
    auto* mkdirAct   = menu.addAction(tr("新建目录..."));
    auto* renameAct  = menu.addAction(tr("重命名..."));
    auto* deleteAct  = menu.addAction(tr("删除"));
    menu.addSeparator();
    auto* copyAct    = menu.addAction(tr("复制到对面"));
    auto* moveAct    = menu.addAction(tr("移动到对面"));
    menu.addSeparator();
    auto* copyPathAct = menu.addAction(tr("复制路径"));
    auto* refreshAct = menu.addAction(tr("刷新"));

    enterAct->setEnabled(selIsDir);            // 仅目录（含 ..）可进入
    renameAct->setEnabled(singleSel);
    deleteAct->setEnabled(hasSel);
    copyAct->setEnabled(hasSel && m_peerPanel);
    moveAct->setEnabled(hasSel && m_peerPanel);
    copyPathAct->setEnabled(hasSel);

    QAction* chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == enterAct) {
        const auto& f = files.front();
        if (f.name == "..") {
            QString parent = m_currentPath;
            if (parent == "/" || parent.isEmpty()) return;
            const int lastSlash = parent.lastIndexOf('/');
            parent = parent.left(lastSlash);
            if (parent.isEmpty()) parent = "/";
            navigateTo(parent);
        } else {
            QString newPath = m_currentPath;
            if (!newPath.endsWith('/')) newPath += '/';
            newPath += QString::fromStdString(f.name);
            navigateTo(newPath);
            emit directoryActivated(f);
        }
    } else if (chosen == editAct) {
        openEditorForSelectedFile();
    } else if (chosen == mkdirAct) {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("新建目录"),
            tr("目录名称："), QLineEdit::Normal, QString(), &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        QString newPath = m_currentPath;
        if (!newPath.endsWith('/')) newPath += '/';
        newPath += name.trimmed();
        if (!m_source->mkdir(newPath))
            m_breadcrumb->setText(tr("新建目录失败: %1").arg(m_source->lastError()));
        else
            refresh();
    } else if (chosen == renameAct) {
        renameSelected();
    } else if (chosen == deleteAct) {
        const auto res = QMessageBox::question(this, tr("删除"),
            tr("确定删除选中的 %1 项？").arg(files.size()));
        if (res != QMessageBox::Yes) return;
        bool anyFailed = false;
        for (const auto& f : files) {
            if (f.name == "..") continue;
            const QString full = m_currentPath + "/" + QString::fromStdString(f.name);
            if (!m_source->remove(full, f.isDir)) anyFailed = true;
        }
        if (anyFailed)
            m_breadcrumb->setText(tr("部分删除失败: %1").arg(m_source->lastError()));
        else
            refresh();
    } else if (chosen == copyAct) {
        copySelectedTo(m_peerPanel);
    } else if (chosen == moveAct) {
        moveSelectedTo(m_peerPanel);
    } else if (chosen == copyPathAct) {
        // 取第一个非 ".." 的选中项（多选混入 ".." 时跳过）
        QString name;
        for (const auto& f : files) {
            if (f.name != "..") { name = QString::fromStdString(f.name); break; }
        }
        const QString p = name.isEmpty() ? m_currentPath
                                         : m_currentPath + "/" + name;
        QApplication::clipboard()->setText(p);
        m_breadcrumb->setText(tr("路径已复制: %1").arg(p));
    } else if (chosen == refreshAct) {
        refresh();
    }
}

// ============================================================
// 拖拽：面板间传输（拖出 DragOnly / 拖入 → copySelectedTo 方向语义）
// ============================================================

FileBrowserPanel* FileBrowserPanel::dragSourcePanel(const QDropEvent* event) const
{
    // 拖拽来源必须是另一个面板（QAbstractItemView::startDrag 以视图为 source，
    // 兼容来源为表格或视口的情况：向上回溯父链查找 FileBrowserPanel）
    auto* src = qobject_cast<QWidget*>(event->source());
    while (src) {
        if (auto* panel = qobject_cast<FileBrowserPanel*>(src)) {
            return panel == this ? nullptr : panel;
        }
        src = src->parentWidget();
    }
    return nullptr;
}

bool FileBrowserPanel::canAcceptDrag(const FileBrowserPanel* sourcePanel,
                                     bool hasUrls) const
{
    return supportsPanelTransfer(sourcePanel, this)
        || (!sourcePanel && hasUrls && m_source
            && m_source->sourceId() != QStringLiteral("local"));
}

bool FileBrowserPanel::eventFilter(QObject* watched, QEvent* event)
{
    // 拦截表格视口上的面板间拖拽事件（与 FtpDeployWidget 的系统文件拖入同套路）
    if ((watched == m_table->viewport() || watched == m_table) && m_source) {
        switch (event->type()) {
        case QEvent::KeyPress: {
            const auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Right) {
                enterSelectedDirectory();
                return true;
            }
            if (key->key() == Qt::Key_Left) {
                navigateToParent();
                return true;
            }
            break;
        }
        case QEvent::DragEnter: {
            auto* drag = static_cast<QDragEnterEvent*>(event);
            const auto sourcePanel = dragSourcePanel(drag);
            const bool accepted = canAcceptDrag(sourcePanel, drag->mimeData()->hasUrls());
            if (accepted) {
                static_cast<QDragEnterEvent*>(event)->setDropAction(Qt::CopyAction);
                static_cast<QDragEnterEvent*>(event)->accept();
                return true;
            }
            drag->ignore();
            return true;
        }
        case QEvent::DragMove: {
            auto* drag = static_cast<QDragMoveEvent*>(event);
            const auto sourcePanel = dragSourcePanel(drag);
            const bool accepted = canAcceptDrag(sourcePanel, drag->mimeData()->hasUrls());
            if (accepted) {
                static_cast<QDragMoveEvent*>(event)->setDropAction(Qt::CopyAction);
                static_cast<QDragMoveEvent*>(event)->accept();
                return true;
            }
            drag->ignore();
            return true;
        }
        case QEvent::Drop: {
            auto* drop = static_cast<QDropEvent*>(event);
            const DropRoute route = handleDrop(dragSourcePanel(drop), drop->mimeData()->urls());
            if (route != DropRoute::Rejected) {
                drop->setDropAction(Qt::CopyAction);
                drop->accept();
                return true;
            }
            drop->ignore();
            return true;
        }
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void FileBrowserPanel::dragEnterEvent(QDragEnterEvent* event)
{
    // 非表格区域（路径栏/面包屑等）上的拖入：
    //   仅本地↔远程面板传输，或系统→远程上传可接受。
    if (canAcceptDrag(dragSourcePanel(event), event->mimeData()->hasUrls())) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}

void FileBrowserPanel::dragMoveEvent(QDragMoveEvent* event)
{
    if (canAcceptDrag(dragSourcePanel(event), event->mimeData()->hasUrls())) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}

void FileBrowserPanel::dropEvent(QDropEvent* event)
{
    const DropRoute route = handleDrop(dragSourcePanel(event), event->mimeData()->urls());
    if (route == DropRoute::Rejected) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
}

FileBrowserPanel::DropRoute FileBrowserPanel::handleDrop(
    FileBrowserPanel* sourcePanel,
    const QList<QUrl>& urls)
{
    if (sourcePanel && supportsPanelTransfer(sourcePanel, this)) {
        sourcePanel->copySelectedTo(this);
        return DropRoute::PanelTransfer;
    }

    if (sourcePanel) {
        m_breadcrumb->setText(tr("远程间传输暂不支持"));
        return DropRoute::Rejected;
    }

    if (!m_source || m_source->sourceId() == QStringLiteral("local")) {
        m_breadcrumb->setText(tr("系统文件只能拖入远程面板"));
        return DropRoute::Rejected;
    }

    QVector<TransferItemRequest> items;
    for (const auto& url : urls) {
        const QString localPath = url.toLocalFile();
        const QFileInfo info(localPath);
        if (localPath.isEmpty() || (info.exists() && !info.isFile()))
            continue;
        TransferItemRequest item;
        item.localPath = localPath;
        item.remotePath = joinedPath(m_currentPath, info.fileName());
        item.direction = TransferDirection::Upload;
        items.push_back(std::move(item));
    }
    if (items.isEmpty()) {
        m_breadcrumb->setText(tr("未检测到可上传的文件"));
        return DropRoute::Rejected;
    }

    const auto policy = chooseOverwritePolicy(this, items);
    if (!policy)
        return DropRoute::Rejected;
    for (auto& item : items)
        item.overwrite = *policy;

    TransferTask task;
    task.displayName = tr("上传 %1 个文件").arg(items.size());
    task.protocol = normalizedTransferProtocol(m_source->sourceId());
    task.generation = ++m_transferGeneration;
    task.items = std::move(items);
    if (submitTransferTask(std::move(task), nullptr, this).isNull())
        return DropRoute::Rejected;
    return DropRoute::SystemUpload;
}

// ============================================================
// F4 预览/编辑（v2.12 文件管理增强）
// 读/写/冲突检查全部经 QtConcurrent + 代际令牌，GUI 线程零阻塞；
// 与 loadDirectory 同一 QPointer+gen 竞态模式。
// ============================================================

void FileBrowserPanel::openEditorForSelectedFile()
{
    QModelIndex index = m_table->currentIndex();
    if (!index.isValid() && m_table->selectionModel()) {
        const auto rows = m_table->selectionModel()->selectedRows();
        if (!rows.isEmpty())
            index = rows.front();
    }
    if (index.isValid())
        openEditorForRow(index.row());
}

void FileBrowserPanel::openEditorForRow(int row)
{
    auto* model = qobject_cast<RemoteFileModel*>(m_table->model());
    if (!model || row < 0 || row >= model->rowCount({}))
        return;
    if (model->isPlaceholderAt(row))
        return;
    const FtpFileInfo fi = model->fileAt(row);
    if (fi.isDir || fi.name == "." || fi.name == "..")
        return;
    if (!m_source)
        return;

    QString dir = m_currentPath;
    if (!dir.endsWith(QLatin1Char('/')))
        dir += QLatin1Char('/');
    const QString path = dir + QString::fromStdString(fi.name);
    const ConflictSnapshot snap{ fi.size, QString::fromStdString(fi.dateTime), true };

    // 评审 C-1：每次打开推进编辑代际，作废更早的在途读——慢链路上连点两个文件时，
    // 先发起的读回晚也会被丢弃，杜绝"甲内容+乙路径"串包后经保存误写远端。
    const quint64 gen = ++m_editGeneration;
    m_editInfo = fi;
    m_editPath = path;
    m_editSnap = snap;

    if (fi.size > kMaxEditBytes) {
        openReadonlyEditor(tr("文件超过大小上限（2 MB），建议下载后本地编辑"));
        return;
    }

    auto source = m_source;
    // 评审 C-1：info/path/snap 随 worker 值捕获成对回传，回调只在仍是最新目标时落成员
    QtConcurrent::run([this, guard = QPointer<FileBrowserPanel>(this), gen, source,
                       path, fi, snap]() {
        std::string bytes;
        QString err;
        const bool ok = source->readFile(path, bytes, kMaxEditBytes);
        if (!ok)
            err = source->lastError();
        QByteArray ba(bytes.data(), static_cast<int>(bytes.size()));
        if (!guard)
            return;
        QMetaObject::invokeMethod(this, [this, gen, ok, err, ba, path, fi, snap]() {
            if (gen != m_editGeneration) return;   // 过期（导航/刷新/更新的打开）：丢弃
            m_editInfo = fi;
            m_editPath = path;
            m_editSnap = snap;
            if (!ok) {
                // 评审 I-4a：size 未知、读侧硬断超限 → 与已知超限同口径转只读
                if (err.contains(QStringLiteral("上限")))
                    openReadonlyEditor(err);
                else
                    QMessageBox::warning(this, tr("打开失败"), err);
                return;
            }
            finishEditorOpen(ba);
        }, Qt::QueuedConnection);
    });
}

void FileBrowserPanel::ensureEditor()
{
    if (m_editor)
        return;
    m_editor = new FileEditorDialog(this);
    connect(m_editor, &FileEditorDialog::saveRequested, this, [this]() { startEditorSave(false); });
    connect(m_editor, &FileEditorDialog::codecChangedRequest, this, &FileBrowserPanel::redecodeEditor);
    m_editor->setDiscardGuard([this]() {
        if (!m_editor || !m_editor->isDirty())
            return true;
        return QMessageBox::question(this, tr("未保存的修改"),
                   tr("丢弃未保存的修改并关闭？"),
                   QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
    });
}

void FileBrowserPanel::openReadonlyEditor(const QString& reason)
{
    ensureEditor();
    m_editor->bind(QString::fromStdString(m_editInfo.name), m_editPath, QString(),
                   { QStringLiteral("UTF-8") }, QStringLiteral("UTF-8"),
                   QStringLiteral("LF"), false, tr("不可编辑"), true, reason);
    ++m_editorOpenCount;
    m_editor->show();
    m_editor->raise();
    m_editor->activateWindow();
}

void FileBrowserPanel::finishEditorOpen(const QByteArray& raw)
{
    m_editRaw = raw;
    const TextProbe probe = probeTextBuffer(raw);
    if (probe.looksBinary) {
        openReadonlyEditor(tr("二进制文件不可编辑"));
        return;
    }
    if (probe.eol == QStringLiteral("mixed")) {
        // 评审 I-2：混合行尾无法在"编辑器统一 LF 模型"下无损还原——
        // 按只读打开而非擅自整改写行尾（规格 §4.3"保存保持原行尾"的就实边界）
        openReadonlyEditor(tr("行尾混用（CRLF/LF），为避免保存时擅自改写行尾，已按只读打开"));
        return;
    }
    QString text, err;
    QString codec = probe.codec;
    if (!decodeText(raw, codec, text, err)) {
        text = QString::fromUtf8(raw);
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        codec = QStringLiteral("UTF-8");
    }
    m_editCodec = codec;
    ensureEditor();
    const QString note = QStringLiteral("%1 · %2 · %3 字节%4")
        .arg(codec, probe.eol)
        .arg(raw.size())
        .arg(probe.fromBom ? QStringLiteral(" · BOM") : QString());
    m_editor->bind(QString::fromStdString(m_editInfo.name), m_editPath, text,
                   suggestedCodecs(codec), codec, probe.eol, probe.fromBom,
                   note, false, QString());
    ++m_editorOpenCount;
    m_editor->show();
    m_editor->raise();
    m_editor->activateWindow();
}

void FileBrowserPanel::redecodeEditor(const QString& newCodec)
{
    if (!m_editor)
        return;
    if (m_editor->isDirty()) {
        if (QMessageBox::question(this, tr("切换编码"),
                tr("切换编码将丢弃未保存的修改，继续？"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            m_editor->setCodecSilently(m_editCodec);
            return;
        }
    }
    QString text, err;
    if (!decodeText(m_editRaw, newCodec, text, err)) {
        QMessageBox::warning(this, tr("解码失败"), err);
        m_editor->setCodecSilently(m_editCodec);
        return;
    }
    m_editCodec = newCodec;
    const TextProbe probe = probeTextBuffer(m_editRaw);
    m_editor->bind(QString::fromStdString(m_editInfo.name), m_editPath, text,
                   suggestedCodecs(newCodec), newCodec, probe.eol, probe.fromBom,
                   tr("已按 %1 重解码").arg(newCodec), false, QString());
}

void FileBrowserPanel::startEditorSave(bool skipConflictCheck)
{
    if (!m_editor || !m_source)
        return;
    // #48：所选编码不可表示全文时 encodeText 会兜底改按 UTF-8——不得静默换码。
    // 弹窗二选一：明示"用 UTF-8 保存"（下拉与 m_editCodec 同步切 UTF-8，
    // 显示与实写恒一致）/ 返回编辑（不保存，脏位保留）。
    bool fellBack = false;
    QByteArray bytes = m_editor->currentBytes(&fellBack);
    if (fellBack) {
        const auto ans = QMessageBox::question(this, tr("所选编码无法表示全文"),
            tr("文本含编码 %1 无法表示的字符，直接保存会悄悄改用 UTF-8 写入整个文件。\n\n"
               "「用 UTF-8 保存」= 明示切换为 UTF-8 后继续保存（编码下拉同步显示）；\n"
               "「返回编辑」= 本次不保存，可修改文本或切换编码。")
                .arg(m_editor->currentCodec()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ans != QMessageBox::Yes)
            return;
        m_editor->setCodecSilently(QStringLiteral("UTF-8"));
        m_editCodec = QStringLiteral("UTF-8");
        bytes = m_editor->currentBytes();
    }
    m_editor->setBusy(true);
    auto source = m_source;
    QPointer<FileEditorDialog> dlg = m_editor;
    const QString path = m_editPath;
    const ConflictSnapshot snap = m_editSnap;
    const bool local = source->sourceId() == QLatin1String("local");
    const quint64 gen = m_editGeneration;
    const std::string data(bytes.toStdString());

    QtConcurrent::run([this, guard = QPointer<FileBrowserPanel>(this), gen, source, dlg,
                       path, data, local, snap, skipConflictCheck, bytes]() {
        QString err;
        bool conflict = false;
        // R4 退化口径：无任何可比字段则跳过检查；否则重拉远端目录比对同名条目
        if (!local && !skipConflictCheck && (snap.hasSize || !snap.mtime.isEmpty())) {
            const int slash = path.lastIndexOf(QLatin1Char('/'));
            const QString parent = slash >= 0 ? path.left(slash + 1) : QStringLiteral("/");
            const std::string name = (slash >= 0 ? path.mid(slash + 1) : path).toStdString();
            for (const auto& e : source->list(parent)) {
                if (e.name == name) {
                    conflict = remoteChanged(snap, e);
                    break;
                }
            }
        }
        if (!conflict && !source->writeFile(path, data))
            err = source->lastError();
        if (!guard)
            return;
        QMetaObject::invokeMethod(this, [this, gen, dlg, conflict, err, bytes, path]() {
            if (gen != m_editGeneration) { if (dlg) dlg->setBusy(false); return; }
            if (!dlg)
                return;
            dlg->setBusy(false);
            if (conflict) {
                const auto r = QMessageBox::question(this, tr("远端文件已变化"),
                    tr("打开后远端同名文件已被其他方改动。\n\n"
                       "覆盖将丢失对方修改。仍要覆盖，还是放弃本地修改并重新拉取最新内容？"),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                if (r == QMessageBox::Yes)
                    startEditorSave(true);
                else
                    reopenEditorForCurrentRow();
                return;
            }
            if (!err.isEmpty()) {
                QMessageBox::warning(this, tr("保存失败"), err);
                return;
            }
            dlg->markSaved();
            dlg->setStatusNote(tr("已保存 %1（%2 字节）")
                .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
                .arg(bytes.size()));
            // 评审 I-1：己方保存后续用快照（写入大小为准，mtime 本地不可知则清空），
            // 同一对话框二次保存不再拿打开时的陈旧快照误判"远端已变化"
            m_editSnap = ConflictSnapshot{ static_cast<uint64_t>(bytes.size()), QString(), true };
            emit fileSavedForLog(path, static_cast<quint64>(bytes.size()));
            refresh();   // 回传成功刷新目录（对比态经 filesChanged 自动失效，属预期）
        }, Qt::QueuedConnection);
    });
}

void FileBrowserPanel::reopenEditorForCurrentRow()
{
    auto* model = qobject_cast<RemoteFileModel*>(m_table->model());
    if (model) {
        for (int row = 0; row < model->rowCount({}); ++row) {
            if (!model->isPlaceholderAt(row) && model->fileAt(row).name == m_editInfo.name) {
                openEditorForRow(row);
                return;
            }
        }
    }
    QMessageBox::warning(this, tr("刷新失败"), tr("文件已不在当前目录，无法重新拉取"));
}
