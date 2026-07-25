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

RemoteFileModel::RemoteFileModel(QObject* parent)
    : QAbstractTableModel(parent) {}

void RemoteFileModel::setFileList(const std::vector<FtpFileInfo>& files)
{
    beginResetModel();
    m_files = files;

    // 重建对比着色索引
    // TODO: 实现精确对比（大小+时间戳）后将匹配项移入 m_syncedNames 显示绿色
    m_syncedNames.clear();
    m_diffNames.clear();
    for (const auto& f : m_files) {
        if (m_localFileNames.count(f.name)) {
            m_diffNames.insert(f.name); // 简化：所有同名文件标记为差异（黄色）
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
    endResetModel();
}

void RemoteFileModel::setLocalFilesForCompare(const std::vector<std::string>& localFileNames)
{
    m_localFileNames.clear();
    for (const auto& n : localFileNames) m_localFileNames.insert(n);

    // 重新计算差异集合（同 setFileList，m_syncedNames 留给未来精确对比）
    m_syncedNames.clear();
    m_diffNames.clear();
    for (const auto& f : m_files) {
        if (m_localFileNames.count(f.name)) {
            m_diffNames.insert(f.name);
        }
    }
    if (!m_files.empty()) {
        emit dataChanged(index(0, 0), index(static_cast<int>(m_files.size()) - 1, ColCount - 1));
    }
}

int RemoteFileModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_files.size());
}

int RemoteFileModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant RemoteFileModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_files.size()))
        return {};

    const auto& fi = m_files[index.row()];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColIcon:        return fi.isDir ? QString("\U0001F4C1") : QString("\U0001F4C4");
        case ColName:        return QString::fromStdString(fi.name);
        case ColSize:        return fi.isDir ? QString() : formatSize(fi.size);
        case ColDateTime:    return QString::fromStdString(fi.dateTime);
        case ColPermissions: return QString::fromStdString(fi.permissions);
        }
    }

    if (role == Qt::BackgroundRole) {
        if (m_syncedNames.count(fi.name))
            return QColor(34, 80, 50);   // 深绿底 = 已同步
        if (m_diffNames.count(fi.name))
            return QColor(80, 70, 30);   // 深黄底 = 有差异
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
    }
    return {};
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
