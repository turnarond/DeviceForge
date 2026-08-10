#include "ui/FileBrowserPanel.h"
#include "ui/IFileSource.h"
#include "tools/FtpDeployTool/RemoteFileModel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QTableView>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QDir>
#include <QFile>
#include <QEvent>
#include <QShortcut>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>

FileBrowserPanel::FileBrowserPanel(QWidget* parent) : QWidget(parent)
{
    setupUi();
}

void FileBrowserPanel::setupUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

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
    m_table->viewport()->installEventFilter(this);

    // 面板快捷键（TC 风格，面板焦点内生效）：
    //   F2 重命名 / F5 复制到对面 / F6 移动到对面 / Tab 焦点切到对面面板
    auto* f2Shortcut = new QShortcut(QKeySequence(Qt::Key_F2), this);
    f2Shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(f2Shortcut, &QShortcut::activated, this, &FileBrowserPanel::renameSelected);

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

    // 底部面包屑（文本展示当前路径，后续版本可点击）
    m_breadcrumb = new QLabel(this);
    m_breadcrumb->setObjectName("panelBreadcrumb");
    layout->addWidget(m_breadcrumb);
}

void FileBrowserPanel::setSource(std::shared_ptr<IFileSource> source)
{
    m_source = std::move(source);
    m_pathEdit->clear();
    m_currentPath.clear();
    if (m_source) {
        // 本地默认当前目录；远程默认根
        m_currentPath = m_source->sourceId() == "local"
            ? QDir::currentPath() : QStringLiteral("/");
        navigateTo(m_currentPath);
    }
}

void FileBrowserPanel::navigateTo(const QString& path)
{
    m_pathEdit->setText(path);
    loadDirectory(path);
}

void FileBrowserPanel::refresh() { loadDirectory(m_currentPath); }

void FileBrowserPanel::loadDirectory(const QString& path)
{
    if (!m_source || m_refreshing) return;
    m_refreshing = true;
    m_pathEdit->setText(path);
    auto files = m_source->list(path);
    m_refreshing = false;
    if (files.empty() && !m_source->lastError().isEmpty()) {
        m_breadcrumb->setText(tr("加载失败: %1").arg(m_source->lastError()));
        m_pathEdit->setText(m_currentPath);   // 失败回写：路径栏与当前有效路径保持一致
        return;
    }
    m_currentPath = path;
    m_files = files;

    // 统一渲染：本地也用 RemoteFileModel（FtpFileInfo 统一结构）
    auto* model = qobject_cast<RemoteFileModel*>(m_table->model());
    if (!model) {
        model = new RemoteFileModel(m_table);
        m_table->setModel(model);
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

    m_breadcrumb->setText(path);
    m_pathEdit->setText(path);
    emit currentPathChanged(path);
}

void FileBrowserPanel::onTableDoubleClicked(const QModelIndex& index)
{
    if (!index.isValid()) return;
    auto* model = qobject_cast<RemoteFileModel*>(m_table->model());
    if (!model) return;
    const auto& fi = model->fileAt(index.row());
    if (fi.name == "..") {
        // 上级目录
        QString parent = m_currentPath;
        if (parent == "/" || parent.isEmpty()) return;
        int lastSlash = parent.lastIndexOf('/');
        parent = parent.left(lastSlash);
        if (parent.isEmpty()) parent = "/";
        navigateTo(parent);
        return;
    }
    if (!fi.isDir) return;
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

std::vector<FtpFileInfo> FileBrowserPanel::selectedFiles() const
{
    std::vector<FtpFileInfo> result;
    auto* model = qobject_cast<RemoteFileModel*>(m_table->model());
    if (!model) return result;
    const auto sel = m_table->selectionModel()->selectedRows();
    for (const auto& idx : sel) result.push_back(model->fileAt(idx.row()));
    return result;
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
    if (!target || !target->source()) return;
    const auto files = selectedFiles();
    if (files.empty()) { m_breadcrumb->setText(tr("未选择文件")); return; }
    const QString srcKind = m_source->sourceId();
    const QString dstKind = target->source()->sourceId();
    const QString dstPath = target->currentPath();
    if (srcKind == "local" && dstKind == "local") {
        // 本地→本地：复制
        for (const auto& f : files) {
            if (f.name == "..") continue;
            const QString srcFull = m_currentPath + "/" + QString::fromStdString(f.name);
            const QString dstFull = dstPath + "/" + QString::fromStdString(f.name);
            if (f.isDir) QDir(srcFull).mkpath(dstFull);   // 简化：目录复制仅建空目录
            else QFile::copy(srcFull, dstFull);
        }
        refresh(); target->refresh();
    } else if (srcKind == "local" && dstKind != "local") {
        // 本地→远程：上传
        for (const auto& f : files) {
            if (f.name == "..") continue;
            const QString srcFull = m_currentPath + "/" + QString::fromStdString(f.name);
            const QString dstFull = dstPath + "/" + QString::fromStdString(f.name);
            target->source()->upload(srcFull, dstFull);
        }
        target->refresh();
    } else if (srcKind != "local" && dstKind == "local") {
        // 远程→本地：下载
        for (const auto& f : files) {
            if (f.name == "..") continue;
            const QString srcFull = m_currentPath + "/" + QString::fromStdString(f.name);
            const QString dstFull = dstPath + "/" + QString::fromStdString(f.name);
            m_source->download(srcFull, dstFull);
        }
        refresh();
    } else {
        // 远程→远程：禁用提示
        m_breadcrumb->setText(tr("远程间复制暂不支持"));
    }
}

void FileBrowserPanel::moveSelectedTo(FileBrowserPanel* target)
{
    if (!target || !target->source()) return;
    const auto files = selectedFiles();
    if (files.empty()) { m_breadcrumb->setText(tr("未选择文件")); return; }
    const QString srcKind = m_source->sourceId();
    const QString dstKind = target->source()->sourceId();
    const QString dstPath = target->currentPath();
    if (srcKind == "local" && dstKind == "local") {
        // 本地→本地：复制后删源（移动语义）
        for (const auto& f : files) {
            if (f.name == "..") continue;
            const QString srcFull = m_currentPath + "/" + QString::fromStdString(f.name);
            const QString dstFull = dstPath + "/" + QString::fromStdString(f.name);
            bool copied = f.isDir ? QDir(srcFull).mkpath(dstFull) : QFile::copy(srcFull, dstFull);
            if (copied) m_source->remove(srcFull, f.isDir);
        }
        refresh(); target->refresh();
    } else if (srcKind == "local" && dstKind != "local") {
        // 本地→远程：上传后删源
        for (const auto& f : files) {
            if (f.name == "..") continue;
            const QString srcFull = m_currentPath + "/" + QString::fromStdString(f.name);
            const QString dstFull = dstPath + "/" + QString::fromStdString(f.name);
            if (target->source()->upload(srcFull, dstFull))
                m_source->remove(srcFull, f.isDir);
        }
        target->refresh(); refresh();
    } else if (srcKind != "local" && dstKind == "local") {
        // 远程→本地：下载后删源
        for (const auto& f : files) {
            if (f.name == "..") continue;
            const QString srcFull = m_currentPath + "/" + QString::fromStdString(f.name);
            const QString dstFull = dstPath + "/" + QString::fromStdString(f.name);
            if (m_source->download(srcFull, dstFull))
                m_source->remove(srcFull, f.isDir);
        }
        refresh(); target->refresh();
    } else {
        // 远程→远程：禁用提示
        m_breadcrumb->setText(tr("远程间移动暂不支持"));
    }
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
    const bool singleSel = files.size() == 1 && files.front().name != "..";
    const bool selIsDir = singleSel && files.front().isDir;

    QMenu menu(this);
    auto* enterAct   = menu.addAction(tr("进入"));           // 目录 / .. 进入
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

bool FileBrowserPanel::eventFilter(QObject* watched, QEvent* event)
{
    // 拦截表格视口上的面板间拖拽事件（与 FtpDeployWidget 的系统文件拖入同套路）
    if ((watched == m_table->viewport() || watched == m_table) && m_source) {
        switch (event->type()) {
        case QEvent::DragEnter:
            if (dragSourcePanel(static_cast<QDragEnterEvent*>(event))) {
                static_cast<QDragEnterEvent*>(event)->setDropAction(Qt::CopyAction);
                static_cast<QDragEnterEvent*>(event)->accept();
                return true;
            }
            break;
        case QEvent::DragMove:
            if (dragSourcePanel(static_cast<QDragMoveEvent*>(event))) {
                static_cast<QDragMoveEvent*>(event)->setDropAction(Qt::CopyAction);
                static_cast<QDragMoveEvent*>(event)->accept();
                return true;
            }
            break;
        case QEvent::Drop: {
            auto* drop = static_cast<QDropEvent*>(event);
            if (auto* srcPanel = dragSourcePanel(drop)) {
                srcPanel->copySelectedTo(this);   // 拖入方向语义 = 源面板复制到本面板
                drop->acceptProposedAction();
                return true;
            }
            break;
        }
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void FileBrowserPanel::dragEnterEvent(QDragEnterEvent* event)
{
    // 非表格区域（路径栏/面包屑等）上的面板间拖入
    if (dragSourcePanel(event)) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}

void FileBrowserPanel::dragMoveEvent(QDragMoveEvent* event)
{
    if (dragSourcePanel(event)) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}

void FileBrowserPanel::dropEvent(QDropEvent* event)
{
    if (auto* srcPanel = dragSourcePanel(event)) {
        srcPanel->copySelectedTo(this);
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}
