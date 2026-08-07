/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: RemoteFileModel.h
 * Date: 2026-07-25
 * Author: turnarond
 *
 * Description: 远程 FTP 目录表格数据模型 — QAbstractTableModel 子类，
 *              提供 5 列视图（图标/名称/大小/修改时间/权限），
 *              支持与本地文件列表对比着色（绿=已同步/黄=有差异）。
 */

#pragma once
#include <QAbstractTableModel>
#include <vector>
#include <set>
#include <map>
#include "FtpFileInfo.h"

class RemoteFileModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { ColIcon = 0, ColName, ColSize, ColDateTime, ColPermissions, ColCount };

    explicit RemoteFileModel(QObject* parent = nullptr);

    void setFileList(const std::vector<FtpFileInfo>& files);
    void clear();
    const FtpFileInfo& fileAt(int row) const { return m_files.at(row); }
    int fileCount() const { return static_cast<int>(m_files.size()); }

    // 设置本地文件信息列表用于精确对比（name + size）
    void setLocalFilesForCompare(const std::vector<LocalFileInfo>& localFiles);

    // QAbstractTableModel
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

private:
    static QString formatSize(uint64_t bytes);

    std::vector<FtpFileInfo> m_files;
    std::map<std::string, LocalFileInfo> m_localFiles; // name → info
    std::set<std::string> m_syncedNames;
    std::set<std::string> m_diffNames;
};
