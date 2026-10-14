/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: RemoteFileModel.h
 * Date: 2026-07-25
 * Author: turnarond
 *
 * Description: 远程 FTP 目录表格数据模型 — QAbstractTableModel 子类，
 *              提供 6 列视图（图标/名称/大小/修改时间/权限/所有者），
 *              支持与本地文件列表对比着色（绿=已同步/黄=有差异）。
 */

#pragma once
#include <QAbstractTableModel>
#include <vector>
#include <set>
#include <map>
#include "FtpFileInfo.h"
#include "ui/DirCompare.h"

class RemoteFileModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { ColIcon = 0, ColName, ColSize, ColDateTime, ColPermissions, ColOwner, ColCount };

    explicit RemoteFileModel(QObject* parent = nullptr);

    void setFileList(const std::vector<FtpFileInfo>& files);
    void clear();
    const FtpFileInfo& fileAt(int row) const { return rowFile(row); }
    int fileCount() const { return rowCount(); }
    const std::vector<FtpFileInfo>& files() const { return m_files; }

    // 设置本地文件信息列表用于精确对比（name + size）
    void setLocalFilesForCompare(const std::vector<LocalFileInfo>& localFiles);

    // --- 目录对比视图（v2.12）：宿主计算 AlignedRow 序列后注入 ---
    // source 为本侧当前完整列表（与 files() 同源）；sourceRow<0 的行渲染占位行，
    // 占位行名称列显示 gapCaption（如"远端独有"），不可选不可激活。
    void setCompareView(const std::vector<FtpFileInfo>& source,
                        const QVector<AlignedRow>& rows,
                        const QString& gapCaption);
    void exitCompareView();
    bool isCompareMode() const { return m_compareMode; }
    bool isPlaceholderAt(int row) const;
    void setThemeLight(bool light);   // 对比行色板按主题切换（默认深色）

    // QAbstractTableModel
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

private:
    static QString formatSize(uint64_t size);
    // 行 → 条目（对比模式取视图行；占位行返回哨兵：name="__gap__"、isDir=false）
    const FtpFileInfo& rowFile(int row) const;

    struct ViewRow {
        FtpFileInfo info;
        bool placeholder = false;
        CompareClass cls = CompareClass::Same;
    };

    std::vector<FtpFileInfo> m_files;
    std::map<std::string, LocalFileInfo> m_localFiles; // name → info
    std::set<std::string> m_syncedNames;
    std::set<std::string> m_diffNames;

    bool m_compareMode = false;
    std::vector<ViewRow> m_viewRows;
    QString m_gapCaption;
    bool m_lightTheme = false;
    static const FtpFileInfo& gapSentinel();
};
