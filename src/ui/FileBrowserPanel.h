#pragma once
#include <QWidget>
#include <QHash>
#include <QPointer>
#include <QStringList>
#include <QUuid>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include "transfer/TransferEvents.h"
#include "tools/FtpDeployTool/FtpFileInfo.h"

class QTableView;
class QLineEdit;
class QLabel;
class QComboBox;
class QProgressBar;
class QPushButton;
class QPoint;
class QUrl;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class IFileSource;

class FileBrowserPanel : public QWidget {
    Q_OBJECT
public:
    enum class DropRoute { PanelTransfer, SystemUpload, Rejected };
    using TransferSubmitter = std::function<QUuid(TransferTask)>;
    using TransferCanceller = std::function<void(const QUuid&)>;
    using OverwritePolicyChooser =
        std::function<std::optional<OverwritePolicy>(int conflictCount)>;

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
    void setTransferSubmitter(TransferSubmitter submitter);
    void setTransferCanceller(TransferCanceller canceller);
    void setOverwritePolicyChooser(OverwritePolicyChooser chooser);
    void renameSelected();                        // F2：重命名
    void copySelectedTo(FileBrowserPanel* target); // F5：复制到目标面板（方向语义）
    void moveSelectedTo(FileBrowserPanel* target); // F6：移动到目标面板（方向语义）
    void showContextMenu(const QPoint& pos);      // 统一右键菜单（新建/重命名/删除/传输/复制路径/刷新）
    void cancelActiveTransfer();
    void resumeLastTransfer();
    // 可由 Scheduler 的任意发布线程调用；实现会把值快照排队到本面板 GUI 线程。
    void consumeTransferEvent(const TransferEvent& event);

public slots:
    void navigateTo(const QString& path);
    void refresh();

signals:
    void currentPathChanged(const QString& path);
    void selectionChanged();
    void directoryActivated(const FtpFileInfo& info);
    // 源选择器变化（仅用户操作发射；程序化设置 blockSignals 不发射）：proto = local/ftp/sftp
    void sourceChooserChanged(const QString& proto, const QString& device);
    void transferSnapshotApplied();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;   // 拦截表格视口拖拽事件
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    // 把 Qt DnD 的来源识别与业务分支分离，便于覆盖三种拖拽路径。
    DropRoute handleDrop(FileBrowserPanel* sourcePanel, const QList<QUrl>& urls);
    // viewport 与面板非表格区域共用同一接受判定，避免 drag enter/move
    // 先展示可落地反馈、最终 drop 却拒绝。
    bool canAcceptDrag(const FileBrowserPanel* sourcePanel, bool hasUrls) const;

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
    void enterSelectedDirectory();
    void navigateToParent();
    QUuid submitSelectedTransfer(FileBrowserPanel* target, bool move);
    QUuid submitTransferTask(TransferTask task,
                             FileBrowserPanel* sourcePanel,
                             FileBrowserPanel* targetPanel);
    std::optional<OverwritePolicy> chooseOverwritePolicy(FileBrowserPanel* target,
                                                         const QVector<TransferItemRequest>& items) const;
    void applyTransferEvent(const TransferEvent& event);
    // 拖拽来源面板判定：来源必须是另一个 FileBrowserPanel（含其表格视口），否则返回 nullptr
    FileBrowserPanel* dragSourcePanel(const QDropEvent* event) const;

    std::shared_ptr<IFileSource> m_source;
    FileBrowserPanel* m_peerPanel = nullptr;   // 对面面板（宿主注入）
    QComboBox*  m_sourceCombo = nullptr;       // 源类型（本地/FTP/SFTP，data role 存 local/ftp/sftp）
    QComboBox*  m_deviceCombo = nullptr;       // 面板级设备（远程源时显示，宿主填充设备列表）
    QLineEdit*  m_pathEdit = nullptr;          // 面板顶部路径栏
    QTableView* m_table = nullptr;             // 统一表格视图
    QLabel*     m_breadcrumb = nullptr;        // 底部面包屑（路径文本，含 / 分隔可点击——首版文本展示）
    QLabel*     m_transferStatus = nullptr;    // Scheduler 快照状态（仅 GUI 线程更新）
    QProgressBar* m_transferProgress = nullptr;
    QPushButton* m_transferCancelButton = nullptr;
    QPushButton* m_transferRetryButton = nullptr;
    QString     m_currentPath;
    QString     m_restoreSelectionName;
    quint64     m_restoreSelectionGeneration = 0;
    std::vector<FtpFileInfo> m_files;
    quint64 m_loadGeneration = 0;      // 代际令牌：每次导航/刷新/源切换 ++
    quint64 m_transferGeneration = 0;

    struct PendingTransfer {
        TransferTask task;
        QPointer<FileBrowserPanel> sourcePanel;
        QPointer<FileBrowserPanel> targetPanel;
        quint64 sourceLoadGeneration = 0;
        quint64 targetLoadGeneration = 0;
    };
    QHash<QUuid, PendingTransfer> m_pendingTransfers;
    QUuid m_activeTransferId;
    std::optional<PendingTransfer> m_lastTransfer;
    TransferTaskSnapshot m_lastTransferSnapshot;
    TransferSubmitter m_transferSubmitter;
    TransferCanceller m_transferCanceller;
    OverwritePolicyChooser m_overwritePolicyChooser;
};
