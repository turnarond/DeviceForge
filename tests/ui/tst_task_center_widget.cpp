// v2.11 Task 6：DeviceBus 设备档案展示/编辑 + TaskCenterWidget 三栏任务中心 UI 测试。
// 覆盖：① 设备胶囊名称优先展示 + 端点详情悬浮；② 双击打开档案编辑对话框并经
// DeviceRegistry 持久化（deviceProfileEdited 信号）；③ 无档案注册表时旧 ip:port
// 行保持可用（回退兼容）；④ 任务中心模板列表/步骤摘要/目标设备名称+地址；
// ⑤ 执行前确认（含风险提示），拒绝则不启动引擎、不落执行记录；
// ⑥ 校验失败运行收口后历史表出现「失败」行，选中后「从失败阶段重试」可用，
// 点击走 TaskExecutionEngine::retryFailed 并产生第二条执行记录；
// ⑦ MultiProgressWidget 行标签显示 名称 + 地址（单次部署无名称时保持原样）。
// 模式参照 tests/panel_async：QSignalSpy + QTRY_* 超时兜底，禁止 sleep 等待；
// 模态对话框经已布防的 QTimer 处理（exec 的嵌套事件循环会触发定时器）。
#include <QtTest>

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QUuid>

#include "config/ConfigStore.h"
#include "device/DeviceRegistry.h"
#include "task/TaskCenterWidget.h"
#include "task/TaskRunStore.h"
#include "task/TaskTemplateStore.h"
#include "tools/FtpDeployTool/MultiProgressWidget.h"
#include "ui/DeviceBusWidget.h"

namespace {

DeviceProfile makeProfile(const QString& deviceId, const QString& name,
                          const QString& ip, int port,
                          const QString& protocol = QStringLiteral("ftp"))
{
    DeviceProfile profile;
    profile.deviceId = deviceId.toStdString();
    profile.name = name.toStdString();
    DeviceEndpoint endpoint;
    endpoint.protocol = protocol.toStdString();
    endpoint.ip = ip.toStdString();
    endpoint.port = port;
    profile.endpoints.push_back(endpoint);
    return profile;
}

// 追加第二个端点（同物理设备跨协议）
void addEndpoint(DeviceProfile& profile, const QString& protocol,
                 const QString& ip, int port)
{
    DeviceEndpoint endpoint;
    endpoint.protocol = protocol.toStdString();
    endpoint.ip = ip.toStdString();
    endpoint.port = port;
    profile.endpoints.push_back(endpoint);
}

TaskStep makeStep(TaskStepType type, const QVariantMap& parameters = {})
{
    TaskStep step;
    step.type = type;
    step.parameters = parameters;
    return step;
}

} // namespace

class TstTaskCenterWidget : public QObject {
    Q_OBJECT

private:
    QString m_workDir;
    QString m_dbPath;
    // 档案编辑对话框 RejectRole 按钮计数（布防定时器在对话框嵌套事件循环中写入，
    // -1 = 未命中对话框；回归：重复 addButton「取消」会计数 >1）
    int m_editorRejectButtonCount = -1;

    // 每个用例独立数据库（同 tst_task_run_store 模式）
    void freshDb()
    {
        ConfigStore::instance().close();
        QFile::remove(m_dbPath);
        QFile::remove(m_dbPath + QStringLiteral("-wal"));
        QFile::remove(m_dbPath + QStringLiteral("-shm"));
        QVERIFY2(ConfigStore::instance().open(m_dbPath),
                 qPrintable(QStringLiteral("open failed path=%1").arg(m_dbPath)));
    }

    QPushButton* firstPill(DeviceBusWidget& bus)
    {
        const auto pills = bus.findChildren<QPushButton*>(QStringLiteral("devicePill"));
        return pills.isEmpty() ? nullptr : pills.first();
    }

private slots:
    void initTestCase()
    {
        m_workDir = QDir::temp().filePath(
            QStringLiteral("tst_task_center_widget_%1")
                .arg(QUuid::createUuid().toString(QUuid::Id128)));
        QVERIFY(QDir().mkpath(m_workDir));
        m_dbPath = QDir(m_workDir).filePath(QStringLiteral("config.db"));
    }

    void cleanupTestCase()
    {
        ConfigStore::instance().close();
        QDir(m_workDir).removeRecursively();
    }

    void init() { freshDb(); }
    void cleanup() { ConfigStore::instance().close(); }

    // ── ① 名称优先 + 端点详情悬浮 ────────────────────────────────
    void deviceBus_nameFirstDisplay()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        DeviceProfile profile = makeProfile(
            QStringLiteral("dev-boiler"), QStringLiteral("锅炉控制器"),
            QStringLiteral("10.0.0.5"), 21);
        addEndpoint(profile, QStringLiteral("telnet"), QStringLiteral("10.0.0.5"), 23);
        profile.note = QStringLiteral("车间 A").toUtf8().toStdString();
        QVERIFY(registry.save(profile));

        DeviceBusWidget bus;
        bus.setRegistry(&registry);

        QPushButton* pill = firstPill(bus);
        QVERIFY(pill);
        // 名称优先展示，地址作为次级信息仍可见
        QVERIFY2(pill->text().startsWith(QStringLiteral("锅炉控制器")),
                 qPrintable(QStringLiteral("胶囊应名称优先，实际: %1").arg(pill->text())));
        QVERIFY(pill->text().contains(QStringLiteral("10.0.0.5")));

        // ② 端点详情：悬浮可见协议端点与备注
        QVERIFY2(pill->toolTip().contains(QStringLiteral("ftp 10.0.0.5:21")),
                 qPrintable(pill->toolTip()));
        QVERIFY2(pill->toolTip().contains(QStringLiteral("telnet 10.0.0.5:23")),
                 qPrintable(pill->toolTip()));
        QVERIFY(pill->toolTip().contains(QStringLiteral("车间 A")));

        // 选中集合以档案（DeviceProfile）暴露，deviceId 稳定
        pill->click();
        const auto selected = bus.selectedDeviceProfiles();
        QCOMPARE(selected.size(), std::size_t{1});
        QCOMPARE(QString::fromStdString(selected.front().deviceId),
                 QStringLiteral("dev-boiler"));
    }

    // ── ② 双击打开编辑对话框 → 经 DeviceRegistry 持久化 + 信号 ───
    void deviceBus_editorDialog_persistsViaRegistryAndEmits()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(
            QStringLiteral("dev-plc"), QStringLiteral("旧名称"),
            QStringLiteral("10.0.0.9"), 21)));

        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        QPushButton* pill = firstPill(bus);
        QVERIFY(pill);

        QSignalSpy spy(&bus, &DeviceBusWidget::deviceProfileEdited);
        QVERIFY(spy.isValid());
        m_editorRejectButtonCount = -1;

        // 布防：对话框 exec 的嵌套事件循环中改名并保存
        QTimer::singleShot(50, this, [this] {
            QDialog* dialog = nullptr;
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget* w : widgets) {
                if (w->objectName() == QStringLiteral("deviceProfileEditor")) {
                    dialog = qobject_cast<QDialog*>(w);
                    break;
                }
            }
            if (!dialog)
                return;
            auto* nameEdit = dialog->findChild<QLineEdit*>(QStringLiteral("deviceProfileName"));
            auto* save = dialog->findChild<QPushButton*>(QStringLiteral("deviceProfileSave"));
            if (!nameEdit || !save)
                return;
            // 回归断言取证：对话框必须只暴露一个 RejectRole（取消）按钮
            if (auto* box = dialog->findChild<QDialogButtonBox*>()) {
                int rejectCount = 0;
                const auto buttons = box->buttons();
                for (QAbstractButton* b : buttons) {
                    if (box->buttonRole(b) == QDialogButtonBox::RejectRole)
                        ++rejectCount;
                }
                m_editorRejectButtonCount = rejectCount;
            }
            nameEdit->setText(QStringLiteral("新名称"));
            save->click();
        });
        // 兜底：若布防未命中（对话框实现漂移），3s 后强制关闭，避免测试挂死
        QTimer::singleShot(3000, [] {
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget* w : widgets) {
                if (w->objectName() == QStringLiteral("deviceProfileEditor"))
                    w->close();
            }
        });

        QTest::mouseDClick(pill, Qt::LeftButton);

        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
        const DeviceProfile edited =
            spy.at(0).at(0).value<DeviceProfile>();
        QCOMPARE(QString::fromStdString(edited.name), QStringLiteral("新名称"));

        // 注册表持久化（内存视图即时更新 + ConfigStore 可回读）
        const auto stored = registry.find("dev-plc");
        QVERIFY(stored.has_value());
        QCOMPARE(QString::fromStdString(stored->name), QStringLiteral("新名称"));
        QVERIFY(ConfigStore::instance().exists(
            QStringLiteral("device.profile"), QStringLiteral("dev-plc")));

        // 胶囊文案即时刷新为新名称
        QTRY_VERIFY_WITH_TIMEOUT(
            firstPill(bus)->text().startsWith(QStringLiteral("新名称")), 5000);

        // 对话框取消按钮唯一（修复回归：曾因 addButton 重复渲染两个「取消」）
        QVERIFY2(m_editorRejectButtonCount >= 0,
                 "布防定时器未命中编辑对话框，无法取证按钮计数");
        QCOMPARE(m_editorRejectButtonCount, 1);
    }

    // ── ②b 终审 Critical 1：编辑器改地址 → 旧端点/旧 device.list 行彻底消失 ──
    void deviceBus_editorAddressChange_dropsOldEndpointAndStaleLegacyRow()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(
            QStringLiteral("dev-ip"), QStringLiteral("改址设备"),
            QStringLiteral("10.3.0.1"), 21)));

        // 模拟旧增添路径持久化过的 device.list 回退行（key=ip:port）+ 无关设备旧行
        QVariantMap legacyRow;
        legacyRow.insert(QStringLiteral("ip"), QStringLiteral("10.3.0.1"));
        legacyRow.insert(QStringLiteral("port"), 21);
        legacyRow.insert(QStringLiteral("displayName"), QStringLiteral("改址设备"));
        QVERIFY(ConfigStore::instance().save(QStringLiteral("device.list"),
                                             QStringLiteral("10.3.0.1:21"), legacyRow));
        QVariantMap unrelatedRow;
        unrelatedRow.insert(QStringLiteral("ip"), QStringLiteral("10.3.0.9"));
        unrelatedRow.insert(QStringLiteral("port"), 21);
        unrelatedRow.insert(QStringLiteral("displayName"), QStringLiteral("无关设备"));
        QVERIFY(ConfigStore::instance().save(QStringLiteral("device.list"),
                                             QStringLiteral("10.3.0.9:21"), unrelatedRow));

        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        QPushButton* pill = firstPill(bus);
        QVERIFY(pill);

        QSignalSpy spy(&bus, &DeviceBusWidget::deviceProfileEdited);
        QVERIFY(spy.isValid());

        // 布防：把端点表首行主机地址 ip1→ip2 后保存（对话框嵌套事件循环）
        QTimer::singleShot(50, this, [this] {
            QDialog* dialog = nullptr;
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget* w : widgets) {
                if (w->objectName() == QStringLiteral("deviceProfileEditor")) {
                    dialog = qobject_cast<QDialog*>(w);
                    break;
                }
            }
            if (!dialog)
                return;
            auto* table = dialog->findChild<QTableWidget*>(
                QStringLiteral("deviceProfileEndpoints"));
            auto* save = dialog->findChild<QPushButton*>(QStringLiteral("deviceProfileSave"));
            if (!table || !save || table->rowCount() == 0 || !table->item(0, 1))
                return;
            table->item(0, 1)->setText(QStringLiteral("10.3.0.2"));
            save->click();
        });
        QTimer::singleShot(3000, [] {
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget* w : widgets) {
                if (w->objectName() == QStringLiteral("deviceProfileEditor"))
                    w->close();
            }
        });

        QTest::mouseDClick(pill, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);

        // 信号携带的已保存档案只剩新地址（旧端点未被并集写回）
        const DeviceProfile saved = spy.at(0).at(0).value<DeviceProfile>();
        QCOMPARE(saved.endpoints.size(), std::size_t{1});
        QCOMPARE(QString::fromStdString(saved.endpoints.front().ip),
                 QStringLiteral("10.3.0.2"));

        // 内存注册表 + device.profile 落库均只剩新端点
        const auto stored = registry.find("dev-ip");
        QVERIFY(stored.has_value());
        QCOMPARE(stored->endpoints.size(), std::size_t{1});
        QCOMPARE(QString::fromStdString(stored->endpoints.front().ip),
                 QStringLiteral("10.3.0.2"));

        // 被取代旧行清扫；persistLegacyRow 回写新地址行；无关旧行保留
        QVERIFY(!ConfigStore::instance().exists(QStringLiteral("device.list"),
                                                QStringLiteral("10.3.0.1:21")));
        QVERIFY(ConfigStore::instance().exists(QStringLiteral("device.list"),
                                               QStringLiteral("10.3.0.2:21")));
        QVERIFY(ConfigStore::instance().exists(QStringLiteral("device.list"),
                                               QStringLiteral("10.3.0.9:21")));

        // 重载：旧地址与旧行不复活（本档案唯一端点 ip2 + 无关旧设备一台）
        DeviceRegistry reloaded;
        QVERIFY(reloaded.load());
        QCOMPARE(reloaded.list().size(), std::size_t{2});
        const auto after = reloaded.find("dev-ip");
        QVERIFY(after.has_value());
        QCOMPARE(after->endpoints.size(), std::size_t{1});
        QCOMPARE(QString::fromStdString(after->endpoints.front().ip),
                 QStringLiteral("10.3.0.2"));
    }

    // ── ②c 终审 Important 4：档案新增/编辑/删除不得清空胶囊选中范围 ──────
    void deviceBus_registryRefreshKeepsPillSelection()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-keep"),
                                           QStringLiteral("保留我"),
                                           QStringLiteral("10.4.0.1"), 21)));
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-other"),
                                           QStringLiteral("旁站"),
                                           QStringLiteral("10.4.0.2"), 21)));

        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        const auto pillsBefore = bus.findChildren<QPushButton*>(QStringLiteral("devicePill"));
        QCOMPARE(pillsBefore.size(), 2);
        QPushButton* keep = nullptr;
        for (QPushButton* p : pillsBefore) {
            if (p->property("deviceId").toString() == QStringLiteral("dev-keep"))
                keep = p;
        }
        QVERIFY(keep);
        keep->click();
        QCOMPARE(keep->property("selected").toBool(), true);

        // 新增一台设备 → 胶囊栏整体重建（refreshFromRegistry）
        DeviceInfo third;
        third.ip = "10.4.0.3";
        third.port = 21;
        bus.addDevice(third);

        // 选中范围必须原样保持：仍只选中 dev-keep
        const auto selected = bus.selectedDeviceProfiles();
        QCOMPARE(selected.size(), std::size_t{1});
        if (!selected.empty())
            QCOMPARE(QString::fromStdString(selected.front().deviceId),
                     QStringLiteral("dev-keep"));
        const auto pillsAfter = bus.findChildren<QPushButton*>(QStringLiteral("devicePill"));
        QCOMPARE(pillsAfter.size(), 3);
        int selectedCount = 0;
        for (QPushButton* p : pillsAfter) {
            if (p->property("selected").toBool())
                ++selectedCount;
        }
        QCOMPARE(selectedCount, 1);
    }

    // ── ③ 无注册表：旧 ip:port 胶囊与 device.list 回退保持可用 ───
    void deviceBus_legacyRowsKeepWorking()
    {
        DeviceBusWidget bus;   // 不设 registry → 纯旧行为
        DeviceInfo device;
        device.ip = "192.168.9.9";
        device.port = 2122;
        bus.addDevice(device);

        QPushButton* pill = firstPill(bus);
        QVERIFY(pill);
        QCOMPARE(pill->text(), QStringLiteral("192.168.9.9:2122  ×"));

        // 旧 device.list 行仍写入（key=ip:port），供回退读取
        const QVariantMap record = ConfigStore::instance().load(
            QStringLiteral("device.list"), QStringLiteral("192.168.9.9:2122"));
        QVERIFY(!record.isEmpty());
        QCOMPARE(record.value(QStringLiteral("ip")).toString(),
                 QStringLiteral("192.168.9.9"));

        // 无档案时 selectedDeviceProfiles 由 DeviceInfo 合成稳定端点身份
        pill->click();
        const auto selected = bus.selectedDeviceProfiles();
        QCOMPARE(selected.size(), std::size_t{1});
        QCOMPARE(QString::fromStdString(selected.front().endpoints.front().ip),
                 QStringLiteral("192.168.9.9"));
    }

    // ── ④ 任务中心：模板列表 + 步骤/参数摘要 + 目标设备名称+地址 ──
    void taskCenter_templateSummary()
    {
        freshDb();
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(
            QStringLiteral("dev-1"), QStringLiteral("焊接机"),
            QStringLiteral("10.1.0.1"), 21)));

        TaskTemplateStore templates;
        TaskTemplate template1;
        template1.name = "固件升级";
        template1.description = "部署并重启";
        template1.deviceIds = {"dev-1"};
        QVariantMap deployParams;
        deployParams.insert(QStringLiteral("files"),
                            QStringList{QStringLiteral("C:/fw/a.bin"),
                                        QStringLiteral("C:/fw/b.bin")});
        deployParams.insert(QStringLiteral("remoteDirectory"), QStringLiteral("/tmp"));
        QVariantMap commandParams;
        commandParams.insert(QStringLiteral("commands"),
                             QStringList{QStringLiteral("ps"), QStringLiteral("df"),
                                         QStringLiteral("free")});
        template1.steps = {makeStep(TaskStepType::DeployFiles, deployParams),
                           makeStep(TaskStepType::RunCommands, commandParams),
                           makeStep(TaskStepType::Reboot)};
        QVERIFY(templates.save(template1));

        TaskRunStore runs;
        TaskCenterWidget center;
        center.setRegistry(&registry);
        center.setStores(&templates, &runs);

        auto* list = center.findChild<QListWidget*>(QStringLiteral("templateList"));
        QVERIFY(list);
        QTRY_COMPARE_WITH_TIMEOUT(list->count(), 1, 5000);
        list->setCurrentRow(0);

        auto* summary = center.findChild<QLabel*>(QStringLiteral("templateSummaryLabel"));
        QVERIFY(summary);
        const QString text = summary->text();
        QVERIFY2(text.contains(QStringLiteral("固件升级")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("目标 1 台")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("文件 2 个")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("命令 3 条")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("重启")), qPrintable(text));

        // 中栏步骤表：三步逐行展示
        auto* steps = center.findChild<QTableWidget*>(QStringLiteral("templateSteps"));
        QVERIFY(steps);
        QCOMPARE(steps->rowCount(), 3);
        QVERIFY(steps->item(0, 1)->text().contains(QStringLiteral("文件部署")));
        QVERIFY(steps->item(2, 1)->text().contains(QStringLiteral("重启")));

        // 右栏目标设备：名称 + 地址
        auto* devices = center.findChild<QListWidget*>(QStringLiteral("taskTargetDevices"));
        QVERIFY(devices);
        QCOMPARE(devices->count(), 1);
        QVERIFY(devices->item(0)->text().contains(QStringLiteral("焊接机")));
        QVERIFY(devices->item(0)->text().contains(QStringLiteral("10.1.0.1")));
    }

    // ── ⑤ 执行确认：摘要+风险提示；拒绝则不启动、不落记录 ─────────
    void taskCenter_riskConfirmation()
    {
        freshDb();
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(
            QStringLiteral("dev-1"), QStringLiteral("焊接机"),
            QStringLiteral("10.1.0.1"), 21)));

        TaskTemplateStore templates;
        TaskTemplate template1;
        template1.templateId = "tpl-confirm";
        template1.name = "维护重启";
        QVariantMap commandParams;
        commandParams.insert(QStringLiteral("commands"),
                             QStringList{QStringLiteral("uptime")});
        template1.steps = {makeStep(TaskStepType::RunCommands, commandParams),
                           makeStep(TaskStepType::Reboot)};
        template1.deviceIds = {"dev-1"};
        QVERIFY(templates.save(template1));
        const std::string templateId = template1.templateId;

        TaskRunStore runs;
        TaskCenterWidget center;
        center.setRegistry(&registry);
        center.setStores(&templates, &runs);

        QString capturedSummary;
        bool capturedRisky = false;
        int confirmCalls = 0;
        center.setRunConfirmer([&](const QString& summary, bool risky) {
            ++confirmCalls;
            capturedSummary = summary;
            capturedRisky = risky;
            return false;   // 操作员拒绝
        });

        QSignalSpy started(&center, &TaskCenterWidget::runStarted);
        center.startTemplate(templateId);

        QCOMPARE(confirmCalls, 1);
        // 摘要含设备名称+地址、命令数量与重启动作；重启动作触发风险提示
        QVERIFY2(capturedSummary.contains(QStringLiteral("焊接机")), qPrintable(capturedSummary));
        QVERIFY2(capturedSummary.contains(QStringLiteral("10.1.0.1")), qPrintable(capturedSummary));
        QVERIFY2(capturedSummary.contains(QStringLiteral("命令 1 条")), qPrintable(capturedSummary));
        QVERIFY(capturedRisky);
        QVERIFY2(capturedSummary.contains(QStringLiteral("风险提示")), qPrintable(capturedSummary));

        // 拒绝：不启动执行、不产生任何历史记录
        QCOMPARE(started.count(), 0);
        QCOMPARE(runs.query({}).size(), std::size_t{0});
    }

    // ── ⑥ 校验失败运行 → 历史「失败」行 → 从失败阶段重试动作 ──────
    void taskCenter_failedRunHistoryAndRetryAction()
    {
        freshDb();
        DeviceRegistry registry;
        QVERIFY(registry.load());
        // 只有 FTP 端点 → RunCommands 步骤校验必失败（设备未配置 Telnet/SSH 端点），
        // 全程不触网、不需要任何真实适配器
        QVERIFY(registry.save(makeProfile(
            QStringLiteral("dev-ftp"), QStringLiteral("只读FTP盒"),
            QStringLiteral("10.2.0.2"), 21)));

        TaskTemplateStore templates;
        TaskTemplate template1;
        template1.templateId = "tpl-retry";
        template1.name = "远程巡检";
        QVariantMap commandParams;
        commandParams.insert(QStringLiteral("commands"),
                             QStringList{QStringLiteral("uptime")});
        template1.steps = {makeStep(TaskStepType::RunCommands, commandParams)};
        template1.deviceIds = {"dev-ftp"};
        QVERIFY(templates.save(template1));
        const std::string templateId = template1.templateId;

        TaskRunStore runs;
        TaskCenterWidget center;
        center.setRegistry(&registry);
        center.setStores(&templates, &runs);
        center.setRunConfirmer([](const QString&, bool) { return true; });

        QSignalSpy started(&center, &TaskCenterWidget::runStarted);
        QVERIFY(started.isValid());
        center.startTemplate(templateId);
        QCOMPARE(started.count(), 1);

        // 引擎在协调线程收口 → 队列回调写历史表
        auto* history = center.findChild<QTableWidget*>(QStringLiteral("taskRunHistory"));
        QVERIFY(history);
        QTRY_COMPARE_WITH_TIMEOUT(history->rowCount(), 1, 10000);
        const int statusCol = 2;
        QCOMPARE(history->item(0, statusCol)->text(), QStringLiteral("失败"));

        // 选中失败行 → 「从失败阶段重试」点亮
        auto* retry = center.findChild<QPushButton*>(QStringLiteral("taskRetryFailedButton"));
        QVERIFY(retry);
        QCOMPARE(retry->isEnabled(), false);
        history->selectRow(0);
        QTRY_VERIFY_WITH_TIMEOUT(retry->isEnabled(), 5000);

        // 点击重试 → engine.retryFailed 复用回调，产生第二轮记录
        retry->click();
        QTRY_COMPARE_WITH_TIMEOUT(started.count(), 2, 5000);

        // 第二轮同样收口为失败（校验必失败）→ 历史中出现两条终态失败记录
        const auto failedCount = [&runs] {
            TaskRunFilter filter;
            filter.status = TaskRunStatus::Failed;
            return runs.query(filter).size();
        };
        QTRY_VERIFY_WITH_TIMEOUT(failedCount() == std::size_t{2}, 10000);
    }

    // ── ⑥b 终审 Important 5：同档案多个不同 credentialRef → 中文告警一次 ──
    void taskCenter_warnsOnMultipleDistinctCredentialRefs()
    {
        freshDb();
        DeviceRegistry registry;
        QVERIFY(registry.load());
        // dev-multi：ftp 与 sftp 端点携带不同凭据引用 → 必须告警（仅 1 次）；
        // dev-single：两端点同引用 → 不得告警。两台都缺 Telnet/SSH 命令端点，
        // 运行只会走 Validating 校验失败路径（同用例⑥，不触碰适配器工厂）。
        DeviceProfile multi;
        multi.deviceId = "dev-multi";
        multi.name = QStringLiteral("多凭据设备").toStdString();
        multi.endpoints.push_back({"ftp", "10.5.0.1", 21, "ref-never-echo-A"});
        multi.endpoints.push_back({"sftp", "10.5.0.1", 22, "ref-never-echo-B"});
        QVERIFY(registry.save(multi));
        DeviceProfile single;
        single.deviceId = "dev-single";
        single.name = QStringLiteral("单凭据设备").toStdString();
        single.endpoints.push_back({"ftp", "10.5.0.2", 21, "ref-never-echo-A"});
        single.endpoints.push_back({"sftp", "10.5.0.2", 22, "ref-never-echo-A"});
        QVERIFY(registry.save(single));

        TaskTemplateStore templates;
        TaskTemplate task;
        task.templateId = "tpl-creds";
        task.name = QStringLiteral("凭据巡检").toStdString();
        QVariantMap commandParams;
        commandParams.insert(QStringLiteral("commands"),
                             QStringList{QStringLiteral("uptime")});
        task.steps = {makeStep(TaskStepType::RunCommands, commandParams)};
        task.deviceIds = {"dev-multi", "dev-single"};
        QVERIFY(templates.save(task));
        const std::string templateId = task.templateId;

        TaskRunStore runs;
        TaskCenterWidget center;
        center.setRegistry(&registry);
        center.setStores(&templates, &runs);
        center.setRunConfirmer([](const QString&, bool) { return true; });

        QSignalSpy logs(&center, &TaskCenterWidget::logMessage);
        QVERIFY(logs.isValid());
        QSignalSpy finished(&center, &TaskCenterWidget::runFinished);
        QVERIFY(finished.isValid());

        center.startTemplate(templateId);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);

        int warnCount = 0;
        for (const QVariantList& args : logs) {
            const QString text = args.at(0).toString();
            if (text.contains(QStringLiteral("不同凭据引用"))) {
                ++warnCount;
                QVERIFY2(text.contains(QStringLiteral("多凭据设备")), qPrintable(text));
                QVERIFY2(text.contains(QStringLiteral("2")), qPrintable(text));
                // 告警绝不泄露引用内容（这里引用键本身也不得出现）
                QVERIFY2(!text.contains(QStringLiteral("ref-never-echo")), qPrintable(text));
            }
        }
        QCOMPARE(warnCount, 1);
    }

    // ── ⑦ MultiProgressWidget：名称 + 地址；无名称保持原样 ────────
    void multiProgress_showsNamePlusAddress()
    {
        MultiProgressWidget progress;
        progress.setDeviceCount(2);
        progress.setDeviceInfo(QStringLiteral("10.0.0.5:21"), QStringLiteral("熔接机"));
        progress.setDeviceInfo(QStringLiteral("10.0.0.6:21"));   // 旧调用：仅地址

        const auto labels = progress.findChildren<QLabel*>();
        bool namedFound = false;
        bool addressOnlyFound = false;
        for (const QLabel* label : labels) {
            if (label->text().contains(QStringLiteral("熔接机"))
                && label->text().contains(QStringLiteral("10.0.0.5:21")))
                namedFound = true;
            if (label->text() == QStringLiteral("10.0.0.6:21"))
                addressOnlyFound = true;
        }
        QVERIFY(namedFound);
        QVERIFY(addressOnlyFound);
    }
};

QTEST_MAIN(TstTaskCenterWidget)
#include "tst_task_center_widget.moc"
