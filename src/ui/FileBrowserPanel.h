#pragma once
#include <QWidget>
#include <QStringList>
#include <memory>
#include <vector>
#include "tools/FtpDeployTool/FtpFileInfo.h"

class QTableView;
class QLineEdit;
class QLabel;
class QComboBox;
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

    // --- Task 3：面板源选择器 ---
    void setSourceChooserVisible(bool visible);          // 源选择器整行显示/隐藏
    void setSourceProto(const QString& proto);           // "local"/"ftp"/"sftp"，程序化设置（blockSignals 防循环）
    void setSourceDevice(const QString& device);         // 面板级设备下拉（blockSignals）
    void setSourceDevices(const QStringList& devices);   // 设备列表重填（保留当前选择，blockSignals）
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
    // 源选择器变化（仅用户操作发射；程序化设置 blockSignals 不发射）：proto = local/ftp/sftp
    void sourceChooserChanged(const QString& proto, const QString& device);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;   // 拦截表格视口拖拽事件
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void setupUi();
    void loadDirectory(const QString& path);
    // --- 异步加载（代际令牌防竞态）---
    void applyFileList(const QString& path, const std::vector<FtpFileInfo>& files);
    void retryWithReconnect(quint64 gen, const QString& path,
                            std::shared_ptr<IFileSource> source);
    void showLoadError(quint64 gen, const QString& err);
    void onTableDoubleClicked(const QModelIndex& index);
    void onPathEnterPressed();
    // 拖拽来源面板判定：来源必须是另一个 FileBrowserPanel（含其表格视口），否则返回 nullptr
    FileBrowserPanel* dragSourcePanel(const QDropEvent* event) const;

    std::shared_ptr<IFileSource> m_source;
    FileBrowserPanel* m_peerPanel = nullptr;   // 对面面板（宿主注入）
    QComboBox*  m_sourceCombo = nullptr;       // 源类型（本地/FTP/SFTP，data role 存 local/ftp/sftp）
    QComboBox*  m_deviceCombo = nullptr;       // 面板级设备（远程源时显示，宿主填充设备列表）
    QLineEdit*  m_pathEdit = nullptr;          // 面板顶部路径栏
    QTableView* m_table = nullptr;             // 统一表格视图
    QLabel*     m_breadcrumb = nullptr;        // 底部面包屑（路径文本，含 / 分隔可点击——首版文本展示）
    QString     m_currentPath;
    std::vector<FtpFileInfo> m_files;
    quint64 m_loadGeneration = 0;      // 代际令牌：每次导航/刷新/源切换 ++
};
