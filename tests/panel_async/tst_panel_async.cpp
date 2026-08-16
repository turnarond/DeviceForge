#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QLineEdit>
#include <QLabel>
#include <QTableView>
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>

#include "ui/IFileSource.h"
#include "ui/FileBrowserPanel.h"
#include "tools/FtpDeployTool/FtpFileInfo.h"
#include "tools/FtpDeployTool/RemoteFileModel.h"

// 可控延迟的 Mock 源：list 返回可配置结果，延迟由 blockList 门控
//（阻塞自旋模拟慢速/断网目录读取；failNextList 模拟首次失败以触发重连重试）
class MockDelayedSource : public IFileSource {
public:
    QString sourceId() const override { return QStringLiteral("mock"); }
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
    bool remove(const QString&, bool) override { return true; }
    bool clearDirectory(const QString&) override { return true; }
    bool upload(const QString&, const QString&) override { return true; }
    bool download(const QString&, const QString&) override { return true; }
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
    std::atomic<int> listCalls{0};
    std::atomic<int> reconnectCalls{0};
    std::atomic<bool> blockList{false};
    std::atomic<bool> failNextList{false};
    std::atomic<bool> failReconnect{false};
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
        QTRY_VERIFY_WITH_TIMEOUT(src->reconnectCalls.load() == 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(src->listCalls.load() >= 2, 5000);

        // 失败导航不改变当前有效路径；路径栏回写为上一个有效路径
        QCOMPARE(panel.currentPath(), QStringLiteral("/"));
        auto* pathEdit = panel.findChild<QLineEdit*>();
        QVERIFY(pathEdit);
        QCOMPARE(pathEdit->text(), QStringLiteral("/"));
        auto* breadcrumb = panel.findChild<QLabel*>("panelBreadcrumb");
        QVERIFY(breadcrumb);
        QVERIFY(breadcrumb->text().contains(QStringLiteral("加载失败")));
        QVERIFY(breadcrumb->text().contains(QStringLiteral("mock: reconnect failed")));
    }
};
QTEST_MAIN(TstPanelAsync)
#include "tst_panel_async.moc"
