/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: RemoteFileModel.cpp
 * Date: 2026-07-25
 * Author: turnarond
 *
 * Description: RemoteFileModel 实现。
 */

#include "RemoteFileModel.h"
#include <QColor>
#include <algorithm>

namespace {

// 对比态行色板（琴色信号族：diff 淡一档、only 深一档；占位行石墨比表格底更深）
// 值与 UI 稿 mockups/2026-10-14-file-mgmt-ui.html 一致
struct RowPalette { QColor diff; QColor only; QColor gap; };
RowPalette paletteFor(bool light)
{
    if (light)
        return { QColor(0xEA, 0xE5, 0xDE), QColor(0xE8, 0xDE, 0xD1), QColor(0xE2, 0xE6, 0xEC) };
    return { QColor(0x1F, 0x1A, 0x10), QColor(0x2A, 0x22, 0x14), QColor(0x08, 0x0B, 0x10) };
}

} // namespace

const FtpFileInfo& RemoteFileModel::gapSentinel()
{
    static const FtpFileInfo gap{ "__gap__", false, 0, {}, {}, {} };
    return gap;
}

const FtpFileInfo& RemoteFileModel::rowFile(int row) const
{
    if (m_compareMode) {
        if (row < 0 || row >= static_cast<int>(m_viewRows.size()))
            return gapSentinel();
        return m_viewRows[row].placeholder ? gapSentinel() : m_viewRows[row].info;
    }
    if (row < 0 || row >= static_cast<int>(m_files.size()))
        return gapSentinel();
    return m_files[row];
}

bool RemoteFileModel::isPlaceholderAt(int row) const
{
    return m_compareMode && row >= 0 && row < static_cast<int>(m_viewRows.size())
        && m_viewRows[row].placeholder;
}

void RemoteFileModel::setCompareView(const std::vector<FtpFileInfo>& source,
                                     const QVector<AlignedRow>& rows,
                                     const QString& gapCaption)
{
    beginResetModel();
    m_viewRows.clear();
    m_gapCaption = gapCaption;
    m_viewRows.reserve(rows.size());
    for (const auto& r : rows) {
        ViewRow vr;
        vr.cls = r.cls;
        if (r.sourceRow < 0 || r.sourceRow >= static_cast<int>(source.size()))
            vr.placeholder = true;
        else
            vr.info = source[r.sourceRow];
        m_viewRows.push_back(std::move(vr));
    }
    m_compareMode = true;
    endResetModel();
}

void RemoteFileModel::exitCompareView()
{
    if (!m_compareMode)
        return;
    beginResetModel();
    m_compareMode = false;
    m_viewRows.clear();
    endResetModel();
}

void RemoteFileModel::setThemeLight(bool light)
{
    if (m_lightTheme == light)
        return;
    m_lightTheme = light;
    const int rows = rowCount();
    if (rows > 0)
        emit dataChanged(index(0, 0), index(rows - 1, ColCount - 1));
}

RemoteFileModel::RemoteFileModel(QObject* parent)
    : QAbstractTableModel(parent) {}

void RemoteFileModel::setFileList(const std::vector<FtpFileInfo>& files)
{
    beginResetModel();
    m_files = files;

    // 重建对比着色索引
    m_syncedNames.clear();
    m_diffNames.clear();
    for (const auto& f : m_files) {
        auto it = m_localFiles.find(f.name);
        if (it != m_localFiles.end()) {
            if (it->second.size == f.size) {
                m_syncedNames.insert(f.name);  // 绿色：完全匹配
            } else {
                m_diffNames.insert(f.name);    // 黄色：大小不一致
            }
        }
    }
    endResetModel();
}

void RemoteFileModel::clear()
{
    beginResetModel();
    m_files.clear();
    m_syncedNames.clear();
    m_diffNames.clear();
    m_compareMode = false;
    m_viewRows.clear();
    endResetModel();
}

void RemoteFileModel::setLocalFilesForCompare(const std::vector<LocalFileInfo>& localFiles)
{
    m_localFiles.clear();
    for (const auto& f : localFiles) m_localFiles[f.name] = f;

    // 精确对比：同名 + 同 size → 绿色（已同步）
    //           同名 + 异 size → 黄色（有差异）
    m_syncedNames.clear();
    m_diffNames.clear();
    for (const auto& f : m_files) {
        auto it = m_localFiles.find(f.name);
        if (it != m_localFiles.end()) {
            if (it->second.size == f.size) {
                m_syncedNames.insert(f.name);  // 绿色
            } else {
                m_diffNames.insert(f.name);    // 黄色
            }
        }
    }
    if (!m_files.empty()) {
        emit dataChanged(index(0, 0),
            index(static_cast<int>(m_files.size()) - 1, ColCount - 1));
    }
}

int RemoteFileModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return m_compareMode ? static_cast<int>(m_viewRows.size())
                         : static_cast<int>(m_files.size());
}

int RemoteFileModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant RemoteFileModel::data(const QModelIndex& index, int role) const
{
    const int rows = rowCount();
    if (!index.isValid() || index.row() >= rows)
        return {};

    // 对比态：占位行与分类行色优先接管（非对比态维持旧绿/黄自动着色）
    if (m_compareMode && role == Qt::BackgroundRole) {
        const auto& vr = m_viewRows[index.row()];
        const auto pal = paletteFor(m_lightTheme);
        if (vr.placeholder)
            return pal.gap;
        if (vr.cls == CompareClass::Diff)
            return pal.diff;
        if (vr.cls == CompareClass::OnlyLeft || vr.cls == CompareClass::OnlyRight)
            return pal.only;
        return {};   // Same：不着色
    }
    if (!m_compareMode && role == Qt::BackgroundRole) {
        if (m_syncedNames.count(m_files[index.row()].name))
            return QColor(34, 80, 50);   // 深绿底 = 已同步
        if (m_diffNames.count(m_files[index.row()].name))
            return QColor(80, 70, 30);   // 深黄底 = 有差异
    }

    // 占位行实体展示：仅名称列给"对面独有"提示，其余列留空
    if (m_compareMode && m_viewRows[index.row()].placeholder) {
        if (role == Qt::DisplayRole && index.column() == ColName)
            return m_gapCaption;
        return {};
    }

    const auto& fi = rowFile(index.row());

    if (role == Qt::DisplayRole) {
        // 缺失值统一显示长破折号 —（U+2014）：目录无大小、Windows 格式 FTP 无权限/属主等
        static const QString kMissing{QChar(0x2014)};
        switch (index.column()) {
        case ColIcon:        return fi.isDir ? QString("\U0001F4C1") : QString("\U0001F4C4");
        case ColName:        return QString::fromStdString(fi.name);
        case ColSize:        return fi.isDir ? kMissing : formatSize(fi.size);
        case ColDateTime:    return fi.dateTime.empty() ? kMissing : QString::fromStdString(fi.dateTime);
        case ColPermissions: return fi.permissions.empty() ? kMissing : QString::fromStdString(fi.permissions);
        case ColOwner:       return fi.owner.empty() ? kMissing : QString::fromStdString(fi.owner);
        }
    }

    if (role == Qt::ForegroundRole) {
        if (fi.isDir && index.column() == ColName)
            return QColor("#F0A030"); // 琴色目录名
    }

    if (role == Qt::TextAlignmentRole) {
        if (index.column() == ColSize)
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    }

    if (role == Qt::ToolTipRole && index.column() == ColName) {
        return QString::fromStdString(fi.name);
    }

    return {};
}

Qt::ItemFlags RemoteFileModel::flags(const QModelIndex& index) const
{
    const auto f = QAbstractTableModel::flags(index);
    if (index.isValid() && isPlaceholderAt(index.row()))
        return f & ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
    return f;
}

QVariant RemoteFileModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case ColIcon:        return QString();
    case ColName:        return QStringLiteral("名称");
    case ColSize:        return QStringLiteral("大小");
    case ColDateTime:    return QStringLiteral("修改时间");
    case ColPermissions: return QStringLiteral("权限");
    case ColOwner:       return QStringLiteral("所有者");
    }
    return {};
}

void RemoteFileModel::sort(int column, Qt::SortOrder order)
{
    // 对比态禁排：对齐行序即语义（占位与配对关系不得被打乱）
    if (m_compareMode) return;
    if (m_files.empty()) return;

    // 分离 . 和 .. （始终置顶）
    std::vector<FtpFileInfo> dots, rest;
    for (auto& f : m_files) {
        if (f.name == "." || f.name == "..") dots.push_back(f);
        else rest.push_back(f);
    }

    // 目录优先，再按列排序
    std::sort(rest.begin(), rest.end(),
        [column, order](const FtpFileInfo& a, const FtpFileInfo& b) {
            // 目录始终排在文件前面
            if (a.isDir != b.isDir) return a.isDir;

            bool less = false, eq = false;
            switch (column) {
            case ColName: {
                const int cmp = QString::compare(QString::fromStdString(a.name),
                    QString::fromStdString(b.name), Qt::CaseInsensitive);
                less = cmp < 0;
                eq = cmp == 0;
                break;
            }
            case ColSize:          less = a.size < b.size; eq = a.size == b.size; break;
            case ColDateTime:      less = a.dateTime < b.dateTime; eq = a.dateTime == b.dateTime; break;
            case ColPermissions:   less = a.permissions < b.permissions; eq = a.permissions == b.permissions; break;
            case ColOwner:         less = a.owner < b.owner; eq = a.owner == b.owner; break;
            default:               less = a.name < b.name; eq = a.name == b.name; break;
            }
            // 相等键：升序/降序均返回 false（严格弱序不可反身性，旧降序分支 comp(a,b) 与 comp(b,a) 双向 true 系 UB）
            if (eq) return false;
            return order == Qt::AscendingOrder ? less : !less;
        });

    beginResetModel();
    m_files.clear();
    m_files.insert(m_files.end(), dots.begin(), dots.end());
    m_files.insert(m_files.end(), rest.begin(), rest.end());
    endResetModel();
}

QString RemoteFileModel::formatSize(uint64_t bytes)
{
    if (bytes >= 1073741824)
        return QString::number(bytes / 1073741824.0, 'f', 1) + " GB";
    if (bytes >= 1048576)
        return QString::number(bytes / 1048576.0, 'f', 1) + " MB";
    if (bytes >= 1024)
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    return QString::number(bytes) + " B";
}
