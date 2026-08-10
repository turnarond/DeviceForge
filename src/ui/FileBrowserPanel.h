#pragma once
#include <QWidget>
#include <memory>
#include <vector>
#include "tools/FtpDeployTool/FtpFileInfo.h"

class QTableView;
class QLineEdit;
class QLabel;
class IFileSource;

class FileBrowserPanel : public QWidget {
    Q_OBJECT
public:
    explicit FileBrowserPanel(QWidget* parent = nullptr);

    void setSource(std::shared_ptr<IFileSource> source);
    std::shared_ptr<IFileSource> source() const { return m_source; }
    QString currentPath() const { return m_currentPath; }
    QTableView* fileTable() const { return m_table; }
    std::vector<FtpFileInfo> selectedFiles() const;

public slots:
    void navigateTo(const QString& path);
    void refresh();

signals:
    void currentPathChanged(const QString& path);
    void selectionChanged();
    void directoryActivated(const FtpFileInfo& info);

private:
    void setupUi();
    void loadDirectory(const QString& path);
    void onTableDoubleClicked(const QModelIndex& index);
    void onPathEnterPressed();

    std::shared_ptr<IFileSource> m_source;
    QLineEdit*  m_pathEdit = nullptr;       // 面板顶部路径栏
    QTableView* m_table = nullptr;          // 统一表格视图
    QLabel*     m_breadcrumb = nullptr;     // 底部面包屑（路径文本，含 / 分隔可点击——首版文本展示）
    QString     m_currentPath;
    std::vector<FtpFileInfo> m_files;
    bool m_refreshing = false;
};
