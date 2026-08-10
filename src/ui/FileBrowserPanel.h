#pragma once
#include <QWidget>
#include <memory>
#include <vector>
#include "tools/FtpDeployTool/FtpFileInfo.h"

class QTableView;
class QLineEdit;
class QLabel;
class QPoint;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
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

    // --- Task 3：面板操作 ---
    void setPeerPanel(FileBrowserPanel* peer) { m_peerPanel = peer; }
    void renameSelected();                        // F2：重命名
    void copySelectedTo(FileBrowserPanel* target); // F5：复制到目标面板（方向语义）
    void moveSelectedTo(FileBrowserPanel* target); // F6：移动到目标面板（方向语义）
    void showContextMenu(const QPoint& pos);      // 统一右键菜单（新建/重命名/删除/传输/复制路径/刷新）

public slots:
    void navigateTo(const QString& path);
    void refresh();

signals:
    void currentPathChanged(const QString& path);
    void selectionChanged();
    void directoryActivated(const FtpFileInfo& info);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;   // 拦截表格视口拖拽事件
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void setupUi();
    void loadDirectory(const QString& path);
    void onTableDoubleClicked(const QModelIndex& index);
    void onPathEnterPressed();
    // 拖拽来源面板判定：来源必须是另一个 FileBrowserPanel（含其表格视口），否则返回 nullptr
    FileBrowserPanel* dragSourcePanel(const QDropEvent* event) const;

    std::shared_ptr<IFileSource> m_source;
    FileBrowserPanel* m_peerPanel = nullptr;   // 对面面板（宿主注入）
    QLineEdit*  m_pathEdit = nullptr;          // 面板顶部路径栏
    QTableView* m_table = nullptr;             // 统一表格视图
    QLabel*     m_breadcrumb = nullptr;        // 底部面包屑（路径文本，含 / 分隔可点击——首版文本展示）
    QString     m_currentPath;
    std::vector<FtpFileInfo> m_files;
    bool m_refreshing = false;
};
