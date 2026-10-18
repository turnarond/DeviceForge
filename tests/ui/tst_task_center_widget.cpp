// v2.11 Task 6：DeviceBus 设备档案展示/编辑 + TaskCenterWidget 三栏任务中心 UI 测试。
// 覆盖：① 设备胶囊名称优先展示 + 端点详情悬浮；② 双击打开档案编辑对话框并经
// DeviceRegistry 持久化（deviceProfileEdited 信号）；③ 无档案注册表时旧 ip:port
// 行保持可用（回退兼容）；④ 任务中心模板列表/步骤摘要/目标设备名称+地址；
// ⑤ 执行前确认（含风险提示），拒绝则不启动引擎、不落执行记录；
// ⑥ 校验失败运行收口后历史表出现「失败」行，选中后「从失败阶段重试」可用，
// 点击走 TaskExecutionEngine::retryFailed 并产生第二条执行记录；
// ⑦ MultiProgressWidget 行标签显示 名称 + 地址（单次部署无名称时保持原样）；
// ⑧ v2.13 模板导出：公共 API 直写契约文件 + 按钮选中启用规则 + 文件名清洗口径；
// ⑨ v2.13 模板导入：新建计数+悬空引用保留/同名三选（覆盖继承 ID 与版本、改名
//    "(2)"、取消静默）/坏文件不落库。
// ⑩ v2.13 #54 Task 2：模板编辑器对话框骨架——元信息/设备多选（含悬空档案展示与
//    默认勾选）/步骤列表/reboot 参数页/validate+collect+canonicalDeployParameters。
// ⑪ v2.13 #54 Task 3：deploy 步骤面板——teItemList 表格绑定 items[]、单元格编辑
//    即时写回、方向/覆盖下拉规范串、collect 从可见表格快照。
// ⑫ v2.13 #54 Task 4：commands/recover 面板（多行命令/timeoutSec/共享 protocol）。
// ⑬ v2.13 #54 Task 5：任务中心接线——新建/编辑/删除三按钮存在与启用态翻转、
//    删除确认「否」不删/「是」删除并复位、applyEditorResult 公共编排缝
//    （原位覆盖保 id / 新模板生成 id / 无效模板拒 save 返回 false）。
// ⑭ v2.13 #54 Task 6：e2e「新建→执行」链——编辑器 collect 产出的 deploy+reboot
//    两步模板经 applyEditorResult 落库（store 生成 id）后 startTemplate 全链
//    发起执行并收口落库（FTP-only 档案不触网，重启步骤必校验失败，模式同⑥）。
// ⑮ v2.14 #49-4：阶段恢复拒绝文案三分派静态直测（retryRejectionMessage 纯函数，
//    lineage 未知 / 引擎 32 轮已淘汰 / 活跃运行三种入参各得对应关键词且两两不同）。
// ⑯ v2.15 #49-6：运行终态聚合纯头直测（taskrun::aggregateRunStatus 分桶收敛规则，
//    单源见 task/TaskRunAggregate.h）。
// 模式参照 tests/panel_async：QSignalSpy + QTRY_* 超时兜底，禁止 sleep 等待；
// 模态对话框经已布防的 QTimer 处理（exec 的嵌套事件循环会触发定时器）。
#include <QtTest>

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>

#include "config/ConfigStore.h"
#include "device/DeviceRegistry.h"
#include "task/TaskCenterWidget.h"
#include "task/TaskExecutionEngine.h"
#include "task/TaskRunAggregate.h"
#include "task/TaskRunStore.h"
#include "task/TaskTemplateCodec.h"
#include "task/TaskTemplateEditorDialog.h"
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
    // v2.13 Task 3：模板导出用例的临时目录（init() 逐用例重建）
    QTemporaryDir m_tempDir;
    // 档案编辑对话框 RejectRole 按钮计数（布防定时器在对话框嵌套事件循环中写入，
    // -1 = 未命中对话框；回归：重复 addButton「取消」会计数 >1）
    int m_editorRejectButtonCount = -1;
    // v2.11.1 取证暂存：端点表协议/凭据下拉内容（布防定时器写入，空 = 未命中）
    QStringList m_captureProtocolItems;
    QStringList m_captureCredentialItems;

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

    // ── v2.13 Task 4：模板导入用例 helper ──
    // 种子模板：name=「导入体」、1 步、1 个 device-dead 悬空引用（encodeTemplateFile 直造文件）
    TaskTemplate makeExportableTemplate() const
    {
        TaskTemplate tmpl;
        tmpl.name = QStringLiteral("导入体").toUtf8().toStdString();
        tmpl.description = QStringLiteral("导入用例种子").toUtf8().toStdString();
        tmpl.deviceIds = {"device-dead"};
        TaskStep step;
        step.type = TaskStepType::Reboot;
        tmpl.steps.push_back(step);
        return tmpl;
    }

    QString writeToTemp(const QByteArray& data)
    {
        const QString path = m_tempDir.filePath(QStringLiteral("template.json"));
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return QString();
        f.write(data);
        f.close();
        return path;
    }

    // 布防-捕获模态对话框：exec 嵌套事件循环触发定时器 → 抓文案 + 关闭。
    // 仅在「本用例必弹框」路径使用；静默路径（取消）布防会把定时器漏进后续用例。
    // 负对照：未弹框则 m_dialogClosed 保持 false（center 析构后布防 lambda 随上下文销毁，不外溢）。
    void armDialogCapture(TaskCenterWidget& center)
    {
        m_dialogText.clear();
        m_dialogClosed = false;
        QTimer::singleShot(50, &center, [this, &center] {
            QMessageBox* box = center.findChild<QMessageBox*>();
            if (!box)
                return;
            m_dialogText = box->text();
            m_dialogClosed = true;
            box->done(QMessageBox::Ok);
        });
    }

    QString m_dialogText;
    bool m_dialogClosed = false;

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

    void init()
    {
        freshDb();
        m_tempDir = QTemporaryDir();            // 每用例独立临时目录（模板导出落盘用）
        QVERIFY2(m_tempDir.isValid(), qPrintable(m_tempDir.errorString()));
    }
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
    // ── v2.11.1 ①：端点表协议下拉 + 凭据下拉/就地新建 ──
    void deviceBus_editorProtocolAndCredentialCombos()
    {
        // 存储层直测：空键拒绝、DPAPI 密文入库、无明文
        QCOMPARE(devicebus::storeFtpCredential(QString(), QStringLiteral("u"),
                                              QStringLiteral("p"), QString(), 0),
                 QString());
        const QString credKey = devicebus::storeFtpCredential(
            QStringLiteral("root@10.6.0.1:21"), QStringLiteral("root"),
            QStringLiteral("s3cret"), QStringLiteral("10.6.0.1"), 21);
        QCOMPARE(credKey, QStringLiteral("root@10.6.0.1:21"));
        const QVariantMap raw = ConfigStore::instance().load(
            QStringLiteral("ftp.credential"), credKey);
        QCOMPARE(raw.value(QStringLiteral("username")).toString(),
                 QStringLiteral("root"));
        const QString storedPass = raw.value(QStringLiteral("password")).toString();
        QVERIFY2(!storedPass.isEmpty(), "DPAPI 密文缺失");
        QVERIFY(!storedPass.contains(QStringLiteral("s3cret")));

        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(
            QStringLiteral("dev-dd"), QStringLiteral("焊机"),
            QStringLiteral("10.6.0.1"), 21)));

        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        QPushButton* pill = firstPill(bus);
        QVERIFY(pill);

        QSignalSpy spy(&bus, &DeviceBusWidget::deviceProfileEdited);
        QVERIFY(spy.isValid());
        m_captureProtocolItems.clear();
        m_captureCredentialItems.clear();

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
            if (!table || !save || table->rowCount() == 0)
                return;
            auto* proto = qobject_cast<QComboBox*>(table->cellWidget(0, 0));
            auto* cred = qobject_cast<QComboBox*>(table->cellWidget(0, 3));
            if (!proto || !cred)
                return;
            for (int i = 0; i < proto->count(); ++i)
                m_captureProtocolItems << proto->itemText(i);
            for (int i = 0; i < cred->count(); ++i)
                m_captureCredentialItems << cred->itemText(i);
            proto->setCurrentIndex(proto->findText(QStringLiteral("telnet")));
            cred->setCurrentIndex(cred->findText(QStringLiteral("root@10.6.0.1:21")));
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

        // 下拉内容取证
        QVERIFY2(!m_captureProtocolItems.isEmpty(), "布防未命中协议下拉");
        for (const char* p : {"ftp", "ftps", "sftp", "ssh", "telnet", "modbus"})
            QVERIFY(m_captureProtocolItems.contains(QString::fromLatin1(p)));
        QVERIFY2(!m_captureCredentialItems.isEmpty(), "布防未命中凭据下拉");
        QVERIFY(m_captureCredentialItems.contains(QStringLiteral("（无凭据）")));
        QVERIFY(m_captureCredentialItems.contains(QStringLiteral("root@10.6.0.1:21")));
        QVERIFY(m_captureCredentialItems.contains(QStringLiteral("＋ 新建凭据…")));

        // 选择结果落库：协议 telnet + 凭据引用回填
        const DeviceProfile saved = spy.at(0).at(0).value<DeviceProfile>();
        QCOMPARE(saved.endpoints.size(), std::size_t{1});
        QCOMPARE(QString::fromStdString(saved.endpoints.front().protocol),
                 QStringLiteral("telnet"));
        QCOMPARE(QString::fromStdString(saved.endpoints.front().credentialRef),
                 QStringLiteral("root@10.6.0.1:21"));
    }

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

    // ── ⑧ v2.13 Task 3：模板导出——公共 API 直调 + 按钮选中启用 + 清洗口径 ──
    void taskCenter_exportTemplate_writesContractFileAndDisabledWithoutSelection()
    {
        TaskTemplateStore store;
        TaskTemplate tmpl; tmpl.name = "导出我"; tmpl.deviceIds = {"device-xxxx"};
        TaskStep step; step.type = TaskStepType::Reboot; tmpl.steps.push_back(step);
        QVERIFY(store.save(tmpl));
        TaskCenterWidget center;
        auto* exportBtn = center.findChild<QPushButton*>(QStringLiteral("taskExportTemplateButton"));
        QVERIFY(exportBtn);
        QVERIFY(!exportBtn->isEnabled());                    // 未选中禁用（尚无 store，亦无选中）
        center.setStores(&store, nullptr);                   // 既有口径：刷新自动选中首行
        auto* list = center.findChild<QListWidget*>(QStringLiteral("templateList"));
        list->setCurrentRow(0);
        QVERIFY(exportBtn->isEnabled());                    // 选中即用
        QCOMPARE(TaskCenterWidget::sanitizedTemplateFileName(QStringLiteral("A/B:*<>|\\?x")),
                 QStringLiteral("A_B_______x.json"));        // 清洗口径钉死（8 个非法字符逐一替换）
        QCOMPARE(TaskCenterWidget::sanitizedTemplateFileName(QStringLiteral("报表.json")),
                 QStringLiteral("报表.json"));                  // #55：endsWith 防重，不再 .json.json
        QCOMPARE(TaskCenterWidget::sanitizedTemplateFileName(QStringLiteral("x.JSON")),
                 QStringLiteral("x.JSON"));                  // 大小写同判
        const QString path = m_tempDir.filePath(QStringLiteral("t.json"));
        QVERIFY(center.exportTemplateToFile(path));
        QFile f(path); QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray content = f.readAll();
        QVERIFY(content.contains("deviceforge.task-template"));
        QVERIFY(content.contains(QString::fromUtf8("导出我").toUtf8()));
        QVERIFY(!content.contains("template-"));             // ID 不外溢（store 生成的前缀不在文件中）

        // 失败路径：指向已存在目录（打开写必失败，跨机器确定，#55 替代 Z: 盘假设）；
        // 错误弹窗布防后自动关闭（沿用本文件布防做法）
        QTimer::singleShot(50, &center, [&center] {
            if (auto* box = center.findChild<QMessageBox*>())
                box->done(QMessageBox::Ok);
        });
        QVERIFY(!center.exportTemplateToFile(m_tempDir.path()));
    }

    // ── ⑧b v2.13 Task 3：模板清空后刷新 → 陈旧选中态复位、导出按钮回禁用 ──
    // 负对照：refreshTemplates 空列表分支若不复位，m_currentTemplateId 残留旧值，
    // 按钮将保持启用（用例变红）。store 无状态（共用 ConfigStore 单例），清空走 freshDb。
    void taskCenter_exportTemplateButtonResetsWhenSelectionCleared()
    {
        TaskTemplateStore store;
        TaskTemplate tmpl; tmpl.name = "会被清库";
        TaskStep step; step.type = TaskStepType::Reboot; tmpl.steps.push_back(step);
        QVERIFY(store.save(tmpl));
        TaskCenterWidget center;
        center.setStores(&store, nullptr);
        auto* exportBtn = center.findChild<QPushButton*>(QStringLiteral("taskExportTemplateButton"));
        QVERIFY(exportBtn);
        QVERIFY(exportBtn->isEnabled());                     // 自动选中首行 → 启用
        freshDb();                                           // 清库 → 模板列表转空
        center.setStores(&store, nullptr);                   // 重绑刷新 → 走复位路
        QVERIFY(!exportBtn->isEnabled());                    // 陈旧选中态必须清除
        auto* list = center.findChild<QListWidget*>(QStringLiteral("templateList"));
        QCOMPARE(list->count(), 0);
    }

    // ── ⑨ v2.13 Task 4：模板导入——新建/覆盖/改名/取消/坏文件五路 ──────────
    void taskCenter_import_newTemplate_countsUpAndKeepsDangling()
    {
        TaskTemplateStore store;
        DeviceRegistry registry;                             // 空档案：device-dead 必计悬空
        QVERIFY(registry.load());
        TaskCenterWidget center;
        center.setRegistry(&registry);
        center.setStores(&store, nullptr);

        auto* importBtn = center.findChild<QPushButton*>(QStringLiteral("taskImportTemplateButton"));
        QVERIFY(importBtn);
        QCOMPARE(importBtn->isEnabled(), true);              // 恒可用（规格 §4）

        const QString path = writeToTemp(encodeTemplateFile(makeExportableTemplate()));
        QVERIFY(!path.isEmpty());
        const int before = static_cast<int>(store.list().size());
        QSignalSpy logs(&center, &TaskCenterWidget::logMessage);
        QVERIFY(logs.isValid());
        armDialogCapture(center);

        QVERIFY(center.importTemplateFromFile(path));
        QVERIFY2(m_dialogClosed, "成功信息框未弹出（布防未命中）");
        QCOMPARE(static_cast<int>(store.list().size()), before + 1);

        // 悬空引用原样保留（不剥离）
        const auto rows = store.list();
        QCOMPARE(QString::fromStdString(rows.front().name), QStringLiteral("导入体"));
        QCOMPARE(rows.front().deviceIds.size(), std::size_t{1});
        QCOMPARE(QString::fromStdString(rows.front().deviceIds.front()),
                 QStringLiteral("device-dead"));

        // 成功提示两行文案：步骤计数 + 悬空统计；logMessage 同文案
        const QString expected = QStringLiteral(
            "已导入模板「导入体」（1 个步骤）\n"
            "引用了 1 个本机不存在的设备档案，建好同端点档案后自动接上");
        QCOMPARE(m_dialogText, expected);
        bool logHit = false;
        for (const QVariantList& args : logs) {
            if (args.at(0).toString() == expected)
                logHit = true;
        }
        QVERIFY2(logHit, qPrintable(expected));

        // 返回 true 即已 refreshTemplates()：列表出现新行
        auto* list = center.findChild<QListWidget*>(QStringLiteral("templateList"));
        QVERIFY(list);
        QTRY_COMPARE_WITH_TIMEOUT(list->count(), 1, 5000);
    }

    void taskCenter_import_conflictOverwrite_replacesInPlace()
    {
        TaskTemplateStore store;
        TaskTemplate existing;
        existing.name = QStringLiteral("导入体").toUtf8().toStdString();
        existing.description = QStringLiteral("本机旧版").toUtf8().toStdString();
        existing.version = 3;                                // 高于文件版本(1)：覆盖须继承既有版本，
        TaskStep oldStep; oldStep.type = TaskStepType::DeployFiles;   // 否则被 store 降级拒绝
        existing.steps.push_back(oldStep);
        QVERIFY(store.save(existing));
        const std::string existingId = store.list().front().templateId;

        TaskCenterWidget center;
        center.setStores(&store, nullptr);
        QString resolverName;
        center.setImportConflictResolver([&](const QString& name) {
            resolverName = name;
            return 0;                                        // 覆盖
        });

        const QString path = writeToTemp(encodeTemplateFile(makeExportableTemplate()));
        QVERIFY(!path.isEmpty());
        armDialogCapture(center);
        QVERIFY(center.importTemplateFromFile(path));

        QCOMPARE(resolverName, QStringLiteral("导入体"));     // resolver 收到导入名
        const auto rows = store.list();
        QCOMPARE(rows.size(), std::size_t{1});               // 数量不变：原位覆写
        QCOMPARE(rows.front().templateId, existingId);       // 继承既有 templateId
        QCOMPARE(rows.front().version, 3);                   // 继承既有 version
        QCOMPARE(QString::fromStdString(rows.front().description),
                 QStringLiteral("导入用例种子"));              // 内容被替换
        QVERIFY2(m_dialogClosed, "成功信息框未弹出");
    }

    void taskCenter_import_conflictRename_addsSuffix2()
    {
        TaskTemplateStore store;
        TaskTemplate existing;
        existing.name = QStringLiteral("导入体").toUtf8().toStdString();
        existing.description = QStringLiteral("本机旧版").toUtf8().toStdString();
        TaskStep step; step.type = TaskStepType::DeployFiles;
        existing.steps.push_back(step);
        QVERIFY(store.save(existing));

        TaskCenterWidget center;
        center.setStores(&store, nullptr);
        center.setImportConflictResolver([](const QString&) { return 1; });   // 改名

        const QString path = writeToTemp(encodeTemplateFile(makeExportableTemplate()));
        QVERIFY(!path.isEmpty());
        armDialogCapture(center);
        QVERIFY(center.importTemplateFromFile(path));

        const auto rows = store.list();
        QCOMPARE(rows.size(), std::size_t{2});
        const auto byName = [&rows](const QString& name) -> std::optional<TaskTemplate> {
            for (const auto& t : rows) {
                if (QString::fromStdString(t.name) == name)
                    return t;
            }
            return std::nullopt;
        };
        const auto renamed = byName(QStringLiteral("导入体 (2)"));
        QVERIFY2(renamed.has_value(), "未找到自动改名行「导入体 (2)」");
        QCOMPARE(QString::fromStdString(renamed->description), QStringLiteral("导入用例种子"));
        const auto kept = byName(QStringLiteral("导入体"));
        QVERIFY(kept.has_value());
        QCOMPARE(QString::fromStdString(kept->description), QStringLiteral("本机旧版"));   // 既有行未动
        QVERIFY2(m_dialogText.contains(QStringLiteral("导入体 (2)")),
                 qPrintable(m_dialogText));                  // 提示用改名后的名称
    }

    void taskCenter_import_conflictCancel_noop()
    {
        TaskTemplateStore store;
        TaskTemplate existing;
        existing.name = QStringLiteral("导入体").toUtf8().toStdString();
        existing.description = QStringLiteral("本机旧版").toUtf8().toStdString();
        TaskStep step; step.type = TaskStepType::DeployFiles;
        existing.steps.push_back(step);
        QVERIFY(store.save(existing));

        TaskCenterWidget center;
        center.setStores(&store, nullptr);
        int resolverCalls = 0;
        center.setImportConflictResolver([&](const QString&) {
            ++resolverCalls;
            return -1;                                       // 取消
        });

        const QString path = writeToTemp(encodeTemplateFile(makeExportableTemplate()));
        QVERIFY(!path.isEmpty());
        // 取消=静默 false 不弹窗：故意不布防——若误弹框，exec 阻塞至测试超时即缺陷证据
        QVERIFY(!center.importTemplateFromFile(path));
        QCOMPARE(resolverCalls, 1);
        const auto rows = store.list();
        QCOMPARE(rows.size(), std::size_t{1});               // 数量不变
        QCOMPARE(QString::fromStdString(rows.front().description),
                 QStringLiteral("本机旧版"));                  // 内容不变
    }

    void taskCenter_import_badFile_rejectedWithoutTouchingStore()
    {
        TaskTemplateStore store;
        TaskTemplate keep;
        keep.name = QStringLiteral("既有模板").toUtf8().toStdString();
        TaskStep step; step.type = TaskStepType::Reboot;
        keep.steps.push_back(step);
        QVERIFY(store.save(keep));

        // 手拼 formatVersion=2 契约文件 → 版本闸拒绝（升级文案），不落库
        const QByteArray bad = QByteArrayLiteral(
            "{\"format\":\"deviceforge.task-template\",\"formatVersion\":2,"
            "\"template\":{\"name\":\"坏文件\",\"steps\":"
            "[{\"type\":\"reboot\",\"parameters\":{}}]}}");
        const QString path = writeToTemp(bad);
        QVERIFY(!path.isEmpty());

        TaskCenterWidget center;
        center.setStores(&store, nullptr);
        int resolverCalls = 0;
        center.setImportConflictResolver([&](const QString&) {
            ++resolverCalls;
            return 0;
        });
        armDialogCapture(center);

        QVERIFY(!center.importTemplateFromFile(path));
        QCOMPARE(resolverCalls, 0);                          // 未触及冲突分支
        const auto rows = store.list();
        QCOMPARE(rows.size(), std::size_t{1});               // store 未被触碰
        QCOMPARE(QString::fromStdString(rows.front().name), QStringLiteral("既有模板"));
        QVERIFY2(m_dialogClosed, "错误框未弹出（布防未命中）");
        QVERIFY2(m_dialogText.contains(QStringLiteral("升级 DeviceForge")),
                 qPrintable(m_dialogText));
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

    // ── ⑩ v2.13 #54 Task 2：编辑器对话框骨架 ──────────────────────
    void editorDialog_metaAndDanglingRoundTrip()
    {
        TaskTemplate in;
        in.name = "编辑器种子"; in.description = "d"; in.tagSelectors = {"A线"};
        in.deviceIds = {"device-notexist"};                       // 悬空：knownDevices 不含
        TaskStep step; step.type = TaskStepType::Reboot;
        step.parameters.insert(QStringLiteral("protocol"), QStringLiteral("telnet"));
        in.steps.push_back(step);
        in.unknownFields.insert(QStringLiteral("x_next"), 7);
        TaskTemplateEditorDialog dlg(in, {});                     // 本机无档案
        auto* devList = dlg.findChild<QListWidget*>(QStringLiteral("teDevices"));
        QVERIFY(devList);
        QVERIFY(devList->item(0)->text().contains(QStringLiteral("本机无档案")));
        QVERIFY(devList->item(0)->checkState() == Qt::Checked);   // 默认保留
        TaskTemplate out;
        QVERIFY(dlg.collect(&out));
        QCOMPARE(out.deviceIds, in.deviceIds);
        QCOMPARE(out.unknownFields, in.unknownFields);
        QCOMPARE(out.steps.front().parameters.value(QStringLiteral("protocol")).toString(),
                 QStringLiteral("telnet"));
        QCOMPARE(out.templateId, in.templateId);
    }
    void editorDialog_validateRejectsEmptyNameAndCommands()
    {
        TaskTemplate in; // 空模板
        TaskTemplateEditorDialog dlg(in, {});
        QVERIFY(!dlg.validate().isEmpty());                        // 缺名称
        TaskTemplate seeded; seeded.name = "n";
        TaskStep cmd; cmd.type = TaskStepType::RunCommands;
        cmd.parameters.insert(QStringLiteral("commands"), QStringList{QStringLiteral("  ")});
        seeded.steps.push_back(cmd);
        TaskTemplateEditorDialog dlg2(seeded, {});
        QVERIFY(dlg2.validate().contains(QStringLiteral("缺少命令列表")));   // v2.14 #49-3 共享口径
    }
    // v2.14 冒烟修复（Critical，自 v2.13 带病发布）：QDialog **不会**自动接线
    // QDialogButtonBox——必须手动 connect(accepted→accept()/rejected→reject())，
    // 否则「保存/取消」点击全无响应，只能点窗口 ✕。原注释误称自动接线。
    void editorDialog_buttonBoxWiringClosesDialog()
    {
        TaskTemplate in;
        in.name = "接线验证";
        QVariantMap deploy;
        deploy.insert(QStringLiteral("items"), QVariantList{
            QVariantMap{{QStringLiteral("localPath"), QStringLiteral("D:/a.bin")},
                        {QStringLiteral("remotePath"), QStringLiteral("/opt/a.bin")}}});
        TaskStep step;
        step.type = TaskStepType::DeployFiles;
        step.parameters = deploy;
        in.steps.push_back(step);
        {
            TaskTemplateEditorDialog dlg(in, {});
            dlg.show();
            auto* save = dlg.findChild<QPushButton*>(QStringLiteral("teSave"));
            QVERIFY(save);
            save->click();                                   // 合法输入：应关闭并 Accepted
            QVERIFY2(!dlg.isVisible(), "点保存（合法输入）应关闭对话框");
            QCOMPARE(dlg.result(), int(QDialog::Accepted));
        }
        {
            TaskTemplateEditorDialog dlg(in, {});
            dlg.show();
            auto* cancel = dlg.findChild<QPushButton*>(QStringLiteral("teCancel"));
            QVERIFY(cancel);
            cancel->click();
            QVERIFY(!dlg.isVisible());
            QCOMPARE(dlg.result(), int(QDialog::Rejected));
        }
    }
    // v2.14 冒烟修复：deploy 条目表只有「添加」没有「删除」入口——添加错了只能
    // 整步重配。「移除选中行」按钮：无选中禁用，点击删表行并即时写回模型
    //（removeRow 不发 cellChanged，必须手动 flush）。
    void editorDialog_removeItemRowDeletesEntry()
    {
        TaskTemplate in;
        in.name = "条目移除";
        QVariantMap deploy;
        deploy.insert(QStringLiteral("items"), QVariantList{
            QVariantMap{{QStringLiteral("localPath"), QStringLiteral("D:/keep.bin")},
                        {QStringLiteral("remotePath"), QStringLiteral("/opt/keep.bin")}},
            QVariantMap{{QStringLiteral("localPath"), QStringLiteral("D:/drop.bin")},
                        {QStringLiteral("remotePath"), QStringLiteral("/opt/drop.bin")}}});
        TaskStep step;
        step.type = TaskStepType::DeployFiles;
        step.parameters = deploy;
        in.steps.push_back(step);
        TaskTemplateEditorDialog dlg(in, {});
        auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("teItemList"));
        QVERIFY(table);
        QCOMPARE(table->rowCount(), 2);
        auto* remove = dlg.findChild<QPushButton*>(QStringLiteral("teRemoveItem"));
        QVERIFY(remove);
        QVERIFY2(!remove->isEnabled(), "无选中行时移除按钮应禁用");
        table->selectRow(1);
        QVERIFY(remove->isEnabled());
        remove->click();
        QCOMPARE(table->rowCount(), 1);
        TaskTemplate out;
        QVERIFY(dlg.collect(&out));
        const QVariantList items = out.steps[0].parameters.value(QStringLiteral("items")).toList();
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].toMap().value(QStringLiteral("localPath")).toString(),
                 QStringLiteral("D:/keep.bin"));
    }
    // v2.14 冒烟修复：部署页「保存为模板」等外部落库源不经过任务中心任何操作，
    // 列表只在构造/setStores/本页操作时刷新——用户保存后切到任务中心看不到新
    // 模板。页显（showEvent）刷新收敛一切外部变更源；选中与搜索过滤由
    // refreshTemplates 既有语义保持。
    void taskCenter_showEventRefreshesExternallySavedTemplates()
    {
        freshDb();
        DeviceRegistry registry;
        QVERIFY(registry.load());
        TaskTemplateStore templates;
        TaskRunStore runs;
        TaskCenterWidget center;
        center.setRegistry(&registry);
        center.setStores(&templates, &runs);              // 此刻空库
        auto* list = center.findChild<QListWidget*>(QStringLiteral("templateList"));
        QVERIFY(list);
        QCOMPARE(list->count(), 0);

        TaskTemplate external;
        external.name = "外部落库";
        external.version = 1;
        external.steps = {makeStep(TaskStepType::Reboot, {})};
        QVERIFY(templates.save(external));                // 模拟部署页保存通道

        center.show();
        QVERIFY2(list->count() == 1,
                 "页显刷新应让外部落库模板立即可见（showEvent→refreshTemplates）");
        QCOMPARE(list->item(0)->text().contains(QStringLiteral("外部落库")), true);
        center.hide();
    }
    void editorDialog_canonicalizeLegacyFilesShorthand()
    {
        QVariantMap legacy;
        legacy.insert(QStringLiteral("files"), QStringList{"/local/a.bin", "/local/b.bin"});
        legacy.insert(QStringLiteral("remoteDirectory"), QStringLiteral("/opt/app/"));
        const auto canon = TaskTemplateEditorDialog::canonicalDeployParameters(legacy);
        QVERIFY(!canon.contains(QStringLiteral("files")));
        QVERIFY(!canon.contains(QStringLiteral("remoteDirectory")));
        const QVariantList items = canon.value(QStringLiteral("items")).toList();
        QCOMPARE(items.size(), 2);
        QCOMPARE(items[0].toMap().value(QStringLiteral("remotePath")).toString(),
                 QStringLiteral("/opt/app/a.bin"));               // 去尾斜杠拼接
    }

    // ── ⑪ v2.13 #54 Task 3：deploy 步骤面板（items 表 + 批量添加口径）──
    void editorDialog_deployPanelRoundTripsItems()
    {
        TaskTemplate in; in.name = "n";
        TaskStep step; step.type = TaskStepType::DeployFiles;
        QVariantList items;
        QVariantMap one;
        one.insert(QStringLiteral("localPath"), QStringLiteral("C:/tmp/a.bin"));
        one.insert(QStringLiteral("remotePath"), QStringLiteral("/opt/a.bin"));
        one.insert(QStringLiteral("direction"), QStringLiteral("upload"));
        one.insert(QStringLiteral("overwrite"), QStringLiteral("skip"));
        items.push_back(one);
        step.parameters.insert(QStringLiteral("items"), items);
        in.steps.push_back(step);
        TaskTemplateEditorDialog dlg(in, {});
        auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("teItemList"));
        QVERIFY(table);                                            // 此前不存在 → 红
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->item(0, 1)->text(), QStringLiteral("/opt/a.bin"));
        table->setItem(0, 0, new QTableWidgetItem(QStringLiteral("C:/tmp/b.bin")));
        // #55：direction 单元格下拉往返断言（程序 setCurrentIndex 不发 activated，
        // 由 assemble() 实时快照读 currentData——与用户 activated 即时写回两路同口径）
        auto* dirCombo = qobject_cast<QComboBox*>(table->cellWidget(0, 2));
        QVERIFY(dirCombo);
        dirCombo->setCurrentIndex(dirCombo->findData(QStringLiteral("download")));
        TaskTemplate out; QVERIFY(dlg.collect(&out));
        const auto row = out.steps.front().parameters.value(QStringLiteral("items"))
                             .toList().front().toMap();
        QCOMPARE(row.value(QStringLiteral("localPath")).toString(), QStringLiteral("C:/tmp/b.bin"));
        QCOMPARE(row.value(QStringLiteral("direction")).toString(), QStringLiteral("download"));
        QCOMPARE(row.value(QStringLiteral("overwrite")).toString(), QStringLiteral("skip"));
    }

    // ── ⑫ v2.13 #54 Task 4：commands/recover 面板（多行命令 + timeoutSec + protocol）──
    void editorDialog_commandsPanelSplitsLinesAndClamps()
    {
        TaskTemplate in; in.name = "n";
        TaskStep step; step.type = TaskStepType::RunCommands;
        step.parameters.insert(QStringLiteral("commands"),
                               QStringList{QStringLiteral("ls"), QStringLiteral("pwd")});
        in.steps.push_back(step);
        TaskTemplateEditorDialog dlg(in, {});
        auto* edit = dlg.findChild<QPlainTextEdit*>(QStringLiteral("teCommands"));
        QVERIFY(edit);                                             // 此前不存在 → 红
        QCOMPARE(edit->toPlainText().split('\n').size(), 2);
        edit->setPlainText(QStringLiteral("reboot\n\n # 注释行会被 trim 去空保留? 否——按行 trim 后仍保留非空"));
        TaskTemplate out; QVERIFY(dlg.collect(&out));
        const auto cmds = out.steps.front().parameters.value(QStringLiteral("commands")).toStringList();
        QCOMPARE(cmds.first(), QStringLiteral("reboot"));
        auto* spin = dlg.findChild<QSpinBox*>(QStringLiteral("teTimeout"));
        QCOMPARE(spin->value(), 10);                               // 默认 10
    }

    // 评审 Critical 1 回归：fillCommandsLike 若在程序化灌值后不解除 blockSignals，
    // 首次绑定后 teCommands 的 textChanged 永久封锁 → 用户编辑永不 flush 到 m_steps，
    // 切步即静默丢改动。用 run_commands(C1)+recover(R1) 两步各自编辑并来回切换验证存活。
    void editorDialog_commandEditsSurviveStepSwitch()
    {
        TaskTemplate in; in.name = "n";
        TaskStep c1; c1.type = TaskStepType::RunCommands;
        c1.parameters.insert(QStringLiteral("commands"),
                             QStringList{QStringLiteral("ls"), QStringLiteral("pwd")});
        TaskStep r1; r1.type = TaskStepType::Recover;
        r1.parameters.insert(QStringLiteral("commands"),
                             QStringList{QStringLiteral("echo recovered")});
        in.steps.push_back(c1);
        in.steps.push_back(r1);
        TaskTemplateEditorDialog dlg(in, {});

        auto* list = dlg.findChild<QListWidget*>(QStringLiteral("teStepList"));
        auto* edit = dlg.findChild<QPlainTextEdit*>(QStringLiteral("teCommands"));
        QVERIFY(list && edit);

        list->setCurrentRow(0);                                    // 绑定 C1
        edit->setPlainText(QStringLiteral("edited-one\nedited-two")); // 信号应实时 flush 到 m_steps[0]

        list->setCurrentRow(1);                                    // 切到 R1（fillCommandsLike 灌 R1）
        list->setCurrentRow(0);                                    // 切回 C1：编辑器应显示已改内容

        TaskTemplate out; QVERIFY(dlg.collect(&out));
        const auto c1cmds = out.steps[0].parameters.value(QStringLiteral("commands")).toStringList();
        QCOMPARE(c1cmds, (QStringList{QStringLiteral("edited-one"), QStringLiteral("edited-two")}));
        const auto r1cmds = out.steps[1].parameters.value(QStringLiteral("commands")).toStringList();
        QCOMPARE(r1cmds, (QStringList{QStringLiteral("echo recovered")})); // R1 自身内容不受扰动
    }

    // ── ⑬ v2.13 #54 Task 5：任务中心接线（新建/编辑/删除入口 + 删除三态）────
    void taskCenter_newEditDeleteFlows()
    {
        TaskTemplateStore store;
        TaskCenterWidget center;
        center.setStores(&store, nullptr);
        auto* list = center.findChild<QListWidget*>(QStringLiteral("templateList"));
        // 新建：注入对话框太直白——经公共缝：直接构造编辑器+save 已是 Task2 覆盖，
        // 本用例聚焦接线：三按钮存在 + 删除三态
        auto* newBtn = center.findChild<QPushButton*>(QStringLiteral("taskNewTemplateButton"));
        auto* editBtn = center.findChild<QPushButton*>(QStringLiteral("taskEditTemplateButton"));
        auto* delBtn = center.findChild<QPushButton*>(QStringLiteral("taskDeleteTemplateButton"));
        QVERIFY(newBtn && editBtn && delBtn);
        QVERIFY(!editBtn->isEnabled());   // 未选中
        QVERIFY(!delBtn->isEnabled());
        QVERIFY(newBtn->isEnabled());     // 恒可用
        // 种一份模板 → 选中 → 按钮态翻转 → 布防确认框点「否」→ 不删
        TaskTemplate t; t.name = "接线票";
        TaskStep s; s.type = TaskStepType::Reboot; t.steps.push_back(s);
        QVERIFY(store.save(t));
        // brief 顺序缺陷修正（其余逐字）：setStores 早于 save，而 store 直存不通知
        // widget（TaskTemplateStore/ConfigStore 非 QObject 无变更信号），列表仍是空的，
        // 不重绑则 setCurrentRow(0) 无行可选中。按 ⑧b「重绑刷新」既有口径补一刀。
        center.setStores(&store, nullptr);
        list->setCurrentRow(0);
        QVERIFY(editBtn->isEnabled() && delBtn->isEnabled());
        QTimer::singleShot(50, this, [this]{
            for (auto* box : QApplication::topLevelWidgets())
                if (auto* mb = qobject_cast<QMessageBox*>(box)) { mb->done(QMessageBox::No); return; }
        });
        delBtn->click();
        QCOMPARE((int)store.list().size(), 1);                    // 取消 → 不删
        QTimer::singleShot(50, this, [this]{
            for (auto* box : QApplication::topLevelWidgets())
                if (auto* mb = qobject_cast<QMessageBox*>(box)) { mb->done(QMessageBox::Yes); return; }
        });
        delBtn->click();
        QVERIFY(store.list().empty());                            // 确认 → 删
        QVERIFY(!delBtn->isEnabled());                            // 列表空 → 复位禁用
    }

    // ── ⑬ v2.13 #54 Task 5：applyEditorResult 公共编排缝（落库+刷新+回选）────
    void taskCenter_editDialogAccepted_savesInPlaceKeepingId()
    {
        // 经 TaskTemplateEditorDialog 直构 + center 提供的公开编排缝：
        // center.applyEditorResult(template) 返回 store 结果 + 刷新（接线复用 collect 流）
        TaskTemplateStore store;
        TaskCenterWidget center;
        center.setStores(&store, nullptr);
        TaskTemplate t; t.name = "改前";
        TaskStep s; s.type = TaskStepType::Reboot; t.steps.push_back(s);
        QVERIFY(store.save(t));
        TaskTemplate edited = store.list().front();
        edited.name = "改后";
        QVERIFY(center.applyEditorResult(edited));                // 原位覆盖（id 非空）
        QCOMPARE(QString::fromStdString(store.list().front().name), QStringLiteral("改后"));
        QCOMPARE((int)store.list().size(), 1);
        TaskTemplate fresh; fresh.name = "新建";                  // id 空 → 生成
        // brief 缺陷修正（其余逐字）：无步骤模板被 TaskTemplateStore::save 的
        // validateTaskTemplate「至少一个有序步骤」契约拒绝，与接线缝无关，补一个步骤
        TaskStep fs; fs.type = TaskStepType::Reboot; fresh.steps.push_back(fs);
        QVERIFY(center.applyEditorResult(fresh));
        QCOMPARE((int)store.list().size(), 2);
        TaskTemplate invalid;                                     // 空名 → 拒
        QVERIFY(!center.applyEditorResult(invalid));
    }

    // ── ⑭ v2.13 #54 Task 6：e2e「新建→执行」链（编辑器产物必须引擎可跑）────
    // 全链走公共缝：TaskTemplateEditorDialog::collect（编辑器出厂两步 deploy+reboot）
    // → applyEditorResult（落库 + store 生成新 id）→ startTemplate（resolveTargets
    // → confirmer → TaskRunStore::begin → 引擎）。档案只给 FTP 端点（同⑥不触网口径）：
    // 重启步骤必在 Validating 阶段校验失败——既证明模板两步真达引擎逐步校验
    // （错误文本点名「步骤 2（重启）」），又保证收口确定且全程无网络/适配器依赖。
    void taskCenter_editorProducedTemplate_executesFullChain()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(
            QStringLiteral("dev-ed"), QStringLiteral("链式目标机"),
            QStringLiteral("10.7.0.1"), 21)));                   // 仅 FTP 端点

        TaskTemplateStore store;
        TaskRunStore runs;

        // ① 编辑器缝：初值两步模板 → collect（templateId 空 = 新建路）
        TaskTemplate in;
        in.name = "链式出厂";
        in.tagSelectors = {"巡检线"};                             // 顺带补 T2 遗留：标签往返
        in.deviceIds = {"dev-ed"};
        TaskStep deploy; deploy.type = TaskStepType::DeployFiles;
        QVariantList items;
        QVariantMap one;
        one.insert(QStringLiteral("localPath"), QStringLiteral("C:/ed/a.bin"));
        one.insert(QStringLiteral("remotePath"), QStringLiteral("/opt/a.bin"));
        items.push_back(one);
        deploy.parameters.insert(QStringLiteral("items"), items);
        TaskStep reboot; reboot.type = TaskStepType::Reboot;
        in.steps.push_back(deploy);
        in.steps.push_back(reboot);
        TaskTemplateEditorDialog dlg(in, registry.list());
        TaskTemplate produced;
        QVERIFY(dlg.collect(&produced));
        QVERIFY(produced.templateId.empty());                    // 新建：id 留给 store 生成
        QCOMPARE(produced.tagSelectors, in.tagSelectors);
        QCOMPARE(produced.deviceIds, in.deviceIds);
        QCOMPARE(produced.steps.size(), std::size_t{2});

        TaskCenterWidget center;
        center.setRegistry(&registry);
        center.setStores(&store, &runs);
        int confirmCalls = 0;
        center.setRunConfirmer([&](const QString& summary, bool risky) {
            ++confirmCalls;
            // 重启动作风险位与摘要行必须随编辑器产物一路传到确认缝
            return risky && summary.contains(QStringLiteral("含重启动作"));
        });

        // ② 落库：applyEditorResult 生成新 id，两步模板一条不缺
        QVERIFY(center.applyEditorResult(produced));
        const auto rows = store.list();
        QCOMPARE(rows.size(), std::size_t{1});
        const std::string newId = rows.front().templateId;
        QVERIFY2(newId.rfind("template-", 0) == 0,
                 qPrintable(QString::fromStdString(newId)));
        const auto reloaded = store.load(newId);
        QVERIFY(reloaded.has_value());
        QCOMPARE(reloaded->steps.size(), std::size_t{2});

        // ③ 执行：startTemplate 全链发起
        QSignalSpy started(&center, &TaskCenterWidget::runStarted);
        QVERIFY(started.isValid());
        QSignalSpy finished(&center, &TaskCenterWidget::runFinished);
        QVERIFY(finished.isValid());
        QSignalSpy logs(&center, &TaskCenterWidget::logMessage);
        QVERIFY(logs.isValid());
        center.startTemplate(newId);
        QCOMPARE(confirmCalls, 1);                               // 确认缝确实过了一遍
        QCOMPARE(started.count(), 1);                            // 执行已发起（同步发射）

        // 收口：重启步骤无 Telnet/SSH 端点 → 校验失败 → 历史落库一条不缺
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        const auto records = runs.query({});
        QCOMPARE(records.size(), std::size_t{1});
        QVERIFY(records.front().status == TaskRunStatus::Failed);
        QCOMPARE(records.front().templateId, newId);             // 记录关联到编辑器产出的模板
        QCOMPARE(records.front().devices.size(), std::size_t{1});
        QCOMPARE(QString::fromStdString(records.front().devices.front().deviceId),
                 QStringLiteral("dev-ed"));
        QCOMPARE(QString::fromStdString(records.front().devices.front().address),
                 QStringLiteral("10.7.0.1:21"));                // begin 时快照的地址定格
        QVERIFY(QString::fromStdString(records.front().devices.front().name)
                    .contains(QStringLiteral("链式目标机")));     // 名称快照同理

        // 引擎逐步骤校验证据链：错误日志点名「步骤 2（重启）缺少可用端点」——
        // 该文本只可能来自编辑器产出的第二步被引擎 validateDeviceSteps 检查
        bool stepValidationLog = false;
        for (const QVariantList& args : logs) {
            if (args.at(0).toString().contains(QStringLiteral("步骤 2（重启）缺少可用端点")))
                stepValidationLog = true;
        }
        QVERIFY2(stepValidationLog, "未见引擎对第二步（重启）的校验失败日志");

        // UI 历史表同步收口（同⑥口径：终态行出现即落库刷新完成）
        auto* history = center.findChild<QTableWidget*>(QStringLiteral("taskRunHistory"));
        QVERIFY(history);
        QTRY_COMPARE_WITH_TIMEOUT(history->rowCount(), 1, 10000);
        QCOMPARE(history->item(0, 2)->text(), QStringLiteral("失败"));
    }

    // ── v2.14 #49-4：阶段恢复拒绝文案三分派（纯静态函数直测，无需真跑 33 轮）──
    void retryRejectionMessagesAreDistinguishable()
    {
        // 分支一：lineage 从未登记（本会话外启动的历史运行）
        const QString lineageUnknown =
            TaskCenterWidget::retryRejectionMessage(false, false);
        // 分支二：lineage 已知，但引擎 32 轮 FIFO 已淘汰该运行存档
        const QString evicted =
            TaskCenterWidget::retryRejectionMessage(true, false);
        // 分支三：lineage 已知且存档仍在，retryFailed 仍拒绝（最可能活跃运行占用）
        const QString stillRejected =
            TaskCenterWidget::retryRejectionMessage(true, true);

        QVERIFY2(lineageUnknown.contains(QStringLiteral("本会话")), qPrintable(lineageUnknown));
        // 轮数上限必须单源取自 TaskExecutionEngine::kHistoryLimit（防文案与引擎漂移）
        QVERIFY2(evicted.contains(QStringLiteral("超出 %1 轮上限")
                                      .arg(TaskExecutionEngine::kHistoryLimit)),
                 qPrintable(evicted));
        QVERIFY2(stillRejected.contains(QStringLiteral("活跃运行")), qPrintable(stillRejected));

        // 三条文案两两不同——分派的意义就在于用户能从文案分辨真因（防同形退化）
        QVERIFY(lineageUnknown != evicted);
        QVERIFY(lineageUnknown != stillRejected);
        QVERIFY(evicted != stillRejected);
    }

    // ── v2.15 #49-6：运行终态聚合纯头直测（收敛规则单源 TaskRunAggregate.h）──
    void aggregatePureHeaderSingleSource()
    {
        // #49-6：收敛规则唯一口径在 TaskRunAggregate.h；成员函数与 e2e 都必须是它的转发
        TaskRunOutcome o;
        o.devices = {{}, {}};
        o.devices[0].state = TaskDeviceState::Cancelled;
        o.devices[1].state = TaskDeviceState::Failed;
        QCOMPARE(taskrun::aggregateRunStatus(o), TaskRunStatus::Failed);
        o.devices[1].state = TaskDeviceState::Succeeded;
        QCOMPARE(taskrun::aggregateRunStatus(o), TaskRunStatus::PartiallySucceeded);
        QCOMPARE(taskrun::aggregateRunStatus(TaskRunOutcome{}), TaskRunStatus::Failed); // 空 outcome 非全成功
        o.devices[0].state = TaskDeviceState::Succeeded;
        QCOMPARE(taskrun::aggregateRunStatus(o), TaskRunStatus::Succeeded);

        // v2.15 终审 [T3]：Skipped 归 succeeded 桶的边界钉桩——
        // 单设备全 Skipped → Succeeded；Skipped+Failed 混布 → PartiallySucceeded
        TaskRunOutcome sk;
        sk.devices = {{}};
        sk.devices[0].state = TaskDeviceState::Skipped;
        QCOMPARE(taskrun::aggregateRunStatus(sk), TaskRunStatus::Succeeded);
        sk.devices = {{}, {}};
        sk.devices[0].state = TaskDeviceState::Skipped;
        sk.devices[1].state = TaskDeviceState::Failed;
        QCOMPARE(taskrun::aggregateRunStatus(sk), TaskRunStatus::PartiallySucceeded);
    }
};

QTEST_MAIN(TstTaskCenterWidget)
#include "tst_task_center_widget.moc"
