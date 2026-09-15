#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QTableView>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>

#include "ui/IFileSource.h"
#include "ui/FileBrowserPanel.h"
#include "ui/LocalFileSource.h"
#include "transfer/TransferEvents.h"
#include "tools/FtpDeployTool/FtpFileInfo.h"
#include "tools/FtpDeployTool/RemoteFileModel.h"

// 可控延迟的 Mock 源：list 返回可配置结果，延迟由 blockList 门控
//（阻塞自旋模拟慢速/断网目录读取；failNextList 模拟首次失败以触发重连重试）
class MockDelayedSource : public IFileSource {
public:
    QString sourceId() const override { return m_sourceId; }
    QString displayName() const override { return QStringLiteral("mock"); }

    std::vector<FtpFileInfo> list(const QString& path) override {
        ++listCalls;
        if (blockList.load()) {
            while (blockList.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (failNextList.exchange(false)) {
            m_err = QStringLiteral("mock: list failed");
            return {};
        }
        m_err.clear();
        if (path == QStringLiteral("/a")) return m_filesA;   // 竞态用例：按路径区分结果
        if (path == QStringLiteral("/b")) return m_filesB;
        return m_files;
    }
    bool mkdir(const QString&) override { return true; }
    bool rename(const QString&, const QString&) override { return true; }
    bool remove(const QString&, bool) override { ++removeCalls; return true; }
    bool clearDirectory(const QString&) override { return true; }
    bool upload(const QString&, const QString&) override { ++uploadCalls; return true; }
    bool download(const QString&, const QString&) override { ++downloadCalls; return true; }
    bool connect(const DeviceInfo&, const AuthInfo&) override { return true; }
    bool reconnect() override {
        ++reconnectCalls;
        if (failReconnect.load()) { m_err = QStringLiteral("mock: reconnect failed"); return false; }
        m_err.clear();
        return true;
    }
    bool isConnected() const override { return true; }
    QString lastError() const override { return m_err; }
    void setProgressCallback(std::function<void(int)>) override {}
    void setCancelFlag(std::atomic<bool>*) override {}

    std::vector<FtpFileInfo> m_files;    // 默认路径（/ 等）结果
    std::vector<FtpFileInfo> m_filesA;   // /a 结果
    std::vector<FtpFileInfo> m_filesB;   // /b 结果
    QString m_err;
    QString m_sourceId = QStringLiteral("mock");
    std::atomic<int> listCalls{0};
    std::atomic<int> reconnectCalls{0};
    std::atomic<int> uploadCalls{0};
    std::atomic<int> downloadCalls{0};
    std::atomic<int> removeCalls{0};
    std::atomic<bool> blockList{false};
    std::atomic<bool> failNextList{false};
    std::atomic<bool> failReconnect{false};
};

class TestableFileBrowserPanel : public FileBrowserPanel {
public:
    using FileBrowserPanel::FileBrowserPanel;
    using FileBrowserPanel::dragEnterEvent;
    using FileBrowserPanel::dragMoveEvent;
    using FileBrowserPanel::eventFilter;
    using FileBrowserPanel::handleDrop;
    using FileBrowserPanel::canAcceptDrag;
};

static FtpFileInfo makeInfo(const char* name, bool isDir = false)
{
    FtpFileInfo f;
    f.name = name;
    f.isDir = isDir;
    return f;
}

// 等待初始加载完全落盘（模型创建 + 首行应用）。
// 仅判 currentPath 不够：setSource 同步置当前路径，异步 worker 可能仍在途，
// 其 list() 与后续导航的 list() 并发会交错 mock 共享状态（m_err），破坏确定性。
// 注意：qobject_cast 必须写在 QTRY 表达式内（QTRY 只重求值表达式本身，
// 提前捕获的指针不会随模型创建而更新）。
static void settleInitialLoad(FileBrowserPanel& panel)
{
    QTRY_VERIFY_WITH_TIMEOUT(
        qobject_cast<RemoteFileModel*>(panel.fileTable()->model()) != nullptr, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        qobject_cast<RemoteFileModel*>(panel.fileTable()->model())->fileCount() >= 1, 5000);
}

static int rowForName(FileBrowserPanel& panel, const char* name)
{
    auto* model = qobject_cast<RemoteFileModel*>(panel.fileTable()->model());
    if (!model) return -1;
    for (int row = 0; row < model->rowCount({}); ++row) {
        if (model->fileAt(row).name == name)
            return row;
    }
    return -1;
}

static void selectRows(FileBrowserPanel& panel, const QList<int>& rows)
{
    auto* selection = panel.fileTable()->selectionModel();
    QVERIFY(selection);
    selection->clearSelection();
    for (int row : rows) {
        const QModelIndex index = panel.fileTable()->model()->index(row, 0);
        selection->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        selection->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
    }
}

// loadDirectory 是 private——通过 FileBrowserPanel 公开 API（setSource/navigateTo）驱动。
// 面板是 GUI 组件（QWidget），QTEST_MAIN 提供 QApplication + 事件循环（QTRY_ 宏）；
// QtConcurrent 的队列回调经事件循环投递到主线程，与真实运行路径一致。
// 断言核心：① 异步 list 结果应用（模型行/信号/面包屑）；② 两次快速导航竞态 → 代际令牌
// 丢弃过期结果；③ list 失败 → 异步重连重试一次；④ 重连亦失败 → showLoadError 回写。
class TstPanelAsync : public QObject {
    Q_OBJECT
private slots:
    // 用例 1：异步 list 结果应用（导航 → currentPathChanged + 表格行 + 面包屑）
    void asyncList_appliesResult()
    {
        auto src = std::make_shared<MockDelayedSource>();
        src->m_files = { makeInfo("a.txt") };
        FileBrowserPanel panel;
        QSignalSpy spy(&panel, &FileBrowserPanel::currentPathChanged);
        panel.setSource(src);   // sourceId()=="mock" → 初始导航 "/"
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
        QCOMPARE(panel.currentPath(), QStringLiteral("/"));
        auto* model = qobject_cast<RemoteFileModel*>(panel.fileTable()->model());
        QVERIFY(model);                       // 模型在异步回调 applyFileList 中惰性创建
        QCOMPARE(model->fileCount(), 2);      // .. + a.txt

        // 再次导航：异步应用新结果
        src->m_files = { makeInfo("sub.txt", true) };
        panel.navigateTo(QStringLiteral("/sub"));
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 5000);
        QCOMPARE(panel.currentPath(), QStringLiteral("/sub"));
        QCOMPARE(model->fileCount(), 2);      // .. + sub.txt
        auto* breadcrumb = panel.findChild<QLabel*>("panelBreadcrumb");
        QVERIFY(breadcrumb);
        QCOMPARE(breadcrumb->text(), QStringLiteral("/sub"));
    }

    // 用例 2：快速连续导航竞态 → 代际令牌丢弃过期结果，最终显示第二次结果
    void raceStaleDropped()
    {
        auto src = std::make_shared<MockDelayedSource>();
        src->m_filesA = { makeInfo("a.txt") };
        src->m_filesB = { makeInfo("b.txt") };
        FileBrowserPanel panel;
        panel.setSource(src);
        settleInitialLoad(panel);

        // 门控延迟：首个 list 未返回时发起第二次导航（两次导航交错）
        src->blockList = true;
        panel.navigateTo(QStringLiteral("/a"));   // gen N+1：worker 阻塞在 blockList 自旋
        panel.navigateTo(QStringLiteral("/b"));   // gen N+2
        QTRY_COMPARE_WITH_TIMEOUT(src->listCalls.load(), 3, 5000);  // 初始 / + /a + /b 均已进入
        src->blockList = false;                   // 放行 → 两个回调先后到达

        // 最终应用第二次导航结果；过期 /a 结果被代际令牌丢弃
        QTRY_VERIFY_WITH_TIMEOUT(panel.currentPath() == QStringLiteral("/b"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(src->listCalls.load(), 3, 5000);  // worker 全部退出（析构安全）
        auto* model = qobject_cast<RemoteFileModel*>(panel.fileTable()->model());
        QVERIFY(model);
        QTRY_COMPARE(model->fileCount(), 2);      // .. + b.txt
        for (int r = 0; r < model->rowCount({}); ++r) {
            QVERIFY2(model->fileAt(r).name != "a.txt", "过期导航结果不应应用");
        }
        auto* breadcrumb = panel.findChild<QLabel*>("panelBreadcrumb");
        QVERIFY(breadcrumb);
        QCOMPARE(breadcrumb->text(), QStringLiteral("/b"));
        QTest::qWait(50);   // 排空已投递的过期回调（避免析构期残留）
    }

    // 用例 3：list 失败 → 异步重连重试一次（reconnectCalls==1），成功后应用结果
    void reconnectOnFailure()
    {
        auto src = std::make_shared<MockDelayedSource>();
        src->m_files = { makeInfo("a.txt") };
        FileBrowserPanel panel;
        panel.setSource(src);
        settleInitialLoad(panel);
        const int callsBefore = src->listCalls.load();

        src->failNextList = true;   // 下一次 list 失败（置错 + 空结果）→ 触发重连重试
        panel.navigateTo(QStringLiteral("/"));
        QTRY_VERIFY_WITH_TIMEOUT(src->reconnectCalls.load() == 1, 5000);   // 仅重试一次
        QTRY_COMPARE_WITH_TIMEOUT(src->listCalls.load(), callsBefore + 2, 5000);  // 首败 + 重试成功
        QCOMPARE(src->lastError().isEmpty(), true);
        QCOMPARE(panel.currentPath(), QStringLiteral("/"));
        auto* model = qobject_cast<RemoteFileModel*>(panel.fileTable()->model());
        QVERIFY(model);
        QCOMPARE(model->fileCount(), 2);   // .. + a.txt（重试结果已应用）
    }

    // 用例 4：重连亦失败 → showLoadError（面包屑「加载失败」+ 路径栏回写当前有效路径）
    void retryFail_showsLoadError()
    {
        auto src = std::make_shared<MockDelayedSource>();
        src->m_files = { makeInfo("a.txt") };
        FileBrowserPanel panel;
        panel.setSource(src);
        settleInitialLoad(panel);

        src->failNextList = true;    // list 失败 → 触发重试
        src->failReconnect = true;   // 重连亦失败
        panel.navigateTo(QStringLiteral("/x"));
        // 首断言等回调落地：showLoadError 是整条链（list 失败→重连→回写）的最后一步，
        // 面包屑出现「加载失败」即证明回调已处理，其余状态（重连次数/路径回写）随后必然就位
        auto* breadcrumb = panel.findChild<QLabel*>("panelBreadcrumb");
        QVERIFY(breadcrumb);
        QTRY_VERIFY_WITH_TIMEOUT(
            breadcrumb->text().contains(QStringLiteral("加载失败")), 5000);
        QVERIFY(breadcrumb->text().contains(QStringLiteral("mock: reconnect failed")));

        // 回调已处理：重连恰好重试一次；重连失败短路不再 list（首败 + 无重试 list）
        QCOMPARE(src->reconnectCalls.load(), 1);
        QCOMPARE(src->listCalls.load(), 2);
        // 失败导航不改变当前有效路径；路径栏回写为上一个有效路径
        QCOMPARE(panel.currentPath(), QStringLiteral("/"));
        auto* pathEdit = panel.findChild<QLineEdit*>();
        QVERIFY(pathEdit);
        QCOMPARE(pathEdit->text(), QStringLiteral("/"));
    }

    // ============================================================
    // Task 3：源选择器状态机（联动逻辑在 FtpDeployWidget 宿主，面板侧单测覆盖：
    // 默认状态 / 用户切换发射信号 / 程序化设置静默（blockSignals）/ setSource 显示同步）
    // ============================================================

    // 用例 5：默认状态 — 源下拉三项（data role local/ftp/sftp），本地默认 + 设备下拉隐藏
    void sourceChooser_defaults()
    {
        FileBrowserPanel panel;
        auto* srcCombo = panel.findChild<QComboBox*>("panelSourceCombo");
        auto* devCombo = panel.findChild<QComboBox*>("panelDeviceCombo");
        QVERIFY(srcCombo);
        QVERIFY(devCombo);
        QCOMPARE(srcCombo->count(), 3);
        QCOMPARE(srcCombo->itemData(0).toString(), QStringLiteral("local"));
        QCOMPARE(srcCombo->itemData(1).toString(), QStringLiteral("ftp"));
        QCOMPARE(srcCombo->itemData(2).toString(), QStringLiteral("sftp"));
        QCOMPARE(srcCombo->currentData().toString(), QStringLiteral("local"));  // 默认本地
        QVERIFY(devCombo->isHidden());   // 本地源设备下拉隐藏
    }

    // 用例 6：用户切换源/设备 → 发射 sourceChooserChanged(proto, device) + 设备下拉可见性
    void sourceChooser_userChange_emits()
    {
        FileBrowserPanel panel;
        panel.setSourceDevices({QStringLiteral("192.168.1.10"),
                                QStringLiteral("192.168.1.11")});
        auto* srcCombo = panel.findChild<QComboBox*>("panelSourceCombo");
        auto* devCombo = panel.findChild<QComboBox*>("panelDeviceCombo");
        QVERIFY(srcCombo && devCombo);

        QSignalSpy spy(&panel, &FileBrowserPanel::sourceChooserChanged);
        srcCombo->setCurrentIndex(1);   // FTP
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("ftp"));
        QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("192.168.1.10"));  // 首台设备
        QVERIFY(!devCombo->isHidden());   // 远程源显示设备下拉

        devCombo->setCurrentIndex(1);   // 设备切换同样上报（携带当前源）
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toString(), QStringLiteral("ftp"));
        QCOMPARE(spy.at(1).at(1).toString(), QStringLiteral("192.168.1.11"));

        srcCombo->setCurrentIndex(2);   // SFTP（保持当前设备）
        QCOMPARE(spy.count(), 3);
        QCOMPARE(spy.at(2).at(0).toString(), QStringLiteral("sftp"));
        QCOMPARE(spy.at(2).at(1).toString(), QStringLiteral("192.168.1.11"));

        srcCombo->setCurrentIndex(0);   // 回本地 → 设备下拉隐藏
        QCOMPARE(spy.count(), 4);
        QCOMPARE(spy.at(3).at(0).toString(), QStringLiteral("local"));
        QVERIFY(devCombo->isHidden());
    }

    // 用例 7：程序化设置（setSourceProto/setSourceDevice/setSourceDevices）静默
    // （blockSignals 防循环——宿主双向联动依赖此语义）
    void sourceChooser_programmatic_silent()
    {
        FileBrowserPanel panel;
        panel.setSourceDevices({QStringLiteral("192.168.1.10"),
                                QStringLiteral("192.168.1.11")});
        auto* srcCombo = panel.findChild<QComboBox*>("panelSourceCombo");
        auto* devCombo = panel.findChild<QComboBox*>("panelDeviceCombo");
        QVERIFY(srcCombo && devCombo);
        QSignalSpy spy(&panel, &FileBrowserPanel::sourceChooserChanged);

        panel.setSourceProto(QStringLiteral("sftp"));
        QCOMPARE(spy.count(), 0);                        // 不发射
        QCOMPARE(srcCombo->currentData().toString(), QStringLiteral("sftp"));
        QVERIFY(!devCombo->isHidden());

        panel.setSourceDevice(QStringLiteral("192.168.1.11"));
        QCOMPARE(spy.count(), 0);
        QCOMPARE(devCombo->currentText(), QStringLiteral("192.168.1.11"));

        panel.setSourceProto(QStringLiteral("ssh"));   // 归一化：ssh（SshAdapter 键）→ sftp 显示
        QCOMPARE(spy.count(), 0);
        QCOMPARE(srcCombo->currentData().toString(), QStringLiteral("sftp"));
        QVERIFY(!devCombo->isHidden());

        panel.setSourceDevices({QStringLiteral("192.168.1.10"),
                                QStringLiteral("192.168.1.11"),
                                QStringLiteral("192.168.1.12")});
        QCOMPARE(spy.count(), 0);
        QCOMPARE(devCombo->currentText(), QStringLiteral("192.168.1.11"));  // 重填保留选择

        panel.setSourceProto(QStringLiteral("local"));
        QCOMPARE(spy.count(), 0);
        QVERIFY(devCombo->isHidden());
    }

    // 用例 8：setSource 显示同步 — 选择器跟随实际源（blockSignals 不发射；防挂载回环）
    void sourceChooser_setSource_syncs()
    {
        FileBrowserPanel panel;
        panel.setSource(std::make_shared<LocalFileSource>());
        settleInitialLoad(panel);   // 排空异步 list（析构期 worker 不再触碰本面板）
        auto* srcCombo = panel.findChild<QComboBox*>("panelSourceCombo");
        auto* devCombo = panel.findChild<QComboBox*>("panelDeviceCombo");
        QVERIFY(srcCombo && devCombo);
        QCOMPARE(srcCombo->currentData().toString(), QStringLiteral("local"));
        QVERIFY(devCombo->isHidden());

        QSignalSpy spy(&panel, &FileBrowserPanel::sourceChooserChanged);
        QCOMPARE(spy.count(), 0);   // setSource 程序化挂载不发射
    }

    // F5/F6 及拖拽只构造可靠传输任务，旧 IFileSource 同步传输入口不得再被调用。
    void transferEntrypoints_submitWithoutSynchronousIo()
    {
        auto local = std::make_shared<MockDelayedSource>();
        local->m_sourceId = QStringLiteral("local");
        local->m_files = {makeInfo("a.bin"), makeInfo("b.bin")};
        auto remote = std::make_shared<MockDelayedSource>();
        remote->m_sourceId = QStringLiteral("ftp");

        TestableFileBrowserPanel left;
        TestableFileBrowserPanel right;
        left.setPeerPanel(&right);
        right.setPeerPanel(&left);
        left.setSource(local);
        right.setSource(remote);
        settleInitialLoad(left);
        settleInitialLoad(right);

        QVector<TransferTask> submitted;
        const auto submitter = [&submitted](TransferTask task) {
            submitted.push_back(std::move(task));
            return QUuid::createUuid();
        };
        left.setTransferSubmitter(submitter);
        right.setTransferSubmitter(submitter);

        selectRows(left, {rowForName(left, "a.bin")});
        left.copySelectedTo(&right);
        QCOMPARE(submitted.size(), 1);
        QCOMPARE(submitted.back().items.size(), 1);
        QCOMPARE(submitted.back().items.front().direction, TransferDirection::Upload);
        QVERIFY(!submitted.back().removeSourceAfterCommit);

        left.moveSelectedTo(&right);
        QCOMPARE(submitted.size(), 2);
        QVERIFY(submitted.back().removeSourceAfterCommit);

        QCOMPARE(right.handleDrop(&left, {}), FileBrowserPanel::DropRoute::PanelTransfer);
        QCOMPARE(submitted.size(), 3);

        const QList<QUrl> urls{QUrl::fromLocalFile(QStringLiteral("C:/固件/drop.bin"))};
        QCOMPARE(right.handleDrop(nullptr, urls), FileBrowserPanel::DropRoute::SystemUpload);
        QCOMPARE(submitted.size(), 4);
        QCOMPARE(submitted.back().items.front().remotePath, QStringLiteral("/drop.bin"));

        QCOMPARE(left.handleDrop(nullptr, urls), FileBrowserPanel::DropRoute::Rejected);
        QCOMPARE(submitted.size(), 4);

        QMimeData systemMime;
        systemMime.setUrls(urls);
        QDragEnterEvent localEnter({1, 1}, Qt::CopyAction, &systemMime,
                                   Qt::NoButton, Qt::NoModifier);
        localEnter.setAccepted(false);
        left.dragEnterEvent(&localEnter);
        QVERIFY(!localEnter.isAccepted());
        QDragMoveEvent localMove({1, 1}, Qt::CopyAction, &systemMime,
                                 Qt::NoButton, Qt::NoModifier);
        localMove.setAccepted(false);
        left.dragMoveEvent(&localMove);
        QVERIFY(!localMove.isAccepted());

        auto otherRemote = std::make_shared<MockDelayedSource>();
        otherRemote->m_sourceId = QStringLiteral("sftp");
        otherRemote->m_files = {makeInfo("other.bin")};
        TestableFileBrowserPanel remoteSource;
        remoteSource.setSource(otherRemote);
        settleInitialLoad(remoteSource);
        selectRows(remoteSource, {rowForName(remoteSource, "other.bin")});
        QCOMPARE(right.handleDrop(&remoteSource, {}), FileBrowserPanel::DropRoute::Rejected);
        QCOMPARE(submitted.size(), 4);
        QCOMPARE(local->uploadCalls.load(), 0);
        QCOMPARE(local->downloadCalls.load(), 0);
        QCOMPARE(remote->uploadCalls.load(), 0);
        QCOMPARE(remote->downloadCalls.load(), 0);
        QCOMPARE(local->removeCalls.load(), 0);
    }

    // 表格 viewport 的 eventFilter 与面板级处理必须同步拒绝无法落地的系统→本地；
    // 远程→远程则由带来源面板的真实 drop handler 拒绝，不能进入同步 I/O 分支。
    void dropRejectionIsConsistentAcrossViewportAndPanelHandlers()
    {
        auto local = std::make_shared<MockDelayedSource>();
        local->m_sourceId = QStringLiteral("local");
        auto remote = std::make_shared<MockDelayedSource>();
        remote->m_sourceId = QStringLiteral("ftp");
        auto otherRemote = std::make_shared<MockDelayedSource>();
        otherRemote->m_sourceId = QStringLiteral("sftp");

        TestableFileBrowserPanel localPanel;
        TestableFileBrowserPanel remotePanel;
        TestableFileBrowserPanel remoteSource;
        localPanel.setSource(local);
        remotePanel.setSource(remote);
        remoteSource.setSource(otherRemote);
        settleInitialLoad(localPanel);
        settleInitialLoad(remotePanel);
        settleInitialLoad(remoteSource);

        QMimeData systemMime;
        systemMime.setUrls({QUrl::fromLocalFile(QStringLiteral("C:/firmware/drop.bin"))});

        QDragEnterEvent enter({1, 1}, Qt::CopyAction, &systemMime,
                              Qt::NoButton, Qt::NoModifier);
        QVERIFY(localPanel.eventFilter(localPanel.fileTable()->viewport(), &enter));
        QVERIFY(!enter.isAccepted());

        QDragMoveEvent move({1, 1}, Qt::CopyAction, &systemMime,
                            Qt::NoButton, Qt::NoModifier);
        QVERIFY(localPanel.eventFilter(localPanel.fileTable(), &move));
        QVERIFY(!move.isAccepted());

        QDropEvent drop({1, 1}, Qt::CopyAction, &systemMime,
                        Qt::NoButton, Qt::NoModifier);
        QVERIFY(localPanel.eventFilter(localPanel.fileTable()->viewport(), &drop));
        QVERIFY(!drop.isAccepted());

        QDragEnterEvent panelEnter({1, 1}, Qt::CopyAction, &systemMime,
                                   Qt::NoButton, Qt::NoModifier);
        localPanel.dragEnterEvent(&panelEnter);
        QVERIFY(!panelEnter.isAccepted());
        QDragMoveEvent panelMove({1, 1}, Qt::CopyAction, &systemMime,
                                 Qt::NoButton, Qt::NoModifier);
        localPanel.dragMoveEvent(&panelMove);
        QVERIFY(!panelMove.isAccepted());

        QVERIFY(!remotePanel.canAcceptDrag(&remoteSource, true));
        QCOMPARE(remotePanel.handleDrop(&remoteSource, {}),
                 FileBrowserPanel::DropRoute::Rejected);
        QCOMPARE(remote->uploadCalls.load(), 0);
        QCOMPARE(otherRemote->downloadCalls.load(), 0);
    }

    // 远程→本地下载与上传使用同一 TransferTask 提交契约。
    void downloadEntrypoint_submitsTransferTask()
    {
        auto remote = std::make_shared<MockDelayedSource>();
        remote->m_sourceId = QStringLiteral("ssh");
        remote->m_files = {makeInfo("remote.bin")};
        auto local = std::make_shared<MockDelayedSource>();
        local->m_sourceId = QStringLiteral("local");

        FileBrowserPanel left;
        FileBrowserPanel right;
        left.setSource(remote);
        right.setSource(local);
        settleInitialLoad(left);
        settleInitialLoad(right);
        TransferTask submitted;
        left.setTransferSubmitter([&submitted](TransferTask task) {
            submitted = std::move(task);
            return QUuid::createUuid();
        });

        selectRows(left, {rowForName(left, "remote.bin")});
        left.copySelectedTo(&right);
        QCOMPARE(submitted.protocol, QStringLiteral("sftp"));
        QCOMPARE(submitted.items.size(), 1);
        QCOMPARE(submitted.items.front().direction, TransferDirection::Download);
        QCOMPARE(remote->downloadCalls.load(), 0);
    }

    // 覆盖策略对整个批次只询问一次，并写入每一个文件请求。
    void overwritePolicy_isChosenOncePerBatch()
    {
        auto local = std::make_shared<MockDelayedSource>();
        local->m_sourceId = QStringLiteral("local");
        local->m_files = {makeInfo("a.bin"), makeInfo("b.bin")};
        auto remote = std::make_shared<MockDelayedSource>();
        remote->m_sourceId = QStringLiteral("ftp");
        remote->m_files = {makeInfo("a.bin"), makeInfo("b.bin")};
        FileBrowserPanel left;
        FileBrowserPanel right;
        left.setSource(local);
        right.setSource(remote);
        settleInitialLoad(left);
        settleInitialLoad(right);
        int chooserCalls = 0;
        int observedConflictCount = 0;
        left.setOverwritePolicyChooser([&](int conflictCount) {
            ++chooserCalls;
            observedConflictCount = conflictCount;
            return std::optional<OverwritePolicy>(OverwritePolicy::Skip);
        });
        TransferTask submitted;
        left.setTransferSubmitter([&submitted](TransferTask task) {
            submitted = std::move(task);
            return QUuid::createUuid();
        });

        selectRows(left, {rowForName(left, "a.bin"), rowForName(left, "b.bin")});
        left.copySelectedTo(&right);
        QCOMPARE(chooserCalls, 1);
        QCOMPARE(observedConflictCount, 2);
        QCOMPARE(submitted.items.size(), 2);
        for (const auto& item : submitted.items)
            QCOMPARE(item.overwrite, OverwritePolicy::Skip);
    }

    // Left/Right 负责退出/进入目录；Up/Down 仍由表格原生选择模型处理。
    void keyboardNavigation_preservesCommanderSemantics()
    {
        auto source = std::make_shared<MockDelayedSource>();
        source->m_sourceId = QStringLiteral("ftp");
        source->m_files = {makeInfo("dir", true), makeInfo("a.bin"), makeInfo("b.bin")};
        FileBrowserPanel panel;
        panel.setSource(source);
        settleInitialLoad(panel);
        panel.fileTable()->setFocus();

        const int dirRow = rowForName(panel, "dir");
        selectRows(panel, {dirRow});
        QTest::keyClick(panel.fileTable(), Qt::Key_Right);
        QTRY_COMPARE_WITH_TIMEOUT(panel.currentPath(), QStringLiteral("/dir"), 5000);
        QTest::keyClick(panel.fileTable(), Qt::Key_Left);
        QTRY_COMPARE_WITH_TIMEOUT(panel.currentPath(), QStringLiteral("/"), 5000);

        const int firstFile = rowForName(panel, "a.bin");
        selectRows(panel, {firstFile});
        QTest::keyClick(panel.fileTable(), Qt::Key_Down);
        QCOMPARE(panel.fileTable()->currentIndex().row(), firstFile + 1);
        QTest::keyClick(panel.fileTable(), Qt::Key_Up);
        QCOMPARE(panel.fileTable()->currentIndex().row(), firstFile);
    }

    // 在已选中行上打开右键菜单不得把已有多选收缩为单选。
    void contextMenu_onSelectedRowKeepsSelection()
    {
        auto source = std::make_shared<MockDelayedSource>();
        source->m_sourceId = QStringLiteral("ftp");
        source->m_files = {makeInfo("a.bin"), makeInfo("b.bin")};
        FileBrowserPanel panel;
        panel.resize(640, 480);
        panel.show();
        panel.setSource(source);
        settleInitialLoad(panel);
        const int a = rowForName(panel, "a.bin");
        const int b = rowForName(panel, "b.bin");
        selectRows(panel, {a, b});
        QCOMPARE(panel.fileTable()->selectionModel()->selectedRows().size(), 2);
        const QPoint pos = panel.fileTable()->visualRect(
            panel.fileTable()->model()->index(a, 0)).center();
        QTimer::singleShot(20, [] {
            if (auto* popup = QApplication::activePopupWidget())
                popup->close();
        });
        panel.showContextMenu(pos);
        QCOMPARE(panel.fileTable()->selectionModel()->selectedRows().size(), 2);
    }

    // cancel 精确绑定活动任务；失败/部分成功后恢复仅重提未成功项。
    void cancelAndResume_bindToTaskSnapshots()
    {
        auto local = std::make_shared<MockDelayedSource>();
        local->m_sourceId = QStringLiteral("local");
        local->m_files = {makeInfo("a.bin"), makeInfo("b.bin")};
        auto remote = std::make_shared<MockDelayedSource>();
        remote->m_sourceId = QStringLiteral("ftp");
        FileBrowserPanel left;
        FileBrowserPanel right;
        left.setSource(local);
        right.setSource(remote);
        settleInitialLoad(left);
        settleInitialLoad(right);
        QVector<TransferTask> submissions;
        QVector<QUuid> ids;
        left.setTransferSubmitter([&](TransferTask task) {
            submissions.push_back(std::move(task));
            const QUuid id = QUuid::createUuid();
            ids.push_back(id);
            return id;
        });
        QUuid cancelled;
        left.setTransferCanceller([&cancelled](const QUuid& id) { cancelled = id; });
        selectRows(left, {rowForName(left, "a.bin"), rowForName(left, "b.bin")});
        left.copySelectedTo(&right);
        left.cancelActiveTransfer();
        QCOMPARE(cancelled, ids.front());

        TransferEvent finished;
        finished.type = TransferEventType::TaskFinished;
        finished.snapshot.id = ids.front();
        finished.snapshot.generation = submissions.front().generation;
        finished.snapshot.state = TransferState::PartiallySucceeded;
        finished.snapshot.itemResults = {
            {TransferState::Succeeded, {}, 1, false, 1},
            {TransferState::Failed,
             {TransferErrorCode::Permission, QStringLiteral("拒绝访问"), {}, false},
             1, false, 0}
        };
        left.consumeTransferEvent(finished);
        QTRY_VERIFY_WITH_TIMEOUT(left.findChild<QPushButton*>("transferRetryButton")->isEnabled(), 5000);
        left.resumeLastTransfer();
        QCOMPARE(submissions.size(), 2);
        QCOMPARE(submissions.back().items.size(), 1);
        QVERIFY(submissions.back().items.front().localPath.endsWith(QStringLiteral("b.bin")));
    }

    // F6 目标已经原子提交、但取消阻止源清理时，恢复不得重复已提交的项。
    void resumeSkipsAtomicallyCommittedCleanupFailure()
    {
        auto local = std::make_shared<MockDelayedSource>();
        local->m_sourceId = QStringLiteral("local");
        local->m_files = {makeInfo("a.bin")};
        auto remote = std::make_shared<MockDelayedSource>();
        remote->m_sourceId = QStringLiteral("ftp");
        FileBrowserPanel left;
        FileBrowserPanel right;
        left.setSource(local);
        right.setSource(remote);
        settleInitialLoad(left);
        settleInitialLoad(right);

        QVector<TransferTask> submissions;
        QVector<QUuid> ids;
        left.setTransferSubmitter([&](TransferTask task) {
            submissions.push_back(std::move(task));
            const QUuid id = QUuid::createUuid();
            ids.push_back(id);
            return id;
        });
        selectRows(left, {rowForName(left, "a.bin")});
        left.moveSelectedTo(&right);

        TransferEvent finished;
        finished.type = TransferEventType::TaskFinished;
        finished.snapshot.id = ids.front();
        finished.snapshot.generation = submissions.front().generation;
        finished.snapshot.state = TransferState::PartiallySucceeded;
        finished.snapshot.itemResults = {
            {TransferState::PartiallySucceeded,
             {TransferErrorCode::Cancelled,
              QStringLiteral("目标已提交，源清理已取消"),
              QStringLiteral("cancelled before source cleanup"), false},
             1, false, 1, true}
        };
        left.consumeTransferEvent(finished);
        QTRY_VERIFY_WITH_TIMEOUT(left.findChild<QPushButton*>("transferRetryButton")->isEnabled(), 5000);
        left.resumeLastTransfer();
        QCOMPARE(submissions.size(), 1);
    }

    void resumeRetriesUncommittedMoveResult()
    {
        auto local = std::make_shared<MockDelayedSource>();
        local->m_sourceId = QStringLiteral("local");
        local->m_files = {makeInfo("a.bin")};
        auto remote = std::make_shared<MockDelayedSource>();
        remote->m_sourceId = QStringLiteral("ftp");
        FileBrowserPanel left;
        FileBrowserPanel right;
        left.setSource(local);
        right.setSource(remote);
        settleInitialLoad(left);
        settleInitialLoad(right);

        QVector<TransferTask> submissions;
        QVector<QUuid> ids;
        left.setTransferSubmitter([&](TransferTask task) {
            submissions.push_back(std::move(task));
            const QUuid id = QUuid::createUuid();
            ids.push_back(id);
            return id;
        });
        selectRows(left, {rowForName(left, "a.bin")});
        left.moveSelectedTo(&right);

        TransferEvent finished;
        finished.type = TransferEventType::TaskFinished;
        finished.snapshot.id = ids.front();
        finished.snapshot.generation = submissions.front().generation;
        finished.snapshot.state = TransferState::Succeeded;
        finished.snapshot.itemResults = {
            {TransferState::Succeeded, {}, 0, true, 1, false}
        };
        left.consumeTransferEvent(finished);
        left.resumeLastTransfer();
        QCOMPARE(submissions.size(), 2);
        QCOMPARE(submissions.back().items.size(), 1);
        QVERIFY(submissions.back().removeSourceAfterCommit);
    }

    // Scheduler 可从任意线程发布；面板只在 GUI 线程消费值快照。导航后旧代际终态
    // 不得刷新新目录，未导航时成功终态只刷新目标面板。
    // nonAtomic 成功仍保持成功数据，但必须提示操作员复核目标文件。
    void nonAtomicSuccessShowsVisibleReviewWarning()
    {
        auto local = std::make_shared<MockDelayedSource>();
        local->m_sourceId = QStringLiteral("local");
        local->m_files = {makeInfo("a.bin")};
        auto remote = std::make_shared<MockDelayedSource>();
        remote->m_sourceId = QStringLiteral("ftp");
        FileBrowserPanel left;
        FileBrowserPanel right;
        left.setSource(local);
        right.setSource(remote);
        settleInitialLoad(left);
        settleInitialLoad(right);

        QVector<TransferTask> submissions;
        QVector<QUuid> ids;
        left.setTransferSubmitter([&](TransferTask task) {
            submissions.push_back(std::move(task));
            const QUuid id = QUuid::createUuid();
            ids.push_back(id);
            return id;
        });
        selectRows(left, {rowForName(left, "a.bin")});
        left.copySelectedTo(&right);

        TransferEvent finished;
        finished.type = TransferEventType::TaskFinished;
        finished.snapshot.id = ids.front();
        finished.snapshot.generation = submissions.front().generation;
        finished.snapshot.state = TransferState::Succeeded;
        finished.snapshot.itemResults = {
            {TransferState::Succeeded, {}, 1, true, 1}
        };
        left.consumeTransferEvent(finished);

        const auto* status = left.findChild<QLabel*>("panelTransferStatus");
        QVERIFY(status);
        QVERIFY(status->text().contains(QStringLiteral("非原子替换")));
        QVERIFY(status->text().contains(QStringLiteral("请复核")));
    }

    // Skip 没有提交目标，任务级 Succeeded 不能刷新任一面板。
    void skippedTransferDoesNotRefreshPanels()
    {
        auto local = std::make_shared<MockDelayedSource>();
        local->m_sourceId = QStringLiteral("local");
        local->m_files = {makeInfo("a.bin")};
        auto remote = std::make_shared<MockDelayedSource>();
        remote->m_sourceId = QStringLiteral("ftp");
        FileBrowserPanel left;
        FileBrowserPanel right;
        left.setSource(local);
        right.setSource(remote);
        settleInitialLoad(left);
        settleInitialLoad(right);

        QVector<TransferTask> submissions;
        QVector<QUuid> ids;
        left.setTransferSubmitter([&](TransferTask task) {
            submissions.push_back(std::move(task));
            const QUuid id = QUuid::createUuid();
            ids.push_back(id);
            return id;
        });
        selectRows(left, {rowForName(left, "a.bin")});
        left.moveSelectedTo(&right);
        const int localLists = local->listCalls.load();
        const int remoteLists = remote->listCalls.load();

        TransferEvent finished;
        finished.type = TransferEventType::TaskFinished;
        finished.snapshot.id = ids.front();
        finished.snapshot.generation = submissions.front().generation;
        finished.snapshot.state = TransferState::Succeeded;
        finished.snapshot.itemResults = {
            {TransferState::Succeeded, {}, 0, false, 0, false, true}
        };
        left.consumeTransferEvent(finished);
        QTest::qWait(100);

        QCOMPARE(local->listCalls.load(), localLists);
        QCOMPARE(remote->listCalls.load(), remoteLists);
    }

    void transferEvents_areQueuedAndGenerationSafe()
    {
        auto local = std::make_shared<MockDelayedSource>();
        local->m_sourceId = QStringLiteral("local");
        local->m_files = {makeInfo("a.bin")};
        auto remote = std::make_shared<MockDelayedSource>();
        remote->m_sourceId = QStringLiteral("ftp");
        FileBrowserPanel left;
        FileBrowserPanel right;
        left.setSource(local);
        right.setSource(remote);
        settleInitialLoad(left);
        settleInitialLoad(right);
        QVector<TransferTask> submissions;
        QVector<QUuid> ids;
        left.setTransferSubmitter([&](TransferTask task) {
            submissions.push_back(std::move(task));
            const QUuid id = QUuid::createUuid();
            ids.push_back(id);
            return id;
        });
        QThread* appliedThread = nullptr;
        QObject::connect(&left, &FileBrowserPanel::transferSnapshotApplied,
                         &left, [&appliedThread] { appliedThread = QThread::currentThread(); });

        selectRows(left, {rowForName(left, "a.bin")});
        left.copySelectedTo(&right);
        const int remoteCallsBeforeNavigation = remote->listCalls.load();
        right.navigateTo(QStringLiteral("/new"));
        QTRY_COMPARE_WITH_TIMEOUT(right.currentPath(), QStringLiteral("/new"), 5000);
        const int remoteCallsAfterNavigation = remote->listCalls.load();

        TransferEvent staleFinished;
        staleFinished.type = TransferEventType::TaskFinished;
        staleFinished.snapshot.id = ids.front();
        staleFinished.snapshot.generation = submissions.front().generation;
        staleFinished.snapshot.state = TransferState::Succeeded;
        staleFinished.snapshot.itemResults = {
            {TransferState::Succeeded, {}, 1, false, 1}
        };
        std::thread worker([&] { left.consumeTransferEvent(staleFinished); });
        worker.join();
        QTRY_COMPARE_WITH_TIMEOUT(appliedThread, left.thread(), 5000);
        QTest::qWait(50);
        QCOMPARE(remote->listCalls.load(), remoteCallsAfterNavigation);
        QVERIFY(remoteCallsAfterNavigation > remoteCallsBeforeNavigation);

        left.copySelectedTo(&right);
        const int beforeFreshFinish = remote->listCalls.load();
        TransferEvent freshFinished = staleFinished;
        freshFinished.snapshot.id = ids.back();
        freshFinished.snapshot.generation = submissions.back().generation;
        left.consumeTransferEvent(freshFinished);
        QTRY_COMPARE_WITH_TIMEOUT(remote->listCalls.load(), beforeFreshFinish + 1, 5000);
        QCOMPARE(local->listCalls.load(), 1);
    }

    // 用例 9：异步 list 在途时面板析构 — guard UAF 定向回归（guard 必须主线程调度时构造）。
    // 旧实现（worker 内构造 QPointer）在「worker 启动前面板已析构」窗口内读已释放
    // QObjectPrivate（getAndRef UAF）。本用例用 blockList 门控把 worker 悬停在 list 上，
    // 在栈作用域内析构面板后再放行——覆盖「面板先析构、worker 后启动/后返回」的交错。
    // 竞态非确定性（worker 启动时刻不定），循环 30 次 + 相位微调提高窗口命中率；
    // 断言=不崩溃（QTest 崩溃处理器会终止本轮并报错）。
    void destructDuringInflightLoad_doesNotCrash()
    {
        for (int i = 0; i < 30; ++i) {
            auto src = std::make_shared<MockDelayedSource>();
            src->m_files = { makeInfo("a.txt") };
            src->blockList = true;   // 门控：worker 阻塞在 list（模拟慢速目录读取）
            {
                FileBrowserPanel panel;
                panel.setSource(src);        // 调度初始加载 worker（门控挂起）
                panel.navigateTo("/a");      // 再调度一个 worker（门控挂起）
                QTest::qWait(i % 3);         // 相位变化：部分迭代给 worker 先行启动窗口
            }                                // 面板先于在途 worker 完成析构（UAF 窗口）
            src->blockList = false;          // 放行：worker list 返回 → guard 判空丢弃
            QTest::qWait(30);                // 排空在途 worker（避免与下一迭代交错）
            QVERIFY2(src->listCalls.load() >= 2,
                     "worker 未执行——用例未命中在途场景");
        }
    }
};
QTEST_MAIN(TstPanelAsync)
#include "tst_panel_async.moc"
