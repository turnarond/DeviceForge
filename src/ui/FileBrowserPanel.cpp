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
        return;
    }
    m_currentPath = path;
    m_files = files;

    // 统一渲染：本地也用 RemoteFileModel（FtpFileInfo 统一结构）
    auto* model = qobject_cast<RemoteFileModel*>(m_table->model());
    if (!model) {
        model = new RemoteFileModel(m_table);
        m_table->setModel(model);
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
